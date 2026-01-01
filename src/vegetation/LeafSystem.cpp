#include "LeafSystem.h"
#include "CullCommon.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "VulkanBarriers.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <cstring>
#include <algorithm>

std::unique_ptr<LeafSystem> LeafSystem::create(const InitInfo& info) {
    std::unique_ptr<LeafSystem> system(new LeafSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

LeafSystem::~LeafSystem() {
    cleanup();
}

bool LeafSystem::initInternal(const InitInfo& info) {
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

void LeafSystem::cleanup() {
    particleSystem.reset();
}

void LeafSystem::destroyBuffers(VmaAllocator alloc) {
    BufferUtils::destroyBuffers(alloc, particleBuffers);
    BufferUtils::destroyBuffers(alloc, indirectBuffers);
    BufferUtils::destroyBuffers(alloc, uniformBuffers);
    BufferUtils::destroyBuffers(alloc, paramsBuffers);

    BufferUtils::destroyBuffers(alloc, displacementRegionBuffers);
}

bool LeafSystem::createBuffers() {
    VkDeviceSize particleBufferSize = sizeof(LeafParticle) * MAX_PARTICLES;
    VkDeviceSize indirectBufferSize = sizeof(VkDrawIndirectCommand);
    VkDeviceSize cullingUniformSize = sizeof(CullingUniforms);
    VkDeviceSize leafPhysicsParamsSize = sizeof(LeafPhysicsParams);

    // Use framesInFlight for buffer set count to ensure proper triple buffering
    uint32_t bufferSetCount = getFramesInFlight();

    BufferUtils::DoubleBufferedBufferBuilder particleBuilder;
    if (!particleBuilder.setAllocator(getAllocator())
             .setSetCount(bufferSetCount)
             .setSize(particleBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
             .build(particleBuffers)) {
        SDL_Log("Failed to create leaf particle buffers");
        return false;
    }

    BufferUtils::DoubleBufferedBufferBuilder indirectBuilder;
    if (!indirectBuilder.setAllocator(getAllocator())
             .setSetCount(bufferSetCount)
             .setSize(indirectBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
             .build(indirectBuffers)) {
        SDL_Log("Failed to create leaf indirect buffers");
        return false;
    }

    BufferUtils::PerFrameBufferBuilder uniformBuilder;
    if (!uniformBuilder.setAllocator(getAllocator())
             .setFrameCount(getFramesInFlight())
             .setSize(cullingUniformSize)
             .build(uniformBuffers)) {
        SDL_Log("Failed to create leaf culling uniform buffers");
        return false;
    }

    BufferUtils::PerFrameBufferBuilder paramsBuilder;
    if (!paramsBuilder.setAllocator(getAllocator())
             .setFrameCount(getFramesInFlight())
             .setSize(leafPhysicsParamsSize)
             .build(paramsBuffers)) {
        SDL_Log("Failed to create leaf physics params buffers");
        return false;
    }

    // Create displacement region uniform buffers (per-frame)
    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(getAllocator())
            .setFrameCount(getFramesInFlight())
            .setSize(sizeof(glm::vec4))  // regionCenterAndSize
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO)
            .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                               VMA_ALLOCATION_CREATE_MAPPED_BIT)
            .build(displacementRegionBuffers)) {
        SDL_Log("Failed to create leaf displacement region buffers");
        return false;
    }

    return true;
}

bool LeafSystem::createComputeDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles) {
    // 0: Particle buffer input (previous frame state)
    // 1: Particle buffer output (current frame result)
    // 2: Indirect buffer (output)
    // 3: CullingUniforms (shared culling parameters)
    // 4: Wind uniforms
    // 5: Terrain heightmap
    // 6: Displacement map (shared with grass system for player interaction)
    // 7: Displacement region uniform buffer
    // 8: Tile array (high-res terrain tiles near camera)
    // 9: Tile info buffer
    // 10: LeafPhysicsParams (leaf-specific physics parameters)

    handles.descriptorSetLayout = DescriptorManager::LayoutBuilder(getDevice())
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 0: Particle buffer input
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 1: Particle buffer output
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 2: Indirect buffer
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 3: CullingUniforms
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 4: Wind uniforms
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)    // 5: Terrain heightmap
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)    // 6: Displacement map
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 7: Displacement region
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)    // 8: Tile array
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 9: Tile info
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 10: LeafPhysicsParams
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

    vk::Device vkDevice(getDevice());

    handles.pipelineLayout = DescriptorManager::createPipelineLayout(
        getDevice(), handles.descriptorSetLayout, {pushConstantRange});
    if (handles.pipelineLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create leaf compute pipeline layout");
        vkDevice.destroyShaderModule(*compShaderModule);
        return false;
    }

    auto pipelineStageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*compShaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(pipelineStageInfo)
        .setLayout(handles.pipelineLayout);

    auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
    vkDevice.destroyShaderModule(*compShaderModule);

    if (result.result != vk::Result::eSuccess) {
        SDL_Log("Failed to create leaf compute pipeline");
        return false;
    }
    handles.pipeline = result.value;

    return true;
}

