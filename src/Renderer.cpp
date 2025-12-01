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

    vkb::InstanceBuilder builder;
    auto instRet = builder.set_app_name("Vulkan Game")
        .request_validation_layers(true)
        .use_default_debug_messenger()
        .require_api_version(1, 2, 0)
        .build();

    if (!instRet) {
        SDL_Log("Failed to create Vulkan instance: %s", instRet.error().message().c_str());
        return false;
    }

    vkbInstance = instRet.value();
    instance = vkbInstance.instance;

    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
        SDL_Log("Failed to create Vulkan surface: %s", SDL_GetError());
        return false;
    }

    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_FALSE;

    vkb::PhysicalDeviceSelector selector{vkbInstance};
    auto physRet = selector.set_minimum_version(1, 2)
        .set_surface(surface)
        .set_required_features(features)
        .select();

    if (!physRet) {
        SDL_Log("Failed to select physical device: %s", physRet.error().message().c_str());
        return false;
    }

    vkb::PhysicalDevice vkbPhysicalDevice = physRet.value();
    physicalDevice = vkbPhysicalDevice.physical_device;

    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};
    auto devRet = deviceBuilder.build();

    if (!devRet) {
        SDL_Log("Failed to create logical device: %s", devRet.error().message().c_str());
        return false;
    }

    vkbDevice = devRet.value();
    device = vkbDevice.device;

    auto graphicsQueueRet = vkbDevice.get_queue(vkb::QueueType::graphics);
    if (!graphicsQueueRet) {
        SDL_Log("Failed to get graphics queue");
        return false;
    }
    graphicsQueue = graphicsQueueRet.value();

    auto presentQueueRet = vkbDevice.get_queue(vkb::QueueType::present);
    if (!presentQueueRet) {
        SDL_Log("Failed to get present queue");
        return false;
    }
    presentQueue = presentQueueRet.value();

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;

    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
        SDL_Log("Failed to create VMA allocator");
        return false;
    }

    if (!createSwapchain()) return false;
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

    // Initialize terrain system (LEB/CBT adaptive terrain)
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
    terrainConfig.size = 500.0f;
    terrainConfig.heightScale = 50.0f;
    terrainConfig.maxDepth = 18;  // Reasonable depth for testing
    terrainConfig.minDepth = 2;
    terrainConfig.targetEdgePixels = 16.0f;
    terrainConfig.splitThreshold = 24.0f;
    terrainConfig.mergeThreshold = 8.0f;

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

    // Create sky descriptor sets now that uniform buffers and LUTs are ready
    if (!skySystem.createDescriptorSets(uniformBuffers, sizeof(UniformBufferObject), atmosphereLUTSystem)) return false;

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

uint32_t Renderer::getGraphicsQueueFamily() const {
    return vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
}

void Renderer::shutdown() {
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
        windSystem.destroy(device, allocator);
        weatherSystem.destroy(device, allocator);
        snowMaskSystem.destroy(device, allocator);
        volumetricSnowSystem.destroy(device, allocator);
        leafSystem.destroy(device, allocator);
        froxelSystem.destroy(device, allocator);
        atmosphereLUTSystem.destroy(device, allocator);
        skySystem.destroy(device, allocator);
        postProcessSystem.destroy(device, allocator);
        bloomSystem.destroy(device, allocator);

        vkDestroyPipeline(device, graphicsPipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        // Shadow system cleanup
        shadowSystem.destroy();

        vkDestroyCommandPool(device, commandPool, nullptr);

        destroySwapchain();

        vmaDestroyAllocator(allocator);
        vkb::destroy_device(vkbDevice);
    }

    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }

    vkb::destroy_instance(vkbInstance);
}

bool Renderer::createSwapchain() {
    int w, h;
    SDL_GetWindowSize(window, &w, &h);

    vkb::SwapchainBuilder swapchainBuilder{vkbDevice};
    auto swapRet = swapchainBuilder
        .set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(w, h)
        .build();

    if (!swapRet) {
        SDL_Log("Failed to create swapchain: %s", swapRet.error().message().c_str());
        return false;
    }

    vkb::Swapchain vkbSwapchain = swapRet.value();
    swapchain = vkbSwapchain.swapchain;
    swapchainImages = vkbSwapchain.get_images().value();
    swapchainImageViews = vkbSwapchain.get_image_views().value();
    swapchainImageFormat = vkbSwapchain.image_format;
    swapchainExtent = vkbSwapchain.extent;

    return true;
}

