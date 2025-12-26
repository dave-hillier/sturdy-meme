#include "TreeLODSystem.h"
#include "TreeSystem.h"
#include "TreeOptions.h"
#include "Mesh.h"
#include "ShaderLoader.h"
#include "core/DescriptorManager.h"
#include "shaders/bindings.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <algorithm>
#include <limits>

std::unique_ptr<TreeLODSystem> TreeLODSystem::create(const InitInfo& info) {
    auto system = std::unique_ptr<TreeLODSystem>(new TreeLODSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

TreeLODSystem::~TreeLODSystem() {
    if (device_ == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device_);

    // billboardVertexBuffer_ and billboardIndexBuffer_ are ManagedBuffer (RAII - auto-cleanup)
    BufferUtils::destroyBuffers(allocator_, instanceBuffers_);
}

bool TreeLODSystem::initInternal(const InitInfo& info) {
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    hdrRenderPass_ = info.hdrRenderPass;
    shadowRenderPass_ = info.shadowRenderPass;
    commandPool_ = info.commandPool;
    graphicsQueue_ = info.graphicsQueue;
    descriptorPool_ = info.descriptorPool;
    resourcePath_ = info.resourcePath;
    extent_ = info.extent;
    maxFramesInFlight_ = info.maxFramesInFlight;
    shadowMapSize_ = info.shadowMapSize;

    // Create impostor atlas
    TreeImpostorAtlas::InitInfo atlasInfo{};
    atlasInfo.device = device_;
    atlasInfo.physicalDevice = physicalDevice_;
    atlasInfo.allocator = allocator_;
    atlasInfo.commandPool = commandPool_;
    atlasInfo.graphicsQueue = graphicsQueue_;
    atlasInfo.descriptorPool = info.descriptorPool;
    atlasInfo.resourcePath = resourcePath_;

    impostorAtlas_ = TreeImpostorAtlas::create(atlasInfo);
    if (!impostorAtlas_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem: Failed to create impostor atlas");
        return false;
    }

    if (!createBillboardMesh()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem: Failed to create billboard mesh");
        return false;
    }

    if (!createDescriptorSetLayout()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem: Failed to create descriptor set layout");
        return false;
    }

    if (!createPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem: Failed to create pipeline");
        return false;
    }

    if (!allocateDescriptorSets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem: Failed to allocate descriptor sets");
        return false;
    }

    // Initialize shadow pipeline if shadow render pass is provided
    if (shadowRenderPass_ != VK_NULL_HANDLE) {
        if (!createShadowDescriptorSetLayout()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem: Failed to create shadow descriptor set layout");
            return false;
        }

        if (!createShadowPipeline()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem: Failed to create shadow pipeline");
            return false;
        }

        if (!allocateShadowDescriptorSets()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem: Failed to allocate shadow descriptor sets");
            return false;
        }
    }

    // Create initial instance buffer
    if (!createInstanceBuffer(256)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem: Failed to create instance buffer");
        return false;
    }

    SDL_Log("TreeLODSystem: Initialized successfully");
    return true;
}

bool TreeLODSystem::createBillboardMesh() {
    // Simple quad for billboard rendering
    // Centered horizontally, bottom at origin
    struct BillboardVertex {
        glm::vec3 position;
        glm::vec2 texCoord;
    };

    std::array<BillboardVertex, 4> vertices = {{
        {{-0.5f, 0.0f, 0.0f}, {0.0f, 1.0f}},  // Bottom-left
        {{ 0.5f, 0.0f, 0.0f}, {1.0f, 1.0f}},  // Bottom-right
        {{ 0.5f, 1.0f, 0.0f}, {1.0f, 0.0f}},  // Top-right
        {{-0.5f, 1.0f, 0.0f}, {0.0f, 0.0f}},  // Top-left
    }};

    std::array<uint32_t, 6> indices = {0, 1, 2, 2, 3, 0};
    billboardIndexCount_ = 6;

    VkDeviceSize vertexSize = sizeof(vertices);
    VkDeviceSize indexSize = sizeof(indices);

    // Create vertex buffer (RAII - ManagedBuffer auto-cleanup)
    VkBufferCreateInfo vertexBufferInfo{};
    vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertexBufferInfo.size = vertexSize;
    vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo gpuAllocInfo{};
    gpuAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (!ManagedBuffer::create(allocator_, vertexBufferInfo, gpuAllocInfo, billboardVertexBuffer_)) {
        return false;
    }

    // Create index buffer (RAII - ManagedBuffer auto-cleanup)
    VkBufferCreateInfo indexBufferInfo{};
    indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indexBufferInfo.size = indexSize;
    indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (!ManagedBuffer::create(allocator_, indexBufferInfo, gpuAllocInfo, billboardIndexBuffer_)) {
        return false;
    }

    // Upload data via staging buffer helper
    std::vector<BufferUtils::StagingUpload> uploads = {
        {vertices.data(), vertexSize, billboardVertexBuffer_.get()},
        {indices.data(), indexSize, billboardIndexBuffer_.get()}
    };

    return BufferUtils::uploadViaStaging(allocator_, commandPool_, graphicsQueue_, uploads);
}

bool TreeLODSystem::createDescriptorSetLayout() {
    return DescriptorManager::LayoutBuilder(device_)
        .addBinding(BINDING_TREE_IMPOSTOR_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(BINDING_TREE_IMPOSTOR_ALBEDO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(BINDING_TREE_IMPOSTOR_NORMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(BINDING_TREE_IMPOSTOR_SHADOW_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(BINDING_TREE_IMPOSTOR_INSTANCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_SHADER_STAGE_VERTEX_BIT)
        .buildManaged(impostorDescriptorSetLayout_);
}

bool TreeLODSystem::createPipeline() {
    // Pipeline layout with push constants
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(glm::vec4) * 3;  // cameraPos, lodParams, atlasParams

    if (!DescriptorManager::createManagedPipelineLayout(
            device_, impostorDescriptorSetLayout_.get(), impostorPipelineLayout_, {pushConstant})) {
        return false;
    }

    // Load shaders
    std::string shaderPath = resourcePath_ + "/shaders/";
    auto vertModule = ShaderLoader::loadShaderModule(device_, shaderPath + "tree_impostor.vert.spv");
    auto fragModule = ShaderLoader::loadShaderModule(device_, shaderPath + "tree_impostor.frag.spv");

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem: Failed to load impostor shaders");
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = *vertModule;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = *fragModule;
    shaderStages[1].pName = "main";

    // Vertex input: billboard vertex + instance data
    std::array<VkVertexInputBindingDescription, 2> bindingDescriptions{};
    bindingDescriptions[0].binding = 0;
    bindingDescriptions[0].stride = sizeof(glm::vec3) + sizeof(glm::vec2);  // position + texcoord
    bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    bindingDescriptions[1].binding = 1;
    bindingDescriptions[1].stride = sizeof(ImpostorInstanceGPU);
    bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    std::array<VkVertexInputAttributeDescription, 10> attributeDescriptions{};
    // Per-vertex attributes
    attributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};  // position
    attributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec3)};  // texcoord

    // Per-instance attributes
    attributeDescriptions[2] = {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ImpostorInstanceGPU, position)};
    attributeDescriptions[3] = {3, 1, VK_FORMAT_R32_SFLOAT, offsetof(ImpostorInstanceGPU, scale)};
    attributeDescriptions[4] = {4, 1, VK_FORMAT_R32_SFLOAT, offsetof(ImpostorInstanceGPU, rotation)};
    attributeDescriptions[5] = {5, 1, VK_FORMAT_R32_UINT, offsetof(ImpostorInstanceGPU, archetypeIndex)};
    attributeDescriptions[6] = {6, 1, VK_FORMAT_R32_SFLOAT, offsetof(ImpostorInstanceGPU, blendFactor)};
    attributeDescriptions[7] = {7, 1, VK_FORMAT_R32_SFLOAT, offsetof(ImpostorInstanceGPU, hSize)};
    attributeDescriptions[8] = {8, 1, VK_FORMAT_R32_SFLOAT, offsetof(ImpostorInstanceGPU, vSize)};
    attributeDescriptions[9] = {9, 1, VK_FORMAT_R32_SFLOAT, offsetof(ImpostorInstanceGPU, baseOffset)};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // Billboard faces camera
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = impostorPipelineLayout_.get();
    pipelineInfo.renderPass = hdrRenderPass_;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

    vkDestroyShaderModule(device_, *vertModule, nullptr);
    vkDestroyShaderModule(device_, *fragModule, nullptr);

    if (result != VK_SUCCESS) {
        return false;
    }
    impostorPipeline_ = ManagedPipeline(makeUniquePipeline(device_, pipeline));

    return true;
}

bool TreeLODSystem::allocateDescriptorSets() {
    impostorDescriptorSets_ = descriptorPool_->allocate(impostorDescriptorSetLayout_.get(), maxFramesInFlight_);
    return !impostorDescriptorSets_.empty();
}

bool TreeLODSystem::createShadowDescriptorSetLayout() {
    // Shadow pass needs UBO (for cascade matrices), albedo atlas (for alpha testing),
    // and instance buffer (for GPU-culled rendering)
    return DescriptorManager::LayoutBuilder(device_)
        .addBinding(BINDING_TREE_IMPOSTOR_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_SHADER_STAGE_VERTEX_BIT)
        .addBinding(BINDING_TREE_IMPOSTOR_ALBEDO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(BINDING_TREE_IMPOSTOR_SHADOW_INSTANCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_SHADER_STAGE_VERTEX_BIT)
        .buildManaged(shadowDescriptorSetLayout_);
}

bool TreeLODSystem::createShadowPipeline() {
    // Push constants: cameraPos, lodParams, atlasParams, cascadeIndex
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(glm::vec4) * 3 + sizeof(int);  // cameraPos, lodParams, atlasParams, cascadeIndex

    if (!DescriptorManager::createManagedPipelineLayout(
            device_, shadowDescriptorSetLayout_.get(), shadowPipelineLayout_, {pushConstant})) {
        return false;
    }

    // Load shadow shaders
    std::string shaderPath = resourcePath_ + "/shaders/";
    auto vertModule = ShaderLoader::loadShaderModule(device_, shaderPath + "tree_impostor_shadow.vert.spv");
    auto fragModule = ShaderLoader::loadShaderModule(device_, shaderPath + "tree_impostor_shadow.frag.spv");

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem: Failed to load impostor shadow shaders");
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = *vertModule;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = *fragModule;
    shaderStages[1].pName = "main";

    // Vertex input: only billboard quad vertices (instances come from SSBO)
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(glm::vec3) + sizeof(glm::vec2);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
    attributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};                  // inPosition
    attributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec3)};     // inTexCoord

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Static viewport and scissor for shadow map
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(shadowMapSize_);
    viewport.height = static_cast<float>(shadowMapSize_);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {shadowMapSize_, shadowMapSize_};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // Billboard, no culling
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;  // Enable depth bias for shadow acne
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // No color attachment for shadow pass
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 0;

    // No dynamic state - viewport and scissor are static
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 0;
    dynamicState.pDynamicStates = nullptr;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = shadowPipelineLayout_.get();
    pipelineInfo.renderPass = shadowRenderPass_;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

    vkDestroyShaderModule(device_, *vertModule, nullptr);
    vkDestroyShaderModule(device_, *fragModule, nullptr);

    if (result != VK_SUCCESS) {
        return false;
    }
    shadowPipeline_ = ManagedPipeline(makeUniquePipeline(device_, pipeline));

    return true;
}

