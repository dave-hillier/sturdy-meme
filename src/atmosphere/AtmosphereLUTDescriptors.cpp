#include "AtmosphereLUTSystem.h"
#include "BindingBuilder.h"
#include <SDL3/SDL_log.h>
#include <array>

bool AtmosphereLUTSystem::createDescriptorSetLayouts() {
    // Transmittance LUT descriptor set layout (just output image and uniform buffer)
    {
        auto outputImage = BindingBuilder()
            .setBinding(0)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto uniformBuffer = BindingBuilder()
            .setBinding(1)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {outputImage, uniformBuffer};

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &transmittanceDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create transmittance descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &transmittanceDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &transmittancePipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create transmittance pipeline layout");
            return false;
        }
    }

    // Multi-scatter LUT descriptor set layout (transmittance input, output image, uniform buffer)
    {
        auto outputImage = BindingBuilder()
            .setBinding(0)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto transmittanceInput = BindingBuilder()
            .setBinding(1)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto uniformBuffer = BindingBuilder()
            .setBinding(2)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        std::array<VkDescriptorSetLayoutBinding, 3> bindings = {outputImage, transmittanceInput, uniformBuffer};

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &multiScatterDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create multi-scatter descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &multiScatterDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &multiScatterPipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create multi-scatter pipeline layout");
            return false;
        }
    }

    // Sky-view LUT descriptor set layout (transmittance + multiscatter inputs, output image, uniform buffer)
    {
        auto outputImage = BindingBuilder()
            .setBinding(0)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto transmittanceInput = BindingBuilder()
            .setBinding(1)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto multiScatterInput = BindingBuilder()
            .setBinding(2)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto uniformBuffer = BindingBuilder()
            .setBinding(3)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        std::array<VkDescriptorSetLayoutBinding, 4> bindings = {outputImage, transmittanceInput, multiScatterInput, uniformBuffer};

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &skyViewDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create sky-view descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &skyViewDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &skyViewPipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create sky-view pipeline layout");
            return false;
        }
    }

    // Irradiance LUT descriptor set layout (Phase 4.1.9)
    // Two output images (Rayleigh and Mie), transmittance input, uniform buffer
    {
        auto rayleighOutput = BindingBuilder()
            .setBinding(0)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto mieOutput = BindingBuilder()
            .setBinding(1)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto transmittanceInput = BindingBuilder()
            .setBinding(2)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto uniformBuffer = BindingBuilder()
            .setBinding(3)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        std::array<VkDescriptorSetLayoutBinding, 4> bindings = {rayleighOutput, mieOutput, transmittanceInput, uniformBuffer};

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &irradianceDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create irradiance descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &irradianceDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &irradiancePipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create irradiance pipeline layout");
            return false;
        }
    }

    // Cloud Map LUT descriptor set layout (output image, uniform buffer)
    {
        auto outputImage = BindingBuilder()
            .setBinding(0)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto uniformBuffer = BindingBuilder()
            .setBinding(1)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {outputImage, uniformBuffer};

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &cloudMapDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create cloud map descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &cloudMapDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &cloudMapPipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create cloud map pipeline layout");
            return false;
        }
    }

    return true;
}

