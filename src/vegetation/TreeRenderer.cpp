#include "TreeRenderer.h"
#include "GraphicsPipelineFactory.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "Mesh.h"
#include "Bindings.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <algorithm>

std::unique_ptr<TreeRenderer> TreeRenderer::create(const InitInfo& info) {
    auto renderer = std::unique_ptr<TreeRenderer>(new TreeRenderer());
    if (!renderer->initInternal(info)) {
        return nullptr;
    }
    return renderer;
}

TreeRenderer::~TreeRenderer() {
    // leafCulling_ cleaned up by unique_ptr
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

    // Create leaf culling subsystem
    TreeLeafCulling::InitInfo cullInfo{};
    cullInfo.device = info.device;
    cullInfo.physicalDevice = info.physicalDevice;
    cullInfo.allocator = info.allocator;
    cullInfo.descriptorPool = info.descriptorPool;
    cullInfo.resourcePath = info.resourcePath;
    cullInfo.maxFramesInFlight = info.maxFramesInFlight;

    leafCulling_ = TreeLeafCulling::create(cullInfo);
    if (!leafCulling_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Leaf culling not available, using direct rendering");
    }

    // Create branch shadow culling subsystem
    if (branchShadowInstancedPipeline_.get() != VK_NULL_HANDLE) {
        TreeBranchCulling::InitInfo branchCullInfo{};
        branchCullInfo.device = info.device;
        branchCullInfo.physicalDevice = info.physicalDevice;
        branchCullInfo.allocator = info.allocator;
        branchCullInfo.descriptorPool = info.descriptorPool;
        branchCullInfo.resourcePath = info.resourcePath;
        branchCullInfo.maxFramesInFlight = info.maxFramesInFlight;

        branchShadowCulling_ = TreeBranchCulling::create(branchCullInfo);
        if (!branchShadowCulling_) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Branch shadow culling not available, using per-tree rendering");
        } else {
            // Allocate instanced shadow descriptor sets
            auto rawSets = descriptorPool_->allocate(
                branchShadowInstancedDescriptorSetLayout_.get(), info.maxFramesInFlight);
            branchShadowInstancedDescriptorSets_.reserve(rawSets.size());
            for (auto set : rawSets) {
                branchShadowInstancedDescriptorSets_.push_back(vk::DescriptorSet(set));
            }
            if (branchShadowInstancedDescriptorSets_.empty()) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeRenderer: Failed to allocate instanced shadow descriptor sets");
                branchShadowCulling_.reset();
            }
        }
    }

    SDL_Log("TreeRenderer initialized successfully");
    return true;
}

