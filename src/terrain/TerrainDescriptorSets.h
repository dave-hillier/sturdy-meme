#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <memory>
#include <cstdint>
#include "DescriptorManager.h"

class TerrainCBT;
class TerrainBuffers;
class TerrainTextures;
class TerrainTileCache;
class TerrainEffects;

/**
 * TerrainDescriptorSets - Owns and manages Vulkan descriptor set layouts and sets
 * for the terrain rendering system.
 *
 * Responsibilities:
 * - Creates compute and render descriptor set layouts
 * - Allocates per-frame descriptor sets from the pool
 * - Writes initial bindings during setup
 * - Updates bindings when external resources change (snow, shadow, caustics, etc.)
 * - Per-frame binding updates for triple-buffered resources
 *
 * This class does NOT manage UBO contents - that's TerrainEffects' responsibility.
 * It only manages the Vulkan descriptor infrastructure (layouts, sets, binding writes).
 */
class TerrainDescriptorSets {
public:
    struct InitInfo {
        vk::Device device;
        DescriptorManager::Pool* descriptorPool;
        uint32_t framesInFlight;
        uint32_t maxVisibleTriangles;
    };

    static std::unique_ptr<TerrainDescriptorSets> create(const InitInfo& info);
    ~TerrainDescriptorSets();

    // Non-copyable, non-movable
    TerrainDescriptorSets(const TerrainDescriptorSets&) = delete;
    TerrainDescriptorSets& operator=(const TerrainDescriptorSets&) = delete;

    // Layout accessors (needed by TerrainPipelines during init)
    vk::DescriptorSetLayout getComputeLayout() const { return computeLayout_; }
    vk::DescriptorSetLayout getRenderLayout() const { return renderLayout_; }

    // Set accessors (needed by TerrainRecording for command buffer binding)
    vk::DescriptorSet getComputeSet(uint32_t frameIndex) const { return computeSets_[frameIndex]; }
    vk::DescriptorSet getRenderSet(uint32_t frameIndex) const { return renderSets_[frameIndex]; }

    // Write initial compute descriptor bindings (called during TerrainSystem init)
    void writeInitialComputeBindings(TerrainCBT* cbt, TerrainBuffers* buffers, TerrainTileCache* tileCache);

    // Write full render descriptor bindings with shared external resources
    void updateRenderBindings(TerrainCBT* cbt,
                              TerrainBuffers* buffers,
                              TerrainTextures* textures,
                              TerrainTileCache* tileCache,
                              TerrainEffects* effects,
                              const std::vector<vk::Buffer>& sceneUniformBuffers,
                              vk::ImageView shadowMapView,
                              vk::Sampler shadowSampler,
                              const std::vector<vk::Buffer>& snowUBOBuffers,
                              const std::vector<vk::Buffer>& cloudShadowUBOBuffers);

    // Individual texture binding updates (called when external systems provide resources)
    void writeSnowMask(vk::ImageView view, vk::Sampler sampler);
    void writeSnowCascades(vk::ImageView cascade0, vk::ImageView cascade1, vk::ImageView cascade2,
                           vk::Sampler sampler);
    void writeCloudShadowMap(vk::ImageView view, vk::Sampler sampler);
    void writeCausticsTexture(vk::ImageView view, vk::Sampler sampler);

    // Per-frame tile info buffer update (triple-buffered, called before compute/draw)
    void writeTileInfoCompute(uint32_t frameIndex, TerrainTileCache* tileCache);
    void writeTileInfoRender(uint32_t frameIndex, TerrainTileCache* tileCache);

    // Screen-space shadow buffer (stored for deferred use in updateRenderBindings)
    void setScreenShadowBuffer(vk::ImageView view, vk::Sampler sampler) {
        screenShadowView_ = view;
        screenShadowSampler_ = sampler;
    }

private:
    TerrainDescriptorSets() = default;

    bool createLayouts();
    bool allocateSets();

    vk::Device device_;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    uint32_t framesInFlight_ = 0;
    uint32_t maxVisibleTriangles_ = 0;

    // Descriptor set layouts
    vk::DescriptorSetLayout computeLayout_;
    vk::DescriptorSetLayout renderLayout_;

    // Per-frame descriptor sets
    std::vector<vk::DescriptorSet> computeSets_;
    std::vector<vk::DescriptorSet> renderSets_;

    // Screen-space shadow buffer (optional, from ScreenSpaceShadowSystem)
    vk::ImageView screenShadowView_;
    vk::Sampler screenShadowSampler_;
};
