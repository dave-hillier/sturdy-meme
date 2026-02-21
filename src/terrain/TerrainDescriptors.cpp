#include "TerrainSystem.h"
#include "TerrainBuffers.h"
#include "DescriptorManager.h"
#include "UBOs.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <cstring>

bool TerrainSystem::createComputeDescriptorSetLayout() {
    // Compute bindings:
    // 0: CBT buffer, 1: indirect dispatch, 2: indirect draw, 3: height map
    // 4: terrain uniforms, 5: visible indices, 6: cull indirect dispatch
    // 14: shadow visible indices, 15: shadow indirect draw

    computeDescriptorSetLayout = DescriptorManager::LayoutBuilder(device)
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 0: CBT buffer
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 1: indirect dispatch
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 2: indirect draw
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)    // 3: height map
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 4: terrain uniforms
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 5: visible indices
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 6: cull indirect dispatch
        .addBinding(14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)  // shadow visible indices
        .addBinding(15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)  // shadow indirect draw
        .addBinding(19, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)  // tile array
        .addBinding(20, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)          // tile info
        .build();

    return computeDescriptorSetLayout != VK_NULL_HANDLE;
}

bool TerrainSystem::createRenderDescriptorSetLayout() {
    // Render bindings:
    // 0: CBT buffer (vertex), 3: height map, 4: terrain uniforms, 5: scene UBO
    // 6: terrain albedo, 7: shadow map, 8: grass far LOD, 9: snow mask
    // 10-12: volumetric snow cascades, 13: cloud shadow map
    // 14: shadow visible indices, 16: hole mask
    // 17: snow UBO, 18: cloud shadow UBO
    // 19: tile array texture, 20: tile info SSBO
    // 21: caustics texture, 22: caustics UBO

    renderDescriptorSetLayout = DescriptorManager::LayoutBuilder(device)
        .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
        .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)  // snow cascade 0
        .addBinding(11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)  // snow cascade 1
        .addBinding(12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)  // snow cascade 2
        .addBinding(13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)  // cloud shadow map
        .addBinding(14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)            // shadow visible indices
        .addBinding(16, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)  // hole mask
        .addBinding(17, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)          // snow UBO
        .addBinding(18, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)          // cloud shadow UBO
        .addBinding(19, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT)    // tile array texture
        .addBinding(20, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)            // tile info SSBO
        .addBinding(21, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)  // caustics texture
        .addBinding(22, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)          // caustics UBO
        .addBinding(29, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)          // liquid UBO (composable materials)
        .addBinding(30, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)          // material layer UBO (composable materials)
        .addBinding(31, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)  // screen-space shadow buffer
        .build();

    return renderDescriptorSetLayout != VK_NULL_HANDLE;
}