bool TreeRenderer::createDescriptorSetLayout() {
    // Branch descriptor set layout using LayoutBuilder
    DescriptorManager::LayoutBuilder branchBuilder(device_);
    branchBuilder.addBinding(Bindings::TREE_GFX_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
                 .addBinding(Bindings::TREE_GFX_SHADOW_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             VK_SHADER_STAGE_FRAGMENT_BIT)
                 .addBinding(Bindings::TREE_GFX_WIND_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                             VK_SHADER_STAGE_VERTEX_BIT)
                 .addBinding(Bindings::TREE_GFX_BARK_ALBEDO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             VK_SHADER_STAGE_FRAGMENT_BIT)
                 .addBinding(Bindings::TREE_GFX_BARK_NORMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             VK_SHADER_STAGE_FRAGMENT_BIT)
                 .addBinding(Bindings::TREE_GFX_BARK_ROUGHNESS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             VK_SHADER_STAGE_FRAGMENT_BIT)
                 .addBinding(Bindings::TREE_GFX_BARK_AO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             VK_SHADER_STAGE_FRAGMENT_BIT);

    if (!branchBuilder.buildManaged(branchDescriptorSetLayout_)) {
        return false;
    }

    // Leaf descriptor set layout using LayoutBuilder
    DescriptorManager::LayoutBuilder leafBuilder(device_);
    leafBuilder.addBinding(Bindings::TREE_GFX_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
               .addBinding(Bindings::TREE_GFX_SHADOW_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           VK_SHADER_STAGE_FRAGMENT_BIT)
               .addBinding(Bindings::TREE_GFX_WIND_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                           VK_SHADER_STAGE_VERTEX_BIT)
               .addBinding(Bindings::TREE_GFX_LEAF_ALBEDO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           VK_SHADER_STAGE_FRAGMENT_BIT)
               .addBinding(Bindings::TREE_GFX_LEAF_INSTANCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                           VK_SHADER_STAGE_VERTEX_BIT)
               .addBinding(Bindings::TREE_GFX_TREE_DATA, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                           VK_SHADER_STAGE_VERTEX_BIT);

    if (!leafBuilder.buildManaged(leafDescriptorSetLayout_)) {
        return false;
    }

    return true;
}

bool TreeRenderer::createPipelines(const InitInfo& info) {
    // Create pipeline layouts with push constants
    auto branchPushRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        .setOffset(0)
        .setSize(sizeof(TreeBranchPushConstants));

    if (!DescriptorManager::createManagedPipelineLayout(device_, branchDescriptorSetLayout_.get(),
                                                        branchPipelineLayout_, {branchPushRange})) {
        return false;
    }

    auto leafPushRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        .setOffset(0)
        .setSize(sizeof(TreeLeafPushConstants));

    if (!DescriptorManager::createManagedPipelineLayout(device_, leafDescriptorSetLayout_.get(),
                                                        leafPipelineLayout_, {leafPushRange})) {
        return false;
    }

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

    // Create leaf pipeline
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
        .setBlendMode(GraphicsPipelineFactory::BlendMode::None)
        .setCullMode(VK_CULL_MODE_NONE)
        .build(rawLeafPipeline);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tree leaf pipeline");
        return false;
    }
    leafPipeline_ = ManagedPipeline::fromRaw(device_, rawLeafPipeline);

    // Create shadow pipeline layouts
    auto branchShadowPushRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex)
        .setOffset(0)
        .setSize(sizeof(TreeBranchShadowPushConstants));

    if (!DescriptorManager::createManagedPipelineLayout(device_, branchDescriptorSetLayout_.get(),
                                                        branchShadowPipelineLayout_, {branchShadowPushRange})) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create branch shadow pipeline layout");
        return false;
    }

    auto leafShadowPushRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        .setOffset(0)
        .setSize(sizeof(TreeLeafShadowPushConstants));

    if (!DescriptorManager::createManagedPipelineLayout(device_, leafDescriptorSetLayout_.get(),
                                                        leafShadowPipelineLayout_, {leafShadowPushRange})) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create leaf shadow pipeline layout");
        return false;
    }

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

    // Create leaf shadow pipeline
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
        .setCullMode(VK_CULL_MODE_NONE)
        .build(rawLeafShadowPipeline);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tree leaf shadow pipeline");
        return false;
    }
    leafShadowPipeline_ = ManagedPipeline::fromRaw(device_, rawLeafShadowPipeline);

    // Create instanced branch shadow pipeline
    // Descriptor layout: UBO (same as branch) + SSBO for instance matrices
    DescriptorManager::LayoutBuilder instancedShadowBuilder(device_);
    instancedShadowBuilder.addBinding(Bindings::TREE_GFX_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                      VK_SHADER_STAGE_VERTEX_BIT)
                          .addBinding(Bindings::TREE_GFX_BRANCH_SHADOW_INSTANCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                      VK_SHADER_STAGE_VERTEX_BIT);

    if (!instancedShadowBuilder.buildManaged(branchShadowInstancedDescriptorSetLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create instanced branch shadow descriptor set layout");
        return false;
    }

    auto instancedShadowPushRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex)
        .setOffset(0)
        .setSize(sizeof(TreeBranchShadowInstancedPushConstants));

    if (!DescriptorManager::createManagedPipelineLayout(device_, branchShadowInstancedDescriptorSetLayout_.get(),
                                                        branchShadowInstancedPipelineLayout_, {instancedShadowPushRange})) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create instanced branch shadow pipeline layout");
        return false;
    }

    VkPipeline rawBranchShadowInstancedPipeline;
    factory.reset();
    success = factory
        .applyPreset(GraphicsPipelineFactory::Preset::Shadow)
        .setShaders(resourcePath_ + "/shaders/tree_branch_shadow_instanced.vert.spv",
                    resourcePath_ + "/shaders/shadow.frag.spv")
        .setVertexInput({bindingDescription},
                        {attributeDescriptions.begin(), attributeDescriptions.end()})
        .setRenderPass(info.shadowRenderPass)
        .setPipelineLayout(branchShadowInstancedPipelineLayout_.get())
        .setExtent({info.shadowMapSize, info.shadowMapSize})
        .setDepthBias(1.25f, 1.75f)
        .build(rawBranchShadowInstancedPipeline);

    if (!success) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create instanced branch shadow pipeline (GPU culling disabled)");
    } else {
        branchShadowInstancedPipeline_ = ManagedPipeline::fromRaw(device_, rawBranchShadowInstancedPipeline);
    }

    SDL_Log("TreeRenderer: Created branch, leaf, and shadow pipelines");
    return true;
}

