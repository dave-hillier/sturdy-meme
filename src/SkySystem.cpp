#include "SkySystem.h"
#include "AtmosphereLUTSystem.h"
#include "GraphicsPipelineFactory.h"
#include "BindingBuilder.h"
#include "UBOs.h"
#include <SDL3/SDL.h>
#include <array>

bool SkySystem::init(const InitInfo& info) {
    device = info.device;
    descriptorPool = info.descriptorPool;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    extent = info.extent;
    hdrRenderPass = info.hdrRenderPass;

    if (!createDescriptorSetLayout()) return false;
    if (!createPipeline()) return false;

    return true;
}

bool SkySystem::init(const InitContext& ctx, VkRenderPass hdrPass) {
    device = ctx.device;
    descriptorPool = ctx.descriptorPool;
    shaderPath = ctx.shaderPath;
    framesInFlight = ctx.framesInFlight;
    extent = ctx.extent;
    hdrRenderPass = hdrPass;

    if (!createDescriptorSetLayout()) return false;
    if (!createPipeline()) return false;

    return true;
}

void SkySystem::destroy(VkDevice device, VmaAllocator allocator) {
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
    // Descriptor sets are freed when the pool is destroyed
    descriptorSets.clear();
}

bool SkySystem::createDescriptorSetLayout() {
    // Sky shader bindings:
    // 0: UBO (same as main shader)
    // 1: Transmittance LUT sampler
    // 2: Multi-scatter LUT sampler
    // 3: Sky-view LUT sampler (updated per-frame)
    // 4: Rayleigh Irradiance LUT sampler (Phase 4.1.9)
    // 5: Mie Irradiance LUT sampler (Phase 4.1.9)
    // 6: Cloud Map LUT sampler (Paraboloid projection, updated per-frame)

    auto uboBinding = BindingBuilder()
        .setBinding(0)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .setStageFlags(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    auto transmittanceLUTBinding = BindingBuilder()
        .setBinding(1)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    auto multiScatterLUTBinding = BindingBuilder()
        .setBinding(2)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    auto skyViewLUTBinding = BindingBuilder()
        .setBinding(3)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    auto rayleighIrradianceLUTBinding = BindingBuilder()
        .setBinding(4)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    auto mieIrradianceLUTBinding = BindingBuilder()
        .setBinding(5)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    auto cloudMapLUTBinding = BindingBuilder()
        .setBinding(6)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    std::array<VkDescriptorSetLayoutBinding, 7> bindings = {
        uboBinding, transmittanceLUTBinding, multiScatterLUTBinding, skyViewLUTBinding,
        rayleighIrradianceLUTBinding, mieIrradianceLUTBinding, cloudMapLUTBinding
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create sky descriptor set layout");
        return false;
    }

    // Create pipeline layout for sky shader
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create sky pipeline layout");
        return false;
    }

    return true;
}

bool SkySystem::createDescriptorSets(const std::vector<VkBuffer>& uniformBuffers,
                                      VkDeviceSize uniformBufferSize,
                                      AtmosphereLUTSystem& atmosphereLUTSystem) {
    // Allocate sky descriptor sets using managed pool
    descriptorSets = descriptorPool->allocate(descriptorSetLayout, framesInFlight);
    if (descriptorSets.size() != framesInFlight) {
        SDL_Log("Failed to allocate sky descriptor sets");
        return false;
    }

    // Get LUT views and sampler from atmosphere system
    VkImageView transmittanceLUTView = atmosphereLUTSystem.getTransmittanceLUTView();
    VkImageView multiScatterLUTView = atmosphereLUTSystem.getMultiScatterLUTView();
    VkImageView skyViewLUTView = atmosphereLUTSystem.getSkyViewLUTView();
    VkImageView rayleighIrradianceLUTView = atmosphereLUTSystem.getRayleighIrradianceLUTView();
    VkImageView mieIrradianceLUTView = atmosphereLUTSystem.getMieIrradianceLUTView();
    VkImageView cloudMapLUTView = atmosphereLUTSystem.getCloudMapLUTView();
    VkSampler lutSampler = atmosphereLUTSystem.getLUTSampler();

    // Update each descriptor set
    for (size_t i = 0; i < framesInFlight; i++) {
        // UBO binding
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = uniformBufferSize;

        // Transmittance LUT binding
        VkDescriptorImageInfo transmittanceInfo{};
        transmittanceInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        transmittanceInfo.imageView = transmittanceLUTView;
        transmittanceInfo.sampler = lutSampler;

        // Multi-scatter LUT binding
        VkDescriptorImageInfo multiScatterInfo{};
        multiScatterInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        multiScatterInfo.imageView = multiScatterLUTView;
        multiScatterInfo.sampler = lutSampler;

        // Sky-view LUT binding (updated per-frame with sun direction)
        VkDescriptorImageInfo skyViewInfo{};
        skyViewInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        skyViewInfo.imageView = skyViewLUTView;
        skyViewInfo.sampler = lutSampler;

        // Rayleigh Irradiance LUT binding (Phase 4.1.9)
        VkDescriptorImageInfo rayleighIrradianceInfo{};
        rayleighIrradianceInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        rayleighIrradianceInfo.imageView = rayleighIrradianceLUTView;
        rayleighIrradianceInfo.sampler = lutSampler;

        // Mie Irradiance LUT binding (Phase 4.1.9)
        VkDescriptorImageInfo mieIrradianceInfo{};
        mieIrradianceInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        mieIrradianceInfo.imageView = mieIrradianceLUTView;
        mieIrradianceInfo.sampler = lutSampler;

        // Cloud Map LUT binding (Paraboloid projection)
        VkDescriptorImageInfo cloudMapInfo{};
        cloudMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cloudMapInfo.imageView = cloudMapLUTView;
        cloudMapInfo.sampler = lutSampler;

        std::array<VkWriteDescriptorSet, 7> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &transmittanceInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = descriptorSets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &multiScatterInfo;

        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = descriptorSets[i];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pImageInfo = &skyViewInfo;

        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = descriptorSets[i];
        descriptorWrites[4].dstBinding = 4;
        descriptorWrites[4].dstArrayElement = 0;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].pImageInfo = &rayleighIrradianceInfo;

        descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[5].dstSet = descriptorSets[i];
        descriptorWrites[5].dstBinding = 5;
        descriptorWrites[5].dstArrayElement = 0;
        descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[5].descriptorCount = 1;
        descriptorWrites[5].pImageInfo = &mieIrradianceInfo;

        descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[6].dstSet = descriptorSets[i];
        descriptorWrites[6].dstBinding = 6;
        descriptorWrites[6].dstArrayElement = 0;
        descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[6].descriptorCount = 1;
        descriptorWrites[6].pImageInfo = &cloudMapInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }

    SDL_Log("Sky descriptor sets created with atmosphere LUTs (including cloud map)");
    return true;
}

bool SkySystem::createPipeline() {
    GraphicsPipelineFactory factory(device);

    bool success = factory
        .applyPreset(GraphicsPipelineFactory::Preset::FullscreenQuad)
        .setShaders(shaderPath + "/sky.vert.spv", shaderPath + "/sky.frag.spv")
        .setRenderPass(hdrRenderPass)
        .setPipelineLayout(pipelineLayout)
        .setExtent(extent)
        .build(pipeline);

    if (!success) {
        SDL_Log("Failed to create sky pipeline");
        return false;
    }

    return true;
}

void SkySystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout, 0, 1, &descriptorSets[frameIndex], 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}
