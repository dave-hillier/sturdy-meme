#include "WeatherSystem.h"
#include "WindSystem.h"
#include "ShaderLoader.h"
#include "PipelineBuilder.h"
#include "VulkanBarriers.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <algorithm>
#include <array>

bool WeatherSystem::init(const InitInfo& info) {
    SystemLifecycleHelper::Hooks hooks{};
    hooks.createBuffers = [this]() { return createBuffers(); };
    hooks.createComputeDescriptorSetLayout = [this]() { return createComputeDescriptorSetLayout(); };
    hooks.createComputePipeline = [this]() { return createComputePipeline(); };
    hooks.createGraphicsDescriptorSetLayout = [this]() { return createGraphicsDescriptorSetLayout(); };
    hooks.createGraphicsPipeline = [this]() { return createGraphicsPipeline(); };
    hooks.createDescriptorSets = [this]() { return createDescriptorSets(); };
    hooks.destroyBuffers = [this](VmaAllocator allocator) { destroyBuffers(allocator); };

    return particleSystem.init(info, hooks, BUFFER_SET_COUNT);
}

void WeatherSystem::destroy(VkDevice dev, VmaAllocator alloc) {
    particleSystem.destroy(dev, alloc);
}

void WeatherSystem::destroyBuffers(VmaAllocator alloc) {
    BufferUtils::destroyBuffers(alloc, particleBuffers);
    BufferUtils::destroyBuffers(alloc, indirectBuffers);
    BufferUtils::destroyBuffers(alloc, uniformBuffers);
}

bool WeatherSystem::createBuffers() {
    VkDeviceSize particleBufferSize = sizeof(WeatherParticle) * MAX_PARTICLES;
    VkDeviceSize indirectBufferSize = sizeof(VkDrawIndirectCommand);
    VkDeviceSize uniformBufferSize = sizeof(WeatherUniforms);

    BufferUtils::DoubleBufferedBufferBuilder particleBuilder;
    if (!particleBuilder.setAllocator(getAllocator())
             .setSetCount(BUFFER_SET_COUNT)
             .setSize(particleBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
             .build(particleBuffers)) {
        SDL_Log("Failed to create weather particle buffers");
        return false;
    }

    BufferUtils::DoubleBufferedBufferBuilder indirectBuilder;
    if (!indirectBuilder.setAllocator(getAllocator())
             .setSetCount(BUFFER_SET_COUNT)
             .setSize(indirectBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
             .build(indirectBuffers)) {
        SDL_Log("Failed to create weather indirect buffers");
        return false;
    }

    BufferUtils::PerFrameBufferBuilder uniformBuilder;
    if (!uniformBuilder.setAllocator(getAllocator())
             .setFrameCount(getFramesInFlight())
             .setSize(uniformBufferSize)
             .build(uniformBuffers)) {
        SDL_Log("Failed to create weather uniform buffers");
        return false;
    }

    return true;
}

bool WeatherSystem::createComputeDescriptorSetLayout() {
    auto& computePipeline = getComputePipelineHandles();
    PipelineBuilder builder(getDevice());
    builder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);

    return builder.buildDescriptorSetLayout(computePipeline.descriptorSetLayout);
}

bool WeatherSystem::createComputePipeline() {
    auto& computePipeline = getComputePipelineHandles();
    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/weather.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT)
        .addPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(WeatherPushConstants));

    if (!builder.buildPipelineLayout({computePipeline.descriptorSetLayout}, computePipeline.pipelineLayout)) {
        return false;
    }

    return builder.buildComputePipeline(computePipeline.pipelineLayout, computePipeline.pipeline);
}

bool WeatherSystem::createGraphicsDescriptorSetLayout() {
    auto& graphicsPipeline = getGraphicsPipelineHandles();
    PipelineBuilder builder(getDevice());
    builder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
        .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);

    return builder.buildDescriptorSetLayout(graphicsPipeline.descriptorSetLayout);
}

