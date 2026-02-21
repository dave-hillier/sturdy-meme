#include "TerrainDescriptorSets.h"
#include "TerrainBuffers.h"
#include "TerrainCBT.h"
#include "TerrainTextures.h"
#include "TerrainTileCache.h"
#include "TerrainEffects.h"
#include "DescriptorManager.h"
#include "UBOs.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>

std::unique_ptr<TerrainDescriptorSets> TerrainDescriptorSets::create(const InitInfo& info) {
    auto ds = std::unique_ptr<TerrainDescriptorSets>(new TerrainDescriptorSets());
    ds->device_ = info.device;
    ds->descriptorPool_ = info.descriptorPool;
    ds->framesInFlight_ = info.framesInFlight;
    ds->maxVisibleTriangles_ = info.maxVisibleTriangles;

    if (!ds->createLayouts()) return nullptr;
    if (!ds->allocateSets()) return nullptr;
    return ds;
}

TerrainDescriptorSets::~TerrainDescriptorSets() {
    if (!device_) return;
    if (computeLayout_) device_.destroyDescriptorSetLayout(computeLayout_);
    if (renderLayout_) device_.destroyDescriptorSetLayout(renderLayout_);
}

bool TerrainDescriptorSets::createLayouts() {
    // Compute bindings:
    // 0: CBT buffer, 1: indirect dispatch, 2: indirect draw, 3: height map
    // 4: terrain uniforms, 5: visible indices, 6: cull indirect dispatch
    // 14: shadow visible indices, 15: shadow indirect draw
    // 19: tile array, 20: tile info
    computeLayout_ = DescriptorManager::LayoutBuilder(device_)
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 0: CBT buffer
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 1: indirect dispatch
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 2: indirect draw
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)    // 3: height map
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 4: terrain uniforms
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 5: visible indices
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 6: cull indirect dispatch
        .addBinding(14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(19, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(20, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    if (!computeLayout_) return false;

    // Render bindings:
    // 0: CBT (vertex), 3: heightmap, 4: terrain UBO, 5: scene UBO
    // 6: albedo, 7: shadow map, 8: grass far LOD, 9: snow mask
    // 10-12: snow cascades, 13: cloud shadow, 14: shadow visible, 16: hole mask
    // 17: snow UBO, 18: cloud shadow UBO, 19: tile array, 20: tile info
    // 21: caustics, 22: caustics UBO, 29: liquid UBO, 30: material layer UBO
    // 31: screen-space shadow
    renderLayout_ = DescriptorManager::LayoutBuilder(device_)
        .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
        .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
        .addBinding(16, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(17, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(18, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(19, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT)
        .addBinding(20, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
        .addBinding(21, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(22, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(29, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(30, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(31, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    return renderLayout_ != VK_NULL_HANDLE;
}

bool TerrainDescriptorSets::allocateSets() {
    auto rawComputeSets = descriptorPool_->allocate(computeLayout_, framesInFlight_);
    if (rawComputeSets.size() != framesInFlight_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainDescriptorSets: Failed to allocate compute sets");
        return false;
    }
    computeSets_.reserve(rawComputeSets.size());
    for (auto set : rawComputeSets) {
        computeSets_.push_back(vk::DescriptorSet(set));
    }

    auto rawRenderSets = descriptorPool_->allocate(renderLayout_, framesInFlight_);
    if (rawRenderSets.size() != framesInFlight_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainDescriptorSets: Failed to allocate render sets");
        return false;
    }
    renderSets_.reserve(rawRenderSets.size());
    for (auto set : rawRenderSets) {
        renderSets_.push_back(vk::DescriptorSet(set));
    }

    return true;
}

void TerrainDescriptorSets::writeInitialComputeBindings(TerrainCBT* cbt, TerrainBuffers* buffers,
                                                         TerrainTileCache* tileCache) {
    for (uint32_t i = 0; i < framesInFlight_; i++) {
        VkImageView tileArrayView = VK_NULL_HANDLE;
        VkSampler tileSampler = VK_NULL_HANDLE;
        if (tileCache) {
            tileArrayView = tileCache->getTileArrayView();
            tileSampler = tileCache->getSampler();
        }

        DescriptorManager::SetWriter writer(device_, computeSets_[i]);
        writer.writeBuffer(0, cbt->getBuffer(), 0, cbt->getBufferSize(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(1, buffers->getIndirectDispatchBuffer(), 0, sizeof(VkDispatchIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(2, buffers->getIndirectDrawBuffer(), 0, sizeof(VkDrawIndexedIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        if (tileCache && tileCache->getBaseHeightMapView() != VK_NULL_HANDLE) {
            writer.writeImage(3, tileCache->getBaseHeightMapView(), tileCache->getBaseHeightMapSampler());
        }
        writer.writeBuffer(4, buffers->getUniformBuffer(i), 0, sizeof(TerrainUniforms));
        writer.writeBuffer(5, buffers->getVisibleIndicesBuffer(), 0, sizeof(uint32_t) * (1 + maxVisibleTriangles_), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(6, buffers->getCullIndirectDispatchBuffer(), 0, sizeof(VkDispatchIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(14, buffers->getShadowVisibleBuffer(), 0, sizeof(uint32_t) * (1 + maxVisibleTriangles_), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(15, buffers->getShadowIndirectDrawBuffer(), 0, sizeof(VkDrawIndexedIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        if (tileArrayView != VK_NULL_HANDLE && tileSampler != VK_NULL_HANDLE) {
            writer.writeImage(19, tileArrayView, tileSampler);
        }
        if (tileCache && tileCache->getTileInfoBuffer(i) != VK_NULL_HANDLE) {
            writer.writeBuffer(20, tileCache->getTileInfoBuffer(i), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        writer.update();
    }
}

void TerrainDescriptorSets::updateRenderBindings(TerrainCBT* cbt,
                                                   TerrainBuffers* buffers,
                                                   TerrainTextures* textures,
                                                   TerrainTileCache* tileCache,
                                                   TerrainEffects* effects,
                                                   const std::vector<vk::Buffer>& sceneUniformBuffers,
                                                   vk::ImageView shadowMapView,
                                                   vk::Sampler shadowSampler,
                                                   const std::vector<vk::Buffer>& snowUBOBuffers,
                                                   const std::vector<vk::Buffer>& cloudShadowUBOBuffers) {
    for (uint32_t i = 0; i < framesInFlight_; i++) {
        auto writer = DescriptorManager::SetWriter(device_, renderSets_[i]);

        writer.writeBuffer(0, cbt->getBuffer(), 0, cbt->getBufferSize(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        if (tileCache && tileCache->getBaseHeightMapView() != VK_NULL_HANDLE) {
            writer.writeImage(3, tileCache->getBaseHeightMapView(), tileCache->getBaseHeightMapSampler(),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        writer.writeBuffer(4, buffers->getUniformBuffer(i), 0, sizeof(TerrainUniforms));

        if (i < sceneUniformBuffers.size()) {
            writer.writeBuffer(5, sceneUniformBuffers[i], 0, VK_WHOLE_SIZE);
        }

        writer.writeImage(6, textures->getAlbedoView(), textures->getAlbedoSampler(),
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (shadowMapView != VK_NULL_HANDLE) {
            writer.writeImage(7, shadowMapView, shadowSampler,
                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        }

        if (textures->getGrassFarLODView() != VK_NULL_HANDLE) {
            writer.writeImage(8, textures->getGrassFarLODView(), textures->getGrassFarLODSampler(),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        if (buffers->getShadowVisibleBuffer() != VK_NULL_HANDLE) {
            writer.writeBuffer(14, buffers->getShadowVisibleBuffer(), 0,
                              sizeof(uint32_t) * (1 + maxVisibleTriangles_),
                              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        if (tileCache && tileCache->getHoleMaskArrayView() != VK_NULL_HANDLE) {
            writer.writeImage(16, tileCache->getHoleMaskArrayView(), tileCache->getHoleMaskSampler(),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        if (i < snowUBOBuffers.size() && snowUBOBuffers[i] != VK_NULL_HANDLE) {
            writer.writeBuffer(17, snowUBOBuffers[i], 0, sizeof(SnowUBO));
        }

        if (i < cloudShadowUBOBuffers.size() && cloudShadowUBOBuffers[i] != VK_NULL_HANDLE) {
            writer.writeBuffer(18, cloudShadowUBOBuffers[i], 0, sizeof(CloudShadowUBO));
        }

        if (tileCache && tileCache->getTileArrayView() != VK_NULL_HANDLE) {
            writer.writeImage(19, tileCache->getTileArrayView(), tileCache->getSampler(),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        if (tileCache && tileCache->getTileInfoBuffer(i) != VK_NULL_HANDLE) {
            writer.writeBuffer(20, tileCache->getTileInfoBuffer(i), 0, VK_WHOLE_SIZE,
                              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        if (tileCache && tileCache->getBaseHeightMapView() != VK_NULL_HANDLE) {
            writer.writeImage(21, tileCache->getBaseHeightMapView(), tileCache->getBaseHeightMapSampler(),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        constexpr VkDeviceSize causticsUBOSize = 32;
        writer.writeBuffer(22, buffers->getCausticsUniformBuffer(i), 0, causticsUBOSize);

        constexpr VkDeviceSize liquidUBOSize = 128;
        writer.writeBuffer(29, buffers->getLiquidUniformBuffer(i), 0, liquidUBOSize);

        constexpr VkDeviceSize materialLayerUBOSize = 336;
        writer.writeBuffer(30, buffers->getMaterialLayerUniformBuffer(i), 0, materialLayerUBOSize);

        if (screenShadowView_) {
            writer.writeImage(31, screenShadowView_, screenShadowSampler_);
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "TerrainDescriptorSets: Screen shadow buffer not available, using heightmap placeholder");
            if (tileCache && tileCache->getBaseHeightMapView() != VK_NULL_HANDLE) {
                writer.writeImage(31, tileCache->getBaseHeightMapView(), tileCache->getBaseHeightMapSampler(),
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }

        writer.update();
    }

    effects->initializeUBOs(buffers);
}

void TerrainDescriptorSets::writeSnowMask(vk::ImageView view, vk::Sampler sampler) {
    for (uint32_t i = 0; i < framesInFlight_; i++) {
        DescriptorManager::SetWriter(device_, renderSets_[i])
            .writeImage(9, view, sampler)
            .update();
    }
}

void TerrainDescriptorSets::writeSnowCascades(vk::ImageView cascade0, vk::ImageView cascade1,
                                                vk::ImageView cascade2, vk::Sampler sampler) {
    for (uint32_t i = 0; i < framesInFlight_; i++) {
        DescriptorManager::SetWriter(device_, renderSets_[i])
            .writeImage(10, cascade0, sampler)
            .writeImage(11, cascade1, sampler)
            .writeImage(12, cascade2, sampler)
            .update();
    }
}

void TerrainDescriptorSets::writeCloudShadowMap(vk::ImageView view, vk::Sampler sampler) {
    for (uint32_t i = 0; i < framesInFlight_; i++) {
        DescriptorManager::SetWriter(device_, renderSets_[i])
            .writeImage(13, view, sampler)
            .update();
    }
}

void TerrainDescriptorSets::writeCausticsTexture(vk::ImageView view, vk::Sampler sampler) {
    for (uint32_t i = 0; i < framesInFlight_; i++) {
        DescriptorManager::SetWriter(device_, renderSets_[i])
            .writeImage(21, view, sampler)
            .update();
    }
}

void TerrainDescriptorSets::writeTileInfoCompute(uint32_t frameIndex, TerrainTileCache* tileCache) {
    if (tileCache && tileCache->getTileInfoBuffer(frameIndex) != VK_NULL_HANDLE) {
        DescriptorManager::SetWriter(device_, computeSets_[frameIndex])
            .writeBuffer(20, tileCache->getTileInfoBuffer(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();
    }
}

void TerrainDescriptorSets::writeTileInfoRender(uint32_t frameIndex, TerrainTileCache* tileCache) {
    if (tileCache && tileCache->getTileInfoBuffer(frameIndex) != VK_NULL_HANDLE) {
        DescriptorManager::SetWriter(device_, renderSets_[frameIndex])
            .writeBuffer(20, tileCache->getTileInfoBuffer(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();
    }
}
