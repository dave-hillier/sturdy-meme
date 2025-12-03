#define VMA_IMPLEMENTATION
#include "Renderer.h"
#include "ShaderLoader.h"
#include "BindingBuilder.h"
#include <SDL3/SDL_vulkan.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <array>
#include <limits>

bool Renderer::init(SDL_Window* win, const std::string& resPath) {
    window = win;
    resourcePath = resPath;

    // Initialize Vulkan context (instance, device, queues, allocator, swapchain)
    if (!vulkanContext.init(window)) {
        SDL_Log("Failed to initialize Vulkan context");
        return false;
    }

    // Get convenience references for the rest of initialization
    VkDevice device = vulkanContext.getDevice();
    VmaAllocator allocator = vulkanContext.getAllocator();
    VkPhysicalDevice physicalDevice = vulkanContext.getPhysicalDevice();
    VkQueue graphicsQueue = vulkanContext.getGraphicsQueue();
    VkExtent2D swapchainExtent = vulkanContext.getSwapchainExtent();
    VkFormat swapchainImageFormat = vulkanContext.getSwapchainImageFormat();
    if (!createRenderPass()) return false;
    if (!createDepthResources()) return false;
    if (!createFramebuffers()) return false;
    if (!createCommandPool()) return false;
    if (!createDescriptorSetLayout()) return false;
    if (!createDescriptorPool()) return false;

    // Initialize post-process system early to get HDR render pass
    PostProcessSystem::InitInfo postProcessInfo{};
    postProcessInfo.device = device;
    postProcessInfo.allocator = allocator;
    postProcessInfo.outputRenderPass = renderPass;
    postProcessInfo.descriptorPool = descriptorPool;
    postProcessInfo.extent = swapchainExtent;
    postProcessInfo.swapchainFormat = swapchainImageFormat;
    postProcessInfo.shaderPath = resourcePath + "/shaders";
    postProcessInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;

    if (!postProcessSystem.init(postProcessInfo)) return false;

    // Initialize bloom system
    BloomSystem::InitInfo bloomInfo{};
    bloomInfo.device = device;
    bloomInfo.allocator = allocator;
    bloomInfo.descriptorPool = descriptorPool;
    bloomInfo.extent = swapchainExtent;
    bloomInfo.shaderPath = resourcePath + "/shaders";

    if (!bloomSystem.init(bloomInfo)) return false;

    // Bind bloom texture to post-process system
    postProcessSystem.setBloomTexture(bloomSystem.getBloomOutput(), bloomSystem.getBloomSampler());

    if (!createGraphicsPipeline()) return false;

    // Initialize skinned mesh rendering (GPU skinning for animated characters)
    if (!createSkinnedDescriptorSetLayout()) return false;
    if (!createSkinnedGraphicsPipeline()) return false;
    if (!createBoneMatricesBuffers()) return false;

    // Initialize sky system (needs HDR render pass from postProcessSystem)
    SkySystem::InitInfo skyInfo{};
    skyInfo.device = device;
    skyInfo.allocator = allocator;
    skyInfo.descriptorPool = descriptorPool;
    skyInfo.shaderPath = resourcePath + "/shaders";
    skyInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    skyInfo.extent = swapchainExtent;
    skyInfo.hdrRenderPass = postProcessSystem.getHDRRenderPass();

    if (!skySystem.init(skyInfo)) return false;
    if (!createCommandBuffers()) return false;
    if (!createUniformBuffers()) return false;
    if (!createLightBuffers()) return false;

    // Initialize shadow system (needs descriptor set layout for pipeline compatibility)
    ShadowSystem::InitInfo shadowInfo{};
    shadowInfo.device = device;
    shadowInfo.physicalDevice = physicalDevice;
    shadowInfo.allocator = allocator;
    shadowInfo.descriptorPool = descriptorPool;
    shadowInfo.mainDescriptorSetLayout = descriptorSetLayout;
    shadowInfo.shaderPath = resourcePath + "/shaders";
    shadowInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;

    if (!shadowSystem.init(shadowInfo)) return false;

    // Initialize scene (meshes, textures, objects, lights)
    SceneBuilder::InitInfo sceneInfo{};
    sceneInfo.allocator = allocator;
    sceneInfo.device = device;
    sceneInfo.commandPool = commandPool;
    sceneInfo.graphicsQueue = graphicsQueue;
    sceneInfo.physicalDevice = physicalDevice;
    sceneInfo.resourcePath = resourcePath;

    if (!sceneManager.init(sceneInfo)) return false;

    // Initialize snow mask system early (before createDescriptorSets, since shader.frag needs binding 8)
    SnowMaskSystem::InitInfo snowMaskInfo{};
    snowMaskInfo.device = device;
    snowMaskInfo.allocator = allocator;
    snowMaskInfo.renderPass = postProcessSystem.getHDRRenderPass();
    snowMaskInfo.descriptorPool = descriptorPool;
    snowMaskInfo.extent = swapchainExtent;
    snowMaskInfo.shaderPath = resourcePath + "/shaders";
    snowMaskInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;

    if (!snowMaskSystem.init(snowMaskInfo)) return false;

    // Initialize volumetric snow system (cascaded heightfield)
    VolumetricSnowSystem::InitInfo volumetricSnowInfo{};
    volumetricSnowInfo.device = device;
    volumetricSnowInfo.allocator = allocator;
    volumetricSnowInfo.renderPass = postProcessSystem.getHDRRenderPass();
    volumetricSnowInfo.descriptorPool = descriptorPool;
    volumetricSnowInfo.extent = swapchainExtent;
    volumetricSnowInfo.shaderPath = resourcePath + "/shaders";
    volumetricSnowInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;

    if (!volumetricSnowSystem.init(volumetricSnowInfo)) return false;

    if (!createDescriptorSets()) return false;
    if (!createSkinnedDescriptorSets()) return false;

    // Initialize grass system using HDR render pass
    GrassSystem::InitInfo grassInfo{};
    grassInfo.device = device;
    grassInfo.allocator = allocator;
    grassInfo.renderPass = postProcessSystem.getHDRRenderPass();
    grassInfo.shadowRenderPass = shadowSystem.getShadowRenderPass();
    grassInfo.descriptorPool = descriptorPool;
    grassInfo.extent = swapchainExtent;
    grassInfo.shadowMapSize = shadowSystem.getShadowMapSize();
    grassInfo.shaderPath = resourcePath + "/shaders";
    grassInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;

    if (!grassSystem.init(grassInfo)) return false;

    // Initialize terrain system with Isle of Wight heightmap
    std::string heightmapPath = resourcePath + "/assets/terrain/isleofwight-0m-200m.png";
    std::string terrainCachePath = resourcePath + "/terrain_cache";

    // Import heightmap into tile cache if needed (for future streaming support)
    TerrainImportConfig importConfig{};
    importConfig.sourceHeightmapPath = heightmapPath;
    importConfig.cacheDirectory = terrainCachePath;
    importConfig.minAltitude = 0.0f;
    importConfig.maxAltitude = 200.0f;
    importConfig.metersPerPixel = 1.0f;  // Treating 3m/px data as 1m/px for more dramatic terrain
    importConfig.tileResolution = 512;
    importConfig.numLODLevels = 4;

    TerrainImporter importer;
    if (!importer.isCacheValid(importConfig)) {
        SDL_Log("Importing terrain heightmap: %s", heightmapPath.c_str());
        if (importer.import(importConfig, [](float progress, const std::string& status) {
            SDL_Log("Terrain import: %.0f%% - %s", progress * 100.0f, status.c_str());
        })) {
            SDL_Log("Terrain cache created: %zu x %zu tiles",
                    importer.getTilesX(), importer.getTilesZ());
        }
    } else {
        SDL_Log("Using existing terrain cache");
    }

    // Initialize terrain system with CBT (loads heightmap directly for now)
    TerrainSystem::InitInfo terrainInfo{};
    terrainInfo.device = device;
    terrainInfo.physicalDevice = physicalDevice;
    terrainInfo.allocator = allocator;
    terrainInfo.renderPass = postProcessSystem.getHDRRenderPass();
    terrainInfo.shadowRenderPass = shadowSystem.getShadowRenderPass();
    terrainInfo.descriptorPool = descriptorPool;
    terrainInfo.extent = swapchainExtent;
    terrainInfo.shadowMapSize = shadowSystem.getShadowMapSize();
    terrainInfo.shaderPath = resourcePath + "/shaders";
    terrainInfo.texturePath = resourcePath + "/textures";
    terrainInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    terrainInfo.graphicsQueue = graphicsQueue;
    terrainInfo.commandPool = commandPool;

    TerrainConfig terrainConfig{};
    // Use world size based on imported terrain (or default if import failed)
    terrainConfig.size = importer.getWorldWidth() > 0 ? importer.getWorldWidth() : 16000.0f;
    terrainConfig.heightScale = 200.0f;
    terrainConfig.maxDepth = 20;  // Higher depth for larger terrain
    terrainConfig.minDepth = 2;
    terrainConfig.targetEdgePixels = 16.0f;
    terrainConfig.splitThreshold = 24.0f;
    terrainConfig.mergeThreshold = 8.0f;
    terrainConfig.heightmapPath = heightmapPath;
    terrainConfig.minAltitude = 0.0f;
    terrainConfig.maxAltitude = 200.0f;

    if (!terrainSystem.init(terrainInfo, terrainConfig)) return false;

    // Initialize wind system
    WindSystem::InitInfo windInfo{};
    windInfo.device = device;
    windInfo.allocator = allocator;
    windInfo.descriptorPool = descriptorPool;
    windInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;

    if (!windSystem.init(windInfo)) return false;

    const EnvironmentSettings* environmentSettings = &windSystem.getEnvironmentSettings();
    grassSystem.setEnvironmentSettings(environmentSettings);
    leafSystem.setEnvironmentSettings(environmentSettings);

    // Get wind buffers for grass descriptor sets
    std::vector<VkBuffer> windBuffers(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        windBuffers[i] = windSystem.getBufferInfo(i).buffer;
    }
    grassSystem.updateDescriptorSets(device, uniformBuffers, shadowSystem.getShadowImageView(), shadowSystem.getShadowSampler(), windBuffers, lightBuffers,
                                      terrainSystem.getHeightMapView(), terrainSystem.getHeightMapSampler());

    // Update terrain descriptor sets with shared resources
    terrainSystem.updateDescriptorSets(device, uniformBuffers, shadowSystem.getShadowImageView(), shadowSystem.getShadowSampler());

    // Initialize rock system (uses terrain for height queries)
    RockSystem::InitInfo rockInfo{};
    rockInfo.device = device;
    rockInfo.allocator = allocator;
    rockInfo.commandPool = commandPool;
    rockInfo.graphicsQueue = graphicsQueue;
    rockInfo.physicalDevice = physicalDevice;
    rockInfo.resourcePath = resourcePath;
    rockInfo.terrainSize = terrainConfig.size;
    rockInfo.getTerrainHeight = [this](float x, float z) {
        return terrainSystem.getHeightAt(x, z);
    };

    RockConfig rockConfig{};
    rockConfig.rockVariations = 6;
    rockConfig.rocksPerVariation = 10;
    rockConfig.minRadius = 0.4f;
    rockConfig.maxRadius = 2.0f;
    rockConfig.placementRadius = 100.0f;
    rockConfig.minDistanceBetween = 4.0f;
    rockConfig.roughness = 0.35f;
    rockConfig.asymmetry = 0.3f;
    rockConfig.subdivisions = 3;
    rockConfig.materialRoughness = 0.75f;
    rockConfig.materialMetallic = 0.0f;

    if (!rockSystem.init(rockInfo, rockConfig)) return false;

    // Update rock descriptor sets now that rock textures are loaded
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        DescriptorManager::SetWriter writer(device, rockDescriptorSets[i]);
        writer
            .writeBuffer(0, uniformBuffers[i], 0, sizeof(UniformBufferObject))
            .writeImage(1, rockSystem.getRockTexture().getImageView(),
                       rockSystem.getRockTexture().getSampler())
            .writeImage(2, shadowSystem.getShadowImageView(), shadowSystem.getShadowSampler())
            .writeImage(3, rockSystem.getRockNormalMap().getImageView(),
                       rockSystem.getRockNormalMap().getSampler())
            .writeBuffer(4, lightBuffers[i], 0, sizeof(LightBuffer),
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeImage(5, sceneManager.getSceneBuilder().getDefaultEmissiveMap().getImageView(),
                       sceneManager.getSceneBuilder().getDefaultEmissiveMap().getSampler())
            .writeImage(6, shadowSystem.getPointShadowArrayView(i), shadowSystem.getPointShadowSampler())
            .writeImage(7, shadowSystem.getSpotShadowArrayView(i), shadowSystem.getSpotShadowSampler())
            .writeImage(8, snowMaskSystem.getSnowMaskView(), snowMaskSystem.getSnowMaskSampler())
            .update();
    }

    // Initialize weather particle system (rain/snow)
    WeatherSystem::InitInfo weatherInfo{};
    weatherInfo.device = device;
    weatherInfo.allocator = allocator;
    weatherInfo.renderPass = postProcessSystem.getHDRRenderPass();
    weatherInfo.descriptorPool = descriptorPool;
    weatherInfo.extent = swapchainExtent;
    weatherInfo.shaderPath = resourcePath + "/shaders";
    weatherInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;

    if (!weatherSystem.init(weatherInfo)) return false;

    // Update weather system descriptor sets with wind buffers
    weatherSystem.updateDescriptorSets(device, uniformBuffers, windBuffers, depthImageView, shadowSystem.getShadowSampler());

    // Connect snow mask to environment settings (already initialized above)
    snowMaskSystem.setEnvironmentSettings(environmentSettings);
    volumetricSnowSystem.setEnvironmentSettings(environmentSettings);

    // Connect snow mask to terrain system (legacy)
    terrainSystem.setSnowMask(device, snowMaskSystem.getSnowMaskView(), snowMaskSystem.getSnowMaskSampler());

    // Connect volumetric snow cascades to terrain system
    terrainSystem.setVolumetricSnowCascades(device,
        volumetricSnowSystem.getCascadeView(0),
        volumetricSnowSystem.getCascadeView(1),
        volumetricSnowSystem.getCascadeView(2),
        volumetricSnowSystem.getCascadeSampler());

    // Connect snow mask to grass system
    grassSystem.setSnowMask(device, snowMaskSystem.getSnowMaskView(), snowMaskSystem.getSnowMaskSampler());

    // Initialize leaf particle system
    LeafSystem::InitInfo leafInfo{};
    leafInfo.device = device;
    leafInfo.allocator = allocator;
    leafInfo.renderPass = postProcessSystem.getHDRRenderPass();
    leafInfo.descriptorPool = descriptorPool;
    leafInfo.extent = swapchainExtent;
    leafInfo.shaderPath = resourcePath + "/shaders";
    leafInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;

    if (!leafSystem.init(leafInfo)) return false;

    // Update leaf system descriptor sets with wind buffers, terrain heightmap, and displacement map
    leafSystem.updateDescriptorSets(device, uniformBuffers, windBuffers,
                                     terrainSystem.getHeightMapView(), terrainSystem.getHeightMapSampler(),
                                     grassSystem.getDisplacementImageView(), grassSystem.getDisplacementSampler());

    // Set default leaf intensity (autumn scene)
    leafSystem.setIntensity(0.5f);

    // Initialize froxel volumetric fog system (Phase 4.3)
    FroxelSystem::InitInfo froxelInfo{};
    froxelInfo.device = device;
    froxelInfo.allocator = allocator;
    froxelInfo.descriptorPool = descriptorPool;
    froxelInfo.extent = swapchainExtent;
    froxelInfo.shaderPath = resourcePath + "/shaders";
    froxelInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    froxelInfo.shadowMapView = shadowSystem.getShadowImageView();
    froxelInfo.shadowSampler = shadowSystem.getShadowSampler();
    froxelInfo.lightBuffers = lightBuffers;  // For local light contribution in fog

    if (!froxelSystem.init(froxelInfo)) return false;

    // Connect froxel volume to post-process system for compositing (use integrated volume)
    postProcessSystem.setFroxelVolume(froxelSystem.getIntegratedVolumeView(), froxelSystem.getVolumeSampler());
    postProcessSystem.setFroxelParams(froxelSystem.getVolumetricFarPlane(), FroxelSystem::DEPTH_DISTRIBUTION);
    postProcessSystem.setFroxelEnabled(true);

    // Connect froxel volume to weather system for fog particle lighting (Phase 4.3.9)
    weatherSystem.setFroxelVolume(froxelSystem.getScatteringVolumeView(), froxelSystem.getVolumeSampler(),
                                   froxelSystem.getVolumetricFarPlane(), FroxelSystem::DEPTH_DISTRIBUTION);

    // Initialize atmosphere LUT system (Phase 4.1)
    AtmosphereLUTSystem::InitInfo atmosphereInfo{};
    atmosphereInfo.device = device;
    atmosphereInfo.allocator = allocator;
    atmosphereInfo.descriptorPool = descriptorPool;
    atmosphereInfo.shaderPath = resourcePath + "/shaders";
    atmosphereInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;

    if (!atmosphereLUTSystem.init(atmosphereInfo)) return false;

    // Compute atmosphere LUTs at startup
    VkCommandBuffer cmdBuffer;
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &allocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    // Compute transmittance and multi-scatter LUTs (once at startup)
    atmosphereLUTSystem.computeTransmittanceLUT(cmdBuffer);
    atmosphereLUTSystem.computeMultiScatterLUT(cmdBuffer);
    // Compute irradiance LUTs after transmittance (Phase 4.1.9)
    atmosphereLUTSystem.computeIrradianceLUT(cmdBuffer);

    // Compute sky-view LUT for current sun direction
    glm::vec3 sunDir = glm::vec3(0.0f, 0.707f, 0.707f);  // Default 45 degree sun
    atmosphereLUTSystem.computeSkyViewLUT(cmdBuffer, sunDir, glm::vec3(0.0f), 0.0f);

    // Compute cloud map LUT (paraboloid projection)
    atmosphereLUTSystem.computeCloudMapLUT(cmdBuffer, glm::vec3(0.0f), 0.0f);

    vkEndCommandBuffer(cmdBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmdBuffer);

    SDL_Log("Atmosphere LUTs computed successfully");

    // Export LUTs as PNG files for visualization
    atmosphereLUTSystem.exportLUTsAsPNG(resourcePath);
    SDL_Log("Atmosphere LUTs exported as PNG to: %s", resourcePath.c_str());

    // Initialize cloud shadow system
    CloudShadowSystem::InitInfo cloudShadowInfo{};
    cloudShadowInfo.device = device;
    cloudShadowInfo.allocator = allocator;
    cloudShadowInfo.descriptorPool = descriptorPool;
    cloudShadowInfo.shaderPath = resourcePath + "/shaders";
    cloudShadowInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    cloudShadowInfo.cloudMapLUTView = atmosphereLUTSystem.getCloudMapLUTView();
    cloudShadowInfo.cloudMapLUTSampler = atmosphereLUTSystem.getLUTSampler();

    if (!cloudShadowSystem.init(cloudShadowInfo)) return false;

    // Connect cloud shadow map to terrain system
    terrainSystem.setCloudShadowMap(device,
        cloudShadowSystem.getShadowMapView(),
        cloudShadowSystem.getShadowMapSampler());

    // Update main descriptor sets with cloud shadow map (binding 9)
    // This is done here because cloudShadowSystem is initialized after createDescriptorSets
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo cloudShadowInfo{};
        cloudShadowInfo.sampler = cloudShadowSystem.getShadowMapSampler();
        cloudShadowInfo.imageView = cloudShadowSystem.getShadowMapView();
        cloudShadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 4> writes{};
        // Update all material descriptor sets
        VkDescriptorSet sets[] = {descriptorSets[i], groundDescriptorSets[i], metalDescriptorSets[i], rockDescriptorSets[i]};
        for (size_t j = 0; j < 4; j++) {
            writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[j].dstSet = sets[j];
            writes[j].dstBinding = 9;
            writes[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[j].descriptorCount = 1;
            writes[j].pImageInfo = &cloudShadowInfo;
        }
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Initialize Catmull-Clark subdivision system
    CatmullClarkSystem::InitInfo catmullClarkInfo{};
    catmullClarkInfo.device = device;
    catmullClarkInfo.physicalDevice = physicalDevice;
    catmullClarkInfo.allocator = allocator;
    catmullClarkInfo.renderPass = postProcessSystem.getHDRRenderPass();
    catmullClarkInfo.descriptorPool = descriptorPool;
    catmullClarkInfo.extent = swapchainExtent;
    catmullClarkInfo.shaderPath = resourcePath + "/shaders";
    catmullClarkInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    catmullClarkInfo.graphicsQueue = graphicsQueue;
    catmullClarkInfo.commandPool = commandPool;

    CatmullClarkConfig catmullClarkConfig{};
    float suzanneX = 5.0f, suzanneZ = -5.0f;
    float terrainY = terrainSystem.getHeightAt(suzanneX, suzanneZ);
    catmullClarkConfig.position = glm::vec3(suzanneX, terrainY + 2.0f, suzanneZ);
    catmullClarkConfig.scale = glm::vec3(2.0f);
    catmullClarkConfig.targetEdgePixels = 12.0f;
    catmullClarkConfig.maxDepth = 16;
    catmullClarkConfig.splitThreshold = 18.0f;
    catmullClarkConfig.mergeThreshold = 6.0f;
    catmullClarkConfig.objPath = resourcePath + "/assets/suzanne.obj";

    if (!catmullClarkSystem.init(catmullClarkInfo, catmullClarkConfig)) return false;

    // Update Catmull-Clark descriptor sets with shared resources
    catmullClarkSystem.updateDescriptorSets(device, uniformBuffers);

    // Create sky descriptor sets now that uniform buffers and LUTs are ready
    if (!skySystem.createDescriptorSets(uniformBuffers, sizeof(UniformBufferObject), atmosphereLUTSystem)) return false;

    // Initialize Hi-Z occlusion culling system
    HiZSystem::InitInfo hiZInfo{};
    hiZInfo.device = device;
    hiZInfo.allocator = allocator;
    hiZInfo.descriptorPool = descriptorPool;
    hiZInfo.extent = swapchainExtent;
    hiZInfo.shaderPath = resourcePath + "/shaders";
    hiZInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    hiZInfo.depthFormat = depthFormat;

    if (!hiZSystem.init(hiZInfo)) {
        SDL_Log("Warning: Hi-Z system initialization failed, occlusion culling disabled");
        // Continue without Hi-Z - it's an optional optimization
    } else {
        // Connect depth buffer to Hi-Z system
        hiZSystem.setDepthBuffer(depthImageView, depthSampler);

        // Initialize object data for culling
        updateHiZObjectData();
    }

    if (!createSyncObjects()) return false;

    return true;
}

void Renderer::setWeatherIntensity(float intensity) {
    weatherSystem.setIntensity(intensity);
}

void Renderer::setWeatherType(uint32_t type) {
    weatherSystem.setWeatherType(type);
}

void Renderer::setPlayerPosition(const glm::vec3& position, float radius) {
    playerPosition = position;
    playerCapsuleRadius = radius;
}

void Renderer::shutdown() {
    VkDevice device = vulkanContext.getDevice();
    VmaAllocator allocator = vulkanContext.getAllocator();

    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
            vkDestroyFence(device, inFlightFences[i], nullptr);
        }

        sceneManager.destroy(allocator, device);

        vkDestroyDescriptorPool(device, descriptorPool, nullptr);

        // Clean up the auto-growing descriptor pool
        if (descriptorManagerPool.has_value()) {
            descriptorManagerPool->destroy();
            descriptorManagerPool.reset();
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vmaDestroyBuffer(allocator, uniformBuffers[i], uniformBuffersAllocations[i]);
        }

        // Clean up light buffers
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (lightBuffers.size() > i && lightBuffers[i] != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, lightBuffers[i], lightBufferAllocations[i]);
            }
        }

        grassSystem.destroy(device, allocator);
        terrainSystem.destroy(device, allocator);
        catmullClarkSystem.destroy(device, allocator);
        rockSystem.destroy(allocator, device);
        windSystem.destroy(device, allocator);
        weatherSystem.destroy(device, allocator);
        snowMaskSystem.destroy(device, allocator);
        volumetricSnowSystem.destroy(device, allocator);
        leafSystem.destroy(device, allocator);
        froxelSystem.destroy(device, allocator);
        cloudShadowSystem.destroy();
        hiZSystem.destroy();
        atmosphereLUTSystem.destroy(device, allocator);
        skySystem.destroy(device, allocator);
        postProcessSystem.destroy(device, allocator);
        bloomSystem.destroy(device, allocator);

        vkDestroyPipeline(device, graphicsPipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        // Clean up skinned mesh resources
        if (skinnedGraphicsPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, skinnedGraphicsPipeline, nullptr);
        }
        if (skinnedPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, skinnedPipelineLayout, nullptr);
        }
        if (skinnedDescriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, skinnedDescriptorSetLayout, nullptr);
        }
        for (size_t i = 0; i < boneMatricesBuffers.size(); ++i) {
            if (boneMatricesBuffers[i] != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, boneMatricesBuffers[i], boneMatricesAllocations[i]);
            }
        }

        // Shadow system cleanup
        shadowSystem.destroy();

        vkDestroyCommandPool(device, commandPool, nullptr);

        // Clean up Renderer-specific resources (depth, framebuffers, renderPass)
        destroyRenderResources();
    }

    vulkanContext.shutdown();
}

