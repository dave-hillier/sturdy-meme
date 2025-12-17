#include "LeafSystem.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "VulkanBarriers.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <algorithm>

bool LeafSystem::init(const InitInfo& info) {
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

    particleSystem = RAIIAdapter<ParticleSystem>::create(
        [&](auto& ps) {
            initializingPS = &ps;
            return ps.init(info, hooks, BUFFER_SET_COUNT);
        },
        [](auto& ps) { ps.destroy(ps.getDevice(), ps.getAllocator()); }
    );

    return particleSystem.has_value();
}

void LeafSystem::destroy(VkDevice dev, VmaAllocator alloc) {
    particleSystem.reset();
}

void LeafSystem::destroyBuffers(VmaAllocator alloc) {
    BufferUtils::destroyBuffers(alloc, particleBuffers);
    BufferUtils::destroyBuffers(alloc, indirectBuffers);
    BufferUtils::destroyBuffers(alloc, uniformBuffers);

    for (size_t i = 0; i < getFramesInFlight(); i++) {
        vmaDestroyBuffer(alloc, displacementRegionBuffers[i], displacementRegionAllocations[i]);
    }
}

bool LeafSystem::createBuffers() {
    VkDeviceSize particleBufferSize = sizeof(LeafParticle) * MAX_PARTICLES;
    VkDeviceSize indirectBufferSize = sizeof(VkDrawIndirectCommand);
    VkDeviceSize uniformBufferSize = sizeof(LeafUniforms);

    BufferUtils::DoubleBufferedBufferBuilder particleBuilder;
    if (!particleBuilder.setAllocator(getAllocator())
             .setSetCount(BUFFER_SET_COUNT)
             .setSize(particleBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
             .build(particleBuffers)) {
        SDL_Log("Failed to create leaf particle buffers");
        return false;
    }

    BufferUtils::DoubleBufferedBufferBuilder indirectBuilder;
    if (!indirectBuilder.setAllocator(getAllocator())
             .setSetCount(BUFFER_SET_COUNT)
             .setSize(indirectBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
             .build(indirectBuffers)) {
        SDL_Log("Failed to create leaf indirect buffers");
        return false;
    }

    BufferUtils::PerFrameBufferBuilder uniformBuilder;
    if (!uniformBuilder.setAllocator(getAllocator())
             .setFrameCount(getFramesInFlight())
             .setSize(uniformBufferSize)
             .build(uniformBuffers)) {
        SDL_Log("Failed to create leaf uniform buffers");
        return false;
    }

    // Create displacement region uniform buffers (per-frame)
    displacementRegionBuffers.resize(getFramesInFlight());
    displacementRegionAllocations.resize(getFramesInFlight());
    displacementRegionMappedPtrs.resize(getFramesInFlight());

    VkDeviceSize dispRegionBufferSize = sizeof(glm::vec4);  // regionCenterAndSize

    for (size_t i = 0; i < getFramesInFlight(); i++) {
        VkBufferCreateInfo dispRegionBufferInfo{};
        dispRegionBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        dispRegionBufferInfo.size = dispRegionBufferSize;
        dispRegionBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        dispRegionBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo dispRegionAllocInfo{};
        dispRegionAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        dispRegionAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                   VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo dispRegionAllocResult;
        if (vmaCreateBuffer(getAllocator(), &dispRegionBufferInfo, &dispRegionAllocInfo,
                           &displacementRegionBuffers[i], &displacementRegionAllocations[i],
                           &dispRegionAllocResult) != VK_SUCCESS) {
            SDL_Log("Failed to create leaf displacement region buffer");
            return false;
        }
        displacementRegionMappedPtrs[i] = dispRegionAllocResult.pMappedData;
    }

    return true;
}

bool LeafSystem::createComputeDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles) {
    // 0: Particle buffer input (previous frame state)
    // 1: Particle buffer output (current frame result)
    // 2: Indirect buffer (output)
    // 3: Leaf uniforms
    // 4: Wind uniforms
    // 5: Terrain heightmap
    // 6: Displacement map (shared with grass system for player interaction)
    // 7: Displacement region uniform buffer

    handles.descriptorSetLayout = DescriptorManager::LayoutBuilder(getDevice())
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 0: Particle buffer input
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 1: Particle buffer output
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 2: Indirect buffer
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 3: Leaf uniforms
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 4: Wind uniforms
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)    // 5: Terrain heightmap
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)    // 6: Displacement map
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 7: Displacement region
        .build();

    if (handles.descriptorSetLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create leaf compute descriptor set layout");
        return false;
    }

    return true;
}

