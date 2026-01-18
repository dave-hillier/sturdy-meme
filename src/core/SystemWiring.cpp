#include "SystemWiring.h"
#include "RendererSystems.h"
#include "GlobalBufferManager.h"
#include "ShadowSystem.h"
#include "TerrainSystem.h"
#include "GrassSystem.h"
#include "LeafSystem.h"
#include "WeatherSystem.h"
#include "CloudShadowSystem.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "FroxelSystem.h"
#include "PostProcessSystem.h"
#include "WindSystem.h"
#include "RockSystem.h"
#include "DetritusSystem.h"
#include "SkinnedMeshRenderer.h"
#include "SceneManager.h"
#include "WaterSystem.h"
#include "MaterialDescriptorFactory.h"
#include "MaterialRegistry.h"
#include "UBOs.h"

SystemWiring::SystemWiring(VkDevice device, uint32_t framesInFlight)
    : device_(device), framesInFlight_(framesInFlight) {}

void SystemWiring::wireAll(RendererSystems& systems) {
    wireTerrainDescriptors(systems);
    wireSnowSystems(systems);
    wireLeafDescriptors(systems);
    wireGrassDescriptors(systems);
    wireWeatherDescriptors(systems);
    wireFroxelToWeather(systems);
    wireCloudShadowToTerrain(systems);
    wireCloudShadowBindings(systems);
    wireCausticsToTerrain(systems);
}

void SystemWiring::wireTerrainDescriptors(RendererSystems& systems) {
    auto& terrain = systems.terrain();
    auto& globalBuffers = systems.globalBuffers();
    auto& shadow = systems.shadow();

    terrain.updateDescriptorSets(
        vk::Device(device_),
        toVkBuffers(globalBuffers.uniformBuffers.buffers),
        vk::ImageView(shadow.getShadowImageView()),
        vk::Sampler(shadow.getShadowSampler()),
        toVkBuffers(globalBuffers.snowBuffers.buffers),
        toVkBuffers(globalBuffers.cloudShadowBuffers.buffers));
}

void SystemWiring::wireGrassDescriptors(RendererSystems& systems) {
    auto& grass = systems.grass();
    auto& globalBuffers = systems.globalBuffers();
    auto& shadow = systems.shadow();
    auto& terrain = systems.terrain();
    auto& cloudShadow = systems.cloudShadow();

    auto windBuffers = collectWindBuffers(systems.wind());
    auto tileInfoBuffers = collectTileInfoBuffers(terrain);

    grass.updateDescriptorSets(
        device_,
        globalBuffers.uniformBuffers.buffers,
        shadow.getShadowImageView(),
        shadow.getShadowSampler(),
        windBuffers,
        globalBuffers.lightBuffers.buffers,
        terrain.getHeightMapView(),
        terrain.getHeightMapSampler(),
        globalBuffers.snowBuffers.buffers,
        globalBuffers.cloudShadowBuffers.buffers,
        cloudShadow.getShadowMapView(),
        cloudShadow.getShadowMapSampler(),
        terrain.getTileArrayView(),
        terrain.getTileSampler(),
        tileInfoBuffers,
        &globalBuffers.dynamicRendererUBO);
}

void SystemWiring::wireLeafDescriptors(RendererSystems& systems) {
    auto& leaf = systems.leaf();
    auto& globalBuffers = systems.globalBuffers();
    auto& terrain = systems.terrain();
    auto& grass = systems.grass();

    auto windBuffers = collectWindBuffers(systems.wind());
    auto tileInfoBuffers = collectTileInfoBuffers(terrain);

    leaf.updateDescriptorSets(
        device_,
        globalBuffers.uniformBuffers.buffers,
        windBuffers,
        terrain.getHeightMapView(),
        terrain.getHeightMapSampler(),
        grass.getDisplacementImageView(),
        grass.getDisplacementSampler(),
        terrain.getTileArrayView(),
        terrain.getTileSampler(),
        tileInfoBuffers,
        &globalBuffers.dynamicRendererUBO);
}

void SystemWiring::wireWeatherDescriptors(RendererSystems& systems) {
    auto& weather = systems.weather();
    auto& globalBuffers = systems.globalBuffers();
    auto& postProcess = systems.postProcess();
    auto& shadow = systems.shadow();

    auto windBuffers = collectWindBuffers(systems.wind());

    weather.updateDescriptorSets(
        device_,
        globalBuffers.uniformBuffers.buffers,
        windBuffers,
        postProcess.getHDRDepthView(),
        shadow.getShadowSampler(),
        &globalBuffers.dynamicRendererUBO);
}

