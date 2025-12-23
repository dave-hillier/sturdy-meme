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
    // Clean up tree data buffer
    if (treeDataBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, treeDataBuffer_, treeDataAllocation_);
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
    // BINDING_TREE_GFX_LEAF_INSTANCES  = 9  - Leaf instance SSBO

    std::vector<VkDescriptorSetLayoutBinding> leafBindings = {
        {Bindings::TREE_GFX_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {Bindings::TREE_GFX_SHADOW_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {Bindings::TREE_GFX_WIND_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {Bindings::TREE_GFX_LEAF_ALBEDO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {Bindings::TREE_GFX_LEAF_INSTANCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
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

    // Calculate buffer sizes
    cullOutputBufferSize_ = maxLeafInstances * sizeof(LeafInstanceGPU);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    // Create double-buffered output buffers
    for (uint32_t i = 0; i < CULL_BUFFER_SET_COUNT; ++i) {
        bufferInfo.size = cullOutputBufferSize_;
        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                            &cullOutputBuffers_[i], &cullOutputAllocations_[i], nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create cull output buffer %u", i);
            return false;
        }
    }

    // Create indirect draw buffers (one command per tree)
    VkBufferCreateInfo indirectInfo{};
    indirectInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indirectInfo.size = numTrees * sizeof(VkDrawIndexedIndirectCommand);
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

    // Create per-tree data buffer (SSBO for batched culling)
    treeDataBufferSize_ = numTrees * sizeof(TreeCullData);
    VkBufferCreateInfo treeDataInfo{};
    treeDataInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    treeDataInfo.size = treeDataBufferSize_;
    treeDataInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    treeDataInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vmaCreateBuffer(allocator_, &treeDataInfo, &allocInfo,
                        &treeDataBuffer_, &treeDataAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to create tree data buffer");
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

    // Initialize per-tree output offsets
    perTreeOutputOffsets_.resize(numTrees, 0);

    SDL_Log("TreeRenderer: Created leaf culling buffers (max %u instances, %u trees, %.2f MB)",
            maxLeafInstances, numTrees,
            static_cast<float>(cullOutputBufferSize_ * CULL_BUFFER_SET_COUNT) / (1024.0f * 1024.0f));
    return true;
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

    // Use culled output buffer for leaf instances
    // IMPORTANT: Must update SSBO binding every frame because we double-buffer
    VkDescriptorBufferInfo leafInstanceInfo{};
    leafInstanceInfo.buffer = cullOutputBuffers_[currentCullBufferSet_];
    leafInstanceInfo.offset = 0;
    leafInstanceInfo.range = VK_WHOLE_SIZE;

    std::array<VkWriteDescriptorSet, 5> writes{};

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
    treeDataList.reserve(leafRenderables.size());

    uint32_t numTrees = 0;
    uint32_t totalLeafInstances = 0;
    uint32_t outputOffset = 0;

    for (const auto& renderable : leafRenderables) {
        if (renderable.leafInstanceIndex >= 0 &&
            static_cast<size_t>(renderable.leafInstanceIndex) < leafDrawInfo.size()) {
            const auto& drawInfo = leafDrawInfo[renderable.leafInstanceIndex];
            if (drawInfo.instanceCount > 0) {
                TreeCullData treeData{};
                treeData.treeModel = renderable.transform;
                treeData.inputFirstInstance = drawInfo.firstInstance;
                treeData.inputInstanceCount = drawInfo.instanceCount;
                treeData.outputBaseOffset = outputOffset;
                treeData.treeIndex = numTrees;

                treeDataList.push_back(treeData);

                // Store per-tree output offset for rendering
                if (numTrees < perTreeOutputOffsets_.size()) {
                    perTreeOutputOffsets_[numTrees] = outputOffset;
                }

                outputOffset += drawInfo.instanceCount;
                totalLeafInstances += drawInfo.instanceCount;
                numTrees++;
            }
        }
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

    // Reset all per-tree indirect draw commands
    std::vector<VkDrawIndexedIndirectCommand> resetCmds(numTreesForIndirect_);
    for (auto& resetCmd : resetCmds) {
        resetCmd.indexCount = 6;  // Quad indices
        resetCmd.instanceCount = 0;  // Will be atomically incremented by compute shader
        resetCmd.firstIndex = 0;
        resetCmd.vertexOffset = 0;
        resetCmd.firstInstance = 0;
    }

    // Batch update: reset indirect commands
    vkCmdUpdateBuffer(cmd, cullIndirectBuffers_[currentCullBufferSet_],
                      0, numTreesForIndirect_ * sizeof(VkDrawIndexedIndirectCommand), resetCmds.data());

    // Batch update: upload per-tree data SSBO
    vkCmdUpdateBuffer(cmd, treeDataBuffer_, 0,
                      numTrees * sizeof(TreeCullData), treeDataList.data());

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
    uniforms._pad0 = 0;
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

    // Bind compute pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipelineLayout_.get(),
                            0, 1, &cullDescriptorSets_[frameIndex], 0, nullptr);

    // SINGLE dispatch for all leaf instances across all trees
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
                         !perTreeOutputOffsets_.empty() &&
                         hasCulledDescriptors &&
                         cullIndirectBuffers_[currentCullBufferSet_] != VK_NULL_HANDLE;

    if (useCulledPath) {
        // GPU-culled indirect draw path
        // Use per-type culled descriptor sets (with culled output buffer as SSBO)
        std::string lastLeafType;
        uint32_t treeIdx = 0;
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
                treeIdx++;
                continue;
            }

            // Ensure we have an offset for this tree
            if (treeIdx >= perTreeOutputOffsets_.size()) break;

            // Bind descriptor set for this leaf type if different from last
            // Use per-type culled descriptor set which has the output buffer as SSBO
            if (renderable.leafType != lastLeafType) {
                VkDescriptorSet descriptorSet = getCulledLeafDescriptorSet(frameIndex, renderable.leafType);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, leafPipelineLayout_.get(),
                                        0, 1, &descriptorSet, 0, nullptr);
                lastLeafType = renderable.leafType;
            }

            TreeLeafPushConstants push{};
            push.model = renderable.transform;
            push.time = time;
            push.lodBlendFactor = lodSystem ? lodSystem->getBlendFactor(leafTreeIndex) : 0.0f;
            push.leafTint = renderable.leafTint;
            push.alphaTest = renderable.alphaTestThreshold > 0.0f ? renderable.alphaTestThreshold : 0.5f;
            push.firstInstance = static_cast<int32_t>(perTreeOutputOffsets_[treeIdx]);
            push.autumnHueShift = std::max(globalAutumnHueShift, renderable.autumnHueShift);

            vkCmdPushConstants(cmd, leafPipelineLayout_.get(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(TreeLeafPushConstants), &push);

            // Indirect draw from per-tree command
            VkDeviceSize indirectOffset = treeIdx * sizeof(VkDrawIndexedIndirectCommand);
            vkCmdDrawIndexedIndirect(cmd, cullIndirectBuffers_[currentCullBufferSet_],
                                     indirectOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
            leafTreeIndex++;
            treeIdx++;
        }

        // Swap buffer sets for next frame (after all indirect draws complete)
        currentCullBufferSet_ = (currentCullBufferSet_ + 1) % CULL_BUFFER_SET_COUNT;
    } else {
        // Direct draw path (fallback when culling not available)
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

            // Bind descriptor set for this leaf type if different from last
            if (renderable.leafType != lastLeafType) {
                VkDescriptorSet descriptorSet = getLeafDescriptorSet(frameIndex, renderable.leafType);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, leafPipelineLayout_.get(),
                                        0, 1, &descriptorSet, 0, nullptr);
                lastLeafType = renderable.leafType;
            }

            TreeLeafPushConstants push{};
            push.model = renderable.transform;
            push.time = time;
            push.lodBlendFactor = lodSystem ? lodSystem->getBlendFactor(leafTreeIndex) : 0.0f;
            push.leafTint = renderable.leafTint;
            push.alphaTest = renderable.alphaTestThreshold > 0.0f ? renderable.alphaTestThreshold : 0.5f;
            push.firstInstance = static_cast<int32_t>(drawInfo.firstInstance);
            push.autumnHueShift = std::max(globalAutumnHueShift, renderable.autumnHueShift);

            vkCmdPushConstants(cmd, leafPipelineLayout_.get(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(TreeLeafPushConstants), &push);

            // Draw instanced: 6 indices for one quad, N instances for this tree's leaves
            vkCmdDrawIndexed(cmd, sharedQuad.getIndexCount(), drawInfo.instanceCount, 0, 0, 0);
            leafTreeIndex++;
        }
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

        // Get leaf draw info from tree system
        const auto& leafDrawInfo = treeSystem.getLeafDrawInfo();
        const Mesh& sharedQuad = treeSystem.getSharedLeafQuadMesh();

        // Bind shared quad mesh once
        if (sharedQuad.getIndexCount() > 0) {
            VkBuffer vertexBuffers[] = {sharedQuad.getVertexBuffer()};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, sharedQuad.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        }

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
            push.model = renderable.transform;
            push.cascadeIndex = cascadeIndex;
            push.alphaTest = renderable.alphaTestThreshold > 0.0f ? renderable.alphaTestThreshold : 0.5f;
            push.firstInstance = static_cast<int32_t>(drawInfo.firstInstance);

            vkCmdPushConstants(cmd, leafShadowPipelineLayout_.get(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(TreeLeafShadowPushConstants), &push);

            // Draw instanced
            vkCmdDrawIndexed(cmd, sharedQuad.getIndexCount(), drawInfo.instanceCount, 0, 0, 0);
            leafTreeIndex++;
        }
    }
}

void TreeRenderer::setExtent(VkExtent2D newExtent) {
    extent_ = newExtent;
    // TODO: Recreate pipelines with new extent if needed
}