bool WeatherSystem::createGraphicsPipeline() {
    auto& graphicsPipeline = getGraphicsPipelineHandles();
    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/weather.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
        .addShaderStage(getShaderPath() + "/weather.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                              sizeof(WeatherPushConstants));

    // No vertex input - procedural geometry from instance buffer
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(getExtent().width);
    viewport.height = static_cast<float>(getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = getExtent();

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling for rain particles
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;  // Don't write depth for transparent particles
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Additive blending for rain (bright streaks)
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;  // Additive
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    if (!builder.buildPipelineLayout({graphicsPipeline.descriptorSetLayout}, graphicsPipeline.pipelineLayout)) {
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.renderPass = getRenderPass();
    pipelineInfo.subpass = 0;

    return builder.buildGraphicsPipeline(pipelineInfo, graphicsPipeline.pipelineLayout, graphicsPipeline.pipeline);
}

bool WeatherSystem::createDescriptorSets() {
    return particleSystem.createStandardDescriptorSets();
}

void WeatherSystem::updateDescriptorSets(VkDevice dev, const std::vector<VkBuffer>& rendererUniformBuffers,
                                          const std::vector<VkBuffer>& windBuffers,
                                          VkImageView depthImageView, VkSampler depthSampler) {
    // Store external buffer references for per-frame descriptor updates
    externalWindBuffers = windBuffers;
    externalRendererUniformBuffers = rendererUniformBuffers;

    // Update compute and graphics descriptor sets for both buffer sets
    for (uint32_t set = 0; set < BUFFER_SET_COUNT; set++) {
        uint32_t inputSet = (set == 0) ? 1 : 0;  // Read from opposite buffer
        uint32_t outputSet = set;

        // Compute descriptor set writes
        VkDescriptorBufferInfo inputParticleBufferInfo{};
        inputParticleBufferInfo.buffer = particleBuffers.buffers[inputSet];
        inputParticleBufferInfo.offset = 0;
        inputParticleBufferInfo.range = sizeof(WeatherParticle) * MAX_PARTICLES;

        VkDescriptorBufferInfo outputParticleBufferInfo{};
        outputParticleBufferInfo.buffer = particleBuffers.buffers[outputSet];
        outputParticleBufferInfo.offset = 0;
        outputParticleBufferInfo.range = sizeof(WeatherParticle) * MAX_PARTICLES;

        VkDescriptorBufferInfo indirectBufferInfo{};
        indirectBufferInfo.buffer = indirectBuffers.buffers[outputSet];
        indirectBufferInfo.offset = 0;
        indirectBufferInfo.range = sizeof(VkDrawIndirectCommand);

        VkDescriptorBufferInfo weatherUniformInfo{};
        weatherUniformInfo.buffer = uniformBuffers.buffers[0];
        weatherUniformInfo.offset = 0;
        weatherUniformInfo.range = sizeof(WeatherUniforms);

        VkDescriptorBufferInfo windBufferInfo{};
        windBufferInfo.buffer = windBuffers[0];
        windBufferInfo.offset = 0;
        windBufferInfo.range = 32;  // sizeof(WindUniforms)

        std::array<VkWriteDescriptorSet, 5> computeWrites{};

        computeWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[0].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[0].dstBinding = 0;
        computeWrites[0].dstArrayElement = 0;
        computeWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeWrites[0].descriptorCount = 1;
        computeWrites[0].pBufferInfo = &inputParticleBufferInfo;

        computeWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[1].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[1].dstBinding = 1;
        computeWrites[1].dstArrayElement = 0;
        computeWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeWrites[1].descriptorCount = 1;
        computeWrites[1].pBufferInfo = &outputParticleBufferInfo;

        computeWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[2].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[2].dstBinding = 2;
        computeWrites[2].dstArrayElement = 0;
        computeWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeWrites[2].descriptorCount = 1;
        computeWrites[2].pBufferInfo = &indirectBufferInfo;

        computeWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[3].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[3].dstBinding = 3;
        computeWrites[3].dstArrayElement = 0;
        computeWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        computeWrites[3].descriptorCount = 1;
        computeWrites[3].pBufferInfo = &weatherUniformInfo;

        computeWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[4].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[4].dstBinding = 4;
        computeWrites[4].dstArrayElement = 0;
        computeWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        computeWrites[4].descriptorCount = 1;
        computeWrites[4].pBufferInfo = &windBufferInfo;

        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(computeWrites.size()),
                               computeWrites.data(), 0, nullptr);

        // Graphics descriptor set writes
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = rendererUniformBuffers[0];
        uboInfo.offset = 0;
        uboInfo.range = 320;  // sizeof(UniformBufferObject)

        VkDescriptorBufferInfo particleBufferInfo{};
        particleBufferInfo.buffer = particleBuffers.buffers[set];
        particleBufferInfo.offset = 0;
        particleBufferInfo.range = sizeof(WeatherParticle) * MAX_PARTICLES;

        VkDescriptorImageInfo depthImageInfo{};
        depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        depthImageInfo.imageView = depthImageView;
        depthImageInfo.sampler = depthSampler;

        std::array<VkWriteDescriptorSet, 3> graphicsWrites{};

        graphicsWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[0].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[0].dstBinding = 0;
        graphicsWrites[0].dstArrayElement = 0;
        graphicsWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        graphicsWrites[0].descriptorCount = 1;
        graphicsWrites[0].pBufferInfo = &uboInfo;

        graphicsWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[1].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[1].dstBinding = 1;
        graphicsWrites[1].dstArrayElement = 0;
        graphicsWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        graphicsWrites[1].descriptorCount = 1;
        graphicsWrites[1].pBufferInfo = &particleBufferInfo;

        graphicsWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[2].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[2].dstBinding = 2;
        graphicsWrites[2].dstArrayElement = 0;
        graphicsWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        graphicsWrites[2].descriptorCount = 1;
        graphicsWrites[2].pImageInfo = &depthImageInfo;

        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(graphicsWrites.size()),
                               graphicsWrites.data(), 0, nullptr);
    }
}

void WeatherSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                                    const glm::mat4& viewProj, float deltaTime, float totalTime,
                                    const WindSystem& windSystem) {
    WeatherUniforms uniforms{};

    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);

    // Extract frustum planes from view-projection matrix
    glm::mat4 m = glm::transpose(viewProj);
    uniforms.frustumPlanes[0] = m[3] + m[0];  // Left
    uniforms.frustumPlanes[1] = m[3] - m[0];  // Right
    uniforms.frustumPlanes[2] = m[3] + m[1];  // Bottom
    uniforms.frustumPlanes[3] = m[3] - m[1];  // Top
    uniforms.frustumPlanes[4] = m[3] + m[2];  // Near
    uniforms.frustumPlanes[5] = m[3] - m[2];  // Far

    // Normalize planes
    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(uniforms.frustumPlanes[i]));
        if (len > 0.0001f) {
            uniforms.frustumPlanes[i] /= len;
        }
    }

    // Sample wind parameters from wind system
    glm::vec2 windDir = windSystem.getWindDirection();
    float windStr = windSystem.getWindStrength();
    float turbulence = windSystem.getGustAmplitude();
    uniforms.windDirectionStrength = glm::vec4(windDir.x, windDir.y, windStr, turbulence);

    // Gravity for rain (downward with terminal velocity)
    uniforms.gravity = glm::vec4(0.0f, -9.8f, 0.0f, 11.0f);  // Terminal velocity ~11 m/s for rain

    // Spawn region centered on camera
    uniforms.spawnRegion = glm::vec4(cameraPos.x, cameraPos.y + 10.0f, cameraPos.z, 80.0f);
    uniforms.spawnHeight = 10.0f;
    uniforms.groundLevel = groundLevel;
    uniforms.particleDensity = 1.0f;
    uniforms.maxDrawDistance = 100.0f;
    uniforms.time = totalTime;
    uniforms.deltaTime = deltaTime;
    uniforms.weatherType = weatherType;
    uniforms.intensity = weatherIntensity;
    uniforms.nearZoneRadius = 8.0f;

    memcpy(uniformBuffers.mappedPointers[frameIndex], &uniforms, sizeof(WeatherUniforms));
}