bool LeafSystem::createComputePipeline(SystemLifecycleHelper::PipelineHandles& handles) {
    auto compShaderCode = ShaderLoader::readFile(getShaderPath() + "/leaf.comp.spv");
    if (!compShaderCode) {
        SDL_Log("Failed to load leaf compute shader");
        return false;
    }

    auto compShaderModule = ShaderLoader::createShaderModule(getDevice(), *compShaderCode);
    if (!compShaderModule) {
        SDL_Log("Failed to create leaf compute shader module");
        return false;
    }

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = *compShaderModule;
    shaderStageInfo.pName = "main";

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(LeafPushConstants);

    handles.pipelineLayout = DescriptorManager::createPipelineLayout(
        getDevice(), handles.descriptorSetLayout, {pushConstantRange});
    if (handles.pipelineLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create leaf compute pipeline layout");
        vkDestroyShaderModule(getDevice(), *compShaderModule, nullptr);
        return false;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = handles.pipelineLayout;

    VkResult result = vkCreateComputePipelines(getDevice(), VK_NULL_HANDLE, 1,
                                               &pipelineInfo, nullptr,
                                               &handles.pipeline);

    vkDestroyShaderModule(getDevice(), *compShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_Log("Failed to create leaf compute pipeline");
        return false;
    }

    return true;
}

bool LeafSystem::createGraphicsDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles) {
    // 0: UBO (scene uniforms)
    // 1: Particle buffer (read-only in vertex shader)
    // 2: Wind uniforms (for consistent animation)

    handles.descriptorSetLayout = DescriptorManager::LayoutBuilder(getDevice())
        .addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)  // 0: UBO
        .addStorageBuffer(VK_SHADER_STAGE_VERTEX_BIT)                                  // 1: Particle buffer
        .addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT)                                  // 2: Wind uniforms
        .build();

    if (handles.descriptorSetLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create leaf graphics descriptor set layout");
        return false;
    }

    return true;
}