bool TreeRenderer::allocateDescriptorSets(uint32_t maxFramesInFlight) {
    branchDescriptorSets_.resize(maxFramesInFlight);
    leafDescriptorSets_.resize(maxFramesInFlight);
    culledLeafDescriptorSets_.resize(maxFramesInFlight);

    auto rawBranchSets = descriptorPool_->allocate(branchDescriptorSetLayout_.get(), maxFramesInFlight);
    if (rawBranchSets.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate default branch descriptor sets");
        return false;
    }
    defaultBranchDescriptorSets_.reserve(rawBranchSets.size());
    for (auto set : rawBranchSets) {
        defaultBranchDescriptorSets_.push_back(vk::DescriptorSet(set));
    }

    auto rawLeafSets = descriptorPool_->allocate(leafDescriptorSetLayout_.get(), maxFramesInFlight);
    if (rawLeafSets.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate default leaf descriptor sets");
        return false;
    }
    defaultLeafDescriptorSets_.reserve(rawLeafSets.size());
    for (auto set : rawLeafSets) {
        defaultLeafDescriptorSets_.push_back(vk::DescriptorSet(set));
    }

    return true;
}

void TreeRenderer::updateSpatialIndex(const TreeSystem& treeSystem) {
    if (leafCulling_) {
        leafCulling_->updateSpatialIndex(treeSystem);
    }
}

void TreeRenderer::updateBarkDescriptorSet(
    uint32_t frameIndex,
    const std::string& barkType,
    vk::Buffer uniformBuffer,
    vk::Buffer windBuffer,
    vk::ImageView shadowMapView,
    vk::Sampler shadowSampler,
    vk::ImageView barkAlbedo,
    vk::ImageView barkNormal,
    vk::ImageView barkRoughness,
    vk::ImageView barkAO,
    vk::Sampler barkSampler) {

    // Skip redundant updates - descriptor bindings don't change per-frame
    std::string key = std::to_string(frameIndex) + ":" + barkType;
    if (initializedBarkDescriptors_.count(key)) {
        return;
    }

    // Allocate descriptor set for this type if not already allocated
    if (branchDescriptorSets_[frameIndex].find(barkType) == branchDescriptorSets_[frameIndex].end()) {
        auto sets = descriptorPool_->allocate(branchDescriptorSetLayout_.get(), 1);
        if (sets.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate bark descriptor set for type: %s", barkType.c_str());
            return;
        }
        branchDescriptorSets_[frameIndex][barkType] = vk::DescriptorSet(sets[0]);
    }

    vk::DescriptorSet dstSet = branchDescriptorSets_[frameIndex][barkType];

    DescriptorManager::SetWriter writer(device_, dstSet);
    writer.writeBuffer(Bindings::TREE_GFX_UBO, uniformBuffer, 0, VK_WHOLE_SIZE)
          .writeImage(Bindings::TREE_GFX_SHADOW_MAP, shadowMapView, shadowSampler)
          .writeBuffer(Bindings::TREE_GFX_WIND_UBO, windBuffer, 0, VK_WHOLE_SIZE)
          .writeImage(Bindings::TREE_GFX_BARK_ALBEDO, barkAlbedo, barkSampler)
          .writeImage(Bindings::TREE_GFX_BARK_NORMAL, barkNormal, barkSampler)
          .writeImage(Bindings::TREE_GFX_BARK_ROUGHNESS, barkRoughness, barkSampler)
          .writeImage(Bindings::TREE_GFX_BARK_AO, barkAO, barkSampler)
          .update();

    // Mark as initialized to skip redundant updates
    initializedBarkDescriptors_.insert(key);
}

