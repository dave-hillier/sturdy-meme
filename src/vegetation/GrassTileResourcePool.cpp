#include "GrassTileResourcePool.h"
#include <SDL3/SDL.h>

GrassTileResourcePool::~GrassTileResourcePool() {
    destroy();
}

bool GrassTileResourcePool::init(const InitInfo& info) {
    device_ = info.device;
    descriptorPool_ = info.descriptorPool;
    framesInFlight_ = info.framesInFlight;
    computeDescriptorSetLayout_ = info.computeDescriptorSetLayout;

    if (!device_ || !descriptorPool_ || !computeDescriptorSetLayout_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GrassTileResourcePool: Invalid initialization parameters");
        return false;
    }

    initialized_ = true;
    return true;
}

void GrassTileResourcePool::destroy() {
    // Descriptor sets are managed by the pool, no need to free individually
    tileDescriptorSets_.clear();
    initialized_ = false;
}

bool GrassTileResourcePool::allocateForTile(const TileCoord& coord) {
    if (!initialized_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GrassTileResourcePool: Not initialized");
        return false;
    }

    // Check if already allocated
    if (tileDescriptorSets_.find(coord) != tileDescriptorSets_.end()) {
        return true;  // Already allocated
    }

    // Allocate descriptor sets for all buffer sets
    auto rawSets = descriptorPool_->allocate(computeDescriptorSetLayout_, framesInFlight_);
    if (rawSets.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GrassTileResourcePool: Failed to allocate descriptor sets for tile (%d, %d, LOD%u)",
            coord.x, coord.z, coord.lod);
        return false;
    }

    std::vector<vk::DescriptorSet> sets(rawSets.size());
    for (size_t i = 0; i < rawSets.size(); ++i) {
        sets[i] = rawSets[i];
    }

    tileDescriptorSets_[coord] = std::move(sets);

    // Write initial descriptor sets
    updateTileDescriptorSets(coord);

    return true;
}

void GrassTileResourcePool::releaseForTile(const TileCoord& coord) {
    // Descriptor sets are managed by pool, just remove from tracking
    tileDescriptorSets_.erase(coord);
}

vk::DescriptorSet GrassTileResourcePool::getDescriptorSet(const TileCoord& coord,
                                                           uint32_t bufferSetIndex) const {
    auto it = tileDescriptorSets_.find(coord);
    if (it == tileDescriptorSets_.end() || bufferSetIndex >= it->second.size()) {
        return vk::DescriptorSet{};
    }
    return it->second[bufferSetIndex];
}

bool GrassTileResourcePool::hasTileResources(const TileCoord& coord) const {
    return tileDescriptorSets_.find(coord) != tileDescriptorSets_.end();
}

void GrassTileResourcePool::setSharedBuffers(vk::Buffer instanceBuffer, vk::Buffer indirectBuffer) {
    sharedInstanceBuffer_ = instanceBuffer;
    sharedIndirectBuffer_ = indirectBuffer;
}

void GrassTileResourcePool::setSharedImages(
    vk::ImageView terrainHeightMapView, vk::Sampler terrainHeightMapSampler,
    vk::ImageView displacementView, vk::Sampler displacementSampler,
    vk::ImageView tileArrayView, vk::Sampler tileSampler
) {
    terrainHeightMapView_ = terrainHeightMapView;
    terrainHeightMapSampler_ = terrainHeightMapSampler;
    displacementView_ = displacementView;
    displacementSampler_ = displacementSampler;
    tileArrayView_ = tileArrayView;
    tileSampler_ = tileSampler;
}

void GrassTileResourcePool::setSharedBufferArrays(
    const std::array<vk::Buffer, 3>& tileInfoBuffers,
    const std::vector<vk::Buffer>& cullingUniformBuffers,
    const std::vector<vk::Buffer>& grassParamsBuffers
) {
    tileInfoBuffers_ = tileInfoBuffers;
    cullingUniformBuffers_ = cullingUniformBuffers;
    grassParamsBuffers_ = grassParamsBuffers;
}