bool LeafSystem::createGraphicsDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles) {
    // 0: UBO (scene uniforms) - DYNAMIC to avoid per-frame descriptor updates
    // 1: Particle buffer (read-only in vertex shader)
    // 2: Wind uniforms (for consistent animation)

    handles.descriptorSetLayout = DescriptorManager::LayoutBuilder(getDevice())
        .addDynamicUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)  // 0: UBO (dynamic)
        .addStorageBuffer(VK_SHADER_STAGE_VERTEX_BIT)                                         // 1: Particle buffer
        .addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT)                                         // 2: Wind uniforms
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

    vk::Device vkDevice(getDevice());

    if (!vertShaderModule || !fragShaderModule) {
        SDL_Log("Failed to create leaf shader modules");
        if (vertShaderModule) vkDevice.destroyShaderModule(*vertShaderModule);
        if (fragShaderModule) vkDevice.destroyShaderModule(*fragShaderModule);
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
        vkDevice.destroyShaderModule(*fragShaderModule);
        vkDevice.destroyShaderModule(*vertShaderModule);
        return false;
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> vkShaderStages = {{
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setModule(*vertShaderModule)
            .setPName("main"),
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setModule(*fragShaderModule)
            .setPName("main")
    }};

    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setStages(vkShaderStages)
        .setPVertexInputState(reinterpret_cast<const vk::PipelineVertexInputStateCreateInfo*>(&vertexInputInfo))
        .setPInputAssemblyState(reinterpret_cast<const vk::PipelineInputAssemblyStateCreateInfo*>(&inputAssembly))
        .setPViewportState(reinterpret_cast<const vk::PipelineViewportStateCreateInfo*>(&viewportState))
        .setPRasterizationState(reinterpret_cast<const vk::PipelineRasterizationStateCreateInfo*>(&rasterizer))
        .setPMultisampleState(reinterpret_cast<const vk::PipelineMultisampleStateCreateInfo*>(&multisampling))
        .setPDepthStencilState(reinterpret_cast<const vk::PipelineDepthStencilStateCreateInfo*>(&depthStencil))
        .setPColorBlendState(reinterpret_cast<const vk::PipelineColorBlendStateCreateInfo*>(&colorBlending))
        .setPDynamicState(reinterpret_cast<const vk::PipelineDynamicStateCreateInfo*>(&dynamicState))
        .setLayout(handles.pipelineLayout)
        .setRenderPass(getRenderPass())
        .setSubpass(0);

    auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
    vkDevice.destroyShaderModule(*fragShaderModule);
    vkDevice.destroyShaderModule(*vertShaderModule);

    if (result.result != vk::Result::eSuccess) {
        SDL_Log("Failed to create leaf graphics pipeline");
        return false;
    }
    handles.pipeline = result.value;

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
                                       VkSampler displacementMapSamplerParam,
                                       VkImageView tileArrayView,
                                       VkSampler tileSampler,
                                       const std::array<VkBuffer, 3>& tileInfoBuffersParam,
                                       const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO) {
    // Store displacement texture references
    this->displacementMapView = displacementMapViewParam;
    this->displacementMapSampler = displacementMapSamplerParam;

    // Store tile info buffers (triple-buffered for frames-in-flight sync)
    tileInfoBuffers_.resize(tileInfoBuffersParam.size());
    for (size_t i = 0; i < tileInfoBuffersParam.size(); ++i) {
        tileInfoBuffers_[i] = tileInfoBuffersParam[i];
    }

    // Store renderer uniform buffers (kept for backward compatibility)
    this->rendererUniformBuffers_ = rendererUniformBuffers;

    // Store dynamic renderer UBO reference for per-frame binding with dynamic offsets
    this->dynamicRendererUBO_ = dynamicRendererUBO;

    // Update compute and graphics descriptor sets for all buffer sets
    // Note: tile info buffer (binding 9) is updated per-frame in recordResetAndCompute
    uint32_t bufferSetCount = particleSystem->getBufferSetCount();
    for (uint32_t set = 0; set < bufferSetCount; set++) {
        // For triple buffering, input is the previous buffer set (wraps around)
        uint32_t inputSet = (set == 0) ? (bufferSetCount - 1) : (set - 1);
        uint32_t outputSet = set;

        // Compute descriptor set - use non-fluent pattern to avoid copy semantics bug
        DescriptorManager::SetWriter computeWriter(dev, particleSystem->getComputeDescriptorSet(set));
        computeWriter.writeBuffer(0, particleBuffers.buffers[inputSet], 0, sizeof(LeafParticle) * MAX_PARTICLES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        computeWriter.writeBuffer(1, particleBuffers.buffers[outputSet], 0, sizeof(LeafParticle) * MAX_PARTICLES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        computeWriter.writeBuffer(2, indirectBuffers.buffers[outputSet], 0, sizeof(VkDrawIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        computeWriter.writeBuffer(3, uniformBuffers.buffers[0], 0, sizeof(CullingUniforms));
        computeWriter.writeBuffer(4, windBuffers[0], 0, 32);  // sizeof(WindUniforms)
        computeWriter.writeImage(5, terrainHeightMapView, terrainHeightMapSampler);
        computeWriter.writeImage(6, displacementMapViewParam, displacementMapSamplerParam);
        computeWriter.writeBuffer(7, displacementRegionBuffers.buffers[0], 0, sizeof(glm::vec4));
        computeWriter.writeBuffer(10, paramsBuffers.buffers[0], 0, sizeof(LeafPhysicsParams));

        // Tile cache bindings (8 and 9) - for high-res terrain sampling
        if (tileArrayView != VK_NULL_HANDLE && tileSampler != VK_NULL_HANDLE) {
            computeWriter.writeImage(8, tileArrayView, tileSampler);
        }
        // Write initial tile info buffer (frame 0) - will be updated per-frame
        if (!tileInfoBuffers_.empty() && tileInfoBuffers_[0] != VK_NULL_HANDLE) {
            computeWriter.writeBuffer(9, tileInfoBuffers_[0], 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        computeWriter.update();

        // Graphics descriptor set - use non-fluent pattern
        // Use dynamic UBO if available (avoids per-frame descriptor updates)
        DescriptorManager::SetWriter graphicsWriter(dev, particleSystem->getGraphicsDescriptorSet(set));
        if (dynamicRendererUBO && dynamicRendererUBO->isValid()) {
            graphicsWriter.writeBuffer(0, dynamicRendererUBO->buffer, 0, dynamicRendererUBO->alignedSize,
                                       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
        } else {
            graphicsWriter.writeBuffer(0, rendererUniformBuffers[0], 0, 320,
                                       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);  // sizeof(UniformBufferObject)
        }
        graphicsWriter.writeBuffer(1, particleBuffers.buffers[set], 0, sizeof(LeafParticle) * MAX_PARTICLES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        graphicsWriter.writeBuffer(2, windBuffers[0], 0, 32);  // sizeof(WindUniforms)
        graphicsWriter.update();
    }
}

void LeafSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                                 const glm::mat4& viewProj, const glm::vec3& playerPos,
                                 const glm::vec3& playerVel, float deltaTime, float totalTime,
                                 float terrainSize, float terrainHeightScale) {
    const EnvironmentSettings fallbackSettings{};
    const EnvironmentSettings& settings = environmentSettings ? *environmentSettings : fallbackSettings;

    // Fill CullingUniforms (shared culling parameters)
    CullingUniforms culling{};
    culling.cameraPosition = glm::vec4(cameraPos, 1.0f);
    extractFrustumPlanes(viewProj, culling.frustumPlanes);
    culling.maxDrawDistance = 60.0f;
    culling.lodTransitionStart = 40.0f;
    culling.lodTransitionEnd = 60.0f;
    culling.maxLodDropRate = 0.5f;
    memcpy(uniformBuffers.mappedPointers[frameIndex], &culling, sizeof(CullingUniforms));

    // Fill LeafPhysicsParams (leaf-specific physics parameters)
    LeafPhysicsParams params{};

    // Player data for disruption
    params.playerPosition = glm::vec4(playerPos, 0.5f);  // w = player collision radius
    float playerSpeed = glm::length(playerVel);
    params.playerVelocity = glm::vec4(playerVel, playerSpeed);

    // Spawn region
    params.spawnRegionMin = glm::vec4(spawnRegionMin, 0.0f);
    params.spawnRegionMax = glm::vec4(spawnRegionMax, 0.0f);

    // Confetti spawn parameters
    params.confettiSpawnPos = glm::vec4(confettiSpawnPosition, confettiConeAngle);
    params.confettiSpawnCount = confettiToSpawn;
    params.confettiVelocity = confettiSpawnVelocity;

    // General parameters
    params.groundLevel = groundLevel;
    params.deltaTime = deltaTime;
    params.time = totalTime;

    // Disruption parameters
    params.disruptionRadius = settings.leafDisruptionRadius;
    params.disruptionStrength = settings.leafDisruptionStrength;
    params.gustThreshold = settings.leafGustLiftThreshold;

    // Target counts based on intensity
    params.targetFallingCount = leafIntensity * 5000.0f;   // 0-5000 falling leaves
    params.targetGroundedCount = leafIntensity * 20000.0f; // 0-20000 grounded leaves

    // Terrain parameters
    params.terrainSize = terrainSize;
    params.terrainHeightScale = terrainHeightScale;

    memcpy(paramsBuffers.mappedPointers[frameIndex], &params, sizeof(LeafPhysicsParams));

    // Update displacement region to follow camera (same as grass system)
    displacementRegionCenter = glm::vec2(cameraPos.x, cameraPos.z);

    // Update displacement region uniform buffer
    glm::vec4 dispRegionData(displacementRegionCenter.x, displacementRegionCenter.y,
                             DISPLACEMENT_REGION_SIZE, 0.0f);
    memcpy(displacementRegionBuffers.mappedPointers[frameIndex], &dispRegionData, sizeof(glm::vec4));

    // Reset confetti spawn count after it's been sent to GPU
    confettiToSpawn = 0.0f;
}

void LeafSystem::recordResetAndCompute(VkCommandBuffer cmd, uint32_t frameIndex, float time, float deltaTime) {
    uint32_t writeSet = particleSystem->getComputeBufferSet();

    // Update compute descriptor set to use this frame's uniform, displacement region, params, and tile info buffers
    DescriptorManager::SetWriter writer(getDevice(), particleSystem->getComputeDescriptorSet(writeSet));
    writer.writeBuffer(3, uniformBuffers.buffers[frameIndex], 0, sizeof(CullingUniforms))
          .writeBuffer(7, displacementRegionBuffers.buffers[frameIndex], 0, sizeof(glm::vec4))
          .writeBuffer(10, paramsBuffers.buffers[frameIndex], 0, sizeof(LeafPhysicsParams));

    // Update tile info buffer to the correct frame's buffer (triple-buffered to avoid CPU-GPU sync)
    if (!tileInfoBuffers_.empty() && tileInfoBuffers_.at(frameIndex) != VK_NULL_HANDLE) {
        writer.writeBuffer(9, tileInfoBuffers_.at(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }
    writer.update();

    // Ensure CPU writes to tile info buffer are visible to GPU before compute dispatch
    // The tile info buffer is written by CPU in TerrainTileCache::updateTileInfoBuffer()
    Barriers::hostToCompute(cmd);

    // Reset indirect buffer before compute dispatch
    Barriers::clearBufferForCompute(cmd, indirectBuffers.buffers[writeSet], 0, sizeof(VkDrawIndirectCommand));

    // Dispatch leaf compute shader
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, getComputePipelineHandles().pipeline);
    VkDescriptorSet computeSet = particleSystem->getComputeDescriptorSet(writeSet);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                             getComputePipelineHandles().pipelineLayout, 0, vk::DescriptorSet(computeSet), {});

    LeafPushConstants pushConstants{};
    pushConstants.time = time;
    pushConstants.deltaTime = deltaTime;
    vkCmd.pushConstants<LeafPushConstants>(getComputePipelineHandles().pipelineLayout,
                                           vk::ShaderStageFlagBits::eCompute, 0, pushConstants);

    // Dispatch: ceil(MAX_PARTICLES / WORKGROUP_SIZE) workgroups
    uint32_t workgroupCount = (MAX_PARTICLES + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    vkCmd.dispatch(workgroupCount, 1, 1);

    // Memory barrier: compute write -> vertex shader read and indirect read
    Barriers::computeToIndirectDraw(cmd);
}

void LeafSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    // Double-buffer: graphics reads from renderBufferSet (previous frame's compute output)
    uint32_t readSet = particleSystem->getRenderBufferSet();

    // Dynamic UBO: no per-frame descriptor update needed - we pass the offset at bind time instead
    // This eliminates per-frame vkUpdateDescriptorSets calls for the renderer UBO

    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, getGraphicsPipelineHandles().pipeline);

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
                                getGraphicsPipelineHandles().pipelineLayout, 0, 1,
                                &graphicsSet, 1, &dynamicOffset);
    } else {
        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                 getGraphicsPipelineHandles().pipelineLayout, 0, vk::DescriptorSet(graphicsSet), {});
    }

    LeafPushConstants pushConstants{};
    pushConstants.time = time;
    pushConstants.deltaTime = 0.0f;  // Not needed for rendering
    vkCmd.pushConstants<LeafPushConstants>(getGraphicsPipelineHandles().pipelineLayout,
                                           vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                           0, pushConstants);

    // Indirect draw: 4 vertices per leaf (quad)
    vkCmd.drawIndirect(indirectBuffers.buffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
}

void LeafSystem::advanceBufferSet() { particleSystem->advanceBufferSet(); }
