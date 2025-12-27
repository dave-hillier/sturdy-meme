#include "AtmosphereLUTSystem.h"
#include "VulkanBarriers.h"
#include <SDL3/SDL_log.h>
#include <cstring>

void AtmosphereLUTSystem::computeTransmittanceLUT(VkCommandBuffer cmd) {
    // Update uniform buffer with atmosphere params
    AtmosphereUniforms uniforms{};
    uniforms.params = atmosphereParams;
    memcpy(staticUniformBuffers.mappedPointers[0], &uniforms, sizeof(AtmosphereUniforms));

    // Transition to GENERAL layout for compute write
    Barriers::prepareImageForCompute(cmd, transmittanceLUT);

    // Bind pipeline and dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, transmittancePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, transmittancePipelineLayout,
                           0, 1, &transmittanceDescriptorSet, 0, nullptr);

    uint32_t groupCountX = (TRANSMITTANCE_WIDTH + 15) / 16;
    uint32_t groupCountY = (TRANSMITTANCE_HEIGHT + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition to SHADER_READ for sampling in later stages
    Barriers::imageComputeToSampling(cmd, transmittanceLUT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    SDL_Log("Computed transmittance LUT (%dx%d)", TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT);
}

void AtmosphereLUTSystem::computeMultiScatterLUT(VkCommandBuffer cmd) {
    // Update uniform buffer with atmosphere params
    AtmosphereUniforms uniforms{};
    uniforms.params = atmosphereParams;
    memcpy(staticUniformBuffers.mappedPointers[0], &uniforms, sizeof(AtmosphereUniforms));

    // Transition to GENERAL layout for compute write
    Barriers::prepareImageForCompute(cmd, multiScatterLUT);

    // Bind pipeline and dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, multiScatterPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, multiScatterPipelineLayout,
                           0, 1, &multiScatterDescriptorSet, 0, nullptr);

    uint32_t groupCountX = (MULTISCATTER_SIZE + 7) / 8;
    uint32_t groupCountY = (MULTISCATTER_SIZE + 7) / 8;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition to SHADER_READ for sampling
    Barriers::imageComputeToSampling(cmd, multiScatterLUT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    SDL_Log("Computed multi-scatter LUT (%dx%d)", MULTISCATTER_SIZE, MULTISCATTER_SIZE);
}

void AtmosphereLUTSystem::computeIrradianceLUT(VkCommandBuffer cmd) {
    // Update uniform buffer with atmosphere params
    AtmosphereUniforms uniforms{};
    uniforms.params = atmosphereParams;
    memcpy(staticUniformBuffers.mappedPointers[0], &uniforms, sizeof(AtmosphereUniforms));

    barrierIrradianceLUTsForCompute(cmd);

    // Bind pipeline and dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, irradiancePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, irradiancePipelineLayout,
                           0, 1, &irradianceDescriptorSet, 0, nullptr);

    uint32_t groupCountX = (IRRADIANCE_WIDTH + 7) / 8;
    uint32_t groupCountY = (IRRADIANCE_HEIGHT + 7) / 8;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    barrierIrradianceLUTsForSampling(cmd);

    SDL_Log("Computed irradiance LUTs (%dx%d)", IRRADIANCE_WIDTH, IRRADIANCE_HEIGHT);
}

void AtmosphereLUTSystem::computeSkyViewLUT(VkCommandBuffer cmd, const glm::vec3& sunDir,
                                            const glm::vec3& cameraPos, float cameraAltitude) {
    // Update uniform buffer (use frame 0's per-frame buffer for startup computation)
    AtmosphereUniforms uniforms{};
    uniforms.params = atmosphereParams;
    uniforms.sunDirection = glm::vec4(sunDir, 0.0f);
    uniforms.cameraPosition = glm::vec4(cameraPos, cameraAltitude);
    memcpy(skyViewUniformBuffers.mappedPointers[0], &uniforms, sizeof(AtmosphereUniforms));

    // Transition to GENERAL layout for compute write (from UNDEFINED at startup)
    Barriers::prepareImageForCompute(cmd, skyViewLUT);

    // Bind pipeline and dispatch (use frame 0's descriptor set for startup computation)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skyViewPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skyViewPipelineLayout,
                           0, 1, &skyViewDescriptorSets[0], 0, nullptr);

    uint32_t groupCountX = (SKYVIEW_WIDTH + 15) / 16;
    uint32_t groupCountY = (SKYVIEW_HEIGHT + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition to SHADER_READ for sampling
    Barriers::imageComputeToSampling(cmd, skyViewLUT);

    SDL_Log("Computed sky-view LUT (%dx%d)", SKYVIEW_WIDTH, SKYVIEW_HEIGHT);
}

