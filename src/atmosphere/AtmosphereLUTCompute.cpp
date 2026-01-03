#include "AtmosphereLUTSystem.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL_log.h>
#include <array>
#include <cstring>

void AtmosphereLUTSystem::computeTransmittanceLUT(VkCommandBuffer cmd) {
    // Update uniform buffer with atmosphere params
    AtmosphereUniforms uniforms{};
    uniforms.params = atmosphereParams;
    memcpy(staticUniformBuffers.mappedPointers[0], &uniforms, sizeof(AtmosphereUniforms));

    vk::CommandBuffer vkCmd(cmd);

    // Transition to GENERAL layout for compute write
    auto prepareBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask({})
        .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setNewLayout(vk::ImageLayout::eGeneral)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(transmittanceLUT)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
                          {}, {}, {}, prepareBarrier);

    // Bind pipeline and dispatch
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, transmittancePipeline);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, transmittancePipelineLayout,
                             0, vk::DescriptorSet(transmittanceDescriptorSet), {});

    uint32_t groupCountX = (TRANSMITTANCE_WIDTH + 15) / 16;
    uint32_t groupCountY = (TRANSMITTANCE_HEIGHT + 15) / 16;
    vkCmd.dispatch(groupCountX, groupCountY, 1);

    // Transition to SHADER_READ for sampling in later stages
    auto samplingBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(transmittanceLUT)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                          {}, {}, {}, samplingBarrier);

    SDL_Log("Computed transmittance LUT (%dx%d)", TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT);
}

void AtmosphereLUTSystem::computeMultiScatterLUT(VkCommandBuffer cmd) {
    // Update uniform buffer with atmosphere params
    AtmosphereUniforms uniforms{};
    uniforms.params = atmosphereParams;
    memcpy(staticUniformBuffers.mappedPointers[0], &uniforms, sizeof(AtmosphereUniforms));

    vk::CommandBuffer vkCmd(cmd);

    // Transition to GENERAL layout for compute write
    auto prepareBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask({})
        .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setNewLayout(vk::ImageLayout::eGeneral)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(multiScatterLUT)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
                          {}, {}, {}, prepareBarrier);

    // Bind pipeline and dispatch
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, multiScatterPipeline);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, multiScatterPipelineLayout,
                             0, vk::DescriptorSet(multiScatterDescriptorSet), {});

    uint32_t groupCountX = (MULTISCATTER_SIZE + 7) / 8;
    uint32_t groupCountY = (MULTISCATTER_SIZE + 7) / 8;
    vkCmd.dispatch(groupCountX, groupCountY, 1);

    // Transition to SHADER_READ for sampling
    auto samplingBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(multiScatterLUT)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                          {}, {}, {}, samplingBarrier);

    SDL_Log("Computed multi-scatter LUT (%dx%d)", MULTISCATTER_SIZE, MULTISCATTER_SIZE);
}

void AtmosphereLUTSystem::computeIrradianceLUT(VkCommandBuffer cmd) {
    // Update uniform buffer with atmosphere params
    AtmosphereUniforms uniforms{};
    uniforms.params = atmosphereParams;
    memcpy(staticUniformBuffers.mappedPointers[0], &uniforms, sizeof(AtmosphereUniforms));

    barrierIrradianceLUTsForCompute(cmd);

    vk::CommandBuffer vkCmd(cmd);

    // Bind pipeline and dispatch
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, irradiancePipeline);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, irradiancePipelineLayout,
                             0, vk::DescriptorSet(irradianceDescriptorSet), {});

    uint32_t groupCountX = (IRRADIANCE_WIDTH + 7) / 8;
    uint32_t groupCountY = (IRRADIANCE_HEIGHT + 7) / 8;
    vkCmd.dispatch(groupCountX, groupCountY, 1);

    barrierIrradianceLUTsForSampling(cmd);

    SDL_Log("Computed irradiance LUTs (%dx%d)", IRRADIANCE_WIDTH, IRRADIANCE_HEIGHT);
}

