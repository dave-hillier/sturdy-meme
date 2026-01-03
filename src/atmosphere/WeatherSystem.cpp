#include "WeatherSystem.h"
#include "LeafSystem.h"
#include "WindSystem.h"
#include "InitContext.h"
#include "ShaderLoader.h"
#include "PipelineBuilder.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <cstring>
#include <algorithm>
#include <array>

std::unique_ptr<WeatherSystem> WeatherSystem::create(const InitInfo& info) {
    std::unique_ptr<WeatherSystem> system(new WeatherSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::optional<WeatherSystem::Bundle> WeatherSystem::createWithDependencies(
    const InitContext& ctx,
    VkRenderPass hdrRenderPass
) {
    // Create weather particle system (rain/snow)
    InitInfo weatherInfo{};
    weatherInfo.device = ctx.device;
    weatherInfo.allocator = ctx.allocator;
    weatherInfo.renderPass = hdrRenderPass;
    weatherInfo.descriptorPool = ctx.descriptorPool;
    weatherInfo.extent = ctx.extent;
    weatherInfo.shaderPath = ctx.shaderPath;
    weatherInfo.framesInFlight = ctx.framesInFlight;

    auto weatherSystem = create(weatherInfo);
    if (!weatherSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize WeatherSystem");
        return std::nullopt;
    }

    // Create leaf particle system
    LeafSystem::InitInfo leafInfo{};
    leafInfo.device = ctx.device;
    leafInfo.allocator = ctx.allocator;
    leafInfo.renderPass = hdrRenderPass;
    leafInfo.descriptorPool = ctx.descriptorPool;
    leafInfo.extent = ctx.extent;
    leafInfo.shaderPath = ctx.shaderPath;
    leafInfo.framesInFlight = ctx.framesInFlight;

    auto leafSystem = LeafSystem::create(leafInfo);
    if (!leafSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize LeafSystem");
        return std::nullopt;
    }

    // Set default leaf intensity (autumn scene)
    leafSystem->setIntensity(0.5f);

    return Bundle{
        std::move(weatherSystem),
        std::move(leafSystem)
    };
}

WeatherSystem::~WeatherSystem() {
    cleanup();
}

bool WeatherSystem::initInternal(const InitInfo& info) {
    // Store init info for accessors used during initialization
    storedDevice = info.device;
    storedAllocator = info.allocator;
    storedRenderPass = info.renderPass;
    storedDescriptorPool = info.descriptorPool;
    storedExtent = info.extent;
    storedShaderPath = info.shaderPath;
    storedFramesInFlight = info.framesInFlight;

    // Pointer to the ParticleSystem being initialized (for hooks to access)
    ParticleSystem* initializingPS = nullptr;

    SystemLifecycleHelper::Hooks hooks{};
    hooks.createBuffers = [this]() { return createBuffers(); };
    hooks.createComputeDescriptorSetLayout = [this, &initializingPS]() {
        return createComputeDescriptorSetLayout(initializingPS->getComputePipelineHandles());
    };
    hooks.createComputePipeline = [this, &initializingPS]() {
        return createComputePipeline(initializingPS->getComputePipelineHandles());
    };
    hooks.createGraphicsDescriptorSetLayout = [this, &initializingPS]() {
        return createGraphicsDescriptorSetLayout(initializingPS->getGraphicsPipelineHandles());
    };
    hooks.createGraphicsPipeline = [this, &initializingPS]() {
        return createGraphicsPipeline(initializingPS->getGraphicsPipelineHandles());
    };
    hooks.createDescriptorSets = [this]() { return createDescriptorSets(); };
    hooks.destroyBuffers = [this](VmaAllocator allocator) { destroyBuffers(allocator); };

    particleSystem = ParticleSystem::create(info, hooks, info.framesInFlight, &initializingPS);
    return particleSystem != nullptr;
}

void WeatherSystem::cleanup() {
    // RAII-managed subsystem destroyed automatically
    particleSystem.reset();
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

    // Use framesInFlight for buffer set count to ensure proper triple buffering
    uint32_t bufferSetCount = getFramesInFlight();

    BufferUtils::DoubleBufferedBufferBuilder particleBuilder;
    if (!particleBuilder.setAllocator(getAllocator())
             .setSetCount(bufferSetCount)
             .setSize(particleBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
             .build(particleBuffers)) {
        SDL_Log("Failed to create weather particle buffers");
        return false;
    }

    BufferUtils::DoubleBufferedBufferBuilder indirectBuilder;
    if (!indirectBuilder.setAllocator(getAllocator())
             .setSetCount(bufferSetCount)
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

bool WeatherSystem::createComputeDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles) {
    PipelineBuilder builder(getDevice());
    builder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);

    return builder.buildDescriptorSetLayout(handles.descriptorSetLayout);
}

bool WeatherSystem::createComputePipeline(SystemLifecycleHelper::PipelineHandles& handles) {
    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/weather.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT)
        .addPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(WeatherPushConstants));

    if (!builder.buildPipelineLayout({handles.descriptorSetLayout}, handles.pipelineLayout)) {
        return false;
    }

    return builder.buildComputePipeline(handles.pipelineLayout, handles.pipeline);
}

bool WeatherSystem::createGraphicsDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles) {
    PipelineBuilder builder(getDevice());
    // Binding 0 uses DYNAMIC to avoid per-frame descriptor updates
    builder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
                                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
        .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);

    return builder.buildDescriptorSetLayout(handles.descriptorSetLayout);
}

