#include "AtmosphereLUTSystem.h"
#include "core/pipeline/ComputePipelineBuilder.h"
#include <SDL3/SDL_log.h>

bool AtmosphereLUTSystem::createComputePipelines() {
    ComputePipelineBuilder builder(device);

    // Create transmittance pipeline
    if (!builder.setShader(shaderPath + "/transmittance_lut.comp.spv")
                .setPipelineLayout(transmittancePipelineLayout)
                .build(transmittancePipeline)) {
        SDL_Log("Failed to create transmittance pipeline");
        return false;
    }

    // Create multi-scatter pipeline
    if (!builder.reset()
                .setShader(shaderPath + "/multiscatter_lut.comp.spv")
                .setPipelineLayout(multiScatterPipelineLayout)
                .build(multiScatterPipeline)) {
        SDL_Log("Failed to create multi-scatter pipeline");
        return false;
    }

    // Create sky-view pipeline
    if (!builder.reset()
                .setShader(shaderPath + "/skyview_lut.comp.spv")
                .setPipelineLayout(skyViewPipelineLayout)
                .build(skyViewPipeline)) {
        SDL_Log("Failed to create sky-view pipeline");
        return false;
    }

    // Create irradiance pipeline
    if (!builder.reset()
                .setShader(shaderPath + "/irradiance_lut.comp.spv")
                .setPipelineLayout(irradiancePipelineLayout)
                .build(irradiancePipeline)) {
        SDL_Log("Failed to create irradiance pipeline");
        return false;
    }

    // Create cloud map pipeline
    if (!builder.reset()
                .setShader(shaderPath + "/cloudmap_lut.comp.spv")
                .setPipelineLayout(cloudMapPipelineLayout)
                .build(cloudMapPipeline)) {
        SDL_Log("Failed to create cloud map pipeline");
        return false;
    }

    return true;
}
