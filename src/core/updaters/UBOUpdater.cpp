#include "UBOUpdater.h"
#include "../RendererSystems.h"
#include "UBOBuilder.h"
#include "GlobalBufferManager.h"
#include "TimeSystem.h"
#include "CelestialCalculator.h"  // Also provides DateTime
#include "WaterSystem.h"
#include "ShadowSystem.h"
#include "ScreenSpaceShadowSystem.h"
#include "WeatherSystem.h"
#include "PostProcessSystem.h"
#include "SceneManager.h"
#include "controls/EnvironmentControlSubsystem.h"
#include "Camera.h"
#include "lighting/LightSystem.h"
#include "ecs/World.h"

UBOUpdater::Result UBOUpdater::update(
    RendererSystems& systems,
    uint32_t frameIndex,
    const Camera& camera,
    const Config& config)
{
    Result result;

    // Get current time of day from time system
    float currentTimeOfDay = systems.time().getTimeOfDay();

    // Pure calculations via UBOBuilder
    UBOBuilder::LightingParams lighting = systems.uboBuilder().calculateLightingParams(currentTimeOfDay);
    systems.time().setCurrentMoonPhase(lighting.moonPhase);  // Track current effective phase

    // Calculate and apply tide based on celestial positions
    DateTime dateTime = DateTime::fromTimeOfDay(currentTimeOfDay, systems.time().getCurrentYear(),
                                                 systems.time().getCurrentMonth(), systems.time().getCurrentDay());
    TideInfo tide = systems.celestial().calculateTide(dateTime);
    systems.water().updateTide(tide.height);

    // Update cascade matrices via shadow system
    systems.shadow().updateCascadeMatrices(lighting.sunDir, camera);

    // Update screen-space shadow resolve uniforms
    if (systems.hasScreenSpaceShadow()) {
        const auto& cascadeMatrices = systems.shadow().getCascadeMatrices();
        const auto& cascadeSplitDepths = systems.shadow().getCascadeSplitDepths();
        glm::vec4 splits(cascadeSplitDepths[1], cascadeSplitDepths[2],
                         cascadeSplitDepths[3], cascadeSplitDepths[4]);
        systems.screenSpaceShadow()->updatePerFrame(
            frameIndex,
            camera.getViewMatrix(),
            camera.getProjectionMatrix(),
            cascadeMatrices.data(),
            splits,
            lighting.sunDir,
            static_cast<float>(systems.shadow().getShadowMapSize()));
    }

    // Build UBO data via UBOBuilder (pure calculation)
    // Get cloud parameters from EnvironmentControlSubsystem (authoritative source)
    auto& envControl = systems.environmentControl();
    UBOBuilder::MainUBOConfig mainConfig{};
    mainConfig.showCascadeDebug = config.showCascadeDebug;
    mainConfig.useParaboloidClouds = envControl.isUsingParaboloidClouds();
    mainConfig.cloudCoverage = envControl.getCloudCoverage();
    mainConfig.cloudDensity = envControl.getCloudDensity();
    mainConfig.skyExposure = envControl.getSkyExposure();
    mainConfig.shadowsEnabled = config.shadowsEnabled;
    UniformBufferObject ubo = systems.uboBuilder().buildUniformBufferData(camera, lighting, currentTimeOfDay, mainConfig);

    UBOBuilder::SnowConfig snowConfig{};
    snowConfig.useVolumetricSnow = config.useVolumetricSnow;
    snowConfig.showSnowDepthDebug = config.showSnowDepthDebug;
    snowConfig.maxSnowHeight = config.maxSnowHeight;
    SnowUBO snowUbo = systems.uboBuilder().buildSnowUBOData(snowConfig);

    // Set rain wetness from weather system (composable material system integration)
    // Weather type 0 = rain, type 1 = snow - rain causes wetness on vegetation
    if (systems.weather().getWeatherType() == 0) {
        snowUbo.rainWetness = systems.weather().getIntensity();
    } else {
        snowUbo.rainWetness = 0.0f;
    }

    CloudShadowUBO cloudShadowUbo = systems.uboBuilder().buildCloudShadowUBOData();

    // Update all UBO buffers
    systems.globalBuffers().updateUniformBuffer(frameIndex, ubo);
    systems.globalBuffers().updateSnowBuffer(frameIndex, snowUbo);
    systems.globalBuffers().updateCloudShadowBuffer(frameIndex, cloudShadowUbo);

    // Update light buffer with camera-based culling
    LightBuffer lightBuffer{};
    glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();

    // Build light buffer from ECS with frustum culling
    if (config.ecsWorld) {
        ecs::light::updateFlicker(*config.ecsWorld, config.deltaTime);
        ecs::light::buildLightBuffer(
            *config.ecsWorld, lightBuffer,
            camera.getPosition(), camera.getForward(),
            viewProj, config.lightCullRadius);
    }
    systems.globalBuffers().updateLightBuffer(frameIndex, lightBuffer);

    // Calculate sun screen position (pure) and update post-process (state mutation)
    // Sun screen position calculation (moved from Renderer::calculateSunScreenPos)
    glm::vec4 sunClipPos = camera.getProjectionMatrix() * camera.getViewMatrix() * glm::vec4(lighting.sunDir * 1000.0f, 1.0f);
    glm::vec2 sunScreenPos(0.5f, 0.5f);
    if (sunClipPos.w > 0.0f) {
        glm::vec3 sunNDC = glm::vec3(sunClipPos) / sunClipPos.w;
        sunScreenPos = glm::vec2(sunNDC.x * 0.5f + 0.5f, sunNDC.y * 0.5f + 0.5f);
        sunScreenPos.y = 1.0f - sunScreenPos.y;
    }
    systems.postProcess().setSunScreenPos(sunScreenPos);

    // Update HDR enabled state
    systems.postProcess().setHDREnabled(config.hdrEnabled);

    // Return computed values needed by caller
    result.sunIntensity = lighting.sunIntensity;
    return result;
}