void Renderer::destroyRenderResources() {
    VkDevice device = vulkanContext.getDevice();
    VmaAllocator allocator = vulkanContext.getAllocator();

    if (depthSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, depthSampler, nullptr);
        depthSampler = VK_NULL_HANDLE;
    }
    if (depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }
    if (depthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, depthImage, depthImageAllocation);
        depthImage = VK_NULL_HANDLE;
    }

    for (auto framebuffer : framebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    framebuffers.clear();

    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
}

bool Renderer::createRenderPass() {
    VkDevice device = vulkanContext.getDevice();
    VkFormat swapchainImageFormat = vulkanContext.getSwapchainImageFormat();

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // Store depth for Hi-Z pyramid generation
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Transition to shader read for Hi-Z
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    depthFormat = VK_FORMAT_D32_SFLOAT;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        SDL_Log("Failed to create render pass");
        return false;
    }

    return true;
}

bool Renderer::createDepthResources() {
    VkDevice device = vulkanContext.getDevice();
    VmaAllocator allocator = vulkanContext.getAllocator();
    VkExtent2D swapchainExtent = vulkanContext.getSwapchainExtent();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = swapchainExtent.width;
    imageInfo.extent.height = swapchainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Add SAMPLED_BIT for Hi-Z pyramid generation
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &depthImage, &depthImageAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create depth image");
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
        SDL_Log("Failed to create depth image view");
        return false;
    }

    // Create depth sampler for Hi-Z pyramid generation
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &depthSampler) != VK_SUCCESS) {
        SDL_Log("Failed to create depth sampler");
        return false;
    }

    return true;
}