void TreeRenderer::updateLeafDescriptorSet(
    uint32_t frameIndex,
    const std::string& leafType,
    vk::Buffer uniformBuffer,
    vk::Buffer windBuffer,
    vk::ImageView shadowMapView,
    vk::Sampler shadowSampler,
    vk::ImageView leafAlbedo,
    vk::Sampler leafSampler,
    vk::Buffer leafInstanceBuffer,
    vk::DeviceSize leafInstanceBufferSize) {

    // Skip redundant updates - descriptor bindings don't change per-frame
    std::string key = std::to_string(frameIndex) + ":" + leafType;
    if (initializedLeafDescriptors_.count(key)) {
        return;
    }

    // Allocate descriptor set for this type if not already allocated
    if (leafDescriptorSets_[frameIndex].find(leafType) == leafDescriptorSets_[frameIndex].end()) {
        auto sets = descriptorPool_->allocate(leafDescriptorSetLayout_.get(), 1);
        if (sets.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate leaf descriptor set for type: %s", leafType.c_str());
            return;
        }
        leafDescriptorSets_[frameIndex][leafType] = vk::DescriptorSet(sets[0]);
    }

    vk::DescriptorSet dstSet = leafDescriptorSets_[frameIndex][leafType];
    vk::DeviceSize range = leafInstanceBufferSize > 0 ? leafInstanceBufferSize : VK_WHOLE_SIZE;

    DescriptorManager::SetWriter writer(device_, dstSet);
    writer.writeBuffer(Bindings::TREE_GFX_UBO, uniformBuffer, 0, VK_WHOLE_SIZE)
          .writeImage(Bindings::TREE_GFX_SHADOW_MAP, shadowMapView, shadowSampler)
          .writeBuffer(Bindings::TREE_GFX_WIND_UBO, windBuffer, 0, VK_WHOLE_SIZE)
          .writeImage(Bindings::TREE_GFX_LEAF_ALBEDO, leafAlbedo, leafSampler)
          .writeBuffer(Bindings::TREE_GFX_LEAF_INSTANCES, leafInstanceBuffer, 0, range, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .update();

    // Mark as initialized to skip redundant updates
    initializedLeafDescriptors_.insert(key);
}

void TreeRenderer::updateCulledLeafDescriptorSet(
    uint32_t frameIndex,
    const std::string& leafType,
    vk::Buffer uniformBuffer,
    vk::Buffer windBuffer,
    vk::ImageView shadowMapView,
    vk::Sampler shadowSampler,
    vk::ImageView leafAlbedo,
    vk::Sampler leafSampler) {

    // Skip if culling not available
    if (!leafCulling_ || leafCulling_->getOutputBuffer(frameIndex) == VK_NULL_HANDLE) {
        return;
    }

    // Allocate descriptor set for this type if not already allocated
    if (culledLeafDescriptorSets_[frameIndex].find(leafType) == culledLeafDescriptorSets_[frameIndex].end()) {
        auto sets = descriptorPool_->allocate(leafDescriptorSetLayout_.get(), 1);
        if (sets.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate culled leaf descriptor set for type: %s", leafType.c_str());
            return;
        }
        culledLeafDescriptorSets_[frameIndex][leafType] = vk::DescriptorSet(sets[0]);
    }

    vk::DescriptorSet dstSet = culledLeafDescriptorSets_[frameIndex][leafType];

    // IMPORTANT: Must update SSBO bindings every frame because getOutputBuffer(frameIndex)
    // and getTreeRenderDataBuffer(frameIndex) return different buffers for each frame
    // due to triple-buffering. This ensures compute pass for frame N writes to buffer N,
    // and graphics pass for frame N reads from buffer N.
    DescriptorManager::SetWriter writer(device_, dstSet);
    writer.writeBuffer(Bindings::TREE_GFX_UBO, uniformBuffer, 0, VK_WHOLE_SIZE)
          .writeImage(Bindings::TREE_GFX_SHADOW_MAP, shadowMapView, shadowSampler)
          .writeBuffer(Bindings::TREE_GFX_WIND_UBO, windBuffer, 0, VK_WHOLE_SIZE)
          .writeImage(Bindings::TREE_GFX_LEAF_ALBEDO, leafAlbedo, leafSampler)
          .writeBuffer(Bindings::TREE_GFX_LEAF_INSTANCES, leafCulling_->getOutputBuffer(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::TREE_GFX_TREE_DATA, leafCulling_->getTreeRenderDataBuffer(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .update();
}

vk::DescriptorSet TreeRenderer::getBranchDescriptorSet(uint32_t frameIndex, const std::string& barkType) const {
    auto frameIt = branchDescriptorSets_[frameIndex].find(barkType);
    if (frameIt != branchDescriptorSets_[frameIndex].end()) {
        return frameIt->second;
    }
    return defaultBranchDescriptorSets_[frameIndex];
}

vk::DescriptorSet TreeRenderer::getLeafDescriptorSet(uint32_t frameIndex, const std::string& leafType) const {
    auto frameIt = leafDescriptorSets_[frameIndex].find(leafType);
    if (frameIt != leafDescriptorSets_[frameIndex].end()) {
        return frameIt->second;
    }
    return defaultLeafDescriptorSets_[frameIndex];
}

vk::DescriptorSet TreeRenderer::getCulledLeafDescriptorSet(uint32_t frameIndex, const std::string& leafType) const {
    if (frameIndex < culledLeafDescriptorSets_.size()) {
        auto frameIt = culledLeafDescriptorSets_[frameIndex].find(leafType);
        if (frameIt != culledLeafDescriptorSets_[frameIndex].end()) {
            return frameIt->second;
        }
    }
    return getLeafDescriptorSet(frameIndex, leafType);
}

void TreeRenderer::recordLeafCulling(vk::CommandBuffer cmd, uint32_t frameIndex,
                                      const TreeSystem& treeSystem,
                                      const TreeLODSystem* lodSystem,
                                      const glm::vec3& cameraPos,
                                      const glm::vec4* frustumPlanes) {
    if (leafCulling_) {
        leafCulling_->recordCulling(cmd, frameIndex, treeSystem, lodSystem, cameraPos, frustumPlanes);
    }
}

bool TreeRenderer::isLeafCullingEnabled() const {
    return leafCulling_ && leafCulling_->isEnabled();
}

bool TreeRenderer::isSpatialIndexEnabled() const {
    return leafCulling_ && leafCulling_->isSpatialIndexEnabled();
}

void TreeRenderer::setTwoPhaseLeafCulling(bool enabled) {
    if (leafCulling_) {
        leafCulling_->setTwoPhaseEnabled(enabled);
    }
}

bool TreeRenderer::isTwoPhaseLeafCullingEnabled() const {
    return leafCulling_ && leafCulling_->isTwoPhaseEnabled();
}

void TreeRenderer::recordBranchShadowCulling(vk::CommandBuffer cmd, uint32_t frameIndex,
                                              uint32_t cascadeIndex,
                                              const glm::vec4* cascadeFrustumPlanes,
                                              const glm::vec3& cameraPos,
                                              const TreeLODSystem* lodSystem) {
    if (branchShadowCulling_ && branchShadowCulling_->isEnabled()) {
        branchShadowCulling_->recordCulling(cmd, frameIndex, cascadeIndex,
                                            cascadeFrustumPlanes, cameraPos, lodSystem);
    }
}

void TreeRenderer::updateBranchCullingData(const TreeSystem& treeSystem, const TreeLODSystem* lodSystem) {
    if (!branchShadowCulling_) return;

    branchShadowCulling_->updateTreeData(treeSystem, lodSystem);

    // Update descriptor sets with frame-specific instance buffers
    // Only proceed if culling system is fully initialized with valid buffers
    if (branchShadowInstancedDescriptorSets_.empty() ||
        !branchShadowCulling_->isEnabled()) {
        return;
    }

    vk::Device vkDevice(device_);
    for (uint32_t i = 0; i < branchShadowInstancedDescriptorSets_.size(); ++i) {
        VkBuffer instanceBuffer = branchShadowCulling_->getInstanceBuffer(i);
        if (instanceBuffer == VK_NULL_HANDLE) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "TreeRenderer: Instance buffer %u is NULL, skipping descriptor update", i);
            continue;
        }

        auto instanceBufferInfo = vk::DescriptorBufferInfo{}
            .setBuffer(instanceBuffer)
            .setOffset(0)
            .setRange(VK_WHOLE_SIZE);

        auto instanceWrite = vk::WriteDescriptorSet{}
            .setDstSet(branchShadowInstancedDescriptorSets_[i])
            .setDstBinding(Bindings::TREE_GFX_BRANCH_SHADOW_INSTANCES)
            .setDstArrayElement(0)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setBufferInfo(instanceBufferInfo);

        vkDevice.updateDescriptorSets(instanceWrite, nullptr);
    }
}

bool TreeRenderer::isBranchShadowCullingAvailable() const {
    return branchShadowCulling_ && branchShadowCulling_->isEnabled();
}

bool TreeRenderer::isBranchShadowCullingEnabled() const {
    return branchShadowCulling_ && branchShadowCulling_->isEnabled() && branchShadowCulling_->isEnabledByUser();
}

void TreeRenderer::setBranchShadowCullingEnabled(bool enabled) {
    if (branchShadowCulling_) {
        branchShadowCulling_->setEnabled(enabled);
    }
}

void TreeRenderer::render(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                          const TreeSystem& treeSystem, const TreeLODSystem* lodSystem) {
    const auto& branchRenderables = treeSystem.getBranchRenderables();
    const auto& leafRenderables = treeSystem.getLeafRenderables();

    if (branchRenderables.empty() && leafRenderables.empty()) return;

    vk::CommandBuffer vkCmd = cmd;

    // Render branches
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, branchPipeline_.get());

    std::string lastBarkType;
    uint32_t branchTreeIndex = 0;
    for (const auto& renderable : branchRenderables) {
        if (lodSystem && !lodSystem->shouldRenderFullGeometry(branchTreeIndex)) {
            branchTreeIndex++;
            continue;
        }

        if (renderable.barkType != lastBarkType) {
            vk::DescriptorSet descriptorSet = getBranchDescriptorSet(frameIndex, renderable.barkType);
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, branchPipelineLayout_.get(),
                                     0, descriptorSet, {});
            lastBarkType = renderable.barkType;
        }

        TreeBranchPushConstants push{};
        push.model = renderable.transform;
        push.time = time;
        push.lodBlendFactor = lodSystem ? lodSystem->getBlendFactor(branchTreeIndex) : 0.0f;
        push.barkTint = glm::vec3(1.0f);
        push.roughnessScale = renderable.roughness;

        vkCmd.pushConstants<TreeBranchPushConstants>(
            branchPipelineLayout_.get(),
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, push);

        if (renderable.mesh) {
            vk::Buffer vertexBuffers[] = {renderable.mesh->getVertexBuffer()};
            vk::DeviceSize offsets[] = {0};
            vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
            vkCmd.bindIndexBuffer(renderable.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);
            vkCmd.drawIndexed(renderable.mesh->getIndexCount(), 1, 0, 0, 0);
        }
        branchTreeIndex++;
    }

    // Render leaves with instancing
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, leafPipeline_.get());

    const Mesh& sharedQuad = treeSystem.getSharedLeafQuadMesh();

    if (sharedQuad.getIndexCount() > 0) {
        vk::Buffer vertexBuffers[] = {sharedQuad.getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
        vkCmd.bindIndexBuffer(sharedQuad.getIndexBuffer(), 0, vk::IndexType::eUint32);
    }

    bool hasCulledDescriptors = !culledLeafDescriptorSets_.empty() &&
                                 frameIndex < culledLeafDescriptorSets_.size() &&
                                 !culledLeafDescriptorSets_[frameIndex].empty();
    bool useCulledPath = isLeafCullingEnabled() && hasCulledDescriptors &&
                         leafCulling_ && leafCulling_->getIndirectBuffer(frameIndex) != VK_NULL_HANDLE;

    if (useCulledPath) {
        static const std::string leafTypeNames[NUM_LEAF_TYPES] = {"oak", "ash", "aspen", "pine"};

        float alphaTest = 0.5f;
        if (!leafRenderables.empty()) {
            alphaTest = leafRenderables[0].alphaTestThreshold > 0.0f ?
                        leafRenderables[0].alphaTestThreshold : 0.5f;
        }

        for (uint32_t leafType = 0; leafType < NUM_LEAF_TYPES; ++leafType) {
            vk::DescriptorSet descriptorSet = getCulledLeafDescriptorSet(frameIndex, leafTypeNames[leafType]);
            if (!descriptorSet) continue;

            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, leafPipelineLayout_.get(),
                                     0, descriptorSet, {});

            TreeLeafPushConstants push{};
            push.time = time;
            push.alphaTest = alphaTest;

            vkCmd.pushConstants<TreeLeafPushConstants>(
                leafPipelineLayout_.get(),
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0, push);

            vk::DeviceSize commandOffset = leafType * sizeof(VkDrawIndexedIndirectCommand);
            vkCmd.drawIndexedIndirect(leafCulling_->getIndirectBuffer(frameIndex),
                                      commandOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
        }
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Leaf culling not available - leaves will not render for close trees");
    }
}