bool TerrainSystem::createDescriptorSets() {
    // Allocate compute descriptor sets using managed pool
    auto rawComputeSets = descriptorPool->allocate(computeDescriptorSetLayout, framesInFlight);
    if (rawComputeSets.size() != framesInFlight) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainSystem: Failed to allocate compute descriptor sets");
        return false;
    }
    computeDescriptorSets.reserve(rawComputeSets.size());
    for (auto set : rawComputeSets) {
        computeDescriptorSets.push_back(vk::DescriptorSet(set));
    }

    // Allocate render descriptor sets using managed pool
    auto rawRenderSets = descriptorPool->allocate(renderDescriptorSetLayout, framesInFlight);
    if (rawRenderSets.size() != framesInFlight) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainSystem: Failed to allocate render descriptor sets");
        return false;
    }
    renderDescriptorSets.reserve(rawRenderSets.size());
    for (auto set : rawRenderSets) {
        renderDescriptorSets.push_back(vk::DescriptorSet(set));
    }

    // Update compute descriptor sets
    for (uint32_t i = 0; i < framesInFlight; i++) {
        // Get tile cache resources if available
        VkImageView tileArrayView = VK_NULL_HANDLE;
        VkSampler tileSampler = VK_NULL_HANDLE;
        if (tileCache) {
            tileArrayView = tileCache->getTileArrayView();
            tileSampler = tileCache->getSampler();
        }

        // Build writer with all bindings - use separate statements to avoid
        // copy/reference issues with the fluent API pattern
        DescriptorManager::SetWriter writer(device, computeDescriptorSets[i]);
        writer.writeBuffer(0, cbt->getBuffer(), 0, cbt->getBufferSize(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(1, buffers->getIndirectDispatchBuffer(), 0, sizeof(VkDispatchIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(2, buffers->getIndirectDrawBuffer(), 0, sizeof(VkDrawIndexedIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        // Height map (binding 3) - use tile cache base heightmap
        if (tileCache && tileCache->getBaseHeightMapView() != VK_NULL_HANDLE) {
            writer.writeImage(3, tileCache->getBaseHeightMapView(), tileCache->getBaseHeightMapSampler());
        }
        writer.writeBuffer(4, buffers->getUniformBuffer(i), 0, sizeof(TerrainUniforms));
        writer.writeBuffer(5, buffers->getVisibleIndicesBuffer(), 0, sizeof(uint32_t) * (1 + MAX_VISIBLE_TRIANGLES), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(6, buffers->getCullIndirectDispatchBuffer(), 0, sizeof(VkDispatchIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(14, buffers->getShadowVisibleBuffer(), 0, sizeof(uint32_t) * (1 + MAX_VISIBLE_TRIANGLES), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(15, buffers->getShadowIndirectDrawBuffer(), 0, sizeof(VkDrawIndexedIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        // LOD tile cache bindings (19 and 20) - for subdivision to use high-res terrain data
        // Note: tile info buffer (binding 20) is updated per-frame in recordCompute
        if (tileArrayView != VK_NULL_HANDLE && tileSampler != VK_NULL_HANDLE) {
            writer.writeImage(19, tileArrayView, tileSampler);
        }
        // Write initial tile info buffer (frame i) - will be updated per-frame in recordCompute
        if (tileCache && tileCache->getTileInfoBuffer(i) != VK_NULL_HANDLE) {
            writer.writeBuffer(20, tileCache->getTileInfoBuffer(i), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        writer.update();
    }
    return true;
}

void TerrainSystem::updateDescriptorSets(vk::Device device,
                                          const std::vector<vk::Buffer>& sceneUniformBuffers,
                                          vk::ImageView shadowMapView,
                                          vk::Sampler shadowSampler,
                                          const std::vector<vk::Buffer>& snowUBOBuffers,
                                          const std::vector<vk::Buffer>& cloudShadowUBOBuffers) {
    for (uint32_t i = 0; i < framesInFlight; i++) {
        auto writer = DescriptorManager::SetWriter(device, renderDescriptorSets[i]);

        // CBT buffer (binding 0)
        writer.writeBuffer(0, cbt->getBuffer(), 0, cbt->getBufferSize(),
                          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        // Height map (binding 3) - use tile cache base heightmap
        if (tileCache && tileCache->getBaseHeightMapView() != VK_NULL_HANDLE) {
            writer.writeImage(3, tileCache->getBaseHeightMapView(), tileCache->getBaseHeightMapSampler(),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        // Terrain uniforms (binding 4)
        writer.writeBuffer(4, buffers->getUniformBuffer(i), 0, sizeof(TerrainUniforms));

        // Scene UBO (binding 5)
        if (i < sceneUniformBuffers.size()) {
            writer.writeBuffer(5, sceneUniformBuffers[i], 0, VK_WHOLE_SIZE);
        }

        // Terrain albedo (binding 6)
        writer.writeImage(6, textures->getAlbedoView(), textures->getAlbedoSampler(),
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // Shadow map (binding 7)
        if (shadowMapView != VK_NULL_HANDLE) {
            writer.writeImage(7, shadowMapView, shadowSampler,
                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        }

        // Grass far LOD texture (binding 8)
        if (textures->getGrassFarLODView() != VK_NULL_HANDLE) {
            writer.writeImage(8, textures->getGrassFarLODView(), textures->getGrassFarLODSampler(),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        // Shadow visible indices (binding 14)
        if (buffers->getShadowVisibleBuffer() != VK_NULL_HANDLE) {
            writer.writeBuffer(14, buffers->getShadowVisibleBuffer(), 0,
                              sizeof(uint32_t) * (1 + MAX_VISIBLE_TRIANGLES),
                              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        // Hole mask array (binding 16) - tiled hole mask for high-res cutouts
        if (tileCache && tileCache->getHoleMaskArrayView() != VK_NULL_HANDLE) {
            writer.writeImage(16, tileCache->getHoleMaskArrayView(), tileCache->getHoleMaskSampler(),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        // Snow UBO (binding 17)
        if (i < snowUBOBuffers.size() && snowUBOBuffers[i] != VK_NULL_HANDLE) {
            writer.writeBuffer(17, snowUBOBuffers[i], 0, sizeof(SnowUBO));
        }

        // Cloud shadow UBO (binding 18)
        if (i < cloudShadowUBOBuffers.size() && cloudShadowUBOBuffers[i] != VK_NULL_HANDLE) {
            writer.writeBuffer(18, cloudShadowUBOBuffers[i], 0, sizeof(CloudShadowUBO));
        }

        // LOD tile array texture (binding 19)
        if (tileCache && tileCache->getTileArrayView() != VK_NULL_HANDLE) {
            writer.writeImage(19, tileCache->getTileArrayView(), tileCache->getSampler(),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        // LOD tile info buffer (binding 20) - use per-frame buffer
        // Note: This is also updated per-frame in recordDraw for proper sync
        if (tileCache && tileCache->getTileInfoBuffer(i) != VK_NULL_HANDLE) {
            writer.writeBuffer(20, tileCache->getTileInfoBuffer(i), 0, VK_WHOLE_SIZE,
                              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        // Caustics texture (binding 21) - use tile cache base heightmap as placeholder until setCaustics is called
        if (tileCache && tileCache->getBaseHeightMapView() != VK_NULL_HANDLE) {
            writer.writeImage(21, tileCache->getBaseHeightMapView(), tileCache->getBaseHeightMapSampler(),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        // Caustics UBO (binding 22) - per-frame buffer for underwater caustics
        constexpr VkDeviceSize causticsUBOSize = 32;  // 8 floats
        writer.writeBuffer(22, buffers->getCausticsUniformBuffer(i), 0, causticsUBOSize);

        // Liquid UBO (binding 29) - per-frame buffer for terrain liquid effects
        constexpr VkDeviceSize liquidUBOSize = 128;  // TerrainLiquidUBO size
        writer.writeBuffer(29, buffers->getLiquidUniformBuffer(i), 0, liquidUBOSize);

        // Material Layer UBO (binding 30) - per-frame buffer for layer blending
        constexpr VkDeviceSize materialLayerUBOSize = 336;  // MaterialLayerUBO size
        writer.writeBuffer(30, buffers->getMaterialLayerUniformBuffer(i), 0, materialLayerUBOSize);

        // Screen-space shadow buffer (binding 31)
        // NOTE: ScreenSpaceShadowSystem must be created BEFORE this wiring runs
        // (see RendererInitPhases.cpp). If missing, the heightmap is used as a
        // "neutral" placeholder (values near 0-1 map to mostly-lit).
        if (screenShadowView_) {
            writer.writeImage(31, screenShadowView_, screenShadowSampler_);
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "TerrainSystem: Screen shadow buffer not available, using heightmap as placeholder");
            writer.writeImage(31, tileCache->getBaseHeightMapView(), tileCache->getBaseHeightMapSampler(),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        writer.update();
    }

    // Initialize effect UBOs (caustics, liquid, material layers)
    effects.initializeUBOs(buffers.get());
}

void TerrainSystem::setSnowMask(vk::Device device, vk::ImageView snowMaskView, vk::Sampler snowMaskSampler) {
    for (uint32_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, renderDescriptorSets[i])
            .writeImage(9, snowMaskView, snowMaskSampler)
            .update();
    }
}

void TerrainSystem::setVolumetricSnowCascades(vk::Device device,
                                               vk::ImageView cascade0View, vk::ImageView cascade1View, vk::ImageView cascade2View,
                                               vk::Sampler cascadeSampler) {
    for (uint32_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, renderDescriptorSets[i])
            .writeImage(10, cascade0View, cascadeSampler)
            .writeImage(11, cascade1View, cascadeSampler)
            .writeImage(12, cascade2View, cascadeSampler)
            .update();
    }
}

void TerrainSystem::setCloudShadowMap(vk::Device device, vk::ImageView cloudShadowView, vk::Sampler cloudShadowSampler) {
    for (uint32_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, renderDescriptorSets[i])
            .writeImage(13, cloudShadowView, cloudShadowSampler)
            .update();
    }
}

void TerrainSystem::setCaustics(vk::Device device, vk::ImageView causticsView, vk::Sampler causticsSampler,
                                 float waterLevel, bool enabled) {
    // Update texture binding (21)
    for (uint32_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, renderDescriptorSets[i])
            .writeImage(21, causticsView, causticsSampler)
            .update();
    }

    // Store state in effects for per-frame UBO updates
    effects.setCausticsParams(waterLevel, enabled);

    // Update caustics UBO with new water level and enabled state
    for (uint32_t i = 0; i < framesInFlight; i++) {
        float* causticsData = static_cast<float*>(buffers->getCausticsMappedPtr(i));
        if (causticsData) {
            causticsData[0] = waterLevel;            // causticsWaterLevel
            causticsData[6] = enabled ? 1.0f : 0.0f; // causticsEnabled
        }
    }
}

void TerrainSystem::setLiquidWetness(float wetness) {
    effects.setLiquidWetness(wetness);

    // Update all frames immediately
    const auto& liquidConfig = effects.getLiquidConfig();
    for (uint32_t i = 0; i < framesInFlight; i++) {
        void* liquidData = buffers->getLiquidMappedPtr(i);
        if (liquidData) {
            memcpy(liquidData, &liquidConfig, sizeof(material::TerrainLiquidUBO));
        }
    }
}

void TerrainSystem::setLiquidConfig(const material::TerrainLiquidUBO& config) {
    effects.setLiquidConfig(config);

    // Update all frames immediately
    for (uint32_t i = 0; i < framesInFlight; i++) {
        void* liquidData = buffers->getLiquidMappedPtr(i);
        if (liquidData) {
            memcpy(liquidData, &config, sizeof(material::TerrainLiquidUBO));
        }
    }
}

void TerrainSystem::setMaterialLayerStack(const material::MaterialLayerStack& stack) {
    effects.setMaterialLayerStack(stack);

    // Update all frames immediately
    const auto& materialLayerUBO = effects.getMaterialLayerUBO();
    for (uint32_t i = 0; i < framesInFlight; i++) {
        void* layerData = buffers->getMaterialLayerMappedPtr(i);
        if (layerData) {
            memcpy(layerData, &materialLayerUBO, sizeof(material::MaterialLayerUBO));
        }
    }
}