bool Renderer::createFramebuffers() {
    VkDevice device = vulkanContext.getDevice();
    const auto& swapchainImageViews = vulkanContext.getSwapchainImageViews();
    VkExtent2D swapchainExtent = vulkanContext.getSwapchainExtent();

    framebuffers.resize(swapchainImageViews.size());

    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = {
            swapchainImageViews[i],
            depthImageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create framebuffer");
            return false;
        }
    }

    return true;
}

bool Renderer::createCommandPool() {
    VkDevice device = vulkanContext.getDevice();
    uint32_t queueFamilyIndex = vulkanContext.getGraphicsQueueFamily();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndex;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        SDL_Log("Failed to create command pool");
        return false;
    }

    return true;
}

bool Renderer::createCommandBuffers() {
    VkDevice device = vulkanContext.getDevice();

    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        SDL_Log("Failed to allocate command buffers");
        return false;
    }

    return true;
}

bool Renderer::createSyncObjects() {
    VkDevice device = vulkanContext.getDevice();

    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create sync objects");
            return false;
        }
    }

    return true;
}

bool Renderer::createDescriptorSetLayout() {
    VkDevice device = vulkanContext.getDevice();

    // Main scene descriptor set layout:
    // 0: UBO (camera/view data)
    // 1: Diffuse texture sampler
    // 2: Shadow map sampler (CSM cascade array)
    // 3: Normal map sampler
    // 4: Light buffer (SSBO for dynamic lights)
    // 5: Emissive map sampler
    // 6: Point shadow cube maps
    // 7: Spot shadow depth maps
    // 8: Snow mask texture
    // 9: Cloud shadow map
    descriptorSetLayout = DescriptorManager::LayoutBuilder(device)
        .addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)  // 0: UBO
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 1: diffuse
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 2: shadow
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 3: normal
        .addStorageBuffer(VK_SHADER_STAGE_FRAGMENT_BIT)         // 4: lights
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 5: emissive
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 6: point shadow
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 7: spot shadow
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 8: snow mask
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 9: cloud shadow map
        .build();

    if (descriptorSetLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create descriptor set layout");
        return false;
    }

    return true;
}