void GrassTileResourcePool::updateTileDescriptorSets(const TileCoord& coord) {
    auto it = tileDescriptorSets_.find(coord);
    if (it == tileDescriptorSets_.end()) return;

    for (uint32_t setIndex = 0; setIndex < framesInFlight_; ++setIndex) {
        vk::DescriptorSet descSet = it->second[setIndex];

        DescriptorManager::SetWriter writer(device_, descSet);

        // Binding 0: Shared instance buffer
        constexpr VkDeviceSize instanceSize = 48;
        if (sharedInstanceBuffer_) {
            writer.writeBuffer(0, sharedInstanceBuffer_, 0,
                               instanceSize * GrassConstants::MAX_INSTANCES,
                               VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        // Binding 1: Shared indirect buffer
        if (sharedIndirectBuffer_) {
            writer.writeBuffer(1, sharedIndirectBuffer_, 0,
                               sizeof(VkDrawIndirectCommand),
                               VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        // Binding 2: Culling uniforms
        if (!cullingUniformBuffers_.empty()) {
            writer.writeBuffer(2, cullingUniformBuffers_[0], 0, sizeof(CullingUniforms));
        }

        // Binding 3: Terrain heightmap
        if (terrainHeightMapView_) {
            writer.writeImage(3, terrainHeightMapView_, terrainHeightMapSampler_);
        }

        // Binding 4: Displacement map
        if (displacementView_) {
            writer.writeImage(4, displacementView_, displacementSampler_);
        }

        // Binding 5: Tile array
        if (tileArrayView_) {
            writer.writeImage(5, tileArrayView_, tileSampler_);
        }

        // Binding 6: Tile info buffer
        if (!tileInfoBuffers_.empty() && tileInfoBuffers_[0]) {
            writer.writeBuffer(6, tileInfoBuffers_[0], 0, VK_WHOLE_SIZE,
                               VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        // Binding 7: GrassParams
        if (!grassParamsBuffers_.empty()) {
            writer.writeBuffer(7, grassParamsBuffers_[0], 0, sizeof(GrassParams));
        }

        writer.update();
    }
}

void GrassTileResourcePool::writePerFrameBindings(const TileCoord& coord, uint32_t bufferSetIndex) {
    auto it = tileDescriptorSets_.find(coord);
    if (it == tileDescriptorSets_.end() || bufferSetIndex >= it->second.size()) return;

    vk::DescriptorSet descSet = it->second[bufferSetIndex];
    DescriptorManager::SetWriter writer(device_, descSet);

    // Update per-frame bindings
    // Binding 0: Shared instance buffer (may have changed due to buffer set rotation)
    constexpr VkDeviceSize instanceSize = 48;
    if (sharedInstanceBuffer_) {
        writer.writeBuffer(0, sharedInstanceBuffer_, 0,
                           instanceSize * GrassConstants::MAX_INSTANCES,
                           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    // Binding 1: Shared indirect buffer
    if (sharedIndirectBuffer_) {
        writer.writeBuffer(1, sharedIndirectBuffer_, 0,
                           sizeof(VkDrawIndirectCommand),
                           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    // Binding 2: Culling uniforms (per-frame)
    if (!cullingUniformBuffers_.empty() && bufferSetIndex < cullingUniformBuffers_.size()) {
        writer.writeBuffer(2, cullingUniformBuffers_[bufferSetIndex], 0,
                           sizeof(CullingUniforms));
    }

    // Binding 6: Tile info buffer (per-frame)
    if (!tileInfoBuffers_.empty() && bufferSetIndex < tileInfoBuffers_.size() &&
        tileInfoBuffers_[bufferSetIndex]) {
        writer.writeBuffer(6, tileInfoBuffers_[bufferSetIndex], 0, VK_WHOLE_SIZE,
                           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    // Binding 7: GrassParams (per-frame)
    if (!grassParamsBuffers_.empty() && bufferSetIndex < grassParamsBuffers_.size()) {
        writer.writeBuffer(7, grassParamsBuffers_[bufferSetIndex], 0,
                           sizeof(GrassParams));
    }

    writer.update();
}