void SystemWiring::wireSnowSystems(RendererSystems& systems) {
    const EnvironmentSettings* envSettings = &systems.wind().getEnvironmentSettings();

    // Connect snow systems to environment settings
    systems.snowMask().setEnvironmentSettings(envSettings);
    systems.volumetricSnow().setEnvironmentSettings(envSettings);
    systems.leaf().setEnvironmentSettings(envSettings);

    // Wire snow mask to terrain and grass
    auto& terrain = systems.terrain();
    auto& grass = systems.grass();
    auto& snowMask = systems.snowMask();
    auto& volumetricSnow = systems.volumetricSnow();

    terrain.setSnowMask(device_, snowMask.getSnowMaskView(), snowMask.getSnowMaskSampler());
    terrain.setVolumetricSnowCascades(device_,
        volumetricSnow.getCascadeView(0),
        volumetricSnow.getCascadeView(1),
        volumetricSnow.getCascadeView(2),
        volumetricSnow.getCascadeSampler());
    grass.setSnowMask(device_, snowMask.getSnowMaskView(), snowMask.getSnowMaskSampler());
}

void SystemWiring::wireFroxelToWeather(RendererSystems& systems) {
    auto& froxel = systems.froxel();
    auto& weather = systems.weather();

    weather.setFroxelVolume(
        froxel.getScatteringVolumeView(),
        froxel.getVolumeSampler(),
        froxel.getVolumetricFarPlane(),
        FroxelSystem::DEPTH_DISTRIBUTION);
}

void SystemWiring::wireCloudShadowToTerrain(RendererSystems& systems) {
    auto& terrain = systems.terrain();
    auto& cloudShadow = systems.cloudShadow();

    terrain.setCloudShadowMap(device_,
        cloudShadow.getShadowMapView(),
        cloudShadow.getShadowMapSampler());
}

void SystemWiring::wireCloudShadowBindings(RendererSystems& systems) {
    auto& cloudShadow = systems.cloudShadow();
    VkImageView cloudShadowView = cloudShadow.getShadowMapView();
    VkSampler cloudShadowSampler = cloudShadow.getShadowMapSampler();

    // Update MaterialRegistry-managed descriptor sets
    systems.scene().getSceneBuilder().getMaterialRegistry().updateCloudShadowBinding(
        device_, cloudShadowView, cloudShadowSampler);

    // Update descriptor sets owned by systems (rocks, detritus)
    MaterialDescriptorFactory factory(device_);
    if (systems.rock().hasDescriptorSets()) {
        for (uint32_t i = 0; i < framesInFlight_; i++) {
            factory.updateCloudShadowBinding(systems.rock().getDescriptorSet(i),
                                              cloudShadowView, cloudShadowSampler);
        }
    }
    if (systems.detritus() && systems.detritus()->hasDescriptorSets()) {
        for (uint32_t i = 0; i < framesInFlight_; i++) {
            factory.updateCloudShadowBinding(systems.detritus()->getDescriptorSet(i),
                                              cloudShadowView, cloudShadowSampler);
        }
    }

    // Update skinned mesh renderer cloud shadow binding
    systems.skinnedMesh().updateCloudShadowBinding(cloudShadowView, cloudShadowSampler);
}

void SystemWiring::wireCausticsToTerrain(RendererSystems& systems) {
    auto& water = systems.water();
    auto& terrain = systems.terrain();

    if (water.getFoamTextureView() != VK_NULL_HANDLE) {
        terrain.setCaustics(device_,
            water.getFoamTextureView(),
            water.getFoamTextureSampler(),
            water.getWaterLevel(),
            true);  // Enable caustics
    }
}

std::vector<VkBuffer> SystemWiring::collectWindBuffers(const WindSystem& wind) const {
    std::vector<VkBuffer> windBuffers(framesInFlight_);
    for (uint32_t i = 0; i < framesInFlight_; i++) {
        windBuffers[i] = wind.getBufferInfo(i).buffer;
    }
    return windBuffers;
}

std::array<VkBuffer, 3> SystemWiring::collectTileInfoBuffers(const TerrainSystem& terrain) const {
    return {
        terrain.getTileInfoBuffer(0),
        terrain.getTileInfoBuffer(1),
        terrain.getTileInfoBuffer(2)
    };
}

std::vector<vk::Buffer> SystemWiring::toVkBuffers(const std::vector<VkBuffer>& raw) {
    std::vector<vk::Buffer> result;
    result.reserve(raw.size());
    for (auto b : raw) {
        result.push_back(vk::Buffer(b));
    }
    return result;
}
