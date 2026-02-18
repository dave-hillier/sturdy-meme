#include "FrameDataBuilder.h"
#include "RendererSystems.h"
#include "Camera.h"
#include "UBOs.h"

// Subsystem includes for data access
#include "TimeSystem.h"
#include "GlobalBufferManager.h"
#include "PostProcessSystem.h"
#include "ShadowSystem.h"
#include "BloomSystem.h"
#include "TerrainSystem.h"
#include "WindSystem.h"
#include "WeatherSystem.h"
#include "EnvironmentSettings.h"
#include "controls/PlayerControlSubsystem.h"

#include <vulkan/vulkan_raii.hpp>
#include <algorithm>

FrameData FrameDataBuilder::buildFrameData(
    const Camera& camera,
    const RendererSystems& systems,
    uint32_t frameIndex,
    float deltaTime,
    float time)
{
    FrameData frame;

    frame.frameIndex = frameIndex;
    frame.deltaTime = deltaTime;
    frame.time = time;
    frame.timeOfDay = systems.time().getTimeOfDay();

    frame.cameraPosition = camera.getPosition();
    frame.view = camera.getViewMatrix();
    frame.projection = camera.getProjectionMatrix();
    frame.viewProj = frame.projection * frame.view;
    frame.nearPlane = camera.getNearPlane();
    frame.farPlane = camera.getFarPlane();

    // Extract frustum planes from view-projection matrix (normalized)
    glm::mat4 m = glm::transpose(frame.viewProj);
    frame.frustumPlanes[0] = m[3] + m[0];  // Left
    frame.frustumPlanes[1] = m[3] - m[0];  // Right
    frame.frustumPlanes[2] = m[3] + m[1];  // Bottom
    frame.frustumPlanes[3] = m[3] - m[1];  // Top
    frame.frustumPlanes[4] = m[3] + m[2];  // Near
    frame.frustumPlanes[5] = m[3] - m[2];  // Far
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(frame.frustumPlanes[i]));
        if (len > 0.0f) {
            frame.frustumPlanes[i] /= len;
        }
    }

    // Get sun direction from last computed UBO (already computed in updateUniformBuffer)
    UniformBufferObject* ubo = static_cast<UniformBufferObject*>(
        systems.globalBuffers().uniformBuffers.mappedPointers[frameIndex]);
    frame.sunDirection = glm::normalize(glm::vec3(ubo->toSunDirection));
    frame.sunIntensity = ubo->toSunDirection.w;

    // Get player state from PlayerControlSubsystem
    const auto& playerControl = systems.playerControl();
    frame.playerPosition = playerControl.getPlayerPosition();
    frame.playerVelocity = playerControl.getPlayerVelocity();
    frame.playerCapsuleRadius = playerControl.getPlayerCapsuleRadius();

    const auto& terrainConfig = systems.terrain().getConfig();
    frame.terrainSize = terrainConfig.size;
    frame.heightScale = terrainConfig.heightScale;

    // Populate wind/weather/snow data
    const auto& envSettings = systems.wind().getEnvironmentSettings();
    frame.windDirection = envSettings.windDirection;
    frame.windStrength = envSettings.windStrength;
    frame.windSpeed = envSettings.windSpeed;
    frame.gustFrequency = envSettings.gustFrequency;
    frame.gustAmplitude = envSettings.gustAmplitude;

    frame.weatherType = systems.weather().getWeatherType();
    frame.weatherIntensity = systems.weather().getIntensity();

    frame.snowAmount = systems.environmentSettings().snowAmount;
    frame.snowColor = systems.environmentSettings().snowColor;

    // Lighting data
    frame.sunColor = glm::vec3(ubo->sunColor);
    frame.moonDirection = glm::normalize(glm::vec3(ubo->moonDirection));
    frame.moonIntensity = ubo->moonDirection.w;

    return frame;
}

RenderResources FrameDataBuilder::buildRenderResources(
    const RendererSystems& systems,
    uint32_t swapchainImageIndex,
    const std::vector<vk::raii::Framebuffer>& framebuffers,
    VkRenderPass swapchainRenderPass,
    VkExtent2D swapchainExtent,
    VkPipeline graphicsPipeline,
    VkPipelineLayout pipelineLayout,
    VkDescriptorSetLayout descriptorSetLayout)
{
    RenderResources resources;

    // HDR target (from PostProcessSystem)
    resources.hdrRenderPass = systems.postProcess().getHDRRenderPass();
    resources.hdrFramebuffer = systems.postProcess().getHDRFramebuffer();
    resources.hdrExtent = systems.postProcess().getExtent();
    resources.hdrColorView = systems.postProcess().getHDRColorView();
    resources.hdrColorImage = systems.postProcess().getHDRColorImage();
    resources.hdrDepthView = systems.postProcess().getHDRDepthView();
    resources.hdrDepthImage = systems.postProcess().getHDRDepthImage();

    // Shadow resources (from ShadowSystem)
    resources.shadowRenderPass = systems.shadow().getShadowRenderPass();
    resources.shadowMapView = systems.shadow().getShadowImageView();
    resources.shadowSampler = systems.shadow().getShadowSampler();
    resources.shadowPipeline = systems.shadow().getShadowPipeline();
    resources.shadowPipelineLayout = systems.shadow().getShadowPipelineLayout();

    // Copy cascade matrices
    const auto& cascadeMatrices = systems.shadow().getCascadeMatrices();
    for (size_t i = 0; i < cascadeMatrices.size(); ++i) {
        resources.cascadeMatrices[i] = cascadeMatrices[i];
    }

    // Copy cascade split depths
    const auto& splitDepths = systems.shadow().getCascadeSplitDepths();
    for (size_t i = 0; i < std::min(splitDepths.size(), size_t(4)); ++i) {
        resources.cascadeSplitDepths[static_cast<int>(i)] = splitDepths[i];
    }

    // Bloom output (from BloomSystem)
    resources.bloomOutput = systems.bloom().getBloomOutput();
    resources.bloomSampler = systems.bloom().getBloomSampler();

    // Swapchain target
    resources.swapchainRenderPass = swapchainRenderPass;
    resources.swapchainFramebuffer = *framebuffers[swapchainImageIndex];
    resources.swapchainExtent = swapchainExtent;

    // Main scene pipeline
    resources.graphicsPipeline = graphicsPipeline;
    resources.pipelineLayout = pipelineLayout;
    resources.descriptorSetLayout = descriptorSetLayout;

    return resources;
}
