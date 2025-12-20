#include "TreeRenderer.h"
#include "GraphicsPipelineFactory.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "Mesh.h"
#include "Bindings.h"
#include <SDL3/SDL_log.h>

std::unique_ptr<TreeRenderer> TreeRenderer::create(const InitInfo& info) {
    auto renderer = std::unique_ptr<TreeRenderer>(new TreeRenderer());
    if (!renderer->initInternal(info)) {
        return nullptr;
    }
    return renderer;
}

TreeRenderer::~TreeRenderer() {
    // RAII handles cleanup
}

bool TreeRenderer::initInternal(const InitInfo& info) {
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    resourcePath_ = info.resourcePath;
    extent_ = info.extent;

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
    // BINDING_TREE_GFX_UBO           = 0  - Scene uniforms
    // BINDING_TREE_GFX_SHADOW_MAP    = 2  - Shadow map
    // BINDING_TREE_GFX_WIND_UBO      = 3  - Wind uniforms
    // BINDING_TREE_GFX_LEAF_ALBEDO   = 8  - Leaf albedo texture

    std::vector<VkDescriptorSetLayoutBinding> leafBindings = {
        {Bindings::TREE_GFX_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {Bindings::TREE_GFX_SHADOW_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {Bindings::TREE_GFX_WIND_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {Bindings::TREE_GFX_LEAF_ALBEDO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
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

    // Create leaf pipeline (with alpha blending and double-sided)
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
        .setBlendMode(GraphicsPipelineFactory::BlendMode::Alpha)
        .setCullMode(VK_CULL_MODE_NONE)  // Double-sided leaves
        .build(rawLeafPipeline);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tree leaf pipeline");
        return false;
    }
    leafPipeline_ = ManagedPipeline::fromRaw(device_, rawLeafPipeline);

    // TODO: Create shadow pipelines
    // For now, we'll skip shadow rendering for trees with custom shaders

    SDL_Log("TreeRenderer: Created branch and leaf pipelines");
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

void TreeRenderer::updateDescriptorSets(
    uint32_t frameIndex,
    VkBuffer uniformBuffer,
    VkBuffer windBuffer,
    VkImageView shadowMapView,
    VkSampler shadowSampler,
    VkImageView barkAlbedo,
    VkImageView barkNormal,
    VkImageView barkRoughness,
    VkImageView barkAO,
    VkSampler barkSampler,
    VkImageView leafAlbedo,
    VkSampler leafSampler) {

    // Update default branch descriptor set
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

    VkDescriptorImageInfo leafAlbedoInfo{};
    leafAlbedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    leafAlbedoInfo.imageView = leafAlbedo;
    leafAlbedoInfo.sampler = leafSampler;

    // Branch descriptor writes (7 bindings)
    std::array<VkWriteDescriptorSet, 7> branchWrites{};

    // UBO binding (binding 0)
    branchWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    branchWrites[0].dstSet = defaultBranchDescriptorSets_[frameIndex];
    branchWrites[0].dstBinding = Bindings::TREE_GFX_UBO;
    branchWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    branchWrites[0].descriptorCount = 1;
    branchWrites[0].pBufferInfo = &uboInfo;

    // Shadow map binding (binding 2)
    branchWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    branchWrites[1].dstSet = defaultBranchDescriptorSets_[frameIndex];
    branchWrites[1].dstBinding = Bindings::TREE_GFX_SHADOW_MAP;
    branchWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    branchWrites[1].descriptorCount = 1;
    branchWrites[1].pImageInfo = &shadowInfo;

    // Wind UBO binding (binding 3)
    branchWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    branchWrites[2].dstSet = defaultBranchDescriptorSets_[frameIndex];
    branchWrites[2].dstBinding = Bindings::TREE_GFX_WIND_UBO;
    branchWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    branchWrites[2].descriptorCount = 1;
    branchWrites[2].pBufferInfo = &windInfo;

    // Bark albedo (binding 4)
    branchWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    branchWrites[3].dstSet = defaultBranchDescriptorSets_[frameIndex];
    branchWrites[3].dstBinding = Bindings::TREE_GFX_BARK_ALBEDO;
    branchWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    branchWrites[3].descriptorCount = 1;
    branchWrites[3].pImageInfo = &barkAlbedoInfo;

    // Bark normal (binding 5)
    branchWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    branchWrites[4].dstSet = defaultBranchDescriptorSets_[frameIndex];
    branchWrites[4].dstBinding = Bindings::TREE_GFX_BARK_NORMAL;
    branchWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    branchWrites[4].descriptorCount = 1;
    branchWrites[4].pImageInfo = &barkNormalInfo;

    // Bark roughness (binding 6)
    branchWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    branchWrites[5].dstSet = defaultBranchDescriptorSets_[frameIndex];
    branchWrites[5].dstBinding = Bindings::TREE_GFX_BARK_ROUGHNESS;
    branchWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    branchWrites[5].descriptorCount = 1;
    branchWrites[5].pImageInfo = &barkRoughnessInfo;

    // Bark AO (binding 7)
    branchWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    branchWrites[6].dstSet = defaultBranchDescriptorSets_[frameIndex];
    branchWrites[6].dstBinding = Bindings::TREE_GFX_BARK_AO;
    branchWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    branchWrites[6].descriptorCount = 1;
    branchWrites[6].pImageInfo = &barkAOInfo;

    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(branchWrites.size()), branchWrites.data(), 0, nullptr);

    // Leaf descriptor writes (4 bindings)
    std::array<VkWriteDescriptorSet, 4> leafWrites{};

    // UBO binding (binding 0)
    leafWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    leafWrites[0].dstSet = defaultLeafDescriptorSets_[frameIndex];
    leafWrites[0].dstBinding = Bindings::TREE_GFX_UBO;
    leafWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    leafWrites[0].descriptorCount = 1;
    leafWrites[0].pBufferInfo = &uboInfo;

    // Shadow map binding (binding 2)
    leafWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    leafWrites[1].dstSet = defaultLeafDescriptorSets_[frameIndex];
    leafWrites[1].dstBinding = Bindings::TREE_GFX_SHADOW_MAP;
    leafWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    leafWrites[1].descriptorCount = 1;
    leafWrites[1].pImageInfo = &shadowInfo;

    // Wind UBO binding (binding 3)
    leafWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    leafWrites[2].dstSet = defaultLeafDescriptorSets_[frameIndex];
    leafWrites[2].dstBinding = Bindings::TREE_GFX_WIND_UBO;
    leafWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    leafWrites[2].descriptorCount = 1;
    leafWrites[2].pBufferInfo = &windInfo;

    // Leaf albedo (binding 8)
    leafWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    leafWrites[3].dstSet = defaultLeafDescriptorSets_[frameIndex];
    leafWrites[3].dstBinding = Bindings::TREE_GFX_LEAF_ALBEDO;
    leafWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    leafWrites[3].descriptorCount = 1;
    leafWrites[3].pImageInfo = &leafAlbedoInfo;

    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(leafWrites.size()), leafWrites.data(), 0, nullptr);
}

void TreeRenderer::render(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                          const TreeSystem& treeSystem) {
    const auto& branchRenderables = treeSystem.getBranchRenderables();
    const auto& leafRenderables = treeSystem.getLeafRenderables();

    if (branchRenderables.empty() && leafRenderables.empty()) return;

    // Render branches
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, branchPipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, branchPipelineLayout_.get(),
                            0, 1, &defaultBranchDescriptorSets_[frameIndex], 0, nullptr);

    for (const auto& renderable : branchRenderables) {
        // Use the pre-computed transform from the renderable
        TreeBranchPushConstants push{};
        push.model = renderable.transform;
        push.time = time;
        push.barkTint = glm::vec3(1.0f);
        push.roughnessScale = renderable.roughness;

        vkCmdPushConstants(cmd, branchPipelineLayout_.get(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(TreeBranchPushConstants), &push);

        // Bind vertex/index buffers and draw
        if (renderable.mesh) {
            VkBuffer vertexBuffers[] = {renderable.mesh->getVertexBuffer()};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, renderable.mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, renderable.mesh->getIndexCount(), 1, 0, 0, 0);
        }
    }

    // Render leaves
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, leafPipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, leafPipelineLayout_.get(),
                            0, 1, &defaultLeafDescriptorSets_[frameIndex], 0, nullptr);

    for (const auto& renderable : leafRenderables) {
        TreeLeafPushConstants push{};
        push.model = renderable.transform;
        push.time = time;
        push.leafTint = glm::vec3(1.0f);
        push.alphaTest = renderable.alphaTestThreshold > 0.0f ? renderable.alphaTestThreshold : 0.5f;

        vkCmdPushConstants(cmd, leafPipelineLayout_.get(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(TreeLeafPushConstants), &push);

        if (renderable.mesh) {
            VkBuffer vertexBuffers[] = {renderable.mesh->getVertexBuffer()};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, renderable.mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, renderable.mesh->getIndexCount(), 1, 0, 0, 0);
        }
    }
}

void TreeRenderer::renderShadows(VkCommandBuffer cmd, uint32_t frameIndex,
                                 const TreeSystem& treeSystem, int cascadeIndex) {
    // TODO: Implement shadow rendering with tree shadow shaders
    // For now, trees won't cast shadows with the new renderer
}

void TreeRenderer::setExtent(VkExtent2D newExtent) {
    extent_ = newExtent;
    // TODO: Recreate pipelines with new extent if needed
}