void Renderer::destroySwapchain() {
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

    vkDestroyRenderPass(device, renderPass, nullptr);
    renderPass = VK_NULL_HANDLE;

    for (auto imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    swapchainImageViews.clear();

    vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

bool Renderer::createRenderPass() {
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
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

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
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
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

    return true;
}

bool Renderer::createFramebuffers() {
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
    auto queueFamilyIndex = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

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
        .build();

    if (descriptorSetLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create descriptor set layout");
        return false;
    }

    return true;
}

bool Renderer::createGraphicsPipeline() {
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
    colorBlendAttachment.blendEnable = VK_FALSE;

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
    // Create the new auto-growing descriptor pool
    // Initial capacity of 64 sets per pool, will automatically grow if exhausted
    descriptorManagerPool.emplace(device, 64);

    // Legacy pool for systems not yet migrated to DescriptorManager
    // This pool is still needed for: GrassSystem, WeatherSystem, LeafSystem, etc.
    std::array<VkDescriptorPoolSize, 4> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 18);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 35);
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 32);
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[3].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 16);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 29);

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        SDL_Log("Failed to create legacy descriptor pool");
        return false;
    }

    return true;
}

bool Renderer::createDescriptorSets() {
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

    // Helper lambda to write common bindings shared across all material sets
    auto writeCommonBindings = [this](DescriptorManager::SetWriter& writer, size_t frameIndex) {
        writer
            .writeBuffer(0, uniformBuffers[frameIndex], 0, sizeof(UniformBufferObject))
            .writeImage(2, shadowImageView, shadowSampler)
            .writeBuffer(4, lightBuffers[frameIndex], 0, sizeof(LightBuffer),
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeImage(5, sceneManager.getSceneBuilder().getDefaultEmissiveMap().getImageView(),
                       sceneManager.getSceneBuilder().getDefaultEmissiveMap().getSampler())
            .writeImage(6, pointShadowArrayViews[frameIndex], pointShadowSampler)
            .writeImage(7, spotShadowArrayViews[frameIndex], spotShadowSampler)
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
    vkDeviceWaitIdle(device);
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
    ubo.shadowMapSize = static_cast<float>(SHADOW_MAP_SIZE);
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

    shadowSystem.recordShadowPass(cmd, frameIndex, descriptorSets[frameIndex],
                                   sceneManager.getSceneObjects(),
                                   terrainCallback, grassCallback);
}

void Renderer::recordSceneObjects(VkCommandBuffer cmd, uint32_t frameIndex) {
    for (const auto& obj : sceneManager.getSceneObjects()) {
        PushConstants push{};
        push.model = obj.transform;
        push.roughness = obj.roughness;
        push.metallic = obj.metallic;
        push.emissiveIntensity = obj.emissiveIntensity;
        push.emissiveColor = glm::vec4(obj.emissiveColor, 1.0f);  // alpha=1 uses emissive color

        vkCmdPushConstants(cmd, pipelineLayout,
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(PushConstants), &push);

        // Select descriptor set based on texture
        VkDescriptorSet* descSet;
        if (obj.texture == &sceneManager.getSceneBuilder().getGroundTexture()) {
            descSet = &groundDescriptorSets[frameIndex];
        } else if (obj.texture == &sceneManager.getSceneBuilder().getMetalTexture()) {
            descSet = &metalDescriptorSets[frameIndex];
        } else {
            descSet = &descriptorSets[frameIndex];
        }
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout, 0, 1, descSet, 0, nullptr);

        VkBuffer vertexBuffers[] = {obj.mesh->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, obj.mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmd, obj.mesh->getIndexCount(), 1, 0, 0, 0);
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

    // Draw scene objects
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    recordSceneObjects(cmd, frameIndex);

    // Draw grass
    grassSystem.recordDraw(cmd, frameIndex, grassTime);

    // Draw falling leaves - after grass, before weather
    leafSystem.recordDraw(cmd, frameIndex, grassTime);

    // Draw weather particles (rain/snow) - after opaque geometry
    weatherSystem.recordDraw(cmd, frameIndex, grassTime);

    vkCmdEndRenderPass(cmd);
}