bool Renderer::createGraphicsPipeline() {
    VkDevice device = vulkanContext.getDevice();
    VkExtent2D swapchainExtent = vulkanContext.getSwapchainExtent();

    auto vertShaderCode = ShaderLoader::readFile(resourcePath + "/shaders/shader.vert.spv");
    auto fragShaderCode = ShaderLoader::readFile(resourcePath + "/shaders/shader.frag.spv");

    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        SDL_Log("Failed to load shader files");
        return false;
    }

    VkShaderModule vertShaderModule = ShaderLoader::createShaderModule(device, vertShaderCode);
    VkShaderModule fragShaderModule = ShaderLoader::createShaderModule(device, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainExtent.width);
    viewport.height = static_cast<float>(swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create pipeline layout");
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = postProcessSystem.getHDRRenderPass();
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        SDL_Log("Failed to create graphics pipeline");
        return false;
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    return true;
}

bool Renderer::createUniformBuffers() {
    VmaAllocator allocator = vulkanContext.getAllocator();
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersAllocations.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo;
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &uniformBuffers[i],
                            &uniformBuffersAllocations[i], &allocationInfo) != VK_SUCCESS) {
            SDL_Log("Failed to create uniform buffer");
            return false;
        }

        uniformBuffersMapped[i] = allocationInfo.pMappedData;
    }

    return true;
}

bool Renderer::createLightBuffers() {
    VmaAllocator allocator = vulkanContext.getAllocator();
    VkDeviceSize bufferSize = sizeof(LightBuffer);

    lightBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    lightBufferAllocations.resize(MAX_FRAMES_IN_FLIGHT);
    lightBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo;
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &lightBuffers[i],
                            &lightBufferAllocations[i], &allocationInfo) != VK_SUCCESS) {
            SDL_Log("Failed to create light buffer");
            return false;
        }

        lightBuffersMapped[i] = allocationInfo.pMappedData;

        // Initialize with empty light buffer
        LightBuffer emptyBuffer{};
        emptyBuffer.lightCount = glm::uvec4(0, 0, 0, 0);
        memcpy(lightBuffersMapped[i], &emptyBuffer, sizeof(LightBuffer));
    }

    return true;
}

void Renderer::updateLightBuffer(uint32_t currentImage, const Camera& camera) {
    LightBuffer buffer{};
    glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    sceneManager.getLightManager().buildLightBuffer(buffer, camera.getPosition(), camera.getFront(), viewProj, lightCullRadius);
    memcpy(lightBuffersMapped[currentImage], &buffer, sizeof(LightBuffer));
}

bool Renderer::createDescriptorPool() {
    VkDevice device = vulkanContext.getDevice();

    // Create the new auto-growing descriptor pool
    // Initial capacity of 64 sets per pool, will automatically grow if exhausted
    descriptorManagerPool.emplace(device, 64);

    // Legacy pool for systems not yet migrated to DescriptorManager
    // This pool is still needed for: GrassSystem, WeatherSystem, LeafSystem, HiZSystem, etc.
    // HiZ system needs: ~11 pyramid descriptor sets (one per mip level) + 2 culling sets
    //   - Combined image samplers: 2 per pyramid set + 1 per culling set = ~24
    //   - Storage images: 1 per pyramid set = ~11
    //   - Storage buffers: 3 per culling set = ~6
    //   - Uniform buffers: 1 per culling set = ~2
    std::array<VkDescriptorPoolSize, 4> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 20);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 50);
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 40);
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[3].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 24);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 42);

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        SDL_Log("Failed to create legacy descriptor pool");
        return false;
    }

    return true;
}

bool Renderer::createDescriptorSets() {
    VkDevice device = vulkanContext.getDevice();

    // Allocate descriptor sets using the new pool manager
    descriptorSets = descriptorManagerPool->allocate(descriptorSetLayout, MAX_FRAMES_IN_FLIGHT);
    if (descriptorSets.empty()) {
        SDL_Log("Failed to allocate descriptor sets");
        return false;
    }

    groundDescriptorSets = descriptorManagerPool->allocate(descriptorSetLayout, MAX_FRAMES_IN_FLIGHT);
    if (groundDescriptorSets.empty()) {
        SDL_Log("Failed to allocate ground descriptor sets");
        return false;
    }

    metalDescriptorSets = descriptorManagerPool->allocate(descriptorSetLayout, MAX_FRAMES_IN_FLIGHT);
    if (metalDescriptorSets.empty()) {
        SDL_Log("Failed to allocate metal descriptor sets");
        return false;
    }

    rockDescriptorSets = descriptorManagerPool->allocate(descriptorSetLayout, MAX_FRAMES_IN_FLIGHT);
    if (rockDescriptorSets.empty()) {
        SDL_Log("Failed to allocate rock descriptor sets");
        return false;
    }

    characterDescriptorSets = descriptorManagerPool->allocate(descriptorSetLayout, MAX_FRAMES_IN_FLIGHT);
    if (characterDescriptorSets.empty()) {
        SDL_Log("Failed to allocate character descriptor sets");
        return false;
    }

    // Helper lambda to write common bindings shared across all material sets
    auto writeCommonBindings = [this](DescriptorManager::SetWriter& writer, size_t frameIndex) {
        writer
            .writeBuffer(0, uniformBuffers[frameIndex], 0, sizeof(UniformBufferObject))
            .writeImage(2, shadowSystem.getShadowImageView(), shadowSystem.getShadowSampler())
            .writeBuffer(4, lightBuffers[frameIndex], 0, sizeof(LightBuffer),
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeImage(5, sceneManager.getSceneBuilder().getDefaultEmissiveMap().getImageView(),
                       sceneManager.getSceneBuilder().getDefaultEmissiveMap().getSampler())
            .writeImage(6, shadowSystem.getPointShadowArrayView(frameIndex), shadowSystem.getPointShadowSampler())
            .writeImage(7, shadowSystem.getSpotShadowArrayView(frameIndex), shadowSystem.getSpotShadowSampler())
            .writeImage(8, snowMaskSystem.getSnowMaskView(), snowMaskSystem.getSnowMaskSampler());
    };

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Crate material descriptor sets
        {
            DescriptorManager::SetWriter writer(device, descriptorSets[i]);
            writeCommonBindings(writer, i);
            writer
                .writeImage(1, sceneManager.getSceneBuilder().getCrateTexture().getImageView(),
                           sceneManager.getSceneBuilder().getCrateTexture().getSampler())
                .writeImage(3, sceneManager.getSceneBuilder().getCrateNormalMap().getImageView(),
                           sceneManager.getSceneBuilder().getCrateNormalMap().getSampler())
                .update();
        }

        // Ground material descriptor sets
        {
            DescriptorManager::SetWriter writer(device, groundDescriptorSets[i]);
            writeCommonBindings(writer, i);
            writer
                .writeImage(1, sceneManager.getSceneBuilder().getGroundTexture().getImageView(),
                           sceneManager.getSceneBuilder().getGroundTexture().getSampler())
                .writeImage(3, sceneManager.getSceneBuilder().getGroundNormalMap().getImageView(),
                           sceneManager.getSceneBuilder().getGroundNormalMap().getSampler())
                .update();
        }

        // Metal material descriptor sets
        {
            DescriptorManager::SetWriter writer(device, metalDescriptorSets[i]);
            writeCommonBindings(writer, i);
            writer
                .writeImage(1, sceneManager.getSceneBuilder().getMetalTexture().getImageView(),
                           sceneManager.getSceneBuilder().getMetalTexture().getSampler())
                .writeImage(3, sceneManager.getSceneBuilder().getMetalNormalMap().getImageView(),
                           sceneManager.getSceneBuilder().getMetalNormalMap().getSampler())
                .update();
        }

        // Character material descriptor sets (white texture for vertex colors)
        {
            DescriptorManager::SetWriter writer(device, characterDescriptorSets[i]);
            writeCommonBindings(writer, i);
            writer
                .writeImage(1, sceneManager.getSceneBuilder().getWhiteTexture().getImageView(),
                           sceneManager.getSceneBuilder().getWhiteTexture().getSampler())
                .writeImage(3, sceneManager.getSceneBuilder().getWhiteTexture().getImageView(),
                           sceneManager.getSceneBuilder().getWhiteTexture().getSampler())  // No normal map, use white
                .update();
        }
    }

    return true;
}