bool WeatherSystem::createGraphicsPipeline(SystemLifecycleHelper::PipelineHandles& handles) {
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

    // Enable dynamic viewport and scissor for window resize handling
    std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    if (!builder.buildPipelineLayout({handles.descriptorSetLayout}, handles.pipelineLayout)) {
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
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.renderPass = getRenderPass();
    pipelineInfo.subpass = 0;

    return builder.buildGraphicsPipeline(pipelineInfo, handles.pipelineLayout, handles.pipeline);
}

bool WeatherSystem::createDescriptorSets() {
    // Note: Standard compute/graphics descriptor sets are allocated by ParticleSystem::init()
    // after all hooks complete. WeatherSystem has no additional custom descriptor sets.
    return true;
}

void WeatherSystem::updateDescriptorSets(VkDevice dev, const std::vector<VkBuffer>& rendererUniformBuffers,
                                          const std::vector<VkBuffer>& windBuffers,
                                          VkImageView depthImageView, VkSampler depthSampler,
                                          const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO) {
    // Store external buffer references (kept for backward compatibility)
    externalWindBuffers = windBuffers;
    externalRendererUniformBuffers = rendererUniformBuffers;

    // Store dynamic renderer UBO reference for per-frame binding with dynamic offsets
    dynamicRendererUBO_ = dynamicRendererUBO;

    // Update compute and graphics descriptor sets for all buffer sets
    uint32_t bufferSetCount = particleSystem->getBufferSetCount();
    for (uint32_t set = 0; set < bufferSetCount; set++) {
        // For triple buffering, input is the previous buffer set (wraps around)
        uint32_t inputSet = (set == 0) ? (bufferSetCount - 1) : (set - 1);
        uint32_t outputSet = set;

        // Compute descriptor set
        DescriptorManager::SetWriter(dev, particleSystem->getComputeDescriptorSet(set))
            .writeBuffer(0, particleBuffers.buffers[inputSet], 0, sizeof(WeatherParticle) * MAX_PARTICLES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(1, particleBuffers.buffers[outputSet], 0, sizeof(WeatherParticle) * MAX_PARTICLES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(2, indirectBuffers.buffers[outputSet], 0, sizeof(VkDrawIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(3, uniformBuffers.buffers[0], 0, sizeof(WeatherUniforms))
            .writeBuffer(4, windBuffers[0], 0, 32)  // sizeof(WindUniforms)
            .update();

        // Graphics descriptor set - use dynamic UBO if available (avoids per-frame descriptor updates)
        DescriptorManager::SetWriter graphicsWriter(dev, particleSystem->getGraphicsDescriptorSet(set));
        if (dynamicRendererUBO && dynamicRendererUBO->isValid()) {
            graphicsWriter.writeBuffer(0, dynamicRendererUBO->buffer, 0, dynamicRendererUBO->alignedSize,
                                       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
        } else {
            graphicsWriter.writeBuffer(0, rendererUniformBuffers[0], 0, 320,
                                       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);  // sizeof(UniformBufferObject)
        }
        graphicsWriter.writeBuffer(1, particleBuffers.buffers[set], 0, sizeof(WeatherParticle) * MAX_PARTICLES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        graphicsWriter.writeImage(2, depthImageView, depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        graphicsWriter.update();
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
    // Early-out: skip compute when weather is disabled
    if (weatherIntensity <= 0.0f) {
        return;
    }

    uint32_t writeSet = particleSystem->getComputeBufferSet();

    // Update compute descriptor set to use this frame's uniform buffers
    DescriptorManager::SetWriter(getDevice(), particleSystem->getComputeDescriptorSet(writeSet))
        .writeBuffer(3, uniformBuffers.buffers[frameIndex], 0, sizeof(WeatherUniforms))
        .writeBuffer(4, externalWindBuffers[frameIndex], 0, 32)  // sizeof(WindUniforms)
        .update();

    // Reset indirect buffer before compute dispatch
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.fillBuffer(indirectBuffers.buffers[writeSet], 0, sizeof(VkDrawIndirectCommand), 0);
    auto transferBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                          {}, transferBarrier, {}, {});

    // Dispatch weather compute shader
    auto& computePipeline = getComputePipelineHandles();
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline.pipeline);
    VkDescriptorSet computeSet = particleSystem->getComputeDescriptorSet(writeSet);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                             computePipeline.pipelineLayout, 0,
                             vk::DescriptorSet(computeSet), {});

    WeatherPushConstants pushConstants{};
    pushConstants.time = time;
    pushConstants.deltaTime = deltaTime;
    vkCmd.pushConstants<WeatherPushConstants>(computePipeline.pipelineLayout,
                                               vk::ShaderStageFlagBits::eCompute, 0, pushConstants);

    // Dispatch: ceil(MAX_PARTICLES / WORKGROUP_SIZE) workgroups
    uint32_t workgroupCount = (MAX_PARTICLES + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    vkCmd.dispatch(workgroupCount, 1, 1);

    // Memory barrier: compute write -> vertex shader read and indirect read
    auto computeBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eIndirectCommandRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                          vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eVertexShader,
                          {}, computeBarrier, {}, {});
}

void WeatherSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    // Early-out: skip all GPU work when weather is disabled
    if (weatherIntensity <= 0.0f) {
        return;
    }

    // Double-buffer: graphics reads from renderBufferSet (previous frame's compute output)
    uint32_t readSet = particleSystem->getRenderBufferSet();

    // Dynamic UBO: no per-frame descriptor update needed - we pass the offset at bind time instead
    // This eliminates per-frame vkUpdateDescriptorSets calls for the renderer UBO

    auto& graphicsPipeline = getGraphicsPipelineHandles();
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline.pipeline);

    // Set dynamic viewport and scissor to handle window resize
    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(getExtent().width))
        .setHeight(static_cast<float>(getExtent().height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    vkCmd.setViewport(0, viewport);

    VkExtent2D ext = getExtent();
    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent(vk::Extent2D{ext.width, ext.height});
    vkCmd.setScissor(0, scissor);

    VkDescriptorSet graphicsSet = particleSystem->getGraphicsDescriptorSet(readSet);

    // Use dynamic offset for binding 0 (renderer UBO) if dynamic buffer is available
    if (dynamicRendererUBO_ && dynamicRendererUBO_->isValid()) {
        uint32_t dynamicOffset = dynamicRendererUBO_->getDynamicOffset(frameIndex);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                graphicsPipeline.pipelineLayout, 0, 1,
                                &graphicsSet, 1, &dynamicOffset);
    } else {
        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                 graphicsPipeline.pipelineLayout, 0,
                                 vk::DescriptorSet(graphicsSet), {});
    }

    WeatherPushConstants pushConstants{};
    pushConstants.time = time;
    pushConstants.deltaTime = 0.0f;  // Not needed for rendering
    vkCmd.pushConstants<WeatherPushConstants>(graphicsPipeline.pipelineLayout,
                                               vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                               0, pushConstants);

    // Indirect draw: 4 vertices per particle (quad)
    vkCmd.drawIndirect(indirectBuffers.buffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
}

void WeatherSystem::advanceBufferSet() { particleSystem->advanceBufferSet(); }

void WeatherSystem::setFroxelVolume(VkImageView volumeView, VkSampler volumeSampler,
                                     float farPlane, float depthDist) {
    froxelVolumeView = volumeView;
    froxelVolumeSampler = volumeSampler;
    froxelFarPlane = farPlane;
    froxelDepthDist = depthDist;

    // Update graphics descriptor sets with froxel volume
    if (froxelVolumeView != VK_NULL_HANDLE && froxelVolumeSampler != VK_NULL_HANDLE) {
        uint32_t bufferSetCount = particleSystem->getBufferSetCount();
        for (uint32_t set = 0; set < bufferSetCount; set++) {
            DescriptorManager::SetWriter(getDevice(), particleSystem->getGraphicsDescriptorSet(set))
                .writeImage(3, froxelVolumeView, froxelVolumeSampler)
                .update();
        }
    }
}