bool TreeLODSystem::allocateShadowDescriptorSets() {
    shadowDescriptorSets_ = descriptorPool_->allocate(shadowDescriptorSetLayout_.get(), maxFramesInFlight_);
    return !shadowDescriptorSets_.empty();
}

bool TreeLODSystem::createInstanceBuffer(size_t maxInstances) {
    maxInstances_ = maxInstances;
    instanceBufferSize_ = maxInstances * sizeof(ImpostorInstanceGPU);

    // Create per-frame instance buffers to avoid GPU race conditions
    return BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator_)
        .setFrameCount(maxFramesInFlight_)
        .setSize(instanceBufferSize_)
        .setUsage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .setMemoryUsage(VMA_MEMORY_USAGE_CPU_TO_GPU)
        .build(instanceBuffers_);
}

// Helper function to compute screen-space error
static float computeScreenError(float worldError, float distance, float screenHeight, float tanHalfFOV) {
    if (distance <= 0.0f) return 9999.0f;
    return worldError * screenHeight / (2.0f * distance * tanHalfFOV);
}

void TreeLODSystem::update(uint32_t frameIndex, float deltaTime, const glm::vec3& cameraPos, const TreeSystem& treeSystem,
                           const ScreenParams& screenParams) {
    currentFrameIndex_ = frameIndex;
    const auto& settings = getLODSettings();
    const auto& instances = treeSystem.getTreeInstances();

    // Resize LOD states if needed
    if (lodStates_.size() != instances.size()) {
        lodStates_.resize(instances.size());
    }

    // Number of archetypes (display trees define the archetypes)
    const uint32_t numArchetypes = static_cast<uint32_t>(impostorAtlas_->getArchetypeCount());
    const uint32_t numDisplayTrees = 4;  // oak, pine, ash, aspen

    visibleImpostors_.clear();

    for (size_t i = 0; i < instances.size(); i++) {
        const auto& tree = instances[i];
        auto& state = lodStates_[i];

        // Skip forest trees if forest is disabled
        if (i >= numDisplayTrees && !settings.enableForest) {
            state.currentLevel = TreeLODState::Level::FullDetail;
            state.targetLevel = TreeLODState::Level::FullDetail;
            state.blendFactor = 0.0f;
            continue;
        }

        // Assign archetype index based on tree type
        // Display trees (0-3) map directly to their archetype
        // Forest trees (4+) cycle through archetypes
        if (numArchetypes > 0) {
            if (i < numDisplayTrees) {
                state.archetypeIndex = static_cast<uint32_t>(i) % numArchetypes;
            } else {
                state.archetypeIndex = static_cast<uint32_t>((i - numDisplayTrees) % numArchetypes);
            }
        }

        float distance = glm::distance(cameraPos, tree.position);
        state.lastDistance = distance;

        // Handle Simple LOD Mode - binary selection with no blending
        if (settings.simpleLODMode == SimpleLODMode::FullDetail) {
            state.currentLevel = TreeLODState::Level::FullDetail;
            state.targetLevel = TreeLODState::Level::FullDetail;
            state.blendFactor = 0.0f;
            continue;
        } else if (settings.simpleLODMode == SimpleLODMode::Impostor) {
            state.currentLevel = TreeLODState::Level::Impostor;
            state.targetLevel = TreeLODState::Level::Impostor;
            state.blendFactor = 1.0f;
            // In forced Impostor mode, always add to impostor list (ignore enableImpostors flag)
            if (state.archetypeIndex < impostorAtlas_->getArchetypeCount()) {
                ImpostorInstanceGPU instance{};
                instance.position = tree.position;
                instance.scale = tree.scale;
                instance.rotation = tree.rotation;
                instance.archetypeIndex = static_cast<float>(state.archetypeIndex);
                instance.blendFactor = 1.0f;

                // Use actual tree mesh bounds
                if (tree.meshIndex < treeSystem.getMeshCount()) {
                    const auto& meshBounds = treeSystem.getBranchMesh(tree.meshIndex).getBounds();
                    glm::vec3 minB = meshBounds.min;
                    glm::vec3 maxB = meshBounds.max;
                    glm::vec3 extent = maxB - minB;
                    float horizontalRadius = std::max(extent.x, extent.z) * 0.5f;
                    float treeHeight = extent.y;
                    float halfHeight = treeHeight * 0.5f;
                    float boundingSphereRadius = glm::length(extent) * 0.5f;
                    // Use separate horizontal and vertical sizes
                    // hSize based on canopy radius only (not bounding sphere which includes height)
                    // vSize uses smaller margin - tree doesn't need much vertical padding
                    float hSize = horizontalRadius * 1.15f * tree.scale;
                    float vSize = halfHeight * 1.02f * tree.scale;  // Only 2% margin
                    instance.hSize = hSize;
                    instance.vSize = vSize;
                    // Position billboard so bottom is at tree base
                    // Billboard extends from baseOffset-vSize to baseOffset+vSize
                    // We want bottom at minB.y, so: baseOffset - vSize = minB.y
                    instance.baseOffset = (minB.y * tree.scale) + vSize;
                } else {
                    const auto* archetype = impostorAtlas_->getArchetype(state.archetypeIndex);
                    float projSize = (archetype ? archetype->boundingSphereRadius * 1.15f : 10.0f) * tree.scale;
                    instance.hSize = projSize;
                    instance.vSize = projSize;
                    // Use archetype's center height for positioning
                    instance.baseOffset = (archetype ? archetype->centerHeight : projSize) * tree.scale;
                }
                visibleImpostors_.push_back(instance);
            }
            continue;
        }

        // Auto mode - determine target LOD level and blend factor using screen-space error
        const auto* archetype = impostorAtlas_->getArchetype(state.archetypeIndex);
        float worldErrorFull = 0.1f * tree.scale;  // ~10cm branch thickness, scaled
        float screenErrorFull = computeScreenError(worldErrorFull, distance,
                                                    screenParams.screenHeight, screenParams.tanHalfFOV);

        // Determine LOD level: high screen error = close = full geometry
        if (screenErrorFull > settings.errorThresholdFull) {
            state.targetLevel = TreeLODState::Level::FullDetail;
            state.blendFactor = 0.0f;
        } else if (screenErrorFull < settings.errorThresholdImpostor) {
            state.targetLevel = TreeLODState::Level::Impostor;
            state.blendFactor = 1.0f;
        } else {
            // Blend zone
            state.targetLevel = TreeLODState::Level::Blending;
            float t = (settings.errorThresholdFull - screenErrorFull) /
                      (settings.errorThresholdFull - settings.errorThresholdImpostor);
            state.blendFactor = t * t * (3.0f - 2.0f * t);  // smoothstep
        }

        // Determine current level based on blend factor
        if (state.blendFactor < 0.01f) {
            state.currentLevel = TreeLODState::Level::FullDetail;
        } else if (state.blendFactor > 0.99f) {
            state.currentLevel = TreeLODState::Level::Impostor;
        } else {
            state.currentLevel = TreeLODState::Level::Blending;
        }

        // Skip CPU impostor list building when GPU culling handles it
        // GPU culling (ImpostorCullSystem) already computes visibility, LOD, and sizing
        if (gpuCullingEnabled_) {
            continue;
        }

        // Collect visible impostors (CPU fallback path only)
        if (settings.enableImpostors && state.blendFactor > 0.0f && state.archetypeIndex < impostorAtlas_->getArchetypeCount()) {
            ImpostorInstanceGPU instance{};
            instance.position = tree.position;
            instance.scale = tree.scale;
            instance.rotation = tree.rotation;
            instance.archetypeIndex = static_cast<float>(state.archetypeIndex);
            instance.blendFactor = state.blendFactor;

            // Use actual tree mesh bounds instead of archetype bounds
            // Each tree has its own procedurally generated mesh with potentially different bounds
            if (tree.meshIndex < treeSystem.getMeshCount()) {
                const auto& meshBounds = treeSystem.getBranchMesh(tree.meshIndex).getBounds();
                glm::vec3 minB = meshBounds.min;
                glm::vec3 maxB = meshBounds.max;
                glm::vec3 extent = maxB - minB;

                float horizontalRadius = std::max(extent.x, extent.z) * 0.5f;
                float treeHeight = extent.y;
                float halfHeight = treeHeight * 0.5f;
                float boundingSphereRadius = glm::length(extent) * 0.5f;

                // Use separate horizontal and vertical sizes
                // hSize based on canopy radius only (not bounding sphere which includes height)
                // vSize uses smaller margin - tree doesn't need much vertical padding
                float hSize = horizontalRadius * 1.15f * tree.scale;
                float vSize = halfHeight * 1.02f * tree.scale;  // Only 2% margin
                instance.hSize = hSize;
                instance.vSize = vSize;
                // Position billboard so bottom is at tree base
                instance.baseOffset = (minB.y * tree.scale) + vSize;
            } else {
                // Fallback to archetype bounds if mesh not available
                const auto* archetype = impostorAtlas_->getArchetype(state.archetypeIndex);
                float projSize = (archetype ? archetype->boundingSphereRadius * 1.15f : 10.0f) * tree.scale;
                instance.hSize = projSize;
                instance.vSize = projSize;
                // Use archetype's center height for positioning
                instance.baseOffset = (archetype ? archetype->centerHeight : projSize) * tree.scale;
            }
            visibleImpostors_.push_back(instance);
        }
    }

    lastCameraPos_ = cameraPos;

    // Update instance buffer for CPU rendering path
    // Always update when in forced Impostor mode, or when GPU culling is disabled
    bool needsCPUInstanceBuffer = !gpuCullingEnabled_ ||
                                   settings.simpleLODMode == SimpleLODMode::Impostor;
    if (needsCPUInstanceBuffer && !visibleImpostors_.empty()) {
        updateInstanceBuffer(visibleImpostors_);
    }

    // Skip debug info calculation when GPU culling is enabled (expensive O(n) loop)
    if (!gpuCullingEnabled_) {
        // Update debug info - find nearest tree and calculate elevation
        debugInfo_.cameraPos = cameraPos;
        debugInfo_.nearestTreeDistance = std::numeric_limits<float>::max();
        for (const auto& tree : instances) {
            float dist = glm::distance(cameraPos, tree.position);
            if (dist < debugInfo_.nearestTreeDistance) {
                debugInfo_.nearestTreeDistance = dist;
                debugInfo_.nearestTreePos = tree.position;

                // Calculate elevation angle (same as shader)
                glm::vec3 toTree = tree.position - cameraPos;
                float toTreeDist = glm::length(toTree);
                if (toTreeDist > 0.001f) {
                    debugInfo_.calculatedElevation = glm::degrees(std::asin(glm::clamp(-toTree.y / toTreeDist, -1.0f, 1.0f)));
                }
            }
        }
    }
}

