#include "TreeRenderer.h"
#include "GraphicsPipelineFactory.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "Mesh.h"
#include "Bindings.h"
#include <SDL3/SDL_log.h>
#include <algorithm>

std::unique_ptr<TreeRenderer> TreeRenderer::create(const InitInfo& info) {
    auto renderer = std::unique_ptr<TreeRenderer>(new TreeRenderer());
    if (!renderer->initInternal(info)) {
        return nullptr;
    }
    return renderer;
}

TreeRenderer::~TreeRenderer() {
    // Clean up culling buffers (not managed by RAII)
    for (uint32_t i = 0; i < CULL_BUFFER_SET_COUNT; ++i) {
        if (cullOutputBuffers_[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, cullOutputBuffers_[i], cullOutputAllocations_[i]);
        }
        if (cullIndirectBuffers_[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, cullIndirectBuffers_[i], cullIndirectAllocations_[i]);
        }
    }
    // Clean up per-frame uniform buffers
    for (size_t i = 0; i < cullUniformBuffers_.buffers.size(); ++i) {
        if (cullUniformBuffers_.buffers[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, cullUniformBuffers_.buffers[i], cullUniformBuffers_.allocations[i]);
        }
    }
    // Clean up tree cull data buffer
    if (treeDataBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, treeDataBuffer_, treeDataAllocation_);
    }
    // Clean up tree render data buffer
    if (treeRenderDataBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, treeRenderDataBuffer_, treeRenderDataAllocation_);
    }
    // Clean up cell culling buffers
    if (visibleCellBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, visibleCellBuffer_, visibleCellAllocation_);
    }
    if (cellCullIndirectBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, cellCullIndirectBuffer_, cellCullIndirectAllocation_);
    }
    for (size_t i = 0; i < cellCullUniformBuffers_.buffers.size(); ++i) {
        if (cellCullUniformBuffers_.buffers[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, cellCullUniformBuffers_.buffers[i], cellCullUniformBuffers_.allocations[i]);
        }
    }
    // Clean up Phase 3 tree filter buffers
    if (visibleTreeBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, visibleTreeBuffer_, visibleTreeAllocation_);
    }
    if (leafCullIndirectDispatch_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, leafCullIndirectDispatch_, leafCullIndirectDispatchAllocation_);
    }
    for (size_t i = 0; i < treeFilterUniformBuffers_.buffers.size(); ++i) {
        if (treeFilterUniformBuffers_.buffers[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, treeFilterUniformBuffers_.buffers[i], treeFilterUniformBuffers_.allocations[i]);
        }
    }
}

bool TreeRenderer::initInternal(const InitInfo& info) {
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    resourcePath_ = info.resourcePath;
    extent_ = info.extent;
    maxFramesInFlight_ = info.maxFramesInFlight;

    if (!createDescriptorSetLayout()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create descriptor set layouts");
        return false;
    }

    if (!createPipelines(info)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create pipelines");
        return false;
    }

    if (!allocateDescriptorSets(info.maxFramesInFlight)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to allocate descriptor sets");
        return false;
    }

    // Create culling pipeline (optional - gracefully degrade if fails)
    if (!createCullPipeline()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Culling pipeline not available, using direct rendering");
    }

    // Create cell culling pipeline for spatial partitioning (Phase 1)
    if (!createCellCullPipeline()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Cell culling pipeline not available, using non-spatial culling");
    }

    // Create tree filter pipeline for two-phase culling (Phase 3)
    if (!createTreeFilterPipeline()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Tree filter pipeline not available, using single-phase culling");
    }

    SDL_Log("TreeRenderer initialized successfully");
    return true;
}