void Renderer::updateUniformBuffer(uint32_t currentImage, const Camera& camera) {
    // Update time of day (state mutation)
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    float cycleDuration = 120.0f;
    if (useManualTime) {
        currentTimeOfDay = manualTime;
    } else {
        currentTimeOfDay = fmod((time * timeScale) / cycleDuration, 1.0f);
    }

    // Pure calculations
    LightingParams lighting = calculateLightingParams(currentTimeOfDay);

    // Update cascade matrices via shadow system
    shadowSystem.updateCascadeMatrices(lighting.sunDir, camera);

    // Build UBO data (pure calculation)
    UniformBufferObject ubo = buildUniformBufferData(camera, lighting, currentTimeOfDay);

    // State mutations
    lastSunIntensity = lighting.sunIntensity;
    memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));

    // Update light buffer with camera-based culling
    updateLightBuffer(currentImage, camera);

    // Calculate sun screen position (pure) and update post-process (state mutation)
    glm::vec2 sunScreenPos = calculateSunScreenPos(camera, lighting.sunDir);
    postProcessSystem.setSunScreenPos(sunScreenPos);
}

void Renderer::render(const Camera& camera) {
    VkDevice device = vulkanContext.getDevice();
    VkSwapchainKHR swapchain = vulkanContext.getSwapchain();
    VkQueue graphicsQueue = vulkanContext.getGraphicsQueue();
    VkQueue presentQueue = vulkanContext.getPresentQueue();

    // Frame synchronization
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                            imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return;
    }

    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    // Update uniform buffer data
    updateUniformBuffer(currentFrame, camera);

    // Update bone matrices for GPU skinning
    updateBoneMatrices(currentFrame);

    // Calculate frame timing
    static auto startTime = std::chrono::high_resolution_clock::now();
    static auto lastTime = startTime;
    auto currentTime = std::chrono::high_resolution_clock::now();
    float grassTime = std::chrono::duration<float>(currentTime - startTime).count();
    float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;

    // Update subsystems (state mutations)
    windSystem.update(deltaTime);
    windSystem.updateUniforms(currentFrame);

    glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    const auto& terrainConfig = terrainSystem.getConfig();
    grassSystem.updateUniforms(currentFrame, camera.getPosition(), viewProj,
                               terrainConfig.size, terrainConfig.heightScale);
    grassSystem.updateDisplacementSources(playerPosition, playerCapsuleRadius, deltaTime);
    weatherSystem.updateUniforms(currentFrame, camera.getPosition(), viewProj, deltaTime, grassTime, windSystem);
    terrainSystem.updateUniforms(currentFrame, camera.getPosition(), camera.getViewMatrix(), camera.getProjectionMatrix(),
                                  volumetricSnowSystem.getCascadeParams(), useVolumetricSnow, MAX_SNOW_HEIGHT);

    // Update snow mask system - accumulation/melting based on weather type
    bool isSnowing = (weatherSystem.getWeatherType() == 1);  // 1 = snow
    float weatherIntensity = weatherSystem.getIntensity();
    // Auto-adjust snow amount based on weather state
    if (isSnowing && weatherIntensity > 0.0f) {
        environmentSettings.snowAmount = glm::min(environmentSettings.snowAmount + environmentSettings.snowAccumulationRate * deltaTime, 1.0f);
    } else if (environmentSettings.snowAmount > 0.0f) {
        environmentSettings.snowAmount = glm::max(environmentSettings.snowAmount - environmentSettings.snowMeltRate * deltaTime, 0.0f);
    }
    snowMaskSystem.setMaskCenter(camera.getPosition());
    snowMaskSystem.updateUniforms(currentFrame, deltaTime, isSnowing, weatherIntensity, environmentSettings);

    // Update volumetric snow system
    volumetricSnowSystem.setCameraPosition(camera.getPosition());
    volumetricSnowSystem.setWindDirection(glm::vec2(windSystem.getEnvironmentSettings().windDirection.x,
                                                     windSystem.getEnvironmentSettings().windDirection.y));
    volumetricSnowSystem.setWindStrength(windSystem.getEnvironmentSettings().windStrength);
    volumetricSnowSystem.updateUniforms(currentFrame, deltaTime, isSnowing, weatherIntensity, environmentSettings);

    // Add player footprint interaction with snow
    if (environmentSettings.snowAmount > 0.1f) {
        snowMaskSystem.addInteraction(playerPosition, playerCapsuleRadius * 1.5f, 0.3f);
        volumetricSnowSystem.addInteraction(playerPosition, playerCapsuleRadius * 1.5f, 0.3f);
    }

    // Update leaf system with player position (using camera as player proxy)
    // TODO: Integrate actual player velocity from Player class for proper disruption
    glm::vec3 playerPos = camera.getPosition();
    glm::vec3 playerVel = glm::vec3(0.0f);  // Will be updated when player movement tracking is added
    leafSystem.updateUniforms(currentFrame, camera.getPosition(), viewProj, playerPos, playerVel, deltaTime, grassTime,
                               terrainConfig.size, terrainConfig.heightScale);

    // Begin command buffer recording
    vkResetCommandBuffer(commandBuffers[currentFrame], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffers[currentFrame], &beginInfo);

    VkCommandBuffer cmd = commandBuffers[currentFrame];

    // Terrain compute pass (adaptive subdivision)
    terrainSystem.recordCompute(cmd, currentFrame);

    // Catmull-Clark subdivision compute pass
    catmullClarkSystem.recordCompute(cmd, currentFrame);

    // Grass displacement update (player/NPC interaction)
    grassSystem.recordDisplacementUpdate(cmd, currentFrame);

    // Grass compute pass
    grassSystem.recordResetAndCompute(cmd, currentFrame, grassTime);

    // Weather particle compute pass
    weatherSystem.recordResetAndCompute(cmd, currentFrame, grassTime, deltaTime);

    // Snow mask accumulation compute pass
    snowMaskSystem.recordCompute(cmd, currentFrame);

    // Volumetric snow cascade compute pass
    volumetricSnowSystem.recordCompute(cmd, currentFrame);

    // Leaf particle compute pass
    leafSystem.recordResetAndCompute(cmd, currentFrame, grassTime, deltaTime);

    // Cloud shadow map compute pass
    if (cloudShadowSystem.isEnabled()) {
        UniformBufferObject* ubo = static_cast<UniformBufferObject*>(uniformBuffersMapped[currentFrame]);
        glm::vec3 sunDir = glm::normalize(glm::vec3(ubo->sunDirection));
        float sunIntensity = ubo->sunDirection.w;

        // Wind offset for cloud animation (matching cloud LUT animation)
        glm::vec2 windDir = windSystem.getWindDirection();
        float windSpeed = windSystem.getWindSpeed();
        float windTime = windSystem.getTime();
        float cloudTimeScale = 0.02f;  // Match cloud map LUT speed
        glm::vec3 windOffset = glm::vec3(windDir.x * windSpeed * windTime * cloudTimeScale,
                                          windTime * 0.002f,
                                          windDir.y * windSpeed * windTime * cloudTimeScale);

        cloudShadowSystem.recordUpdate(cmd, currentFrame, sunDir, sunIntensity,
                                        windOffset, windTime * cloudTimeScale, camera.getPosition());
    }

    // Shadow pass (skip when sun is below horizon)
    if (lastSunIntensity > 0.001f) {
        recordShadowPass(cmd, currentFrame, grassTime);
    }

    // Froxel volumetric fog compute pass
    {
        UniformBufferObject* ubo = static_cast<UniformBufferObject*>(uniformBuffersMapped[currentFrame]);
        glm::vec3 sunDir = glm::normalize(glm::vec3(ubo->sunDirection));
        float sunIntensity = ubo->sunDirection.w;
        glm::vec3 sunColor = glm::vec3(ubo->sunColor);

        // Pass cascade matrices for volumetric shadow sampling
        froxelSystem.recordFroxelUpdate(cmd, currentFrame,
                                        camera.getViewMatrix(), camera.getProjectionMatrix(),
                                        camera.getPosition(),
                                        sunDir, sunIntensity, sunColor,
                                        shadowSystem.getCascadeMatrices().data(),
                                        ubo->cascadeSplits);

        postProcessSystem.setCameraPlanes(camera.getNearPlane(), camera.getFarPlane());

        // Update sky-view LUT with current sun direction (Phase 4.1.5)
        // This precomputes atmospheric scattering for all view directions
        atmosphereLUTSystem.updateSkyViewLUT(cmd, sunDir, camera.getPosition(), 0.0f);

        // Update cloud map LUT with wind animation (Paraboloid projection)
        glm::vec2 windDir = windSystem.getWindDirection();
        float windSpeed = windSystem.getWindSpeed();
        float windTime = windSystem.getTime();
        // Slow down cloud animation for realistic drift (0.02x speed)
        float cloudTimeScale = 0.02f;
        glm::vec3 windOffset = glm::vec3(windDir.x * windSpeed * windTime * cloudTimeScale,
                                          windTime * 0.002f,  // Slow vertical evolution
                                          windDir.y * windSpeed * windTime * cloudTimeScale);
        atmosphereLUTSystem.updateCloudMapLUT(cmd, windOffset, windTime * cloudTimeScale);
    }

    // HDR scene render pass
    recordHDRPass(cmd, currentFrame, grassTime);

    // Multi-pass bloom
    bloomSystem.setThreshold(postProcessSystem.getBloomThreshold());
    bloomSystem.recordBloomPass(cmd, postProcessSystem.getHDRColorView());

    // Post-process pass (with optional GUI overlay callback)
    postProcessSystem.recordPostProcess(cmd, currentFrame, framebuffers[imageIndex], deltaTime, guiRenderCallback);

    vkEndCommandBuffer(cmd);

    // Queue submission
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]);

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(presentQueue, &presentInfo);

    // Advance grass double-buffer sets after frame submission
    // This swaps compute/render buffer sets so next frame can overlap:
    // - Next frame's compute writes to what was the render set
    // - Next frame's render reads from what was the compute set (now contains fresh data)
    grassSystem.advanceBufferSet();
    weatherSystem.advanceBufferSet();
    leafSystem.advanceBufferSet();

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer::waitIdle() {
    vulkanContext.waitIdle();
}