void TreeLODSystem::updateInstanceBuffer(const std::vector<ImpostorInstanceGPU>& instances) {
    if (instances.empty()) return;

    // Resize buffers if needed (recreates all per-frame buffers)
    if (instances.size() > maxInstances_) {
        BufferUtils::destroyBuffers(allocator_, instanceBuffers_);
        createInstanceBuffer(instances.size() * 2);
    }

    // Upload instance data to current frame's buffer
    if (currentFrameIndex_ < instanceBuffers_.buffers.size()) {
        void* data;
        vmaMapMemory(allocator_, instanceBuffers_.allocations[currentFrameIndex_], &data);
        memcpy(data, instances.data(), instances.size() * sizeof(ImpostorInstanceGPU));
        vmaUnmapMemory(allocator_, instanceBuffers_.allocations[currentFrameIndex_]);
    }
}

void TreeLODSystem::updateDescriptorSets(uint32_t frameIndex, VkBuffer uniformBuffer,
                                          VkImageView shadowMap, VkSampler shadowSampler) {
    // Validate frame index
    if (frameIndex >= impostorDescriptorSets_.size() ||
        frameIndex >= instanceBuffers_.buffers.size()) {
        return;
    }

    // Use the shared array views that contain all archetypes
    VkImageView albedoView = impostorAtlas_->getAlbedoAtlasArrayView();
    VkImageView normalView = impostorAtlas_->getNormalAtlasArrayView();
    VkSampler atlasSampler = impostorAtlas_->getAtlasSampler();

    if (albedoView == VK_NULL_HANDLE || normalView == VK_NULL_HANDLE) return;
    if (instanceBuffers_.buffers[frameIndex] == VK_NULL_HANDLE) return;

    DescriptorManager::SetWriter(device_, impostorDescriptorSets_[frameIndex])
        .writeBuffer(BINDING_TREE_IMPOSTOR_UBO, uniformBuffer, 0, VK_WHOLE_SIZE)
        .writeImage(BINDING_TREE_IMPOSTOR_ALBEDO, albedoView, atlasSampler)
        .writeImage(BINDING_TREE_IMPOSTOR_NORMAL, normalView, atlasSampler)
        .writeImage(BINDING_TREE_IMPOSTOR_SHADOW_MAP, shadowMap, shadowSampler)
        .writeBuffer(BINDING_TREE_IMPOSTOR_INSTANCES, instanceBuffers_.buffers[frameIndex],
                     0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .update();
}

void TreeLODSystem::renderImpostors(VkCommandBuffer cmd, uint32_t frameIndex,
                                     VkBuffer uniformBuffer, VkImageView shadowMap, VkSampler shadowSampler,
                                     VkBuffer gpuInstanceBuffer, VkBuffer indirectDrawBuffer) {
    if (impostorAtlas_->getArchetypeCount() == 0) return;

    const auto& settings = getLODSettings();
    // Skip enableImpostors check when in forced Impostor mode
    if (settings.simpleLODMode != SimpleLODMode::Impostor && !settings.enableImpostors) return;

    // Determine if using GPU-culled path
    bool useGPUPath = (gpuInstanceBuffer != VK_NULL_HANDLE && indirectDrawBuffer != VK_NULL_HANDLE);

    // For CPU path, check we have visible impostors
    if (!useGPUPath && visibleImpostors_.empty()) return;

    // Ensure atlas textures are ready
    VkImageView albedoView = impostorAtlas_->getAlbedoAtlasArrayView();
    VkImageView normalView = impostorAtlas_->getNormalAtlasArrayView();
    VkSampler atlasSampler = impostorAtlas_->getAtlasSampler();
    if (albedoView == VK_NULL_HANDLE || normalView == VK_NULL_HANDLE) return;

    // For CPU path, ensure instance buffers are valid
    if (!useGPUPath) {
        if (frameIndex >= instanceBuffers_.buffers.size() ||
            instanceBuffers_.buffers[frameIndex] == VK_NULL_HANDLE) {
            return;
        }
    }

    // Update descriptor sets with appropriate instance buffer
    VkBuffer instanceBuffer = useGPUPath ? gpuInstanceBuffer : instanceBuffers_.buffers[frameIndex];
    DescriptorManager::SetWriter(device_, impostorDescriptorSets_[frameIndex])
        .writeBuffer(BINDING_TREE_IMPOSTOR_UBO, uniformBuffer, 0, VK_WHOLE_SIZE)
        .writeImage(BINDING_TREE_IMPOSTOR_ALBEDO, albedoView, atlasSampler)
        .writeImage(BINDING_TREE_IMPOSTOR_NORMAL, normalView, atlasSampler)
        .writeImage(BINDING_TREE_IMPOSTOR_SHADOW_MAP, shadowMap, shadowSampler)
        .writeBuffer(BINDING_TREE_IMPOSTOR_INSTANCES, instanceBuffer,
                     0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .update();

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impostorPipeline_.get());

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent_;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind descriptor sets
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impostorPipelineLayout_.get(),
                           0, 1, &impostorDescriptorSets_[frameIndex], 0, nullptr);

    // Push constants
    struct {
        glm::vec4 cameraPos;
        glm::vec4 lodParams;
        glm::vec4 atlasParams;
    } pushConstants;

    pushConstants.cameraPos = glm::vec4(lastCameraPos_, settings.autumnHueShift);
    pushConstants.lodParams = glm::vec4(
        settings.useOctahedralMapping ? 1.0f : 0.0f,
        settings.impostorBrightness,
        settings.normalStrength,
        settings.enableDebugElevation ? settings.debugElevation : -999.0f
    );
    pushConstants.atlasParams = glm::vec4(
        settings.enableFrameBlending ? 1.0f : 0.0f,
        0.0f,
        0.0f,
        settings.debugShowCellIndex ? 1.0f : 0.0f
    );

    vkCmdPushConstants(cmd, impostorPipelineLayout_.get(),
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(pushConstants), &pushConstants);

    // Bind vertex and index buffers
    VkBuffer vb = billboardVertexBuffer_.get();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
    vkCmdBindIndexBuffer(cmd, billboardIndexBuffer_.get(), 0, VK_INDEX_TYPE_UINT32);

    // Draw - indirect for GPU path, direct for CPU path
    if (useGPUPath) {
        vkCmdDrawIndexedIndirect(cmd, indirectDrawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
    } else {
        vkCmdDrawIndexed(cmd, billboardIndexCount_, static_cast<uint32_t>(visibleImpostors_.size()), 0, 0, 0);
    }
}

void TreeLODSystem::renderImpostorShadows(VkCommandBuffer cmd, uint32_t frameIndex,
                                           int cascadeIndex, VkBuffer uniformBuffer,
                                           VkBuffer gpuInstanceBuffer, VkBuffer indirectDrawBuffer) {
    if (impostorAtlas_->getArchetypeCount() == 0) return;
    if (shadowPipeline_.get() == VK_NULL_HANDLE) return;
    if (uniformBuffer == VK_NULL_HANDLE) return;

    const auto& settings = getLODSettings();
    // Skip enableImpostors check when in forced Impostor mode
    if (settings.simpleLODMode != SimpleLODMode::Impostor && !settings.enableImpostors) return;

    // Determine if using GPU-culled path
    bool useGPUPath = (gpuInstanceBuffer != VK_NULL_HANDLE && indirectDrawBuffer != VK_NULL_HANDLE);

    // For CPU path, check we have visible impostors
    if (!useGPUPath && visibleImpostors_.empty()) return;

    // Update shadow descriptor set with UBO, albedo atlas, and instance buffer
    if (shadowDescriptorSets_.empty()) return;

    // For CPU path, ensure instance buffers are valid
    if (!useGPUPath) {
        if (frameIndex >= instanceBuffers_.buffers.size() ||
            instanceBuffers_.buffers[frameIndex] == VK_NULL_HANDLE) {
            return;
        }
    }

    VkImageView albedoView = impostorAtlas_->getAlbedoAtlasArrayView();
    VkSampler atlasSampler = impostorAtlas_->getAtlasSampler();
    if (albedoView == VK_NULL_HANDLE) return;

    // Update descriptor with appropriate instance buffer
    VkBuffer instanceBuffer = useGPUPath ? gpuInstanceBuffer : instanceBuffers_.buffers[frameIndex];
    DescriptorManager::SetWriter(device_, shadowDescriptorSets_[frameIndex])
        .writeBuffer(BINDING_TREE_IMPOSTOR_UBO, uniformBuffer, 0, VK_WHOLE_SIZE)
        .writeImage(BINDING_TREE_IMPOSTOR_ALBEDO, albedoView, atlasSampler)
        .writeBuffer(BINDING_TREE_IMPOSTOR_SHADOW_INSTANCES, instanceBuffer,
                     0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .update();

    // Bind shadow pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout_.get(),
                           0, 1, &shadowDescriptorSets_[frameIndex], 0, nullptr);

    // Push constants
    struct {
        glm::vec4 cameraPos;
        glm::vec4 lodParams;
        int cascadeIndex;
        float _pad[3];
    } pushConstants;
    pushConstants.cameraPos = glm::vec4(lastCameraPos_, 0.0f);
    pushConstants.lodParams = glm::vec4(settings.useOctahedralMapping ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
    pushConstants.cascadeIndex = cascadeIndex;

    vkCmdPushConstants(cmd, shadowPipelineLayout_.get(),
                      VK_SHADER_STAGE_VERTEX_BIT,
                      0, sizeof(pushConstants), &pushConstants);

    // Bind vertex and index buffers
    VkBuffer vb = billboardVertexBuffer_.get();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
    vkCmdBindIndexBuffer(cmd, billboardIndexBuffer_.get(), 0, VK_INDEX_TYPE_UINT32);

    // Draw - indirect for GPU path, direct for CPU path
    if (useGPUPath) {
        vkCmdDrawIndexedIndirect(cmd, indirectDrawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
    } else {
        vkCmdDrawIndexed(cmd, billboardIndexCount_, static_cast<uint32_t>(visibleImpostors_.size()), 0, 0, 0);
    }
}

const TreeLODState& TreeLODSystem::getTreeLODState(uint32_t treeIndex) const {
    static TreeLODState defaultState;
    if (treeIndex < lodStates_.size()) {
        return lodStates_[treeIndex];
    }
    return defaultState;
}

bool TreeLODSystem::shouldRenderFullGeometry(uint32_t treeIndex) const {
    if (treeIndex >= lodStates_.size()) return true;

    // Skip forest trees when forest is disabled
    const uint32_t numDisplayTrees = 4;
    const auto& settings = getLODSettings();
    if (treeIndex >= numDisplayTrees && !settings.enableForest) {
        return false;
    }

    const auto& state = lodStates_[treeIndex];
    return state.currentLevel == TreeLODState::Level::FullDetail ||
           state.currentLevel == TreeLODState::Level::Blending;
}

bool TreeLODSystem::shouldRenderImpostor(uint32_t treeIndex) const {
    if (treeIndex >= lodStates_.size()) return false;

    // Skip forest trees when forest is disabled
    const uint32_t numDisplayTrees = 4;
    const auto& settings = getLODSettings();
    if (treeIndex >= numDisplayTrees && !settings.enableForest) {
        return false;
    }

    const auto& state = lodStates_[treeIndex];
    return state.currentLevel == TreeLODState::Level::Impostor ||
           state.currentLevel == TreeLODState::Level::Blending;
}

float TreeLODSystem::getBlendFactor(uint32_t treeIndex) const {
    if (treeIndex >= lodStates_.size()) return 0.0f;
    return lodStates_[treeIndex].blendFactor;
}

int32_t TreeLODSystem::generateImpostor(const std::string& name, const TreeOptions& options,
                                         const Mesh& branchMesh,
                                         const std::vector<LeafInstanceGPU>& leafInstances,
                                         VkImageView barkAlbedo, VkImageView barkNormal,
                                         VkImageView leafAlbedo, VkSampler sampler) {
    return impostorAtlas_->generateArchetype(name, options, branchMesh, leafInstances,
                                              barkAlbedo, barkNormal, leafAlbedo, sampler);
}

void TreeLODSystem::updateTreeCount(size_t count) {
    lodStates_.resize(count);
}

void TreeLODSystem::setExtent(VkExtent2D newExtent) {
    extent_ = newExtent;
}