bool TreeRenderer::createDescriptorSetLayout() {
    // Branch descriptor set layout (bindings from bindings.h):
    // BINDING_TREE_GFX_UBO           = 0  - Scene uniforms
    // BINDING_TREE_GFX_SHADOW_MAP    = 2  - Shadow map
    // BINDING_TREE_GFX_WIND_UBO      = 3  - Wind uniforms
    // BINDING_TREE_GFX_BARK_ALBEDO   = 4  - Bark albedo texture
    // BINDING_TREE_GFX_BARK_NORMAL   = 5  - Bark normal map
    // BINDING_TREE_GFX_BARK_ROUGHNESS = 6 - Bark roughness map
    // BINDING_TREE_GFX_BARK_AO       = 7  - Bark AO map

    std::vector<VkDescriptorSetLayoutBinding> branchBindings = {
        {Bindings::TREE_GFX_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {Bindings::TREE_GFX_SHADOW_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {Bindings::TREE_GFX_WIND_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {Bindings::TREE_GFX_BARK_ALBEDO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {Bindings::TREE_GFX_BARK_NORMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {Bindings::TREE_GFX_BARK_ROUGHNESS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {Bindings::TREE_GFX_BARK_AO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };

    VkDescriptorSetLayoutCreateInfo branchLayoutInfo{};
    branchLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    branchLayoutInfo.bindingCount = static_cast<uint32_t>(branchBindings.size());
    branchLayoutInfo.pBindings = branchBindings.data();

    VkDescriptorSetLayout rawBranchLayout;
    if (vkCreateDescriptorSetLayout(device_, &branchLayoutInfo, nullptr, &rawBranchLayout) != VK_SUCCESS) {
        return false;
    }
    branchDescriptorSetLayout_ = ManagedDescriptorSetLayout::fromRaw(device_, rawBranchLayout);

    // Leaf descriptor set layout (bindings from bindings.h):
    // BINDING_TREE_GFX_UBO             = 0  - Scene uniforms
    // BINDING_TREE_GFX_SHADOW_MAP      = 2  - Shadow map
    // BINDING_TREE_GFX_WIND_UBO        = 3  - Wind uniforms
    // BINDING_TREE_GFX_LEAF_ALBEDO     = 8  - Leaf albedo texture
    // BINDING_TREE_GFX_LEAF_INSTANCES  = 9  - World-space leaf instance SSBO
    // BINDING_TREE_GFX_TREE_DATA       = 10 - Tree render data SSBO

    std::vector<VkDescriptorSetLayoutBinding> leafBindings = {
        {Bindings::TREE_GFX_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {Bindings::TREE_GFX_SHADOW_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {Bindings::TREE_GFX_WIND_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {Bindings::TREE_GFX_LEAF_ALBEDO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {Bindings::TREE_GFX_LEAF_INSTANCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {Bindings::TREE_GFX_TREE_DATA, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
    };

    VkDescriptorSetLayoutCreateInfo leafLayoutInfo{};
    leafLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    leafLayoutInfo.bindingCount = static_cast<uint32_t>(leafBindings.size());
    leafLayoutInfo.pBindings = leafBindings.data();

    VkDescriptorSetLayout rawLeafLayout;
    if (vkCreateDescriptorSetLayout(device_, &leafLayoutInfo, nullptr, &rawLeafLayout) != VK_SUCCESS) {
        return false;
    }
    leafDescriptorSetLayout_ = ManagedDescriptorSetLayout::fromRaw(device_, rawLeafLayout);

    return true;
}

bool TreeRenderer::createPipelines(const InitInfo& info) {
    // Create pipeline layouts with push constants

    // Branch push constants
    VkPushConstantRange branchPushRange{};
    branchPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    branchPushRange.offset = 0;
    branchPushRange.size = sizeof(TreeBranchPushConstants);

    VkPipelineLayoutCreateInfo branchLayoutInfo{};
    branchLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    branchLayoutInfo.setLayoutCount = 1;
    VkDescriptorSetLayout branchSetLayout = branchDescriptorSetLayout_.get();
    branchLayoutInfo.pSetLayouts = &branchSetLayout;
    branchLayoutInfo.pushConstantRangeCount = 1;
    branchLayoutInfo.pPushConstantRanges = &branchPushRange;

    VkPipelineLayout rawBranchPipelineLayout;
    if (vkCreatePipelineLayout(device_, &branchLayoutInfo, nullptr, &rawBranchPipelineLayout) != VK_SUCCESS) {
        return false;
    }
    branchPipelineLayout_ = ManagedPipelineLayout::fromRaw(device_, rawBranchPipelineLayout);

    // Leaf push constants
    VkPushConstantRange leafPushRange{};
    leafPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    leafPushRange.offset = 0;
    leafPushRange.size = sizeof(TreeLeafPushConstants);

    VkPipelineLayoutCreateInfo leafLayoutInfo{};
    leafLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    leafLayoutInfo.setLayoutCount = 1;
    VkDescriptorSetLayout leafSetLayout = leafDescriptorSetLayout_.get();
    leafLayoutInfo.pSetLayouts = &leafSetLayout;
    leafLayoutInfo.pushConstantRangeCount = 1;
    leafLayoutInfo.pPushConstantRanges = &leafPushRange;

    VkPipelineLayout rawLeafPipelineLayout;
    if (vkCreatePipelineLayout(device_, &leafLayoutInfo, nullptr, &rawLeafPipelineLayout) != VK_SUCCESS) {
        return false;
    }
    leafPipelineLayout_ = ManagedPipelineLayout::fromRaw(device_, rawLeafPipelineLayout);

    // Get vertex input descriptions from Vertex
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    // Create branch pipeline
    GraphicsPipelineFactory factory(device_);
    VkPipeline rawBranchPipeline;

    bool success = factory
        .applyPreset(GraphicsPipelineFactory::Preset::Default)
        .setShaders(resourcePath_ + "/shaders/tree.vert.spv",
                    resourcePath_ + "/shaders/tree.frag.spv")
        .setVertexInput({bindingDescription},
                        {attributeDescriptions.begin(), attributeDescriptions.end()})
        .setRenderPass(info.hdrRenderPass)
        .setPipelineLayout(branchPipelineLayout_.get())
        .setExtent(extent_)
        .setBlendMode(GraphicsPipelineFactory::BlendMode::None)
        .build(rawBranchPipeline);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tree branch pipeline");
        return false;
    }
    branchPipeline_ = ManagedPipeline::fromRaw(device_, rawBranchPipeline);

    // Create leaf pipeline (alpha-test via discard, double-sided, no blending needed)
    VkPipeline rawLeafPipeline;
    factory.reset();
    success = factory
        .applyPreset(GraphicsPipelineFactory::Preset::Default)
        .setShaders(resourcePath_ + "/shaders/tree_leaf.vert.spv",
                    resourcePath_ + "/shaders/tree_leaf.frag.spv")
        .setVertexInput({bindingDescription},
                        {attributeDescriptions.begin(), attributeDescriptions.end()})
        .setRenderPass(info.hdrRenderPass)
        .setPipelineLayout(leafPipelineLayout_.get())
        .setExtent(extent_)
        .setBlendMode(GraphicsPipelineFactory::BlendMode::None)  // Alpha-test via discard, no blending
        .setCullMode(VK_CULL_MODE_NONE)  // Double-sided leaves
        .build(rawLeafPipeline);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tree leaf pipeline");
        return false;
    }
    leafPipeline_ = ManagedPipeline::fromRaw(device_, rawLeafPipeline);

    // Create shadow pipeline layouts
    // Branch shadow push constants
    VkPushConstantRange branchShadowPushRange{};
    branchShadowPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    branchShadowPushRange.offset = 0;
    branchShadowPushRange.size = sizeof(TreeBranchShadowPushConstants);

    VkPipelineLayoutCreateInfo branchShadowLayoutInfo{};
    branchShadowLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    branchShadowLayoutInfo.setLayoutCount = 1;
    branchShadowLayoutInfo.pSetLayouts = &branchSetLayout;
    branchShadowLayoutInfo.pushConstantRangeCount = 1;
    branchShadowLayoutInfo.pPushConstantRanges = &branchShadowPushRange;

    VkPipelineLayout rawBranchShadowLayout;
    if (vkCreatePipelineLayout(device_, &branchShadowLayoutInfo, nullptr, &rawBranchShadowLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create branch shadow pipeline layout");
        return false;
    }
    branchShadowPipelineLayout_ = ManagedPipelineLayout::fromRaw(device_, rawBranchShadowLayout);

    // Leaf shadow push constants
    VkPushConstantRange leafShadowPushRange{};
    leafShadowPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    leafShadowPushRange.offset = 0;
    leafShadowPushRange.size = sizeof(TreeLeafShadowPushConstants);

    VkPipelineLayoutCreateInfo leafShadowLayoutInfo{};
    leafShadowLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    leafShadowLayoutInfo.setLayoutCount = 1;
    leafShadowLayoutInfo.pSetLayouts = &leafSetLayout;
    leafShadowLayoutInfo.pushConstantRangeCount = 1;
    leafShadowLayoutInfo.pPushConstantRanges = &leafShadowPushRange;

    VkPipelineLayout rawLeafShadowLayout;
    if (vkCreatePipelineLayout(device_, &leafShadowLayoutInfo, nullptr, &rawLeafShadowLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create leaf shadow pipeline layout");
        return false;
    }
    leafShadowPipelineLayout_ = ManagedPipelineLayout::fromRaw(device_, rawLeafShadowLayout);

    // Create branch shadow pipeline
    VkPipeline rawBranchShadowPipeline;
    factory.reset();
    success = factory
        .applyPreset(GraphicsPipelineFactory::Preset::Shadow)
        .setShaders(resourcePath_ + "/shaders/tree_shadow.vert.spv",
                    resourcePath_ + "/shaders/shadow.frag.spv")
        .setVertexInput({bindingDescription},
                        {attributeDescriptions.begin(), attributeDescriptions.end()})
        .setRenderPass(info.shadowRenderPass)
        .setPipelineLayout(branchShadowPipelineLayout_.get())
        .setExtent({info.shadowMapSize, info.shadowMapSize})
        .setDepthBias(1.25f, 1.75f)
        .build(rawBranchShadowPipeline);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tree branch shadow pipeline");
        return false;
    }
    branchShadowPipeline_ = ManagedPipeline::fromRaw(device_, rawBranchShadowPipeline);

    // Create leaf shadow pipeline (with alpha test)
    VkPipeline rawLeafShadowPipeline;
    factory.reset();
    success = factory
        .applyPreset(GraphicsPipelineFactory::Preset::Shadow)
        .setShaders(resourcePath_ + "/shaders/tree_leaf_shadow.vert.spv",
                    resourcePath_ + "/shaders/tree_leaf_shadow.frag.spv")
        .setVertexInput({bindingDescription},
                        {attributeDescriptions.begin(), attributeDescriptions.end()})
        .setRenderPass(info.shadowRenderPass)
        .setPipelineLayout(leafShadowPipelineLayout_.get())
        .setExtent({info.shadowMapSize, info.shadowMapSize})
        .setDepthBias(1.25f, 1.75f)
        .setCullMode(VK_CULL_MODE_NONE)  // Double-sided leaves
        .build(rawLeafShadowPipeline);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tree leaf shadow pipeline");
        return false;
    }
    leafShadowPipeline_ = ManagedPipeline::fromRaw(device_, rawLeafShadowPipeline);

    SDL_Log("TreeRenderer: Created branch, leaf, and shadow pipelines");
    return true;
}

bool TreeRenderer::allocateDescriptorSets(uint32_t maxFramesInFlight) {
    branchDescriptorSets_.resize(maxFramesInFlight);
    leafDescriptorSets_.resize(maxFramesInFlight);

    // Allocate default descriptor sets using DescriptorManager::Pool
    defaultBranchDescriptorSets_ = descriptorPool_->allocate(branchDescriptorSetLayout_.get(), maxFramesInFlight);
    if (defaultBranchDescriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate default branch descriptor sets");
        return false;
    }

    defaultLeafDescriptorSets_ = descriptorPool_->allocate(leafDescriptorSetLayout_.get(), maxFramesInFlight);
    if (defaultLeafDescriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate default leaf descriptor sets");
        return false;
    }

    return true;
}

bool TreeRenderer::createCullPipeline() {
    // Create culling descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> cullBindings = {
        {Bindings::TREE_LEAF_CULL_INPUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {Bindings::TREE_LEAF_CULL_OUTPUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {Bindings::TREE_LEAF_CULL_INDIRECT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {Bindings::TREE_LEAF_CULL_UNIFORMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {Bindings::TREE_LEAF_CULL_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    VkDescriptorSetLayoutCreateInfo cullLayoutInfo{};
    cullLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    cullLayoutInfo.bindingCount = static_cast<uint32_t>(cullBindings.size());
    cullLayoutInfo.pBindings = cullBindings.data();

    VkDescriptorSetLayout rawCullLayout;
    if (vkCreateDescriptorSetLayout(device_, &cullLayoutInfo, nullptr, &rawCullLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create cull descriptor set layout");
        return false;
    }
    cullDescriptorSetLayout_ = ManagedDescriptorSetLayout::fromRaw(device_, rawCullLayout);

    // Create culling pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    VkDescriptorSetLayout cullSetLayout = cullDescriptorSetLayout_.get();
    pipelineLayoutInfo.pSetLayouts = &cullSetLayout;

    VkPipelineLayout rawCullPipelineLayout;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &rawCullPipelineLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create cull pipeline layout");
        return false;
    }
    cullPipelineLayout_ = ManagedPipelineLayout::fromRaw(device_, rawCullPipelineLayout);

    // Load compute shader
    std::string shaderPath = resourcePath_ + "/shaders/tree_leaf_cull.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModuleOpt.has_value()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Cull shader not found: %s", shaderPath.c_str());
        return false;
    }
    VkShaderModule computeShaderModule = shaderModuleOpt.value();

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = computeShaderModule;
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = cullPipelineLayout_.get();

    VkPipeline rawCullPipeline;
    VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rawCullPipeline);
    vkDestroyShaderModule(device_, computeShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create cull compute pipeline");
        return false;
    }
    cullPipeline_ = ManagedPipeline::fromRaw(device_, rawCullPipeline);

    SDL_Log("TreeRenderer: Created leaf culling compute pipeline");
    return true;
}

bool TreeRenderer::createCullBuffers(uint32_t maxLeafInstances, uint32_t numTrees) {
    // Store number of trees for indirect buffer sizing
    numTreesForIndirect_ = numTrees;

    // Calculate per-type limits (use full count per type for unbalanced forests)
    maxLeavesPerType_ = maxLeafInstances;  // Conservative: each type can have all leaves

    // Calculate buffer sizes - output buffer partitioned into NUM_LEAF_TYPES regions
    // Each region can hold maxLeavesPerType_ leaves (48 bytes each)
    cullOutputBufferSize_ = NUM_LEAF_TYPES * maxLeavesPerType_ * sizeof(WorldLeafInstanceGPU);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    // Create double-buffered output buffers (world-space leaves, partitioned by type)
    for (uint32_t i = 0; i < CULL_BUFFER_SET_COUNT; ++i) {
        bufferInfo.size = cullOutputBufferSize_;
        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                            &cullOutputBuffers_[i], &cullOutputAllocations_[i], nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create cull output buffer %u", i);
            return false;
        }
    }

    // Create indirect draw buffers (one command per leaf type)
    VkBufferCreateInfo indirectInfo{};
    indirectInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indirectInfo.size = NUM_LEAF_TYPES * sizeof(VkDrawIndexedIndirectCommand);  // One per type
    indirectInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT;  // For vkCmdUpdateBuffer
    indirectInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    for (uint32_t i = 0; i < CULL_BUFFER_SET_COUNT; ++i) {
        if (vmaCreateBuffer(allocator_, &indirectInfo, &allocInfo,
                            &cullIndirectBuffers_[i], &cullIndirectAllocations_[i], nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create cull indirect buffer %u", i);
            return false;
        }
    }

    // Create per-frame uniform buffers using builder pattern
    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator_)
            .setFrameCount(maxFramesInFlight_)
            .setSize(sizeof(TreeLeafCullUniforms))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(cullUniformBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create cull uniform buffers");
        return false;
    }

    // Create per-tree cull data buffer (SSBO for batched culling)
    treeDataBufferSize_ = numTrees * sizeof(TreeCullData);
    VkBufferCreateInfo treeDataInfo{};
    treeDataInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    treeDataInfo.size = treeDataBufferSize_;
    treeDataInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    treeDataInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vmaCreateBuffer(allocator_, &treeDataInfo, &allocInfo,
                        &treeDataBuffer_, &treeDataAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create tree cull data buffer");
        return false;
    }

    // Create per-tree render data buffer (SSBO for vertex shader tints, autumn, etc.)
    treeRenderDataBufferSize_ = numTrees * sizeof(TreeRenderDataGPU);
    VkBufferCreateInfo treeRenderDataInfo{};
    treeRenderDataInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    treeRenderDataInfo.size = treeRenderDataBufferSize_;
    treeRenderDataInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    treeRenderDataInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vmaCreateBuffer(allocator_, &treeRenderDataInfo, &allocInfo,
                        &treeRenderDataBuffer_, &treeRenderDataAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create tree render data buffer");
        return false;
    }

    // Allocate descriptor sets for culling compute shader
    cullDescriptorSets_ = descriptorPool_->allocate(cullDescriptorSetLayout_.get(), maxFramesInFlight_);
    if (cullDescriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to allocate cull descriptor sets");
        return false;
    }

    // Initialize per-frame maps for culled leaf descriptor sets (allocated lazily per-type)
    culledLeafDescriptorSets_.resize(maxFramesInFlight_);

    SDL_Log("TreeRenderer: Created leaf culling buffers (max %u instances, %u trees, %.2f MB world-space output)",
            maxLeafInstances, numTrees,
            static_cast<float>(cullOutputBufferSize_ * CULL_BUFFER_SET_COUNT) / (1024.0f * 1024.0f));
    return true;
}

bool TreeRenderer::createCellCullPipeline() {
    // Create cell culling descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> cellCullBindings = {
        {Bindings::TREE_CELL_CULL_CELLS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {Bindings::TREE_CELL_CULL_VISIBLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {Bindings::TREE_CELL_CULL_INDIRECT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {Bindings::TREE_CELL_CULL_UNIFORMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(cellCullBindings.size());
    layoutInfo.pBindings = cellCullBindings.data();

    VkDescriptorSetLayout rawLayout;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &rawLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create cell cull descriptor set layout");
        return false;
    }
    cellCullDescriptorSetLayout_ = ManagedDescriptorSetLayout::fromRaw(device_, rawLayout);

    // Create cell culling pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    VkDescriptorSetLayout setLayout = cellCullDescriptorSetLayout_.get();
    pipelineLayoutInfo.pSetLayouts = &setLayout;

    VkPipelineLayout rawPipelineLayout;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &rawPipelineLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create cell cull pipeline layout");
        return false;
    }
    cellCullPipelineLayout_ = ManagedPipelineLayout::fromRaw(device_, rawPipelineLayout);

    // Load compute shader
    std::string shaderPath = resourcePath_ + "/shaders/tree_cell_cull.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModuleOpt.has_value()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Cell cull shader not found: %s", shaderPath.c_str());
        return false;
    }
    VkShaderModule computeShaderModule = shaderModuleOpt.value();

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = computeShaderModule;
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = cellCullPipelineLayout_.get();

    VkPipeline rawPipeline;
    VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rawPipeline);
    vkDestroyShaderModule(device_, computeShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create cell cull compute pipeline");
        return false;
    }
    cellCullPipeline_ = ManagedPipeline::fromRaw(device_, rawPipeline);

    SDL_Log("TreeRenderer: Created cell culling compute pipeline");
    return true;
}

bool TreeRenderer::createCellCullBuffers() {
    if (!spatialIndex_ || !spatialIndex_->isValid()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Cannot create cell cull buffers without valid spatial index");
        return false;
    }

    uint32_t numCells = spatialIndex_->getCellCount();

    // Create visible cell output buffer (one uint per cell for worst case)
    // Plus 1 uint at the beginning for the count
    visibleCellBufferSize_ = (numCells + 1) * sizeof(uint32_t);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = visibleCellBufferSize_;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &visibleCellBuffer_, &visibleCellAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create visible cell buffer");
        return false;
    }

    // Create indirect dispatch buffer (4 uints: dispatchX, dispatchY, dispatchZ, totalVisibleTrees)
    VkBufferCreateInfo indirectInfo{};
    indirectInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indirectInfo.size = 4 * sizeof(uint32_t);
    indirectInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    indirectInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vmaCreateBuffer(allocator_, &indirectInfo, &allocInfo,
                        &cellCullIndirectBuffer_, &cellCullIndirectAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create cell cull indirect buffer");
        return false;
    }

    // Create uniform buffers for cell culling (per-frame)
    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator_)
            .setFrameCount(maxFramesInFlight_)
            .setSize(sizeof(TreeCellCullUniforms))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(cellCullUniformBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create cell cull uniform buffers");
        return false;
    }

    // Allocate descriptor sets for cell culling
    cellCullDescriptorSets_ = descriptorPool_->allocate(cellCullDescriptorSetLayout_.get(), maxFramesInFlight_);
    if (cellCullDescriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to allocate cell cull descriptor sets");
        return false;
    }

    // Update descriptor sets with buffer bindings
    for (uint32_t f = 0; f < maxFramesInFlight_; ++f) {
        VkDescriptorBufferInfo cellsInfo{};
        cellsInfo.buffer = spatialIndex_->getCellBuffer();
        cellsInfo.offset = 0;
        cellsInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo visibleInfo{};
        visibleInfo.buffer = visibleCellBuffer_;
        visibleInfo.offset = 0;
        visibleInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo indirectBufferInfo{};
        indirectBufferInfo.buffer = cellCullIndirectBuffer_;
        indirectBufferInfo.offset = 0;
        indirectBufferInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo uniformInfo{};
        uniformInfo.buffer = cellCullUniformBuffers_.buffers[f];
        uniformInfo.offset = 0;
        uniformInfo.range = sizeof(TreeCellCullUniforms);

        std::array<VkWriteDescriptorSet, 4> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = cellCullDescriptorSets_[f];
        writes[0].dstBinding = Bindings::TREE_CELL_CULL_CELLS;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &cellsInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = cellCullDescriptorSets_[f];
        writes[1].dstBinding = Bindings::TREE_CELL_CULL_VISIBLE;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &visibleInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = cellCullDescriptorSets_[f];
        writes[2].dstBinding = Bindings::TREE_CELL_CULL_INDIRECT;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &indirectBufferInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = cellCullDescriptorSets_[f];
        writes[3].dstBinding = Bindings::TREE_CELL_CULL_UNIFORMS;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &uniformInfo;

        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    SDL_Log("TreeRenderer: Created cell culling buffers (%u cells, %.2f KB visible buffer)",
            numCells, visibleCellBufferSize_ / 1024.0f);
    return true;
}

bool TreeRenderer::createTreeFilterPipeline() {
    // Create tree filter descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> treeFilterBindings = {
        {Bindings::TREE_FILTER_ALL_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {Bindings::TREE_FILTER_VISIBLE_CELLS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {Bindings::TREE_FILTER_CELL_DATA, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {Bindings::TREE_FILTER_SORTED_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {Bindings::TREE_FILTER_VISIBLE_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {Bindings::TREE_FILTER_INDIRECT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {Bindings::TREE_FILTER_UNIFORMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(treeFilterBindings.size());
    layoutInfo.pBindings = treeFilterBindings.data();

    VkDescriptorSetLayout rawLayout;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &rawLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create tree filter descriptor set layout");
        return false;
    }
    treeFilterDescriptorSetLayout_ = ManagedDescriptorSetLayout::fromRaw(device_, rawLayout);

    // Create tree filter pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    VkDescriptorSetLayout setLayout = treeFilterDescriptorSetLayout_.get();
    pipelineLayoutInfo.pSetLayouts = &setLayout;

    VkPipelineLayout rawPipelineLayout;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &rawPipelineLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create tree filter pipeline layout");
        return false;
    }
    treeFilterPipelineLayout_ = ManagedPipelineLayout::fromRaw(device_, rawPipelineLayout);

    // Load compute shader
    std::string shaderPath = resourcePath_ + "/shaders/tree_filter.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModuleOpt.has_value()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Tree filter shader not found: %s", shaderPath.c_str());
        return false;
    }
    VkShaderModule computeShaderModule = shaderModuleOpt.value();

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = computeShaderModule;
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = treeFilterPipelineLayout_.get();

    VkPipeline rawPipeline;
    VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rawPipeline);
    vkDestroyShaderModule(device_, computeShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create tree filter compute pipeline");
        return false;
    }
    treeFilterPipeline_ = ManagedPipeline::fromRaw(device_, rawPipeline);

    SDL_Log("TreeRenderer: Created tree filter compute pipeline (Phase 3)");
    return true;
}

bool TreeRenderer::createTreeFilterBuffers(uint32_t maxTrees) {
    if (!spatialIndex_ || !spatialIndex_->isValid()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Cannot create tree filter buffers without valid spatial index");
        return false;
    }

    // Create visible tree output buffer
    // Size: 1 uint for count + maxTrees * sizeof(VisibleTreeData)
    visibleTreeBufferSize_ = sizeof(uint32_t) + maxTrees * sizeof(VisibleTreeData);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = visibleTreeBufferSize_;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &visibleTreeBuffer_, &visibleTreeAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create visible tree buffer");
        return false;
    }

    // Create indirect dispatch buffer for leaf culling (3 uints: dispatchX, dispatchY, dispatchZ)
    VkBufferCreateInfo indirectInfo{};
    indirectInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indirectInfo.size = 3 * sizeof(uint32_t);
    indirectInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    indirectInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vmaCreateBuffer(allocator_, &indirectInfo, &allocInfo,
                        &leafCullIndirectDispatch_, &leafCullIndirectDispatchAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create leaf cull indirect dispatch buffer");
        return false;
    }

    // Create uniform buffers for tree filtering (per-frame)
    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator_)
            .setFrameCount(maxFramesInFlight_)
            .setSize(sizeof(TreeFilterUniforms))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(treeFilterUniformBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create tree filter uniform buffers");
        return false;
    }

    // Allocate descriptor sets for tree filtering
    treeFilterDescriptorSets_ = descriptorPool_->allocate(treeFilterDescriptorSetLayout_.get(), maxFramesInFlight_);
    if (treeFilterDescriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to allocate tree filter descriptor sets");
        return false;
    }

    // Update descriptor sets with buffer bindings
    for (uint32_t f = 0; f < maxFramesInFlight_; ++f) {
        VkDescriptorBufferInfo allTreesInfo{};
        allTreesInfo.buffer = treeDataBuffer_;  // Per-tree cull data buffer
        allTreesInfo.offset = 0;
        allTreesInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo visibleCellsInfo{};
        visibleCellsInfo.buffer = visibleCellBuffer_;
        visibleCellsInfo.offset = 0;
        visibleCellsInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo cellDataInfo{};
        cellDataInfo.buffer = spatialIndex_->getCellBuffer();
        cellDataInfo.offset = 0;
        cellDataInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo sortedTreesInfo{};
        sortedTreesInfo.buffer = spatialIndex_->getSortedTreeBuffer();
        sortedTreesInfo.offset = 0;
        sortedTreesInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo visibleTreesInfo{};
        visibleTreesInfo.buffer = visibleTreeBuffer_;
        visibleTreesInfo.offset = 0;
        visibleTreesInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo indirectBufferInfo{};
        indirectBufferInfo.buffer = leafCullIndirectDispatch_;
        indirectBufferInfo.offset = 0;
        indirectBufferInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo uniformInfo{};
        uniformInfo.buffer = treeFilterUniformBuffers_.buffers[f];
        uniformInfo.offset = 0;
        uniformInfo.range = sizeof(TreeFilterUniforms);

        std::array<VkWriteDescriptorSet, 7> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = treeFilterDescriptorSets_[f];
        writes[0].dstBinding = Bindings::TREE_FILTER_ALL_TREES;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &allTreesInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = treeFilterDescriptorSets_[f];
        writes[1].dstBinding = Bindings::TREE_FILTER_VISIBLE_CELLS;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &visibleCellsInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = treeFilterDescriptorSets_[f];
        writes[2].dstBinding = Bindings::TREE_FILTER_CELL_DATA;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &cellDataInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = treeFilterDescriptorSets_[f];
        writes[3].dstBinding = Bindings::TREE_FILTER_SORTED_TREES;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &sortedTreesInfo;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = treeFilterDescriptorSets_[f];
        writes[4].dstBinding = Bindings::TREE_FILTER_VISIBLE_TREES;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].pBufferInfo = &visibleTreesInfo;

        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = treeFilterDescriptorSets_[f];
        writes[5].dstBinding = Bindings::TREE_FILTER_INDIRECT;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].descriptorCount = 1;
        writes[5].pBufferInfo = &indirectBufferInfo;

        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = treeFilterDescriptorSets_[f];
        writes[6].dstBinding = Bindings::TREE_FILTER_UNIFORMS;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[6].descriptorCount = 1;
        writes[6].pBufferInfo = &uniformInfo;

        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    SDL_Log("TreeRenderer: Created tree filter buffers (Phase 3, max %u trees, %.2f KB visible tree buffer)",
            maxTrees, visibleTreeBufferSize_ / 1024.0f);
    return true;
}

void TreeRenderer::updateSpatialIndex(const TreeSystem& treeSystem) {
    const auto& trees = treeSystem.getTreeInstances();
    const auto& leafRenderables = treeSystem.getLeafRenderables();

    if (trees.empty()) {
        spatialIndex_.reset();
        return;
    }

    // Create spatial index if needed
    if (!spatialIndex_) {
        TreeSpatialIndex::InitInfo indexInfo{};
        indexInfo.device = device_;
        indexInfo.allocator = allocator_;
        indexInfo.cellSize = 64.0f;  // 64m cells
        indexInfo.worldSize = terrainSize_;

        spatialIndex_ = TreeSpatialIndex::create(indexInfo);
        if (!spatialIndex_) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create spatial index");
            return;
        }
    }

    // Collect model matrices from leaf renderables
    std::vector<glm::mat4> treeModels;
    treeModels.reserve(leafRenderables.size());
    for (const auto& renderable : leafRenderables) {
        treeModels.push_back(renderable.transform);
    }

    // Rebuild index with current tree data
    spatialIndex_->rebuild(trees, treeModels);

    // Upload to GPU
    if (!spatialIndex_->uploadToGPU()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to upload spatial index to GPU");
        return;
    }

    // Create cell culling buffers if needed
    if (visibleCellBuffer_ == VK_NULL_HANDLE && cellCullPipeline_.get() != VK_NULL_HANDLE) {
        createCellCullBuffers();
    }

    // Create tree filter buffers if needed (Phase 3)
    if (visibleTreeBuffer_ == VK_NULL_HANDLE && treeFilterPipeline_.get() != VK_NULL_HANDLE &&
        visibleCellBuffer_ != VK_NULL_HANDLE) {
        createTreeFilterBuffers(static_cast<uint32_t>(trees.size()));
    }

    SDL_Log("TreeRenderer: Updated spatial index (%zu trees, %u non-empty cells)",
            trees.size(), spatialIndex_->getNonEmptyCellCount());
}

void TreeRenderer::updateBarkDescriptorSet(
    uint32_t frameIndex,
    const std::string& barkType,
    VkBuffer uniformBuffer,
    VkBuffer windBuffer,
    VkImageView shadowMapView,
    VkSampler shadowSampler,
    VkImageView barkAlbedo,
    VkImageView barkNormal,
    VkImageView barkRoughness,
    VkImageView barkAO,
    VkSampler barkSampler) {

    // Allocate descriptor set for this type if not already allocated
    if (branchDescriptorSets_[frameIndex].find(barkType) == branchDescriptorSets_[frameIndex].end()) {
        auto sets = descriptorPool_->allocate(branchDescriptorSetLayout_.get(), 1);
        if (sets.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate bark descriptor set for type: %s", barkType.c_str());
            return;
        }
        branchDescriptorSets_[frameIndex][barkType] = sets[0];
    }

    VkDescriptorSet dstSet = branchDescriptorSets_[frameIndex][barkType];

    VkDescriptorBufferInfo uboInfo{};
    uboInfo.buffer = uniformBuffer;
    uboInfo.offset = 0;
    uboInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo windInfo{};
    windInfo.buffer = windBuffer;
    windInfo.offset = 0;
    windInfo.range = VK_WHOLE_SIZE;

    VkDescriptorImageInfo shadowInfo{};
    shadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    shadowInfo.imageView = shadowMapView;
    shadowInfo.sampler = shadowSampler;

    VkDescriptorImageInfo barkAlbedoInfo{};
    barkAlbedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barkAlbedoInfo.imageView = barkAlbedo;
    barkAlbedoInfo.sampler = barkSampler;

    VkDescriptorImageInfo barkNormalInfo{};
    barkNormalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barkNormalInfo.imageView = barkNormal;
    barkNormalInfo.sampler = barkSampler;

    VkDescriptorImageInfo barkRoughnessInfo{};
    barkRoughnessInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barkRoughnessInfo.imageView = barkRoughness;
    barkRoughnessInfo.sampler = barkSampler;

    VkDescriptorImageInfo barkAOInfo{};
    barkAOInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barkAOInfo.imageView = barkAO;
    barkAOInfo.sampler = barkSampler;

    std::array<VkWriteDescriptorSet, 7> branchWrites{};

    branchWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    branchWrites[0].dstSet = dstSet;
    branchWrites[0].dstBinding = Bindings::TREE_GFX_UBO;
    branchWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    branchWrites[0].descriptorCount = 1;
    branchWrites[0].pBufferInfo = &uboInfo;

    branchWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    branchWrites[1].dstSet = dstSet;
    branchWrites[1].dstBinding = Bindings::TREE_GFX_SHADOW_MAP;
    branchWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    branchWrites[1].descriptorCount = 1;
    branchWrites[1].pImageInfo = &shadowInfo;

    branchWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    branchWrites[2].dstSet = dstSet;
    branchWrites[2].dstBinding = Bindings::TREE_GFX_WIND_UBO;
    branchWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    branchWrites[2].descriptorCount = 1;
    branchWrites[2].pBufferInfo = &windInfo;

    branchWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    branchWrites[3].dstSet = dstSet;
    branchWrites[3].dstBinding = Bindings::TREE_GFX_BARK_ALBEDO;
    branchWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    branchWrites[3].descriptorCount = 1;
    branchWrites[3].pImageInfo = &barkAlbedoInfo;

    branchWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    branchWrites[4].dstSet = dstSet;
    branchWrites[4].dstBinding = Bindings::TREE_GFX_BARK_NORMAL;
    branchWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    branchWrites[4].descriptorCount = 1;
    branchWrites[4].pImageInfo = &barkNormalInfo;

    branchWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    branchWrites[5].dstSet = dstSet;
    branchWrites[5].dstBinding = Bindings::TREE_GFX_BARK_ROUGHNESS;
    branchWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    branchWrites[5].descriptorCount = 1;
    branchWrites[5].pImageInfo = &barkRoughnessInfo;

    branchWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    branchWrites[6].dstSet = dstSet;
    branchWrites[6].dstBinding = Bindings::TREE_GFX_BARK_AO;
    branchWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    branchWrites[6].descriptorCount = 1;
    branchWrites[6].pImageInfo = &barkAOInfo;

    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(branchWrites.size()), branchWrites.data(), 0, nullptr);
}

void TreeRenderer::updateLeafDescriptorSet(
    uint32_t frameIndex,
    const std::string& leafType,
    VkBuffer uniformBuffer,
    VkBuffer windBuffer,
    VkImageView shadowMapView,
    VkSampler shadowSampler,
    VkImageView leafAlbedo,
    VkSampler leafSampler,
    VkBuffer leafInstanceBuffer,
    VkDeviceSize leafInstanceBufferSize) {

    // Allocate descriptor set for this type if not already allocated
    if (leafDescriptorSets_[frameIndex].find(leafType) == leafDescriptorSets_[frameIndex].end()) {
        auto sets = descriptorPool_->allocate(leafDescriptorSetLayout_.get(), 1);
        if (sets.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate leaf descriptor set for type: %s", leafType.c_str());
            return;
        }
        leafDescriptorSets_[frameIndex][leafType] = sets[0];
    }

    VkDescriptorSet dstSet = leafDescriptorSets_[frameIndex][leafType];

    VkDescriptorBufferInfo uboInfo{};
    uboInfo.buffer = uniformBuffer;
    uboInfo.offset = 0;
    uboInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo windInfo{};
    windInfo.buffer = windBuffer;
    windInfo.offset = 0;
    windInfo.range = VK_WHOLE_SIZE;

    VkDescriptorImageInfo shadowInfo{};
    shadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    shadowInfo.imageView = shadowMapView;
    shadowInfo.sampler = shadowSampler;

    VkDescriptorImageInfo leafAlbedoInfo{};
    leafAlbedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    leafAlbedoInfo.imageView = leafAlbedo;
    leafAlbedoInfo.sampler = leafSampler;

    VkDescriptorBufferInfo leafInstanceInfo{};
    leafInstanceInfo.buffer = leafInstanceBuffer;
    leafInstanceInfo.offset = 0;
    leafInstanceInfo.range = leafInstanceBufferSize > 0 ? leafInstanceBufferSize : VK_WHOLE_SIZE;

    std::array<VkWriteDescriptorSet, 5> leafWrites{};

    leafWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    leafWrites[0].dstSet = dstSet;
    leafWrites[0].dstBinding = Bindings::TREE_GFX_UBO;
    leafWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    leafWrites[0].descriptorCount = 1;
    leafWrites[0].pBufferInfo = &uboInfo;

    leafWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    leafWrites[1].dstSet = dstSet;
    leafWrites[1].dstBinding = Bindings::TREE_GFX_SHADOW_MAP;
    leafWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    leafWrites[1].descriptorCount = 1;
    leafWrites[1].pImageInfo = &shadowInfo;

    leafWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    leafWrites[2].dstSet = dstSet;
    leafWrites[2].dstBinding = Bindings::TREE_GFX_WIND_UBO;
    leafWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    leafWrites[2].descriptorCount = 1;
    leafWrites[2].pBufferInfo = &windInfo;

    leafWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    leafWrites[3].dstSet = dstSet;
    leafWrites[3].dstBinding = Bindings::TREE_GFX_LEAF_ALBEDO;
    leafWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    leafWrites[3].descriptorCount = 1;
    leafWrites[3].pImageInfo = &leafAlbedoInfo;

    leafWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    leafWrites[4].dstSet = dstSet;
    leafWrites[4].dstBinding = Bindings::TREE_GFX_LEAF_INSTANCES;
    leafWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    leafWrites[4].descriptorCount = 1;
    leafWrites[4].pBufferInfo = &leafInstanceInfo;

    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(leafWrites.size()), leafWrites.data(), 0, nullptr);
}

void TreeRenderer::updateCulledLeafDescriptorSet(
    uint32_t frameIndex,
    const std::string& leafType,
    VkBuffer uniformBuffer,
    VkBuffer windBuffer,
    VkImageView shadowMapView,
    VkSampler shadowSampler,
    VkImageView leafAlbedo,
    VkSampler leafSampler) {

    // Skip if cull output buffer not created yet
    if (cullOutputBuffers_[currentCullBufferSet_] == VK_NULL_HANDLE) {
        return;
    }

    // Ensure per-frame maps exist
    if (culledLeafDescriptorSets_.size() <= frameIndex) {
        culledLeafDescriptorSets_.resize(maxFramesInFlight_);
    }

    // Allocate descriptor set for this type if not already allocated
    if (culledLeafDescriptorSets_[frameIndex].find(leafType) == culledLeafDescriptorSets_[frameIndex].end()) {
        auto sets = descriptorPool_->allocate(leafDescriptorSetLayout_.get(), 1);
        if (sets.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate culled leaf descriptor set for type: %s", leafType.c_str());
            return;
        }
        culledLeafDescriptorSets_[frameIndex][leafType] = sets[0];
    }

    VkDescriptorSet dstSet = culledLeafDescriptorSets_[frameIndex][leafType];

    VkDescriptorBufferInfo uboInfo{};
    uboInfo.buffer = uniformBuffer;
    uboInfo.offset = 0;
    uboInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo windInfo{};
    windInfo.buffer = windBuffer;
    windInfo.offset = 0;
    windInfo.range = VK_WHOLE_SIZE;

    VkDescriptorImageInfo shadowInfo{};
    shadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    shadowInfo.imageView = shadowMapView;
    shadowInfo.sampler = shadowSampler;

    VkDescriptorImageInfo leafAlbedoInfo{};
    leafAlbedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    leafAlbedoInfo.imageView = leafAlbedo;
    leafAlbedoInfo.sampler = leafSampler;

    // Use culled output buffer for leaf instances (world-space)
    // IMPORTANT: Must update SSBO binding every frame because we double-buffer
    VkDescriptorBufferInfo leafInstanceInfo{};
    leafInstanceInfo.buffer = cullOutputBuffers_[currentCullBufferSet_];
    leafInstanceInfo.offset = 0;
    leafInstanceInfo.range = VK_WHOLE_SIZE;

    // Tree render data buffer (for vertex shader to look up tints, autumn, wind phase)
    VkDescriptorBufferInfo treeRenderDataInfo{};
    treeRenderDataInfo.buffer = treeRenderDataBuffer_;
    treeRenderDataInfo.offset = 0;
    treeRenderDataInfo.range = VK_WHOLE_SIZE;

    std::array<VkWriteDescriptorSet, 6> writes{};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = dstSet;
    writes[0].dstBinding = Bindings::TREE_GFX_UBO;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &uboInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = dstSet;
    writes[1].dstBinding = Bindings::TREE_GFX_SHADOW_MAP;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &shadowInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = dstSet;
    writes[2].dstBinding = Bindings::TREE_GFX_WIND_UBO;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[2].descriptorCount = 1;
    writes[2].pBufferInfo = &windInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = dstSet;
    writes[3].dstBinding = Bindings::TREE_GFX_LEAF_ALBEDO;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    writes[3].pImageInfo = &leafAlbedoInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = dstSet;
    writes[4].dstBinding = Bindings::TREE_GFX_LEAF_INSTANCES;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    writes[4].pBufferInfo = &leafInstanceInfo;

    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = dstSet;
    writes[5].dstBinding = Bindings::TREE_GFX_TREE_DATA;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    writes[5].pBufferInfo = &treeRenderDataInfo;

    // Update all bindings every frame (not just on first allocation)
    // This ensures the double-buffered SSBO binding stays in sync
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

VkDescriptorSet TreeRenderer::getBranchDescriptorSet(uint32_t frameIndex, const std::string& barkType) const {
    auto frameIt = branchDescriptorSets_[frameIndex].find(barkType);
    if (frameIt != branchDescriptorSets_[frameIndex].end()) {
        return frameIt->second;
    }
    // Fall back to default descriptor set
    return defaultBranchDescriptorSets_[frameIndex];
}

VkDescriptorSet TreeRenderer::getLeafDescriptorSet(uint32_t frameIndex, const std::string& leafType) const {
    auto frameIt = leafDescriptorSets_[frameIndex].find(leafType);
    if (frameIt != leafDescriptorSets_[frameIndex].end()) {
        return frameIt->second;
    }
    // Fall back to default descriptor set
    return defaultLeafDescriptorSets_[frameIndex];
}

VkDescriptorSet TreeRenderer::getCulledLeafDescriptorSet(uint32_t frameIndex, const std::string& leafType) const {
    if (frameIndex < culledLeafDescriptorSets_.size()) {
        auto frameIt = culledLeafDescriptorSets_[frameIndex].find(leafType);
        if (frameIt != culledLeafDescriptorSets_[frameIndex].end()) {
            return frameIt->second;
        }
    }
    // Fall back to regular leaf descriptor set if culled version not found
    return getLeafDescriptorSet(frameIndex, leafType);
}

void TreeRenderer::recordLeafCulling(VkCommandBuffer cmd, uint32_t frameIndex,
                                      const TreeSystem& treeSystem,
                                      const TreeLODSystem* lodSystem,
                                      const glm::vec3& cameraPos,
                                      const glm::vec4* frustumPlanes) {
    // Skip if culling not enabled or no leaves
    if (!isLeafCullingEnabled()) {
        return;
    }

    const auto& leafRenderables = treeSystem.getLeafRenderables();
    const auto& leafDrawInfo = treeSystem.getLeafDrawInfo();

    if (leafRenderables.empty() || leafDrawInfo.empty()) {
        return;
    }

    // Count valid trees and build per-tree data for batched culling
    std::vector<TreeCullData> treeDataList;
    std::vector<TreeRenderDataGPU> treeRenderDataList;
    treeDataList.reserve(leafRenderables.size());
    treeRenderDataList.reserve(leafRenderables.size());

    uint32_t numTrees = 0;
    uint32_t totalLeafInstances = 0;
    uint32_t lodTreeIndex = 0;  // Index for LOD system lookup (matches tree instance order)

    for (const auto& renderable : leafRenderables) {
        if (renderable.leafInstanceIndex >= 0 &&
            static_cast<size_t>(renderable.leafInstanceIndex) < leafDrawInfo.size()) {
            const auto& drawInfo = leafDrawInfo[renderable.leafInstanceIndex];
            if (drawInfo.instanceCount > 0) {
                // Get LOD blend factor from LOD system
                float lodBlendFactor = 0.0f;
                if (lodSystem) {
                    lodBlendFactor = lodSystem->getBlendFactor(lodTreeIndex);
                }

                // Convert leaf type string to index (oak=0, ash=1, aspen=2, pine=3)
                uint32_t leafTypeIdx = LEAF_TYPE_OAK;  // default
                if (renderable.leafType == "ash") leafTypeIdx = LEAF_TYPE_ASH;
                else if (renderable.leafType == "aspen") leafTypeIdx = LEAF_TYPE_ASPEN;
                else if (renderable.leafType == "pine") leafTypeIdx = LEAF_TYPE_PINE;

                // Cull data for compute shader
                TreeCullData treeData{};
                treeData.treeModel = renderable.transform;
                treeData.inputFirstInstance = drawInfo.firstInstance;
                treeData.inputInstanceCount = drawInfo.instanceCount;
                treeData.treeIndex = numTrees;  // Index for render data lookup
                treeData.leafTypeIndex = leafTypeIdx;
                treeData.lodBlendFactor = lodBlendFactor;
                treeData._pad0 = 0;
                treeData._pad1 = 0;
                treeData._pad2 = 0;

                treeDataList.push_back(treeData);

                // Render data for vertex shader
                TreeRenderDataGPU renderData{};
                renderData.model = renderable.transform;
                renderData.tintAndParams = glm::vec4(renderable.leafTint, renderable.autumnHueShift);
                // Wind phase offset based on tree position for variation
                float windPhase = glm::fract(renderable.transform[3][0] * 0.1f + renderable.transform[3][2] * 0.1f) * 6.28318f;
                renderData.windPhaseAndLOD = glm::vec4(windPhase, lodBlendFactor, 0.0f, 0.0f);

                treeRenderDataList.push_back(renderData);

                totalLeafInstances += drawInfo.instanceCount;
                numTrees++;
            }
        }
        lodTreeIndex++;  // Always increment to stay in sync with tree instance order
    }
    if (numTrees == 0 || totalLeafInstances == 0) return;

    // Ensure culling buffers are allocated (lazy initialization based on actual leaf count)
    if (cullOutputBuffers_[0] == VK_NULL_HANDLE) {
        if (!createCullBuffers(totalLeafInstances, numTrees)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create cull buffers");
            return;
        }

        // Update compute descriptor sets with buffer bindings
        for (uint32_t f = 0; f < maxFramesInFlight_; ++f) {
            VkDescriptorBufferInfo inputInfo{};
            inputInfo.buffer = treeSystem.getLeafInstanceBuffer();
            inputInfo.offset = 0;
            inputInfo.range = VK_WHOLE_SIZE;

            VkDescriptorBufferInfo outputInfo{};
            outputInfo.buffer = cullOutputBuffers_[currentCullBufferSet_];
            outputInfo.offset = 0;
            outputInfo.range = VK_WHOLE_SIZE;

            VkDescriptorBufferInfo indirectInfo{};
            indirectInfo.buffer = cullIndirectBuffers_[currentCullBufferSet_];
            indirectInfo.offset = 0;
            indirectInfo.range = VK_WHOLE_SIZE;

            VkDescriptorBufferInfo uniformInfo{};
            uniformInfo.buffer = cullUniformBuffers_.buffers[f];
            uniformInfo.offset = 0;
            uniformInfo.range = sizeof(TreeLeafCullUniforms);

            VkDescriptorBufferInfo treeDataInfo{};
            treeDataInfo.buffer = treeDataBuffer_;
            treeDataInfo.offset = 0;
            treeDataInfo.range = VK_WHOLE_SIZE;

            std::array<VkWriteDescriptorSet, 5> writes{};

            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = cullDescriptorSets_[f];
            writes[0].dstBinding = Bindings::TREE_LEAF_CULL_INPUT;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &inputInfo;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = cullDescriptorSets_[f];
            writes[1].dstBinding = Bindings::TREE_LEAF_CULL_OUTPUT;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo = &outputInfo;

            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = cullDescriptorSets_[f];
            writes[2].dstBinding = Bindings::TREE_LEAF_CULL_INDIRECT;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[2].descriptorCount = 1;
            writes[2].pBufferInfo = &indirectInfo;

            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet = cullDescriptorSets_[f];
            writes[3].dstBinding = Bindings::TREE_LEAF_CULL_UNIFORMS;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[3].descriptorCount = 1;
            writes[3].pBufferInfo = &uniformInfo;

            writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[4].dstSet = cullDescriptorSets_[f];
            writes[4].dstBinding = Bindings::TREE_LEAF_CULL_TREES;
            writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[4].descriptorCount = 1;
            writes[4].pBufferInfo = &treeDataInfo;

            vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    // Reset SINGLE indirect draw command (instanceCount will be set by compute shader)
    VkDrawIndexedIndirectCommand resetCmd{};
    resetCmd.indexCount = 6;  // Quad indices
    resetCmd.instanceCount = 0;  // Will be atomically incremented by compute shader
    resetCmd.firstIndex = 0;
    resetCmd.vertexOffset = 0;
    resetCmd.firstInstance = 0;

    // Batch update: reset single indirect command
    vkCmdUpdateBuffer(cmd, cullIndirectBuffers_[currentCullBufferSet_],
                      0, sizeof(VkDrawIndexedIndirectCommand), &resetCmd);

    // Batch update: upload per-tree cull data SSBO
    vkCmdUpdateBuffer(cmd, treeDataBuffer_, 0,
                      numTrees * sizeof(TreeCullData), treeDataList.data());

    // Batch update: upload per-tree render data SSBO (for vertex shader)
    vkCmdUpdateBuffer(cmd, treeRenderDataBuffer_, 0,
                      numTrees * sizeof(TreeRenderDataGPU), treeRenderDataList.data());

    // Batch update: upload global uniforms
    TreeLeafCullUniforms uniforms{};
    uniforms.cameraPosition = glm::vec4(cameraPos, 0.0f);
    for (int i = 0; i < 6; ++i) {
        uniforms.frustumPlanes[i] = frustumPlanes[i];
    }
    uniforms.maxDrawDistance = leafMaxDrawDistance_;
    uniforms.lodTransitionStart = leafLodTransitionStart_;
    uniforms.lodTransitionEnd = leafLodTransitionEnd_;
    uniforms.maxLodDropRate = leafMaxLodDropRate_;
    uniforms.numTrees = numTrees;
    uniforms.totalLeafInstances = totalLeafInstances;
    uniforms.maxLeavesPerType = maxLeavesPerType_;
    uniforms._pad1 = 0;

    vkCmdUpdateBuffer(cmd, cullUniformBuffers_.buffers[frameIndex], 0,
                      sizeof(TreeLeafCullUniforms), &uniforms);

    // Single barrier for all buffer updates
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_UNIFORM_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    // Update compute descriptor sets to use current buffer set (double-buffering)
    {
        VkDescriptorBufferInfo outputInfo{};
        outputInfo.buffer = cullOutputBuffers_[currentCullBufferSet_];
        outputInfo.offset = 0;
        outputInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo indirectInfo{};
        indirectInfo.buffer = cullIndirectBuffers_[currentCullBufferSet_];
        indirectInfo.offset = 0;
        indirectInfo.range = VK_WHOLE_SIZE;

        std::array<VkWriteDescriptorSet, 2> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = cullDescriptorSets_[frameIndex];
        writes[0].dstBinding = Bindings::TREE_LEAF_CULL_OUTPUT;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &outputInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = cullDescriptorSets_[frameIndex];
        writes[1].dstBinding = Bindings::TREE_LEAF_CULL_INDIRECT;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &indirectInfo;

        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // =========================================================================
    // Phase 1: Cell Culling (Spatial Partitioning)
    // Cull cells before leaf culling to reduce workload
    // =========================================================================
    if (isSpatialIndexEnabled() && cellCullPipeline_.get() != VK_NULL_HANDLE) {
        // Update cell culling uniforms
        TreeCellCullUniforms cellUniforms{};
        cellUniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);
        for (int i = 0; i < 6; ++i) {
            cellUniforms.frustumPlanes[i] = frustumPlanes[i];
        }
        cellUniforms.maxDrawDistance = 250.0f;  // Match leaf culling distance
        cellUniforms.numCells = spatialIndex_->getCellCount();
        cellUniforms.treesPerWorkgroup = 64;    // Tunable parameter

        void* mappedData;
        vmaMapMemory(allocator_, cellCullUniformBuffers_.allocations[frameIndex], &mappedData);
        memcpy(mappedData, &cellUniforms, sizeof(TreeCellCullUniforms));
        vmaUnmapMemory(allocator_, cellCullUniformBuffers_.allocations[frameIndex]);

        // Memory barrier for uniform buffer update
        VkMemoryBarrier cellUniformBarrier{};
        cellUniformBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        cellUniformBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        cellUniformBarrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &cellUniformBarrier, 0, nullptr, 0, nullptr);

        // Bind cell culling pipeline and dispatch
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cellCullPipeline_.get());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cellCullPipelineLayout_.get(),
                                0, 1, &cellCullDescriptorSets_[frameIndex], 0, nullptr);

        // One thread per cell
        uint32_t cellWorkgroups = (cellUniforms.numCells + 255) / 256;
        vkCmdDispatch(cmd, cellWorkgroups, 1, 1);

        // Memory barrier for cell culling output
        VkMemoryBarrier cellBarrier{};
        cellBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        cellBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        cellBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &cellBarrier, 0, nullptr, 0, nullptr);

        // =========================================================================
        // Phase 3: Tree Filtering (Two-Phase Culling)
        // Filter trees from visible cells into compacted visible tree list
        // =========================================================================
        if (twoPhaseLeafCullingEnabled_ && treeFilterPipeline_.get() != VK_NULL_HANDLE &&
            visibleTreeBuffer_ != VK_NULL_HANDLE && !treeFilterDescriptorSets_.empty()) {

            // Update tree filter uniforms
            TreeFilterUniforms filterUniforms{};
            filterUniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);
            for (int i = 0; i < 6; ++i) {
                filterUniforms.frustumPlanes[i] = frustumPlanes[i];
            }
            filterUniforms.maxDrawDistance = leafMaxDrawDistance_;
            filterUniforms.maxTreesPerCell = 64;  // Reasonable limit

            void* filterMappedData;
            vmaMapMemory(allocator_, treeFilterUniformBuffers_.allocations[frameIndex], &filterMappedData);
            memcpy(filterMappedData, &filterUniforms, sizeof(TreeFilterUniforms));
            vmaUnmapMemory(allocator_, treeFilterUniformBuffers_.allocations[frameIndex]);

            // Memory barrier for uniform buffer update
            VkMemoryBarrier filterUniformBarrier{};
            filterUniformBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            filterUniformBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
            filterUniformBarrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 1, &filterUniformBarrier, 0, nullptr, 0, nullptr);

            // Bind tree filter pipeline and dispatch
            // Dispatch one workgroup per visible cell (indirectly using cell cull output)
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, treeFilterPipeline_.get());
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, treeFilterPipelineLayout_.get(),
                                    0, 1, &treeFilterDescriptorSets_[frameIndex], 0, nullptr);

            // Use indirect dispatch from cell culling (dispatchX = number of visible cells)
            vkCmdDispatchIndirect(cmd, cellCullIndirectBuffer_, 0);

            // Memory barrier for tree filter output
            VkMemoryBarrier treeFilterBarrier{};
            treeFilterBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            treeFilterBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            treeFilterBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 1, &treeFilterBarrier, 0, nullptr, 0, nullptr);

            // TODO: Future enhancement - use Phase 3 leaf culling with indirect dispatch
            // vkCmdDispatchIndirect(cmd, leafCullIndirectDispatch_, 0);
        }
    }

    // Bind compute pipeline and descriptor set (single-phase leaf culling)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipelineLayout_.get(),
                            0, 1, &cullDescriptorSets_[frameIndex], 0, nullptr);

    // SINGLE dispatch for all leaf instances across all trees
    // Note: When Phase 3 is fully implemented, this will be replaced by indirect dispatch
    uint32_t workgroupCount = (totalLeafInstances + 255) / 256;
    vkCmdDispatch(cmd, workgroupCount, 1, 1);

    // Memory barrier for compute -> graphics
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    // Note: Buffer set swap happens in render() after drawing
}