void AtmosphereLUTSystem::updateSkyViewLUT(VkCommandBuffer cmd, uint32_t frameIndex,
                                           const glm::vec3& sunDir,
                                           const glm::vec3& cameraPos, float cameraAltitude) {
    // Check if update is needed based on input changes
    bool sunDirChanged = glm::dot(sunDir, lastSkyViewSunDir) < (1.0f - SUN_DIR_THRESHOLD);
    bool cameraPosChanged = glm::length(cameraPos - lastSkyViewCameraPos) > CAMERA_POS_THRESHOLD;
    bool altitudeChanged = std::abs(cameraAltitude - lastSkyViewCameraAltitude) > ALTITUDE_THRESHOLD;

    if (!skyViewNeedsUpdate && !sunDirChanged && !cameraPosChanged && !altitudeChanged) {
        // No significant change, skip LUT update
        return;
    }

    // Store current values for next frame comparison
    lastSkyViewSunDir = sunDir;
    lastSkyViewCameraPos = cameraPos;
    lastSkyViewCameraAltitude = cameraAltitude;
    skyViewNeedsUpdate = false;

    // Update per-frame uniform buffer with new sun direction (double-buffered)
    AtmosphereUniforms uniforms{};
    uniforms.params = atmosphereParams;
    uniforms.sunDirection = glm::vec4(sunDir, 0.0f);
    uniforms.cameraPosition = glm::vec4(cameraPos, cameraAltitude);
    memcpy(skyViewUniformBuffers.mappedPointers[frameIndex], &uniforms, sizeof(AtmosphereUniforms));

    // Transition from SHADER_READ_ONLY to GENERAL for compute write
    Barriers::transitionImage(cmd, skyViewLUT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);

    // Bind pipeline and per-frame descriptor set (double-buffered)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skyViewPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skyViewPipelineLayout,
                           0, 1, &skyViewDescriptorSets[frameIndex], 0, nullptr);

    uint32_t groupCountX = (SKYVIEW_WIDTH + 15) / 16;
    uint32_t groupCountY = (SKYVIEW_HEIGHT + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition back to SHADER_READ for sampling in sky.frag
    Barriers::imageComputeToSampling(cmd, skyViewLUT);
}

void AtmosphereLUTSystem::computeCloudMapLUT(VkCommandBuffer cmd, const glm::vec3& windOffset, float time) {
    // Update cloud map uniform buffer (use frame 0's per-frame buffer for startup computation)
    CloudMapUniforms uniforms{};
    uniforms.windOffset = glm::vec4(windOffset, time);
    uniforms.coverage = 0.6f;      // 60% cloud coverage
    uniforms.density = 1.0f;       // Full density multiplier
    uniforms.sharpness = 0.3f;     // Coverage transition sharpness
    uniforms.detailScale = 2.5f;   // Detail noise scale
    memcpy(cloudMapUniformBuffers.mappedPointers[0], &uniforms, sizeof(CloudMapUniforms));

    // Transition to GENERAL layout for compute write
    Barriers::prepareImageForCompute(cmd, cloudMapLUT);

    // Bind pipeline and dispatch (use frame 0's descriptor set for startup computation)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cloudMapPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cloudMapPipelineLayout,
                           0, 1, &cloudMapDescriptorSets[0], 0, nullptr);

    uint32_t groupCountX = (CLOUDMAP_SIZE + 15) / 16;
    uint32_t groupCountY = (CLOUDMAP_SIZE + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition to SHADER_READ for sampling in sky.frag
    Barriers::imageComputeToSampling(cmd, cloudMapLUT);

    SDL_Log("Computed cloud map LUT (%dx%d)", CLOUDMAP_SIZE, CLOUDMAP_SIZE);
}