void TreeRenderer::renderShadows(vk::CommandBuffer cmd, uint32_t frameIndex,
                                 const TreeSystem& treeSystem, int cascadeIndex,
                                 const TreeLODSystem* lodSystem) {
    const auto& branchRenderables = treeSystem.getBranchRenderables();
    const auto& leafRenderables = treeSystem.getLeafRenderables();

    if (branchRenderables.empty() && leafRenderables.empty()) {
        return;
    }

    vk::CommandBuffer vkCmd = cmd;
    const uint32_t cascade = static_cast<uint32_t>(cascadeIndex);

    // Check if this cascade should skip geometry entirely (cascade-aware shadow LOD)
    bool renderBranches = true;
    bool renderLeaves = true;
    if (lodSystem) {
        const auto& shadowSettings = lodSystem->getLODSettings().shadow;
        if (shadowSettings.enableCascadeLOD) {
            renderBranches = cascade < shadowSettings.geometryCascadeCutoff;
            renderLeaves = cascade < shadowSettings.leafCascadeCutoff &&
                           cascade < shadowSettings.geometryCascadeCutoff;
        }
    }

    // Render branch shadows
    if (renderBranches && !branchRenderables.empty()) {
        bool useInstancedPath = isBranchShadowCullingEnabled() &&
                                branchShadowInstancedPipeline_.get() != VK_NULL_HANDLE &&
                                !branchShadowInstancedDescriptorSets_.empty() &&
                                frameIndex < branchShadowInstancedDescriptorSets_.size() &&
                                branchShadowCulling_->getIndirectBuffer(frameIndex) != VK_NULL_HANDLE;

        if (useInstancedPath) {
            // GPU-driven instanced branch shadow rendering
            vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, branchShadowInstancedPipeline_.get());

            vk::DescriptorSet instancedSet = branchShadowInstancedDescriptorSets_[frameIndex];
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                     branchShadowInstancedPipelineLayout_.get(), 0,
                                     instancedSet, {});

            const auto& meshGroups = branchShadowCulling_->getMeshGroups();
            for (size_t groupIdx = 0; groupIdx < meshGroups.size(); ++groupIdx) {
                const auto& group = meshGroups[groupIdx];

                // Get the mesh for this group
                if (group.meshIndex < branchRenderables.size()) {
                    const auto& renderable = branchRenderables[group.meshIndex];
                    if (renderable.mesh) {
                        vk::Buffer vertexBuffers[] = {renderable.mesh->getVertexBuffer()};
                        vk::DeviceSize offsets[] = {0};
                        vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
                        vkCmd.bindIndexBuffer(renderable.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);

                        TreeBranchShadowInstancedPushConstants push{};
                        push.cascadeIndex = cascade;
                        push.instanceOffset = group.instanceOffset;

                        vkCmd.pushConstants<TreeBranchShadowInstancedPushConstants>(
                            branchShadowInstancedPipelineLayout_.get(),
                            vk::ShaderStageFlagBits::eVertex,
                            0, push);

                        vkCmd.drawIndexedIndirect(branchShadowCulling_->getIndirectBuffer(frameIndex),
                                                  group.indirectOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
                    }
                }
            }
        } else if (branchShadowPipeline_.get() != VK_NULL_HANDLE) {
            // Fallback: per-tree branch shadow rendering
            vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, branchShadowPipeline_.get());

            std::string lastBarkType;
            uint32_t branchTreeIndex = 0;
            for (const auto& renderable : branchRenderables) {
                if (lodSystem && !lodSystem->shouldRenderBranchShadow(branchTreeIndex, cascade)) {
                    branchTreeIndex++;
                    continue;
                }

                if (renderable.barkType != lastBarkType) {
                    vk::DescriptorSet branchSet = getBranchDescriptorSet(frameIndex, renderable.barkType);
                    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                             branchShadowPipelineLayout_.get(), 0,
                                             branchSet, {});
                    lastBarkType = renderable.barkType;
                }

                TreeBranchShadowPushConstants push{};
                push.model = renderable.transform;
                push.cascadeIndex = cascadeIndex;

                vkCmd.pushConstants<TreeBranchShadowPushConstants>(
                    branchShadowPipelineLayout_.get(),
                    vk::ShaderStageFlagBits::eVertex,
                    0, push);

                if (renderable.mesh) {
                    vk::Buffer vertexBuffers[] = {renderable.mesh->getVertexBuffer()};
                    vk::DeviceSize offsets[] = {0};
                    vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
                    vkCmd.bindIndexBuffer(renderable.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);
                    vkCmd.drawIndexed(renderable.mesh->getIndexCount(), 1, 0, 0, 0);
                }
                branchTreeIndex++;
            }
        }
    }

    // Render leaf shadows with instancing
    if (renderLeaves && !leafRenderables.empty() && leafShadowPipeline_.get() != VK_NULL_HANDLE) {
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, leafShadowPipeline_.get());

        const Mesh& sharedQuad = treeSystem.getSharedLeafQuadMesh();

        if (sharedQuad.getIndexCount() > 0) {
            vk::Buffer vertexBuffers[] = {sharedQuad.getVertexBuffer()};
            vk::DeviceSize offsets[] = {0};
            vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
            vkCmd.bindIndexBuffer(sharedQuad.getIndexBuffer(), 0, vk::IndexType::eUint32);
        }

        bool hasCulledDescriptors = !culledLeafDescriptorSets_.empty() &&
                                     frameIndex < culledLeafDescriptorSets_.size() &&
                                     !culledLeafDescriptorSets_[frameIndex].empty();
        bool useCulledPath = isLeafCullingEnabled() && hasCulledDescriptors &&
                             leafCulling_ && leafCulling_->getIndirectBuffer(frameIndex) != VK_NULL_HANDLE;

        if (useCulledPath && !leafRenderables.empty()) {
            static const std::string leafTypeNames[NUM_LEAF_TYPES] = {"oak", "ash", "aspen", "pine"};

            float alphaTest = 0.5f;
            if (!leafRenderables.empty()) {
                alphaTest = leafRenderables[0].alphaTestThreshold > 0.0f ?
                            leafRenderables[0].alphaTestThreshold : 0.5f;
            }

            for (uint32_t leafType = 0; leafType < NUM_LEAF_TYPES; ++leafType) {
                vk::DescriptorSet leafSet = getCulledLeafDescriptorSet(frameIndex, leafTypeNames[leafType]);
                if (!leafSet) continue;

                vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         leafShadowPipelineLayout_.get(), 0,
                                         leafSet, {});

                TreeLeafShadowPushConstants push{};
                push.cascadeIndex = cascadeIndex;
                push.alphaTest = alphaTest;

                vkCmd.pushConstants<TreeLeafShadowPushConstants>(
                    leafShadowPipelineLayout_.get(),
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                    0, push);

                vk::DeviceSize commandOffset = leafType * sizeof(VkDrawIndexedIndirectCommand);
                vkCmd.drawIndexedIndirect(leafCulling_->getIndirectBuffer(frameIndex),
                                          commandOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
            }
        } else {
            // Direct draw path (fallback)
            const auto& leafDrawInfo = treeSystem.getLeafDrawInfo();
            std::string lastLeafType;
            uint32_t leafTreeIndex = 0;
            for (const auto& renderable : leafRenderables) {
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

                if (lodSystem && !lodSystem->shouldRenderLeafShadow(leafTreeIndex, cascade)) {
                    leafTreeIndex++;
                    continue;
                }

                if (renderable.leafType != lastLeafType) {
                    vk::DescriptorSet leafSet = getLeafDescriptorSet(frameIndex, renderable.leafType);
                    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                             leafShadowPipelineLayout_.get(), 0,
                                             leafSet, {});
                    lastLeafType = renderable.leafType;
                }

                TreeLeafShadowPushConstants push{};
                push.cascadeIndex = cascadeIndex;
                push.alphaTest = renderable.alphaTestThreshold > 0.0f ? renderable.alphaTestThreshold : 0.5f;

                vkCmd.pushConstants<TreeLeafShadowPushConstants>(
                    leafShadowPipelineLayout_.get(),
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                    0, push);

                vkCmd.drawIndexed(sharedQuad.getIndexCount(), drawInfo.instanceCount, 0, 0, 0);
                leafTreeIndex++;
            }
        }
    }
}

void TreeRenderer::setExtent(vk::Extent2D newExtent) {
    extent_ = newExtent;
}

void TreeRenderer::invalidateDescriptorCache() {
    initializedBarkDescriptors_.clear();
    initializedLeafDescriptors_.clear();
    initializedCulledLeafDescriptors_.clear();
}