void TreeRenderer::render(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                          const TreeSystem& treeSystem, const TreeLODSystem* lodSystem) {
    const auto& branchRenderables = treeSystem.getBranchRenderables();
    const auto& leafRenderables = treeSystem.getLeafRenderables();

    if (branchRenderables.empty() && leafRenderables.empty()) return;

    // Render branches
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, branchPipeline_.get());

    std::string lastBarkType;
    uint32_t branchTreeIndex = 0;
    for (const auto& renderable : branchRenderables) {
        // Skip if LOD system says this tree should be pure impostor (no full geometry)
        if (lodSystem && !lodSystem->shouldRenderFullGeometry(branchTreeIndex)) {
            branchTreeIndex++;
            continue;
        }
        // Bind descriptor set for this bark type if different from last
        if (renderable.barkType != lastBarkType) {
            VkDescriptorSet descriptorSet = getBranchDescriptorSet(frameIndex, renderable.barkType);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, branchPipelineLayout_.get(),
                                    0, 1, &descriptorSet, 0, nullptr);
            lastBarkType = renderable.barkType;
        }

        TreeBranchPushConstants push{};
        push.model = renderable.transform;
        push.time = time;
        push.lodBlendFactor = lodSystem ? lodSystem->getBlendFactor(branchTreeIndex) : 0.0f;
        push.barkTint = glm::vec3(1.0f);
        push.roughnessScale = renderable.roughness;

        vkCmdPushConstants(cmd, branchPipelineLayout_.get(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(TreeBranchPushConstants), &push);

        if (renderable.mesh) {
            VkBuffer vertexBuffers[] = {renderable.mesh->getVertexBuffer()};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, renderable.mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, renderable.mesh->getIndexCount(), 1, 0, 0, 0);
        }
        branchTreeIndex++;
    }

    // Render leaves with instancing
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, leafPipeline_.get());

    // Get leaf draw info from tree system
    const auto& leafDrawInfo = treeSystem.getLeafDrawInfo();
    const Mesh& sharedQuad = treeSystem.getSharedLeafQuadMesh();

    // Get global autumn setting from LOD system (affects both impostors and full geometry)
    float globalAutumnHueShift = lodSystem ? lodSystem->getLODSettings().autumnHueShift : 0.0f;

    // Bind shared quad mesh once (used for all leaf instances)
    if (sharedQuad.getIndexCount() > 0) {
        VkBuffer vertexBuffers[] = {sharedQuad.getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, sharedQuad.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
    }

    // Check if we should use GPU culling with indirect draws
    // Ensure culled descriptor sets are actually populated (not just resized)
    bool hasCulledDescriptors = !culledLeafDescriptorSets_.empty() &&
                                 frameIndex < culledLeafDescriptorSets_.size() &&
                                 !culledLeafDescriptorSets_[frameIndex].empty();
    bool useCulledPath = isLeafCullingEnabled() &&
                         hasCulledDescriptors &&
                         cullIndirectBuffers_[currentCullBufferSet_] != VK_NULL_HANDLE;

    if (useCulledPath) {
        // GPU-culled multi-indirect draw path
        // One indirect draw per leaf type, each binding appropriate texture

        // Leaf type names in order matching shader indices (oak=0, ash=1, aspen=2, pine=3)
        static const std::string leafTypeNames[NUM_LEAF_TYPES] = {"oak", "ash", "aspen", "pine"};

        // Get alpha test threshold from first renderable (or default)
        float alphaTest = 0.5f;
        if (!leafRenderables.empty()) {
            alphaTest = leafRenderables[0].alphaTestThreshold > 0.0f ?
                        leafRenderables[0].alphaTestThreshold : 0.5f;
        }

        // Issue one indirect draw per leaf type
        for (uint32_t leafType = 0; leafType < NUM_LEAF_TYPES; ++leafType) {
            // Get descriptor set for this leaf type's texture
            VkDescriptorSet descriptorSet = getCulledLeafDescriptorSet(frameIndex, leafTypeNames[leafType]);
            if (descriptorSet == VK_NULL_HANDLE) continue;

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, leafPipelineLayout_.get(),
                                    0, 1, &descriptorSet, 0, nullptr);

            // Push constants (same for all types in this implementation)
            TreeLeafPushConstants push{};
            push.time = time;
            push.alphaTest = alphaTest;

            vkCmdPushConstants(cmd, leafPipelineLayout_.get(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(TreeLeafPushConstants), &push);

            // Indirect draw for this leaf type
            // Offset to the correct command in the indirect buffer
            VkDeviceSize commandOffset = leafType * sizeof(VkDrawIndexedIndirectCommand);
            vkCmdDrawIndexedIndirect(cmd, cullIndirectBuffers_[currentCullBufferSet_],
                                     commandOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
        }

        // Swap buffer sets for next frame (after all indirect draws complete)
        currentCullBufferSet_ = (currentCullBufferSet_ + 1) % CULL_BUFFER_SET_COUNT;
    } else {
        // GPU-driven path not available - leaves will be skipped
        // (branches still render, and impostors handle distant trees)
        // This fallback can be enabled later with a non-GPU-driven path if needed
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Leaf culling not available - leaves will not render for close trees");
    }
}

void TreeRenderer::renderShadows(VkCommandBuffer cmd, uint32_t frameIndex,
                                 const TreeSystem& treeSystem, int cascadeIndex,
                                 const TreeLODSystem* lodSystem) {
    const auto& branchRenderables = treeSystem.getBranchRenderables();
    const auto& leafRenderables = treeSystem.getLeafRenderables();

    if (branchRenderables.empty() && leafRenderables.empty()) {
        return;
    }

    // Render branch shadows
    if (!branchRenderables.empty() && branchShadowPipeline_.get() != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, branchShadowPipeline_.get());

        // Bind descriptor set for UBO access
        VkDescriptorSet branchSet = defaultBranchDescriptorSets_[frameIndex];
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                branchShadowPipelineLayout_.get(), 0, 1, &branchSet, 0, nullptr);

        uint32_t branchTreeIndex = 0;
        for (const auto& renderable : branchRenderables) {
            // Skip if LOD system says this tree should be pure impostor (no full geometry)
            if (lodSystem && !lodSystem->shouldRenderFullGeometry(branchTreeIndex)) {
                branchTreeIndex++;
                continue;
            }

            TreeBranchShadowPushConstants push{};
            push.model = renderable.transform;
            push.cascadeIndex = cascadeIndex;

            vkCmdPushConstants(cmd, branchShadowPipelineLayout_.get(),
                               VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(TreeBranchShadowPushConstants), &push);

            if (renderable.mesh) {
                VkBuffer vertexBuffers[] = {renderable.mesh->getVertexBuffer()};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(cmd, renderable.mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, renderable.mesh->getIndexCount(), 1, 0, 0, 0);
            }
            branchTreeIndex++;
        }
    }

    // Render leaf shadows with instancing (with alpha test)
    if (!leafRenderables.empty() && leafShadowPipeline_.get() != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, leafShadowPipeline_.get());

        const Mesh& sharedQuad = treeSystem.getSharedLeafQuadMesh();

        // Bind shared quad mesh once
        if (sharedQuad.getIndexCount() > 0) {
            VkBuffer vertexBuffers[] = {sharedQuad.getVertexBuffer()};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, sharedQuad.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        }

        // Check if we should use GPU culling with indirect draws for shadows
        bool hasCulledDescriptors = !culledLeafDescriptorSets_.empty() &&
                                     frameIndex < culledLeafDescriptorSets_.size() &&
                                     !culledLeafDescriptorSets_[frameIndex].empty();
        bool useCulledPath = isLeafCullingEnabled() &&
                             hasCulledDescriptors &&
                             cullIndirectBuffers_[currentCullBufferSet_] != VK_NULL_HANDLE;

        if (useCulledPath && !leafRenderables.empty()) {
            // GPU-culled multi-indirect draw path for shadows
            // One indirect draw per leaf type

            // Leaf type names in order matching shader indices (oak=0, ash=1, aspen=2, pine=3)
            static const std::string leafTypeNames[NUM_LEAF_TYPES] = {"oak", "ash", "aspen", "pine"};

            // Get alpha test threshold from first renderable (or default)
            float alphaTest = 0.5f;
            if (!leafRenderables.empty()) {
                alphaTest = leafRenderables[0].alphaTestThreshold > 0.0f ?
                            leafRenderables[0].alphaTestThreshold : 0.5f;
            }

            // Issue one indirect draw per leaf type
            for (uint32_t leafType = 0; leafType < NUM_LEAF_TYPES; ++leafType) {
                // Get descriptor set for this leaf type's texture
                VkDescriptorSet leafSet = getCulledLeafDescriptorSet(frameIndex, leafTypeNames[leafType]);
                if (leafSet == VK_NULL_HANDLE) continue;

                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        leafShadowPipelineLayout_.get(), 0, 1, &leafSet, 0, nullptr);

                // Push constants
                TreeLeafShadowPushConstants push{};
                push.cascadeIndex = cascadeIndex;
                push.alphaTest = alphaTest;

                vkCmdPushConstants(cmd, leafShadowPipelineLayout_.get(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(TreeLeafShadowPushConstants), &push);

                // Indirect draw for this leaf type
                VkDeviceSize commandOffset = leafType * sizeof(VkDrawIndexedIndirectCommand);
                vkCmdDrawIndexedIndirect(cmd, cullIndirectBuffers_[currentCullBufferSet_],
                                         commandOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
            }
        } else {
            // Direct draw path (fallback when culling not available)
            const auto& leafDrawInfo = treeSystem.getLeafDrawInfo();
            std::string lastLeafType;
            uint32_t leafTreeIndex = 0;
            for (const auto& renderable : leafRenderables) {
                // Skip if no leaf instances for this tree
                if (renderable.leafInstanceIndex < 0 ||
                    static_cast<size_t>(renderable.leafInstanceIndex) >= leafDrawInfo.size()) {
                    leafTreeIndex++;
                    continue;
                }

                const auto& drawInfo = leafDrawInfo[renderable.leafInstanceIndex];
                if (drawInfo.instanceCount == 0) {
                    leafTreeIndex++;
                    continue;
                }

                // Skip if LOD system says this tree should be pure impostor (no full geometry)
                if (lodSystem && !lodSystem->shouldRenderFullGeometry(leafTreeIndex)) {
                    leafTreeIndex++;
                    continue;
                }

                // Bind descriptor set for this leaf type (for alpha test texture)
                if (renderable.leafType != lastLeafType) {
                    VkDescriptorSet leafSet = getLeafDescriptorSet(frameIndex, renderable.leafType);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            leafShadowPipelineLayout_.get(), 0, 1, &leafSet, 0, nullptr);
                    lastLeafType = renderable.leafType;
                }

                TreeLeafShadowPushConstants push{};
                push.cascadeIndex = cascadeIndex;
                push.alphaTest = renderable.alphaTestThreshold > 0.0f ? renderable.alphaTestThreshold : 0.5f;

                vkCmdPushConstants(cmd, leafShadowPipelineLayout_.get(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(TreeLeafShadowPushConstants), &push);

                // Draw instanced (fallback path - uses original tree-local SSBO)
                vkCmdDrawIndexed(cmd, sharedQuad.getIndexCount(), drawInfo.instanceCount, 0, 0, 0);
                leafTreeIndex++;
            }
        }
    }
}

void TreeRenderer::setExtent(VkExtent2D newExtent) {
    extent_ = newExtent;
    // TODO: Recreate pipelines with new extent if needed
}
