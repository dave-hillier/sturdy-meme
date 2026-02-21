#include "LeafCullPhase3Stage.h"
#include "Bindings.h"
#include "UBOs.h"
#include "core/pipeline/ComputePipelineBuilder.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include <SDL3/SDL_log.h>

bool LeafCullPhase3Stage::createPipeline(const vk::raii::Device& raiiDevice, VkDevice device,
                                          const std::string& resourcePath) {
    DescriptorManager::LayoutBuilder builder(device);
    builder.addBinding(Bindings::LEAF_CULL_P3_VISIBLE_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_ALL_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_INPUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_OUTPUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_INDIRECT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_CULLING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);

    VkDescriptorSetLayout rawLayout = builder.build();
    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LeafCullPhase3Stage: Failed to create descriptor set layout");
        return false;
    }
    descriptorSetLayout.emplace(raiiDevice, rawLayout);

    auto layoutOpt = PipelineLayoutBuilder(raiiDevice)
        .addDescriptorSetLayout(**descriptorSetLayout)
        .build();
    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LeafCullPhase3Stage: Failed to create pipeline layout");
        return false;
    }
    pipelineLayout = std::move(layoutOpt);

    if (!ComputePipelineBuilder(raiiDevice)
            .setShader(resourcePath + "/shaders/tree_leaf_cull_phase3.comp.spv")
            .setPipelineLayout(**pipelineLayout)
            .buildInto(pipeline)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LeafCullPhase3Stage: Failed to create pipeline");
        return false;
    }

    SDL_Log("LeafCullPhase3Stage: Created leaf cull phase 3 compute pipeline");
    return true;
}

bool LeafCullPhase3Stage::createDescriptorSets(VkDevice device, VmaAllocator allocator,
                                                DescriptorManager::Pool* descriptorPool,
                                                uint32_t maxFramesInFlight) {
    if (!descriptorSetLayout) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LeafCullPhase3Stage: Descriptor set layout is null");
        return false;
    }

    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(maxFramesInFlight)
            .setSize(sizeof(CullingUniforms))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(uniformBuffers)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LeafCullPhase3Stage: Failed to create uniform buffers");
        return false;
    }

    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(maxFramesInFlight)
            .setSize(sizeof(LeafCullP3Params))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(paramsBuffers)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LeafCullPhase3Stage: Failed to create params buffers");
        return false;
    }

    descriptorSets = descriptorPool->allocate(**descriptorSetLayout, maxFramesInFlight);
    if (descriptorSets.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LeafCullPhase3Stage: Failed to allocate descriptor sets");
        return false;
    }

    SDL_Log("LeafCullPhase3Stage: Allocated %u descriptor sets", maxFramesInFlight);
    return true;
}

void LeafCullPhase3Stage::destroy(VmaAllocator allocator) {
    BufferUtils::destroyBuffers(allocator, uniformBuffers);
    BufferUtils::destroyBuffers(allocator, paramsBuffers);
}