void AtmosphereLUTSystem::computeSkyViewLUT(VkCommandBuffer cmd, const glm::vec3& sunDir,
                                            const glm::vec3& cameraPos, float cameraAltitude) {
    // Update uniform buffer (use frame 0's per-frame buffer for startup computation)
    AtmosphereUniforms uniforms{};
    uniforms.params = atmosphereParams;
    uniforms.toSunDirection = glm::vec4(sunDir, 0.0f);
    uniforms.cameraPosition = glm::vec4(cameraPos, cameraAltitude);
    memcpy(skyViewUniformBuffers.mappedPointers[0], &uniforms, sizeof(AtmosphereUniforms));

    vk::CommandBuffer vkCmd(cmd);

    // Transition to GENERAL layout for compute write (from UNDEFINED at startup)
    auto prepareBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask({})
        .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setNewLayout(vk::ImageLayout::eGeneral)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(skyViewLUT)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
                          {}, {}, {}, prepareBarrier);

    // Bind pipeline and dispatch (use frame 0's descriptor set for startup computation)
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, skyViewPipeline);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, skyViewPipelineLayout,
                             0, vk::DescriptorSet(skyViewDescriptorSets[0]), {});

    uint32_t groupCountX = (SKYVIEW_WIDTH + 15) / 16;
    uint32_t groupCountY = (SKYVIEW_HEIGHT + 15) / 16;
    vkCmd.dispatch(groupCountX, groupCountY, 1);

    // Transition to SHADER_READ for sampling
    auto samplingBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(skyViewLUT)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
                          {}, {}, {}, samplingBarrier);

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
    uniforms.toSunDirection = glm::vec4(sunDir, 0.0f);
    uniforms.cameraPosition = glm::vec4(cameraPos, cameraAltitude);
    memcpy(skyViewUniformBuffers.mappedPointers[frameIndex], &uniforms, sizeof(AtmosphereUniforms));

    vk::CommandBuffer vkCmd(cmd);

    // Transition from SHADER_READ_ONLY to GENERAL for compute write
    auto prepareBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
        .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setNewLayout(vk::ImageLayout::eGeneral)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(skyViewLUT)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eComputeShader,
                          {}, {}, {}, prepareBarrier);

    // Bind pipeline and per-frame descriptor set (double-buffered)
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, skyViewPipeline);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, skyViewPipelineLayout,
                             0, vk::DescriptorSet(skyViewDescriptorSets[frameIndex]), {});

    uint32_t groupCountX = (SKYVIEW_WIDTH + 15) / 16;
    uint32_t groupCountY = (SKYVIEW_HEIGHT + 15) / 16;
    vkCmd.dispatch(groupCountX, groupCountY, 1);

    // Transition back to SHADER_READ for sampling in sky.frag
    auto samplingBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(skyViewLUT)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
                          {}, {}, {}, samplingBarrier);
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

    vk::CommandBuffer vkCmd(cmd);

    // Transition to GENERAL layout for compute write
    auto prepareBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask({})
        .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setNewLayout(vk::ImageLayout::eGeneral)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(cloudMapLUT)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
                          {}, {}, {}, prepareBarrier);

    // Bind pipeline and dispatch (use frame 0's descriptor set for startup computation)
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, cloudMapPipeline);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, cloudMapPipelineLayout,
                             0, vk::DescriptorSet(cloudMapDescriptorSets[0]), {});

    uint32_t groupCountX = (CLOUDMAP_SIZE + 15) / 16;
    uint32_t groupCountY = (CLOUDMAP_SIZE + 15) / 16;
    vkCmd.dispatch(groupCountX, groupCountY, 1);

    // Transition to SHADER_READ for sampling in sky.frag
    auto samplingBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(cloudMapLUT)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
                          {}, {}, {}, samplingBarrier);

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

    vk::CommandBuffer vkCmd(cmd);

    // Transition from SHADER_READ_ONLY to GENERAL for compute write
    auto prepareBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
        .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setNewLayout(vk::ImageLayout::eGeneral)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(cloudMapLUT)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eComputeShader,
                          {}, {}, {}, prepareBarrier);

    // Bind pipeline and per-frame descriptor set (double-buffered)
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, cloudMapPipeline);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, cloudMapPipelineLayout,
                             0, vk::DescriptorSet(cloudMapDescriptorSets[frameIndex]), {});

    uint32_t groupCountX = (CLOUDMAP_SIZE + 15) / 16;
    uint32_t groupCountY = (CLOUDMAP_SIZE + 15) / 16;
    vkCmd.dispatch(groupCountX, groupCountY, 1);

    // Transition back to SHADER_READ for sampling in sky.frag
    auto samplingBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(cloudMapLUT)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
                          {}, {}, {}, samplingBarrier);
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
    vk::CommandBuffer vkCmd(cmd);
    std::array<vk::ImageMemoryBarrier, 2> barriers = {{
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask({})
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(rayleighIrradianceLUT)
            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}),
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask({})
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(mieIrradianceLUT)
            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1})
    }};
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
                          {}, {}, {}, barriers);
}

void AtmosphereLUTSystem::barrierIrradianceLUTsForSampling(VkCommandBuffer cmd) {
    vk::CommandBuffer vkCmd(cmd);
    std::array<vk::ImageMemoryBarrier, 2> barriers = {{
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(rayleighIrradianceLUT)
            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}),
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(mieIrradianceLUT)
            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1})
    }};
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
                          {}, {}, {}, barriers);
}