// Pure calculation helpers - no state mutation

Renderer::LightingParams Renderer::calculateLightingParams(float timeOfDay) const {
    LightingParams params{};

    DateTime dateTime = DateTime::fromTimeOfDay(timeOfDay, currentYear, currentMonth, currentDay);
    CelestialPosition sunPos = celestialCalculator.calculateSunPosition(dateTime);
    MoonPosition moonPos = celestialCalculator.calculateMoonPosition(dateTime);

    params.sunDir = sunPos.direction;
    params.moonDir = moonPos.direction;
    params.sunIntensity = sunPos.intensity;
    params.moonIntensity = moonPos.intensity;

    // Smooth transition for moon as light source during twilight
    if (moonPos.altitude > -5.0f) {
        float twilightFactor = glm::smoothstep(10.0f, -6.0f, sunPos.altitude);
        params.moonIntensity *= (1.0f + twilightFactor * 1.0f);
    }

    params.sunColor = celestialCalculator.getSunColor(sunPos.altitude);
    params.moonColor = celestialCalculator.getMoonColor(moonPos.altitude, moonPos.illumination);
    params.ambientColor = celestialCalculator.getAmbientColor(sunPos.altitude);
    params.moonPhase = moonPos.phase;  // Moon phase for lunar cycle simulation
    params.julianDay = dateTime.toJulianDay();

    return params;
}

UniformBufferObject Renderer::buildUniformBufferData(const Camera& camera, const LightingParams& lighting, float timeOfDay) const {
    UniformBufferObject ubo{};
    ubo.model = glm::mat4(1.0f);
    ubo.view = camera.getViewMatrix();
    ubo.proj = camera.getProjectionMatrix();

    // Copy cascade matrices from shadow system
    const auto& cascadeMatrices = shadowSystem.getCascadeMatrices();
    for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
        ubo.cascadeViewProj[i] = cascadeMatrices[i];
    }

    // Store view-space split depths from shadow system
    const auto& cascadeSplitDepths = shadowSystem.getCascadeSplitDepths();
    ubo.cascadeSplits = glm::vec4(
        cascadeSplitDepths[1],
        cascadeSplitDepths[2],
        cascadeSplitDepths[3],
        cascadeSplitDepths[4]
    );

    ubo.sunDirection = glm::vec4(lighting.sunDir, lighting.sunIntensity);
    ubo.moonDirection = glm::vec4(lighting.moonDir, lighting.moonIntensity);
    ubo.sunColor = glm::vec4(lighting.sunColor, 1.0f);
    ubo.moonColor = glm::vec4(lighting.moonColor, lighting.moonPhase);  // Pass moon phase in alpha channel
    ubo.ambientColor = glm::vec4(lighting.ambientColor, 1.0f);
    ubo.cameraPosition = glm::vec4(camera.getPosition(), 1.0f);

    // Point light from the glowing sphere (position updated by physics)
    float pointLightIntensity = 5.0f;
    float pointLightRadius = 8.0f;
    ubo.pointLightPosition = glm::vec4(sceneManager.getOrbLightPosition(), pointLightIntensity);
    ubo.pointLightColor = glm::vec4(1.0f, 0.9f, 0.7f, pointLightRadius);

    // Wind parameters for cloud animation
    glm::vec2 windDir = windSystem.getWindDirection();
    float windSpeed = windSystem.getWindSpeed();
    float windTime = windSystem.getTime();
    ubo.windDirectionAndSpeed = glm::vec4(windDir.x, windDir.y, windSpeed, windTime);

    ubo.timeOfDay = timeOfDay;
    ubo.shadowMapSize = static_cast<float>(shadowSystem.getShadowMapSize());
    ubo.debugCascades = showCascadeDebug ? 1.0f : 0.0f;
    ubo.julianDay = static_cast<float>(lighting.julianDay);
    ubo.cloudStyle = useParaboloidClouds ? 1.0f : 0.0f;

    // Snow parameters
    ubo.snowAmount = environmentSettings.snowAmount;
    ubo.snowRoughness = environmentSettings.snowRoughness;
    ubo.snowTexScale = environmentSettings.snowTexScale;
    ubo.snowColor = glm::vec4(environmentSettings.snowColor, 1.0f);
    ubo.snowMaskParams = glm::vec4(snowMaskSystem.getMaskOrigin(), snowMaskSystem.getMaskSize(), 0.0f);

    // Volumetric snow cascade parameters
    auto cascadeParams = volumetricSnowSystem.getCascadeParams();
    ubo.snowCascade0Params = cascadeParams[0];
    ubo.snowCascade1Params = cascadeParams[1];
    ubo.snowCascade2Params = cascadeParams[2];
    ubo.useVolumetricSnow = useVolumetricSnow ? 1.0f : 0.0f;
    ubo.snowMaxHeight = MAX_SNOW_HEIGHT;
    ubo.debugSnowDepth = showSnowDepthDebug ? 1.0f : 0.0f;
    ubo.snowPadding2 = 0.0f;

    // Cloud shadow parameters
    ubo.cloudShadowMatrix = cloudShadowSystem.getWorldToShadowUV();
    ubo.cloudShadowIntensity = cloudShadowSystem.getShadowIntensity();
    ubo.cloudShadowEnabled = cloudShadowSystem.isEnabled() ? 1.0f : 0.0f;
    ubo.cloudShadowPadding1 = 0.0f;
    ubo.cloudShadowPadding2 = 0.0f;

    return ubo;
}

glm::vec2 Renderer::calculateSunScreenPos(const Camera& camera, const glm::vec3& sunDir) const {
    glm::vec3 sunWorldPos = camera.getPosition() + sunDir * 1000.0f;
    glm::vec4 sunClipPos = camera.getProjectionMatrix() * camera.getViewMatrix() * glm::vec4(sunWorldPos, 1.0f);

    glm::vec2 sunScreenPos(0.5f, 0.5f);
    if (sunClipPos.w > 0.0f) {
        glm::vec3 sunNDC = glm::vec3(sunClipPos) / sunClipPos.w;
        sunScreenPos = glm::vec2(sunNDC.x * 0.5f + 0.5f, sunNDC.y * 0.5f + 0.5f);
        sunScreenPos.y = 1.0f - sunScreenPos.y;
    }
    return sunScreenPos;
}

// Render pass recording helpers - pure command recording, no state mutation

void Renderer::recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime) {
    // Delegate to the shadow system with callbacks for terrain and grass
    auto terrainCallback = [this, frameIndex](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        terrainSystem.recordShadowDraw(cb, frameIndex, lightMatrix, static_cast<int>(cascade));
    };

    auto grassCallback = [this, frameIndex, grassTime](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        (void)lightMatrix;  // Grass uses cascade index only
        grassSystem.recordShadowDraw(cb, frameIndex, grassTime, cascade);
    };

    // Combine scene objects and rock objects for shadow rendering
    // Skip player character if using GPU skinning (skinned shadow pass not implemented yet)
    std::vector<Renderable> allObjects;
    const auto& sceneObjects = sceneManager.getSceneObjects();
    size_t playerIndex = sceneManager.getSceneBuilder().getPlayerObjectIndex();
    bool hasCharacter = sceneManager.getSceneBuilder().hasCharacter();
    bool useGPUSkinning = hasCharacter && sceneManager.getSceneBuilder().getAnimatedCharacter().isGPUSkinningEnabled();

    allObjects.reserve(sceneObjects.size() + rockSystem.getSceneObjects().size());
    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        // Skip player character if using GPU skinning
        if (useGPUSkinning && i == playerIndex && hasCharacter) {
            continue;
        }
        allObjects.push_back(sceneObjects[i]);
    }
    allObjects.insert(allObjects.end(), rockSystem.getSceneObjects().begin(), rockSystem.getSceneObjects().end());

    shadowSystem.recordShadowPass(cmd, frameIndex, descriptorSets[frameIndex],
                                   allObjects,
                                   terrainCallback, grassCallback);
}