bool LeafSystem::createGraphicsPipeline(SystemLifecycleHelper::PipelineHandles& handles) {
    auto vertShaderCode = ShaderLoader::readFile(getShaderPath() + "/leaf.vert.spv");
    auto fragShaderCode = ShaderLoader::readFile(getShaderPath() + "/leaf.frag.spv");

    if (!vertShaderCode || !fragShaderCode) {
        SDL_Log("Failed to load leaf shader files");
        return false;
    }

    auto vertShaderModule = ShaderLoader::createShaderModule(getDevice(), *vertShaderCode);
    auto fragShaderModule = ShaderLoader::createShaderModule(getDevice(), *fragShaderCode);

    if (!vertShaderModule || !fragShaderModule) {
        SDL_Log("Failed to create leaf shader modules");
        if (vertShaderModule) vkDestroyShaderModule(getDevice(), *vertShaderModule, nullptr);
        if (fragShaderModule) vkDestroyShaderModule(getDevice(), *fragShaderModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = *vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = *fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling for leaves (visible from both sides)
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;  // Write depth for proper sorting
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Alpha blending for leaf edges
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
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

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(LeafPushConstants);

    handles.pipelineLayout = DescriptorManager::createPipelineLayout(
        getDevice(), handles.descriptorSetLayout, {pushConstantRange});
    if (handles.pipelineLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create leaf graphics pipeline layout");
        vkDestroyShaderModule(getDevice(), *fragShaderModule, nullptr);
        vkDestroyShaderModule(getDevice(), *vertShaderModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = handles.pipelineLayout;
    pipelineInfo.renderPass = getRenderPass();
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(getDevice(), VK_NULL_HANDLE, 1,
                                                &pipelineInfo, nullptr,
                                                &handles.pipeline);

    vkDestroyShaderModule(getDevice(), *fragShaderModule, nullptr);
    vkDestroyShaderModule(getDevice(), *vertShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_Log("Failed to create leaf graphics pipeline");
        return false;
    }

    return true;
}

bool LeafSystem::createDescriptorSets() {
    // Note: Standard compute/graphics descriptor sets are allocated by ParticleSystem::init()
    // after all hooks complete. LeafSystem has no additional custom descriptor sets.
    return true;
}

void LeafSystem::updateDescriptorSets(VkDevice dev, const std::vector<VkBuffer>& rendererUniformBuffers,
                                       const std::vector<VkBuffer>& windBuffers,
                                       VkImageView terrainHeightMapView,
                                       VkSampler terrainHeightMapSampler,
                                       VkImageView displacementMapViewParam,
                                       VkSampler displacementMapSamplerParam) {
    // Store displacement texture references
    this->displacementMapView = displacementMapViewParam;
    this->displacementMapSampler = displacementMapSamplerParam;

    // Update compute and graphics descriptor sets for both buffer sets
    for (uint32_t set = 0; set < BUFFER_SET_COUNT; set++) {
        uint32_t inputSet = (set == 0) ? 1 : 0;  // Read from opposite buffer
        uint32_t outputSet = set;

        // Compute descriptor set
        DescriptorManager::SetWriter(dev, (*particleSystem)->getComputeDescriptorSet(set))
            .writeBuffer(0, particleBuffers.buffers[inputSet], 0, sizeof(LeafParticle) * MAX_PARTICLES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(1, particleBuffers.buffers[outputSet], 0, sizeof(LeafParticle) * MAX_PARTICLES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(2, indirectBuffers.buffers[outputSet], 0, sizeof(VkDrawIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(3, uniformBuffers.buffers[0], 0, sizeof(LeafUniforms))
            .writeBuffer(4, windBuffers[0], 0, 32)  // sizeof(WindUniforms)
            .writeImage(5, terrainHeightMapView, terrainHeightMapSampler)
            .writeImage(6, displacementMapViewParam, displacementMapSamplerParam)
            .writeBuffer(7, displacementRegionBuffers[0], 0, sizeof(glm::vec4))
            .update();

        // Graphics descriptor set
        DescriptorManager::SetWriter(dev, (*particleSystem)->getGraphicsDescriptorSet(set))
            .writeBuffer(0, rendererUniformBuffers[0], 0, 320)  // sizeof(UniformBufferObject)
            .writeBuffer(1, particleBuffers.buffers[set], 0, sizeof(LeafParticle) * MAX_PARTICLES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(2, windBuffers[0], 0, 32)  // sizeof(WindUniforms)
            .update();
    }
}

void LeafSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                                 const glm::mat4& viewProj, const glm::vec3& playerPos,
                                 const glm::vec3& playerVel, float deltaTime, float totalTime,
                                 float terrainSize, float terrainHeightScale) {
    LeafUniforms uniforms{};
    const EnvironmentSettings fallbackSettings{};
    const EnvironmentSettings& settings = environmentSettings ? *environmentSettings : fallbackSettings;

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

    // Player data for disruption
    uniforms.playerPosition = glm::vec4(playerPos, 0.5f);  // w = player collision radius
    float playerSpeed = glm::length(playerVel);
    uniforms.playerVelocity = glm::vec4(playerVel, playerSpeed);

    // Spawn region
    uniforms.spawnRegionMin = glm::vec4(spawnRegionMin, 0.0f);
    uniforms.spawnRegionMax = glm::vec4(spawnRegionMax, 0.0f);

    // Confetti spawn parameters
    uniforms.confettiSpawnPos = glm::vec4(confettiSpawnPosition, confettiConeAngle);
    uniforms.confettiSpawnCount = confettiToSpawn;
    uniforms.confettiVelocity = confettiSpawnVelocity;

    // General parameters
    uniforms.groundLevel = groundLevel;
    uniforms.deltaTime = deltaTime;
    uniforms.time = totalTime;
    uniforms.maxDrawDistance = 60.0f;

    // Disruption parameters
    uniforms.disruptionRadius = settings.leafDisruptionRadius;
    uniforms.disruptionStrength = settings.leafDisruptionStrength;
    uniforms.gustThreshold = settings.leafGustLiftThreshold;

    // Target counts based on intensity
    uniforms.targetFallingCount = leafIntensity * 5000.0f;   // 0-5000 falling leaves
    uniforms.targetGroundedCount = leafIntensity * 20000.0f; // 0-20000 grounded leaves

    // Terrain parameters
    uniforms.terrainSize = terrainSize;
    uniforms.terrainHeightScale = terrainHeightScale;

    memcpy(uniformBuffers.mappedPointers[frameIndex], &uniforms, sizeof(LeafUniforms));

    // Update displacement region to follow camera (same as grass system)
    displacementRegionCenter = glm::vec2(cameraPos.x, cameraPos.z);

    // Update displacement region uniform buffer
    glm::vec4 dispRegionData(displacementRegionCenter.x, displacementRegionCenter.y,
                             DISPLACEMENT_REGION_SIZE, 0.0f);
    memcpy(displacementRegionMappedPtrs[frameIndex], &dispRegionData, sizeof(glm::vec4));

    // Reset confetti spawn count after it's been sent to GPU
    confettiToSpawn = 0.0f;
}

void LeafSystem::recordResetAndCompute(VkCommandBuffer cmd, uint32_t frameIndex, float time, float deltaTime) {
    uint32_t writeSet = (*particleSystem)->getComputeBufferSet();

    // Update compute descriptor set to use this frame's uniform and displacement region buffers
    DescriptorManager::SetWriter(getDevice(), (*particleSystem)->getComputeDescriptorSet(writeSet))
        .writeBuffer(3, uniformBuffers.buffers[frameIndex], 0, sizeof(LeafUniforms))
        .writeBuffer(7, displacementRegionBuffers[frameIndex], 0, sizeof(glm::vec4))
        .update();

    // Reset indirect buffer before compute dispatch
    Barriers::clearBufferForCompute(cmd, indirectBuffers.buffers[writeSet], 0, sizeof(VkDrawIndirectCommand));

    // Dispatch leaf compute shader
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, getComputePipelineHandles().pipeline);
    VkDescriptorSet computeSet = (*particleSystem)->getComputeDescriptorSet(writeSet);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            getComputePipelineHandles().pipelineLayout, 0, 1,
                            &computeSet, 0, nullptr);

    LeafPushConstants pushConstants{};
    pushConstants.time = time;
    pushConstants.deltaTime = deltaTime;
    vkCmdPushConstants(cmd, getComputePipelineHandles().pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(LeafPushConstants), &pushConstants);

    // Dispatch: ceil(MAX_PARTICLES / WORKGROUP_SIZE) workgroups
    uint32_t workgroupCount = (MAX_PARTICLES + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    vkCmdDispatch(cmd, workgroupCount, 1, 1);

    // Memory barrier: compute write -> vertex shader read and indirect read
    Barriers::computeToIndirectDraw(cmd);
}

void LeafSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    uint32_t readSet = (*particleSystem)->getRenderBufferSet();

    // Bootstrap: if we haven't diverged yet, read from compute set
    if ((*particleSystem)->getComputeBufferSet() == (*particleSystem)->getRenderBufferSet()) {
        readSet = (*particleSystem)->getComputeBufferSet();
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getGraphicsPipelineHandles().pipeline);

    // Set dynamic viewport and scissor to handle window resize
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(getExtent().width);
    viewport.height = static_cast<float>(getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = getExtent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    VkDescriptorSet graphicsSet = (*particleSystem)->getGraphicsDescriptorSet(readSet);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            getGraphicsPipelineHandles().pipelineLayout, 0, 1,
                            &graphicsSet, 0, nullptr);

    LeafPushConstants pushConstants{};
    pushConstants.time = time;
    pushConstants.deltaTime = 0.0f;  // Not needed for rendering
    vkCmdPushConstants(cmd, getGraphicsPipelineHandles().pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(LeafPushConstants), &pushConstants);

    // Indirect draw: 4 vertices per leaf (quad)
    vkCmdDrawIndirect(cmd, indirectBuffers.buffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
}

void LeafSystem::advanceBufferSet() { (*particleSystem)->advanceBufferSet(); }