void WeatherSystem::recordResetAndCompute(VkCommandBuffer cmd, uint32_t frameIndex, float time, float deltaTime) {
    uint32_t writeSet = particleSystem.getComputeBufferSet();

    // Update compute descriptor set to use this frame's uniform buffers
    VkDescriptorBufferInfo uniformBufferInfo{};
    uniformBufferInfo.buffer = uniformBuffers.buffers[frameIndex];
    uniformBufferInfo.offset = 0;
    uniformBufferInfo.range = sizeof(WeatherUniforms);

    VkDescriptorBufferInfo windBufferInfo{};
    windBufferInfo.buffer = externalWindBuffers[frameIndex];
    windBufferInfo.offset = 0;
    windBufferInfo.range = 32;  // sizeof(WindUniforms)

    std::array<VkWriteDescriptorSet, 2> computeWrites{};

    computeWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    computeWrites[0].dstSet = particleSystem.getComputeDescriptorSet(writeSet);
    computeWrites[0].dstBinding = 3;
    computeWrites[0].dstArrayElement = 0;
    computeWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    computeWrites[0].descriptorCount = 1;
    computeWrites[0].pBufferInfo = &uniformBufferInfo;

    computeWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    computeWrites[1].dstSet = particleSystem.getComputeDescriptorSet(writeSet);
    computeWrites[1].dstBinding = 4;
    computeWrites[1].dstArrayElement = 0;
    computeWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    computeWrites[1].descriptorCount = 1;
    computeWrites[1].pBufferInfo = &windBufferInfo;

    vkUpdateDescriptorSets(getDevice(), static_cast<uint32_t>(computeWrites.size()), computeWrites.data(), 0, nullptr);

    // Reset indirect buffer before compute dispatch
    Barriers::clearBufferForComputeReadWrite(cmd, indirectBuffers.buffers[writeSet], 0, sizeof(VkDrawIndirectCommand));

    // Dispatch weather compute shader
    auto& computePipeline = getComputePipelineHandles();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.pipeline);
    VkDescriptorSet computeSet = particleSystem.getComputeDescriptorSet(writeSet);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            computePipeline.pipelineLayout, 0, 1,
                            &computeSet, 0, nullptr);

    WeatherPushConstants pushConstants{};
    pushConstants.time = time;
    pushConstants.deltaTime = deltaTime;
    vkCmdPushConstants(cmd, computePipeline.pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(WeatherPushConstants), &pushConstants);

    // Dispatch: ceil(MAX_PARTICLES / WORKGROUP_SIZE) workgroups
    uint32_t workgroupCount = (MAX_PARTICLES + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    vkCmdDispatch(cmd, workgroupCount, 1, 1);

    // Memory barrier: compute write -> vertex shader read and indirect read
    Barriers::computeToVertexAndIndirectDraw(cmd);
}

void WeatherSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    uint32_t readSet = particleSystem.getRenderBufferSet();

    // Bootstrap: if we haven't diverged yet, read from compute set
    if (particleSystem.getComputeBufferSet() == particleSystem.getRenderBufferSet()) {
        readSet = particleSystem.getComputeBufferSet();
    }

    // Update graphics descriptor set to use this frame's renderer UBO
    VkDescriptorBufferInfo uboInfo{};
    uboInfo.buffer = externalRendererUniformBuffers[frameIndex];
    uboInfo.offset = 0;
    uboInfo.range = 320;  // sizeof(UniformBufferObject)

    VkWriteDescriptorSet uboWrite{};
    uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    uboWrite.dstSet = particleSystem.getGraphicsDescriptorSet(readSet);
    uboWrite.dstBinding = 0;
    uboWrite.dstArrayElement = 0;
    uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboWrite.descriptorCount = 1;
    uboWrite.pBufferInfo = &uboInfo;

    vkUpdateDescriptorSets(getDevice(), 1, &uboWrite, 0, nullptr);

    auto& graphicsPipeline = getGraphicsPipelineHandles();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline.pipeline);
    VkDescriptorSet graphicsSet = particleSystem.getGraphicsDescriptorSet(readSet);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphicsPipeline.pipelineLayout, 0, 1,
                            &graphicsSet, 0, nullptr);

    WeatherPushConstants pushConstants{};
    pushConstants.time = time;
    pushConstants.deltaTime = 0.0f;  // Not needed for rendering
    vkCmdPushConstants(cmd, graphicsPipeline.pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(WeatherPushConstants), &pushConstants);

    // Indirect draw: 4 vertices per particle (quad)
    vkCmdDrawIndirect(cmd, indirectBuffers.buffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
}

void WeatherSystem::advanceBufferSet() { particleSystem.advanceBufferSet(); }

void WeatherSystem::setFroxelVolume(VkImageView volumeView, VkSampler volumeSampler,
                                     float farPlane, float depthDist) {
    froxelVolumeView = volumeView;
    froxelVolumeSampler = volumeSampler;
    froxelFarPlane = farPlane;
    froxelDepthDist = depthDist;

    // Update graphics descriptor sets with froxel volume
    if (froxelVolumeView != VK_NULL_HANDLE && froxelVolumeSampler != VK_NULL_HANDLE) {
        for (uint32_t set = 0; set < BUFFER_SET_COUNT; set++) {
            VkDescriptorImageInfo froxelImageInfo{};
            froxelImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            froxelImageInfo.imageView = froxelVolumeView;
            froxelImageInfo.sampler = froxelVolumeSampler;

            VkWriteDescriptorSet froxelWrite{};
            froxelWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            froxelWrite.dstSet = particleSystem.getGraphicsDescriptorSet(set);
            froxelWrite.dstBinding = 3;
            froxelWrite.dstArrayElement = 0;
            froxelWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            froxelWrite.descriptorCount = 1;
            froxelWrite.pImageInfo = &froxelImageInfo;

            vkUpdateDescriptorSets(getDevice(), 1, &froxelWrite, 0, nullptr);
        }
    }
}