void Renderer::recordSceneObjects(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Helper lambda to render a scene object
    auto renderObject = [&](const Renderable& obj, VkDescriptorSet* descSet) {
        PushConstants push{};
        push.model = obj.transform;
        push.roughness = obj.roughness;
        push.metallic = obj.metallic;
        push.emissiveIntensity = obj.emissiveIntensity;
        push.opacity = obj.opacity;
        push.emissiveColor = glm::vec4(obj.emissiveColor, 1.0f);

        vkCmdPushConstants(cmd, pipelineLayout,
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(PushConstants), &push);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout, 0, 1, descSet, 0, nullptr);

        VkBuffer vertexBuffers[] = {obj.mesh->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, obj.mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmd, obj.mesh->getIndexCount(), 1, 0, 0, 0);
    };

    // Render scene manager objects
    const auto& sceneObjects = sceneManager.getSceneObjects();
    size_t playerIndex = sceneManager.getSceneBuilder().getPlayerObjectIndex();
    bool hasCharacter = sceneManager.getSceneBuilder().hasCharacter();
    bool useGPUSkinning = hasCharacter && sceneManager.getSceneBuilder().getAnimatedCharacter().isGPUSkinningEnabled();

    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        // Skip player character if using GPU skinning (rendered separately)
        if (useGPUSkinning && i == playerIndex && hasCharacter) {
            continue;
        }

        const auto& obj = sceneObjects[i];
        VkDescriptorSet* descSet;
        if (obj.texture == &sceneManager.getSceneBuilder().getGroundTexture()) {
            descSet = &groundDescriptorSets[frameIndex];
        } else if (obj.texture == &sceneManager.getSceneBuilder().getMetalTexture()) {
            descSet = &metalDescriptorSets[frameIndex];
        } else if (obj.texture == &sceneManager.getSceneBuilder().getWhiteTexture()) {
            descSet = &characterDescriptorSets[frameIndex];
        } else {
            descSet = &descriptorSets[frameIndex];
        }
        renderObject(obj, descSet);
    }

    // Render procedural rocks
    for (const auto& rock : rockSystem.getSceneObjects()) {
        renderObject(rock, &rockDescriptorSets[frameIndex]);
    }
}

void Renderer::recordHDRPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime) {
    VkRenderPassBeginInfo hdrPassInfo{};
    hdrPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    hdrPassInfo.renderPass = postProcessSystem.getHDRRenderPass();
    hdrPassInfo.framebuffer = postProcessSystem.getHDRFramebuffer();
    hdrPassInfo.renderArea.offset = {0, 0};
    hdrPassInfo.renderArea.extent = postProcessSystem.getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    hdrPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    hdrPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &hdrPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Draw sky (with atmosphere LUT bindings)
    skySystem.recordDraw(cmd, frameIndex);

    // Draw terrain (LEB adaptive tessellation)
    terrainSystem.recordDraw(cmd, frameIndex);

    // Draw Catmull-Clark subdivision surfaces
    catmullClarkSystem.recordDraw(cmd, frameIndex);

    // Draw scene objects (static meshes)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    recordSceneObjects(cmd, frameIndex);

    // Draw skinned character with GPU skinning
    recordSkinnedCharacter(cmd, frameIndex);

    // Draw grass
    grassSystem.recordDraw(cmd, frameIndex, grassTime);

    // Draw falling leaves - after grass, before weather
    leafSystem.recordDraw(cmd, frameIndex, grassTime);

    // Draw weather particles (rain/snow) - after opaque geometry
    weatherSystem.recordDraw(cmd, frameIndex, grassTime);

    vkCmdEndRenderPass(cmd);
}

// ===== GPU Skinning Implementation =====

bool Renderer::createSkinnedDescriptorSetLayout() {
    VkDevice device = vulkanContext.getDevice();

    // Skinned descriptor set layout:
    // Same as main layout but with binding 10 for bone matrices UBO
    // 0: UBO (camera/view data)
    // 1: Diffuse texture sampler
    // 2: Shadow map sampler (CSM cascade array)
    // 3: Normal map sampler
    // 4: Light buffer (SSBO for dynamic lights)
    // 5: Emissive map sampler
    // 6: Point shadow cube maps
    // 7: Spot shadow depth maps
    // 8: Snow mask texture
    // 9: Cloud shadow map
    // 10: Bone matrices UBO
    skinnedDescriptorSetLayout = DescriptorManager::LayoutBuilder(device)
        .addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)  // 0: UBO
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 1: diffuse
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 2: shadow
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 3: normal
        .addStorageBuffer(VK_SHADER_STAGE_FRAGMENT_BIT)         // 4: lights
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 5: emissive
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 6: point shadow
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 7: spot shadow
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 8: snow mask
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 9: cloud shadow map
        .addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT)           // 10: bone matrices
        .build();

    if (skinnedDescriptorSetLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create skinned descriptor set layout");
        return false;
    }

    return true;
}

bool Renderer::createSkinnedGraphicsPipeline() {
    VkDevice device = vulkanContext.getDevice();
    VkExtent2D swapchainExtent = vulkanContext.getSwapchainExtent();

    auto vertShaderCode = ShaderLoader::readFile(resourcePath + "/shaders/skinned.vert.spv");
    auto fragShaderCode = ShaderLoader::readFile(resourcePath + "/shaders/shader.frag.spv");

    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        SDL_Log("Failed to load skinned shader files");
        return false;
    }

    VkShaderModule vertShaderModule = ShaderLoader::createShaderModule(device, vertShaderCode);
    VkShaderModule fragShaderModule = ShaderLoader::createShaderModule(device, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Use SkinnedVertex input layout
    auto bindingDescription = SkinnedVertex::getBindingDescription();
    auto attributeDescriptions = SkinnedVertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainExtent.width);
    viewport.height = static_cast<float>(swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &skinnedDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &skinnedPipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create skinned pipeline layout");
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = skinnedPipelineLayout;
    pipelineInfo.renderPass = postProcessSystem.getHDRRenderPass();
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &skinnedGraphicsPipeline) != VK_SUCCESS) {
        SDL_Log("Failed to create skinned graphics pipeline");
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    SDL_Log("Created skinned graphics pipeline for GPU skinning");
    return true;
}

bool Renderer::createBoneMatricesBuffers() {
    VmaAllocator allocator = vulkanContext.getAllocator();
    VkDeviceSize bufferSize = sizeof(BoneMatricesUBO);

    boneMatricesBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    boneMatricesAllocations.resize(MAX_FRAMES_IN_FLIGHT);
    boneMatricesMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo;
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &boneMatricesBuffers[i],
                            &boneMatricesAllocations[i], &allocationInfo) != VK_SUCCESS) {
            SDL_Log("Failed to create bone matrices buffer");
            return false;
        }

        boneMatricesMapped[i] = allocationInfo.pMappedData;

        // Initialize with identity matrices
        BoneMatricesUBO* ubo = static_cast<BoneMatricesUBO*>(boneMatricesMapped[i]);
        for (uint32_t j = 0; j < MAX_BONES; j++) {
            ubo->bones[j] = glm::mat4(1.0f);
        }
    }

    SDL_Log("Created bone matrices buffers for GPU skinning");
    return true;
}