bool AtmosphereLUTSystem::createDescriptorSets() {
    // Allocate transmittance descriptor set using managed pool
    {
        transmittanceDescriptorSet = descriptorPool->allocateSingle(transmittanceDescriptorSetLayout);
        if (transmittanceDescriptorSet == VK_NULL_HANDLE) {
            SDL_Log("Failed to allocate transmittance descriptor set");
            return false;
        }

        std::array<VkWriteDescriptorSet, 2> writes{};

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = transmittanceLUTView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = transmittanceDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &imageInfo;

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(AtmosphereUniforms);

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = transmittanceDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Allocate multi-scatter descriptor set using managed pool
    {
        multiScatterDescriptorSet = descriptorPool->allocateSingle(multiScatterDescriptorSetLayout);
        if (multiScatterDescriptorSet == VK_NULL_HANDLE) {
            SDL_Log("Failed to allocate multi-scatter descriptor set");
            return false;
        }

        std::array<VkWriteDescriptorSet, 3> writes{};

        VkDescriptorImageInfo outputImageInfo{};
        outputImageInfo.imageView = multiScatterLUTView;
        outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = multiScatterDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &outputImageInfo;

        VkDescriptorImageInfo transmittanceImageInfo{};
        transmittanceImageInfo.imageView = transmittanceLUTView;
        transmittanceImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        transmittanceImageInfo.sampler = lutSampler;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = multiScatterDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &transmittanceImageInfo;

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(AtmosphereUniforms);

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = multiScatterDescriptorSet;
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Allocate per-frame sky-view descriptor sets (double-buffered) using managed pool
    {
        skyViewDescriptorSets = descriptorPool->allocate(skyViewDescriptorSetLayout, framesInFlight);
        if (skyViewDescriptorSets.empty()) {
            SDL_Log("Failed to allocate sky-view descriptor sets");
            return false;
        }

        // Update each per-frame descriptor set with its corresponding uniform buffer
        for (uint32_t i = 0; i < framesInFlight; ++i) {
            std::array<VkWriteDescriptorSet, 4> writes{};

            VkDescriptorImageInfo outputImageInfo{};
            outputImageInfo.imageView = skyViewLUTView;
            outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = skyViewDescriptorSets[i];
            writes[0].dstBinding = 0;
            writes[0].dstArrayElement = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &outputImageInfo;

            VkDescriptorImageInfo transmittanceImageInfo{};
            transmittanceImageInfo.imageView = transmittanceLUTView;
            transmittanceImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            transmittanceImageInfo.sampler = lutSampler;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = skyViewDescriptorSets[i];
            writes[1].dstBinding = 1;
            writes[1].dstArrayElement = 0;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &transmittanceImageInfo;

            VkDescriptorImageInfo multiScatterImageInfo{};
            multiScatterImageInfo.imageView = multiScatterLUTView;
            multiScatterImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            multiScatterImageInfo.sampler = lutSampler;

            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = skyViewDescriptorSets[i];
            writes[2].dstBinding = 2;
            writes[2].dstArrayElement = 0;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[2].descriptorCount = 1;
            writes[2].pImageInfo = &multiScatterImageInfo;

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = skyViewUniformBuffers.buffers[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(AtmosphereUniforms);

            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet = skyViewDescriptorSets[i];
            writes[3].dstBinding = 3;
            writes[3].dstArrayElement = 0;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[3].descriptorCount = 1;
            writes[3].pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    // Allocate irradiance descriptor set using managed pool
    {
        irradianceDescriptorSet = descriptorPool->allocateSingle(irradianceDescriptorSetLayout);
        if (irradianceDescriptorSet == VK_NULL_HANDLE) {
            SDL_Log("Failed to allocate irradiance descriptor set");
            return false;
        }

        std::array<VkWriteDescriptorSet, 4> writes{};

        VkDescriptorImageInfo rayleighImageInfo{};
        rayleighImageInfo.imageView = rayleighIrradianceLUTView;
        rayleighImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = irradianceDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &rayleighImageInfo;

        VkDescriptorImageInfo mieImageInfo{};
        mieImageInfo.imageView = mieIrradianceLUTView;
        mieImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = irradianceDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &mieImageInfo;

        VkDescriptorImageInfo transmittanceImageInfo{};
        transmittanceImageInfo.imageView = transmittanceLUTView;
        transmittanceImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        transmittanceImageInfo.sampler = lutSampler;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = irradianceDescriptorSet;
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &transmittanceImageInfo;

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(AtmosphereUniforms);

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = irradianceDescriptorSet;
        writes[3].dstBinding = 3;
        writes[3].dstArrayElement = 0;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Allocate per-frame cloud map descriptor sets (double-buffered) using managed pool
    {
        cloudMapDescriptorSets = descriptorPool->allocate(cloudMapDescriptorSetLayout, framesInFlight);
        if (cloudMapDescriptorSets.empty()) {
            SDL_Log("Failed to allocate cloud map descriptor sets");
            return false;
        }

        // Update each per-frame descriptor set with its corresponding uniform buffer
        for (uint32_t i = 0; i < framesInFlight; ++i) {
            std::array<VkWriteDescriptorSet, 2> writes{};

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageView = cloudMapLUTView;
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = cloudMapDescriptorSets[i];
            writes[0].dstBinding = 0;
            writes[0].dstArrayElement = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &imageInfo;

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = cloudMapUniformBuffers.buffers[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(CloudMapUniforms);

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = cloudMapDescriptorSets[i];
            writes[1].dstBinding = 1;
            writes[1].dstArrayElement = 0;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    return true;
}