void AtmosphereLUTSystem::updateCloudMapLUT(VkCommandBuffer cmd, uint32_t frameIndex,
                                            const glm::vec3& windOffset, float time) {
    // Check if update is needed based on input changes
    bool windChanged = glm::length(windOffset - lastCloudWindOffset) > WIND_OFFSET_THRESHOLD;
    bool timeChanged = std::abs(time - lastCloudTime) > WIND_OFFSET_THRESHOLD;
    bool coverageChanged = std::abs(cloudCoverage - lastCloudCoverage) > CLOUD_PARAM_THRESHOLD;
    bool densityChanged = std::abs(cloudDensity - lastCloudDensity) > CLOUD_PARAM_THRESHOLD;

    if (!cloudMapNeedsUpdate && !windChanged && !timeChanged && !coverageChanged && !densityChanged) {
        // No significant change, skip LUT update
        return;
    }

    // Store current values for next frame comparison
    lastCloudWindOffset = windOffset;
    lastCloudTime = time;
    lastCloudCoverage = cloudCoverage;
    lastCloudDensity = cloudDensity;
    cloudMapNeedsUpdate = false;

    // Update per-frame cloud map uniform buffer (double-buffered)
    CloudMapUniforms uniforms{};
    uniforms.windOffset = glm::vec4(windOffset, time);
    uniforms.coverage = cloudCoverage;    // From UI controls
    uniforms.density = cloudDensity;      // From UI controls
    uniforms.sharpness = 0.3f;            // Coverage transition sharpness
    uniforms.detailScale = 2.5f;          // Detail noise scale
    memcpy(cloudMapUniformBuffers.mappedPointers[frameIndex], &uniforms, sizeof(CloudMapUniforms));

    // Transition from SHADER_READ_ONLY to GENERAL for compute write
    Barriers::transitionImage(cmd, cloudMapLUT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);

    // Bind pipeline and per-frame descriptor set (double-buffered)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cloudMapPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cloudMapPipelineLayout,
                           0, 1, &cloudMapDescriptorSets[frameIndex], 0, nullptr);

    uint32_t groupCountX = (CLOUDMAP_SIZE + 15) / 16;
    uint32_t groupCountY = (CLOUDMAP_SIZE + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition back to SHADER_READ for sampling in sky.frag
    Barriers::imageComputeToSampling(cmd, cloudMapLUT);
}

void AtmosphereLUTSystem::recomputeStaticLUTs(VkCommandBuffer cmd) {
    if (!paramsDirty) return;

    // Update uniform buffer with new atmosphere parameters
    AtmosphereUniforms uniforms{};
    uniforms.params = atmosphereParams;
    memcpy(staticUniformBuffers.mappedPointers[0], &uniforms, sizeof(AtmosphereUniforms));

    // Recompute the static LUTs that depend on atmosphere parameters
    computeTransmittanceLUT(cmd);
    computeMultiScatterLUT(cmd);
    computeIrradianceLUT(cmd);

    paramsDirty = false;
    SDL_Log("Atmosphere LUTs recomputed with new parameters");
}

void AtmosphereLUTSystem::barrierIrradianceLUTsForCompute(VkCommandBuffer cmd) {
    Barriers::BarrierBatch batch(cmd);
    batch.imageTransition(rayleighIrradianceLUT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        0, VK_ACCESS_SHADER_WRITE_BIT);
    batch.imageTransition(mieIrradianceLUT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        0, VK_ACCESS_SHADER_WRITE_BIT);
    batch.setStages(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void AtmosphereLUTSystem::barrierIrradianceLUTsForSampling(VkCommandBuffer cmd) {
    Barriers::BarrierBatch batch(cmd);
    batch.imageTransition(rayleighIrradianceLUT,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    batch.imageTransition(mieIrradianceLUT,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    batch.setStages(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}