bool Renderer::createSkinnedDescriptorSets() {
    VkDevice device = vulkanContext.getDevice();

    // Allocate skinned descriptor sets
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, skinnedDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    skinnedDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device, &allocInfo, skinnedDescriptorSets.data()) != VK_SUCCESS) {
        SDL_Log("Failed to allocate skinned descriptor sets");
        return false;
    }

    // Update skinned descriptor sets
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::vector<VkWriteDescriptorSet> descriptorWrites;

        // Binding 0: UBO (same as regular pipeline)
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet uboWrite{};
        uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uboWrite.dstSet = skinnedDescriptorSets[i];
        uboWrite.dstBinding = 0;
        uboWrite.dstArrayElement = 0;
        uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboWrite.descriptorCount = 1;
        uboWrite.pBufferInfo = &bufferInfo;
        descriptorWrites.push_back(uboWrite);

        // Binding 1: Diffuse texture (white texture for vertex colors)
        VkDescriptorImageInfo diffuseInfo{};
        diffuseInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        diffuseInfo.imageView = sceneManager.getSceneBuilder().getWhiteTexture().getImageView();
        diffuseInfo.sampler = sceneManager.getSceneBuilder().getWhiteTexture().getSampler();

        VkWriteDescriptorSet diffuseWrite{};
        diffuseWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        diffuseWrite.dstSet = skinnedDescriptorSets[i];
        diffuseWrite.dstBinding = 1;
        diffuseWrite.dstArrayElement = 0;
        diffuseWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        diffuseWrite.descriptorCount = 1;
        diffuseWrite.pImageInfo = &diffuseInfo;
        descriptorWrites.push_back(diffuseWrite);

        // Binding 2: Shadow map
        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowInfo.imageView = shadowSystem.getShadowImageView();
        shadowInfo.sampler = shadowSystem.getShadowSampler();

        VkWriteDescriptorSet shadowWrite{};
        shadowWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        shadowWrite.dstSet = skinnedDescriptorSets[i];
        shadowWrite.dstBinding = 2;
        shadowWrite.dstArrayElement = 0;
        shadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        shadowWrite.descriptorCount = 1;
        shadowWrite.pImageInfo = &shadowInfo;
        descriptorWrites.push_back(shadowWrite);

        // Binding 3: Normal map (use white texture as dummy)
        VkDescriptorImageInfo normalInfo{};
        normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        normalInfo.imageView = sceneManager.getSceneBuilder().getWhiteTexture().getImageView();
        normalInfo.sampler = sceneManager.getSceneBuilder().getWhiteTexture().getSampler();

        VkWriteDescriptorSet normalWrite{};
        normalWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        normalWrite.dstSet = skinnedDescriptorSets[i];
        normalWrite.dstBinding = 3;
        normalWrite.dstArrayElement = 0;
        normalWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        normalWrite.descriptorCount = 1;
        normalWrite.pImageInfo = &normalInfo;
        descriptorWrites.push_back(normalWrite);

        // Binding 4: Light buffer
        VkDescriptorBufferInfo lightBufferInfo{};
        lightBufferInfo.buffer = lightBuffers[i];
        lightBufferInfo.offset = 0;
        lightBufferInfo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet lightWrite{};
        lightWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        lightWrite.dstSet = skinnedDescriptorSets[i];
        lightWrite.dstBinding = 4;
        lightWrite.dstArrayElement = 0;
        lightWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lightWrite.descriptorCount = 1;
        lightWrite.pBufferInfo = &lightBufferInfo;
        descriptorWrites.push_back(lightWrite);

        // Binding 5: Emissive map (black)
        VkDescriptorImageInfo emissiveInfo{};
        emissiveInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        emissiveInfo.imageView = sceneManager.getSceneBuilder().getDefaultEmissiveMap().getImageView();
        emissiveInfo.sampler = sceneManager.getSceneBuilder().getDefaultEmissiveMap().getSampler();

        VkWriteDescriptorSet emissiveWrite{};
        emissiveWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        emissiveWrite.dstSet = skinnedDescriptorSets[i];
        emissiveWrite.dstBinding = 5;
        emissiveWrite.dstArrayElement = 0;
        emissiveWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        emissiveWrite.descriptorCount = 1;
        emissiveWrite.pImageInfo = &emissiveInfo;
        descriptorWrites.push_back(emissiveWrite);

        // Binding 6: Point shadow (use default emissive as dummy)
        VkWriteDescriptorSet pointShadowWrite{};
        pointShadowWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        pointShadowWrite.dstSet = skinnedDescriptorSets[i];
        pointShadowWrite.dstBinding = 6;
        pointShadowWrite.dstArrayElement = 0;
        pointShadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pointShadowWrite.descriptorCount = 1;
        pointShadowWrite.pImageInfo = &emissiveInfo;
        descriptorWrites.push_back(pointShadowWrite);

        // Binding 7: Spot shadow (use default emissive as dummy)
        VkWriteDescriptorSet spotShadowWrite{};
        spotShadowWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        spotShadowWrite.dstSet = skinnedDescriptorSets[i];
        spotShadowWrite.dstBinding = 7;
        spotShadowWrite.dstArrayElement = 0;
        spotShadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        spotShadowWrite.descriptorCount = 1;
        spotShadowWrite.pImageInfo = &emissiveInfo;
        descriptorWrites.push_back(spotShadowWrite);

        // Binding 8: Snow mask
        VkDescriptorImageInfo snowMaskInfo{};
        snowMaskInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        snowMaskInfo.imageView = snowMaskSystem.getSnowMaskView();
        snowMaskInfo.sampler = snowMaskSystem.getSnowMaskSampler();

        VkWriteDescriptorSet snowMaskWrite{};
        snowMaskWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        snowMaskWrite.dstSet = skinnedDescriptorSets[i];
        snowMaskWrite.dstBinding = 8;
        snowMaskWrite.dstArrayElement = 0;
        snowMaskWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        snowMaskWrite.descriptorCount = 1;
        snowMaskWrite.pImageInfo = &snowMaskInfo;
        descriptorWrites.push_back(snowMaskWrite);

        // Binding 9: Cloud shadow map
        VkDescriptorImageInfo cloudShadowInfo{};
        cloudShadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cloudShadowInfo.imageView = cloudShadowSystem.getShadowMapView();
        cloudShadowInfo.sampler = cloudShadowSystem.getShadowMapSampler();

        VkWriteDescriptorSet cloudShadowWrite{};
        cloudShadowWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cloudShadowWrite.dstSet = skinnedDescriptorSets[i];
        cloudShadowWrite.dstBinding = 9;
        cloudShadowWrite.dstArrayElement = 0;
        cloudShadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        cloudShadowWrite.descriptorCount = 1;
        cloudShadowWrite.pImageInfo = &cloudShadowInfo;
        descriptorWrites.push_back(cloudShadowWrite);

        // Binding 10: Bone matrices UBO
        VkDescriptorBufferInfo boneInfo{};
        boneInfo.buffer = boneMatricesBuffers[i];
        boneInfo.offset = 0;
        boneInfo.range = sizeof(BoneMatricesUBO);

        VkWriteDescriptorSet boneWrite{};
        boneWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        boneWrite.dstSet = skinnedDescriptorSets[i];
        boneWrite.dstBinding = 10;
        boneWrite.dstArrayElement = 0;
        boneWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        boneWrite.descriptorCount = 1;
        boneWrite.pBufferInfo = &boneInfo;
        descriptorWrites.push_back(boneWrite);

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }

    SDL_Log("Created skinned descriptor sets for GPU skinning");
    return true;
}

void Renderer::updateBoneMatrices(uint32_t currentImage) {
    SceneBuilder& sceneBuilder = sceneManager.getSceneBuilder();
    if (!sceneBuilder.hasCharacter()) return;

    // Get bone matrices from animated character
    std::vector<glm::mat4> boneMatrices;
    sceneBuilder.getAnimatedCharacter().computeBoneMatrices(boneMatrices);

    // Copy to mapped buffer
    BoneMatricesUBO* ubo = static_cast<BoneMatricesUBO*>(boneMatricesMapped[currentImage]);
    size_t numBones = std::min(boneMatrices.size(), static_cast<size_t>(MAX_BONES));
    for (size_t i = 0; i < numBones; i++) {
        ubo->bones[i] = boneMatrices[i];
    }
}

void Renderer::recordSkinnedCharacter(VkCommandBuffer cmd, uint32_t frameIndex) {
    SceneBuilder& sceneBuilder = sceneManager.getSceneBuilder();
    if (!sceneBuilder.hasCharacter()) return;

    // Bind skinned pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedGraphicsPipeline);

    // Bind skinned descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            skinnedPipelineLayout, 0, 1, &skinnedDescriptorSets[frameIndex], 0, nullptr);

    // Get the player object to get transform
    const auto& sceneObjects = sceneBuilder.getSceneObjects();
    size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
    if (playerIndex >= sceneObjects.size()) return;

    const Renderable& playerObj = sceneObjects[playerIndex];

    // Push constants
    PushConstants push{};
    push.model = playerObj.transform;
    push.roughness = playerObj.roughness;
    push.metallic = playerObj.metallic;
    push.emissiveIntensity = playerObj.emissiveIntensity;
    push.opacity = playerObj.opacity;
    push.emissiveColor = glm::vec4(playerObj.emissiveColor, 1.0f);

    vkCmdPushConstants(cmd, skinnedPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &push);

    // Bind skinned mesh vertex and index buffers
    AnimatedCharacter& character = sceneBuilder.getAnimatedCharacter();
    SkinnedMesh& skinnedMesh = character.getSkinnedMesh();

    VkBuffer vertexBuffers[] = {skinnedMesh.getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, skinnedMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(cmd, skinnedMesh.getIndexCount(), 1, 0, 0, 0);
}

void Renderer::updateHiZObjectData() {
    std::vector<CullObjectData> cullObjects;

    // Gather scene objects for culling
    const auto& sceneObjects = sceneManager.getSceneObjects();
    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        const auto& obj = sceneObjects[i];
        if (obj.mesh == nullptr) continue;

        // Get local AABB and transform to world space
        const AABB& localBounds = obj.mesh->getBounds();
        AABB worldBounds = localBounds.transformed(obj.transform);

        CullObjectData cullData{};

        // Calculate bounding sphere from transformed AABB
        glm::vec3 center = worldBounds.getCenter();
        glm::vec3 extents = worldBounds.getExtents();
        float radius = glm::length(extents);

        cullData.boundingSphere = glm::vec4(center, radius);
        cullData.aabbMin = glm::vec4(worldBounds.min, 0.0f);
        cullData.aabbMax = glm::vec4(worldBounds.max, 0.0f);
        cullData.meshIndex = static_cast<uint32_t>(i);
        cullData.firstIndex = 0;  // Single mesh per object
        cullData.indexCount = obj.mesh->getIndexCount();
        cullData.vertexOffset = 0;

        cullObjects.push_back(cullData);
    }

    // Also add procedural rocks
    const auto& rockObjects = rockSystem.getSceneObjects();
    for (size_t i = 0; i < rockObjects.size(); ++i) {
        const auto& obj = rockObjects[i];
        if (obj.mesh == nullptr) continue;

        const AABB& localBounds = obj.mesh->getBounds();
        AABB worldBounds = localBounds.transformed(obj.transform);

        CullObjectData cullData{};
        glm::vec3 center = worldBounds.getCenter();
        glm::vec3 extents = worldBounds.getExtents();
        float radius = glm::length(extents);

        cullData.boundingSphere = glm::vec4(center, radius);
        cullData.aabbMin = glm::vec4(worldBounds.min, 0.0f);
        cullData.aabbMax = glm::vec4(worldBounds.max, 0.0f);
        cullData.meshIndex = static_cast<uint32_t>(sceneObjects.size() + i);
        cullData.firstIndex = 0;
        cullData.indexCount = obj.mesh->getIndexCount();
        cullData.vertexOffset = 0;

        cullObjects.push_back(cullData);
    }

    hiZSystem.updateObjectData(cullObjects);
}
