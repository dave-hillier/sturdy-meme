#include "TreeLODSystem.h"
#include "TreeSystem.h"
#include "TreeOptions.h"
#include "CullCommon.h"
#include "Mesh.h"
#include "ShaderLoader.h"
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

    if (billboardVertexBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, billboardVertexBuffer_, billboardVertexAllocation_);
    }
    if (billboardIndexBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, billboardIndexBuffer_, billboardIndexAllocation_);
    }
    if (instanceBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, instanceBuffer_, instanceAllocation_);
    }
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

    // Create vertex buffer
    VkDeviceSize vertexSize = sizeof(vertices);
    VkBufferCreateInfo vertexBufferInfo{};
    vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertexBufferInfo.size = vertexSize;
    vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(allocator_, &vertexBufferInfo, &allocInfo,
                        &billboardVertexBuffer_, &billboardVertexAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Create index buffer
    VkDeviceSize indexSize = sizeof(indices);
    VkBufferCreateInfo indexBufferInfo{};
    indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indexBufferInfo.size = indexSize;
    indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (vmaCreateBuffer(allocator_, &indexBufferInfo, &allocInfo,
                        &billboardIndexBuffer_, &billboardIndexAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Upload data via staging buffer
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    VkDeviceSize stagingSize = vertexSize + indexSize;

    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = stagingSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    if (vmaCreateBuffer(allocator_, &stagingInfo, &stagingAllocInfo,
                        &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    void* data;
    vmaMapMemory(allocator_, stagingAllocation, &data);
    memcpy(data, vertices.data(), vertexSize);
    memcpy(static_cast<char*>(data) + vertexSize, indices.data(), indexSize);
    vmaUnmapMemory(allocator_, stagingAllocation);

    // Copy to GPU buffers
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool_;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy vertexCopy{};
    vertexCopy.size = vertexSize;
    vkCmdCopyBuffer(cmd, stagingBuffer, billboardVertexBuffer_, 1, &vertexCopy);

    VkBufferCopy indexCopy{};
    indexCopy.srcOffset = vertexSize;
    indexCopy.size = indexSize;
    vkCmdCopyBuffer(cmd, stagingBuffer, billboardIndexBuffer_, 1, &indexCopy);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);

    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);

    return true;
}

bool TreeLODSystem::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};

    // UBO
    bindings[0].binding = BINDING_TREE_IMPOSTOR_UBO;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Albedo atlas
    bindings[1].binding = BINDING_TREE_IMPOSTOR_ALBEDO;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Normal atlas
    bindings[2].binding = BINDING_TREE_IMPOSTOR_NORMAL;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Shadow map
    bindings[3].binding = BINDING_TREE_IMPOSTOR_SHADOW_MAP;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Instance buffer (SSBO for GPU-culled rendering)
    bindings[4].binding = BINDING_TREE_IMPOSTOR_INSTANCES;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        return false;
    }
    impostorDescriptorSetLayout_ = ManagedDescriptorSetLayout(makeUniqueDescriptorSetLayout(device_, layout));

    return true;
}

bool TreeLODSystem::createPipeline() {
    // Pipeline layout with push constants
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(glm::vec4) * 3;  // cameraPos, lodParams, atlasParams

    VkDescriptorSetLayout layouts[] = {impostorDescriptorSetLayout_.get()};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = layouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        return false;
    }
    impostorPipelineLayout_ = ManagedPipelineLayout(makeUniquePipelineLayout(device_, pipelineLayout));

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
    pipelineInfo.layout = pipelineLayout;
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
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

    // UBO
    bindings[0].binding = BINDING_TREE_IMPOSTOR_UBO;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Albedo atlas (for alpha testing in fragment shader)
    bindings[1].binding = BINDING_TREE_IMPOSTOR_ALBEDO;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Instance buffer (SSBO for GPU-culled rendering)
    bindings[2].binding = BINDING_TREE_IMPOSTOR_SHADOW_INSTANCES;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        return false;
    }
    shadowDescriptorSetLayout_ = ManagedDescriptorSetLayout(makeUniqueDescriptorSetLayout(device_, layout));

    return true;
}

bool TreeLODSystem::createShadowPipeline() {
    // Push constants: cameraPos, lodParams, atlasParams, cascadeIndex
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(glm::vec4) * 3 + sizeof(int);  // cameraPos, lodParams, atlasParams, cascadeIndex

    VkDescriptorSetLayout layouts[] = {shadowDescriptorSetLayout_.get()};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = layouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        return false;
    }
    shadowPipelineLayout_ = ManagedPipelineLayout(makeUniquePipelineLayout(device_, pipelineLayout));

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
    pipelineInfo.layout = pipelineLayout;
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

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = instanceBufferSize_;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    return vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                           &instanceBuffer_, &instanceAllocation_, nullptr) == VK_SUCCESS;
}

// computeScreenError is now in CullCommon.h

void TreeLODSystem::update(float deltaTime, const glm::vec3& cameraPos, const TreeSystem& treeSystem,
                           const ScreenParams& screenParams) {
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

        // Use the tree's stored archetype index (set based on leaf type in TreeSystem)
        if (numArchetypes > 0) {
            state.archetypeIndex = tree.archetypeIndex;
            if (state.archetypeIndex >= numArchetypes) {
                state.archetypeIndex = state.archetypeIndex % numArchetypes;
            }
        }

        float distance = glm::distance(cameraPos, tree.position);
        state.lastDistance = distance;

        // Determine target LOD level and blend factor
        TreeLODState::Level newTarget = state.targetLevel;

        if (settings.useScreenSpaceError) {
            // Screen-space error based LOD
            // Get archetype world error values
            const auto* archetype = impostorAtlas_->getArchetype(state.archetypeIndex);
            float worldErrorFull = 0.1f * tree.scale;  // ~10cm branch thickness, scaled
            float worldErrorImpostor = (archetype ? archetype->boundingSphereRadius * 0.1f : 1.0f) * tree.scale;

            float screenErrorFull = computeScreenError(worldErrorFull, distance,
                                                        screenParams.screenHeight, screenParams.tanHalfFOV);

            // Determine LOD level based on screen error
            // High screen error = close = needs full geometry
            // Low screen error = far = can use impostor
            if (screenErrorFull > settings.errorThresholdFull) {
                newTarget = TreeLODState::Level::FullDetail;
            } else {
                newTarget = TreeLODState::Level::Impostor;
            }

            // Compute blend factor based on screen error
            // blendFactor: 0.0 = full geometry only (close), 1.0 = impostor only (far)
            if (screenErrorFull > settings.errorThresholdFull) {
                state.blendFactor = 0.0f;  // Close: full geometry
            } else if (screenErrorFull < settings.errorThresholdImpostor) {
                state.blendFactor = 1.0f;  // Far: full impostor
            } else {
                // Blend zone: errorThresholdImpostor < screenError < errorThresholdFull
                // As screenError decreases (farther), blend increases toward 1.0
                float t = (settings.errorThresholdFull - screenErrorFull) /
                          (settings.errorThresholdFull - settings.errorThresholdImpostor);
                state.blendFactor = t * t * (3.0f - 2.0f * t);  // smoothstep
            }
        } else {
            // Legacy distance-based LOD
            if (state.targetLevel == TreeLODState::Level::FullDetail) {
                // Currently at full detail, check if should switch to impostor
                if (distance > settings.fullDetailDistance + settings.hysteresis) {
                    newTarget = TreeLODState::Level::Impostor;
                }
            } else {
                // Currently at impostor, check if should switch to full detail
                if (distance < settings.fullDetailDistance - settings.hysteresis) {
                    newTarget = TreeLODState::Level::FullDetail;
                }
            }

            // Update blend factor
            if (settings.blendRange > 0.0f) {
                float blendStart = settings.fullDetailDistance;
                float blendEnd = settings.fullDetailDistance + settings.blendRange;

                if (distance < blendStart) {
                    state.blendFactor = 0.0f;
                } else if (distance > blendEnd) {
                    state.blendFactor = 1.0f;
                } else {
                    float t = (distance - blendStart) / settings.blendRange;
                    state.blendFactor = std::pow(t, settings.blendExponent);
                }
            } else {
                state.blendFactor = (state.targetLevel == TreeLODState::Level::Impostor) ? 1.0f : 0.0f;
            }
        }

        state.targetLevel = newTarget;

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
            ImpostorInstanceGPU instance;
            instance.position = tree.position;
            instance.scale = tree.scale;
            instance.rotation = tree.rotation;
            instance.archetypeIndex = state.archetypeIndex;
            instance.blendFactor = state.blendFactor;

            // Use actual tree mesh bounds instead of archetype bounds
            // Each tree has its own procedurally generated mesh with potentially different bounds
            if (tree.meshIndex < treeSystem.getMeshCount()) {
                const auto& meshBounds = treeSystem.getBranchMesh(tree.meshIndex).getBounds();
                glm::vec3 minB = meshBounds.min;
                glm::vec3 maxB = meshBounds.max;
                glm::vec3 extent = maxB - minB;

                // Billboard sizing: hSize uses bounding sphere for horizontal coverage,
                // vSize uses half height to prevent ground penetration.
                float horizontalRadius = std::max(extent.x, extent.z) * 0.5f;
                float halfHeight = extent.y * 0.5f;
                float boundingSphereRadius = glm::length(extent) * 0.5f;

                float hSize = boundingSphereRadius * TreeLODConstants::IMPOSTOR_SIZE_MARGIN * tree.scale;
                float vSize = halfHeight * TreeLODConstants::IMPOSTOR_SIZE_MARGIN * tree.scale;

                instance.hSize = hSize;
                instance.vSize = vSize;
                // Center offset: tree center height relative to origin
                float centerY = (minB.y + maxB.y) * 0.5f;
                instance.baseOffset = centerY * tree.scale;
            } else {
                // Fallback to archetype bounds if mesh not available
                const auto* archetype = impostorAtlas_->getArchetype(state.archetypeIndex);
                float hSize = (archetype ? archetype->boundingSphereRadius * TreeLODConstants::IMPOSTOR_SIZE_MARGIN : 10.0f) * tree.scale;
                float vSize = (archetype ? archetype->treeHeight * 0.5f * TreeLODConstants::IMPOSTOR_SIZE_MARGIN : 10.0f) * tree.scale;
                instance.hSize = hSize;
                instance.vSize = vSize;
                instance.baseOffset = (archetype ? archetype->centerHeight : 0.0f) * tree.scale;
            }
            visibleImpostors_.push_back(instance);
        }
    }

    lastCameraPos_ = cameraPos;

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

        // Update instance buffer (CPU fallback path only)
        if (!visibleImpostors_.empty()) {
            updateInstanceBuffer(visibleImpostors_);
        }
    }
}

void TreeLODSystem::updateInstanceBuffer(const std::vector<ImpostorInstanceGPU>& instances) {
    if (instances.empty()) return;

    // Resize buffer if needed
    if (instances.size() > maxInstances_) {
        vmaDestroyBuffer(allocator_, instanceBuffer_, instanceAllocation_);
        createInstanceBuffer(instances.size() * 2);
    }

    // Upload instance data
    void* data;
    vmaMapMemory(allocator_, instanceAllocation_, &data);
    memcpy(data, instances.data(), instances.size() * sizeof(ImpostorInstanceGPU));
    vmaUnmapMemory(allocator_, instanceAllocation_);
}

void TreeLODSystem::initializeDescriptorSets(const std::vector<VkBuffer>& uniformBuffers,
                                               VkImageView shadowMap, VkSampler shadowSampler) {
    // Use the shared array views that contain all archetypes
    VkImageView albedoView = impostorAtlas_->getAlbedoAtlasArrayView();
    VkImageView normalView = impostorAtlas_->getNormalAtlasArrayView();
    VkSampler atlasSampler = impostorAtlas_->getAtlasSampler();

    if (albedoView == VK_NULL_HANDLE || normalView == VK_NULL_HANDLE) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem: Atlas views not ready for descriptor initialization");
        return;
    }

    // Initialize main impostor descriptor sets for all frames
    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight_; ++frameIndex) {
        std::array<VkWriteDescriptorSet, 5> writes{};

        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = uniformBuffers[frameIndex];
        uboInfo.offset = 0;
        uboInfo.range = VK_WHOLE_SIZE;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = impostorDescriptorSets_[frameIndex];
        writes[0].dstBinding = BINDING_TREE_IMPOSTOR_UBO;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &uboInfo;

        VkDescriptorImageInfo albedoInfo{};
        albedoInfo.sampler = atlasSampler;
        albedoInfo.imageView = albedoView;
        albedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = impostorDescriptorSets_[frameIndex];
        writes[1].dstBinding = BINDING_TREE_IMPOSTOR_ALBEDO;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &albedoInfo;

        VkDescriptorImageInfo normalInfo{};
        normalInfo.sampler = atlasSampler;
        normalInfo.imageView = normalView;
        normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = impostorDescriptorSets_[frameIndex];
        writes[2].dstBinding = BINDING_TREE_IMPOSTOR_NORMAL;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &normalInfo;

        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.sampler = shadowSampler;
        shadowInfo.imageView = shadowMap;
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = impostorDescriptorSets_[frameIndex];
        writes[3].dstBinding = BINDING_TREE_IMPOSTOR_SHADOW_MAP;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo = &shadowInfo;

        // Instance buffer (CPU instance buffer - will be overwritten by initializeGPUCulledDescriptors for GPU path)
        VkDescriptorBufferInfo instanceInfo{};
        instanceInfo.buffer = instanceBuffer_;
        instanceInfo.offset = 0;
        instanceInfo.range = VK_WHOLE_SIZE;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = impostorDescriptorSets_[frameIndex];
        writes[4].dstBinding = BINDING_TREE_IMPOSTOR_INSTANCES;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].pBufferInfo = &instanceInfo;

        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Initialize shadow descriptor sets for all frames
    if (!shadowDescriptorSets_.empty()) {
        for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight_; ++frameIndex) {
            std::array<VkWriteDescriptorSet, 3> writes{};

            VkDescriptorBufferInfo uboInfo{};
            uboInfo.buffer = uniformBuffers[frameIndex];
            uboInfo.offset = 0;
            uboInfo.range = VK_WHOLE_SIZE;

            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = shadowDescriptorSets_[frameIndex];
            writes[0].dstBinding = BINDING_TREE_IMPOSTOR_UBO;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &uboInfo;

            VkDescriptorImageInfo albedoInfo{};
            albedoInfo.sampler = atlasSampler;
            albedoInfo.imageView = albedoView;
            albedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = shadowDescriptorSets_[frameIndex];
            writes[1].dstBinding = BINDING_TREE_IMPOSTOR_ALBEDO;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &albedoInfo;

            // Instance buffer (CPU instance buffer - will be overwritten by initializeGPUCulledDescriptors for GPU path)
            VkDescriptorBufferInfo instanceInfo{};
            instanceInfo.buffer = instanceBuffer_;
            instanceInfo.offset = 0;
            instanceInfo.range = VK_WHOLE_SIZE;

            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = shadowDescriptorSets_[frameIndex];
            writes[2].dstBinding = BINDING_TREE_IMPOSTOR_SHADOW_INSTANCES;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[2].descriptorCount = 1;
            writes[2].pBufferInfo = &instanceInfo;

            vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    SDL_Log("TreeLODSystem: Descriptor sets initialized");
}

void TreeLODSystem::initializeGPUCulledDescriptors(VkBuffer gpuInstanceBuffer) {
    // Update the instance buffer binding to use GPU-culled buffer instead of CPU buffer
    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight_; ++frameIndex) {
        VkDescriptorBufferInfo instanceInfo{};
        instanceInfo.buffer = gpuInstanceBuffer;
        instanceInfo.offset = 0;
        instanceInfo.range = VK_WHOLE_SIZE;

        // Update main descriptor set
        VkWriteDescriptorSet mainWrite{};
        mainWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        mainWrite.dstSet = impostorDescriptorSets_[frameIndex];
        mainWrite.dstBinding = BINDING_TREE_IMPOSTOR_INSTANCES;
        mainWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        mainWrite.descriptorCount = 1;
        mainWrite.pBufferInfo = &instanceInfo;

        vkUpdateDescriptorSets(device_, 1, &mainWrite, 0, nullptr);

        // Update shadow descriptor set
        if (!shadowDescriptorSets_.empty()) {
            VkWriteDescriptorSet shadowWrite{};
            shadowWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            shadowWrite.dstSet = shadowDescriptorSets_[frameIndex];
            shadowWrite.dstBinding = BINDING_TREE_IMPOSTOR_SHADOW_INSTANCES;
            shadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            shadowWrite.descriptorCount = 1;
            shadowWrite.pBufferInfo = &instanceInfo;

            vkUpdateDescriptorSets(device_, 1, &shadowWrite, 0, nullptr);
        }
    }

    SDL_Log("TreeLODSystem: GPU-culled descriptor sets initialized");
}

void TreeLODSystem::renderImpostors(VkCommandBuffer cmd, uint32_t frameIndex,
                                     VkBuffer uniformBuffer, VkImageView shadowMap, VkSampler shadowSampler) {
    (void)uniformBuffer; (void)shadowMap; (void)shadowSampler; // Descriptors bound at initialization
    if (visibleImpostors_.empty() || impostorAtlas_->getArchetypeCount() == 0) return;

    const auto& settings = getLODSettings();
    if (!settings.enableImpostors) return;

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
    // cameraPos: xyz=camera position, w=autumnHueShift
    // lodParams: x=blend, y=brightness, z=normalStrength, w=debugElevation
    // atlasParams: x=hSize, y=vSize, z=baseOffset, w=debugShowCellIndex
    struct {
        glm::vec4 cameraPos;
        glm::vec4 lodParams;
        glm::vec4 atlasParams;
    } pushConstants;

    pushConstants.cameraPos = glm::vec4(lastCameraPos_, settings.autumnHueShift);
    // lodParams: x=useOctahedral, y=brightness, z=normalStrength, w=unused
    pushConstants.lodParams = glm::vec4(
        settings.useOctahedralMapping ? 1.0f : 0.0f,
        settings.impostorBrightness,
        settings.normalStrength,
        0.0f
    );

    // atlasParams: x=enableFrameBlending, y=unused, z=unused, w=unused
    pushConstants.atlasParams = glm::vec4(
        settings.enableFrameBlending ? 1.0f : 0.0f,
        0.0f,
        0.0f,
        0.0f
    );

    vkCmdPushConstants(cmd, impostorPipelineLayout_.get(),
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(pushConstants), &pushConstants);

    // Bind buffers
    VkBuffer vertexBuffers[] = {billboardVertexBuffer_, instanceBuffer_};
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, billboardIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);

    // Draw instanced
    vkCmdDrawIndexed(cmd, billboardIndexCount_, static_cast<uint32_t>(visibleImpostors_.size()), 0, 0, 0);
}

void TreeLODSystem::renderImpostorShadows(VkCommandBuffer cmd, uint32_t frameIndex,
                                           int cascadeIndex, VkBuffer uniformBuffer) {
    (void)uniformBuffer; // Descriptors bound at initialization
    if (visibleImpostors_.empty() || impostorAtlas_->getArchetypeCount() == 0) return;
    if (shadowPipeline_.get() == VK_NULL_HANDLE) return;

    const auto& settings = getLODSettings();
    if (!settings.enableImpostors) return;

    // Bind shadow pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_.get());

    // Bind descriptor sets - use the main UBO descriptor set passed in for cascade matrices
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout_.get(),
                           0, 1, &shadowDescriptorSets_[frameIndex], 0, nullptr);

    // Push constants with cascade index
    struct {
        glm::vec4 cameraPos;
        glm::vec4 lodParams;
        glm::vec4 atlasParams;
        int cascadeIndex;
    } pushConstants;

    pushConstants.cameraPos = glm::vec4(lastCameraPos_, 1.0f);
    // lodParams: x=useOctahedral, y=brightness, z=normalStrength, w=unused
    pushConstants.lodParams = glm::vec4(
        settings.useOctahedralMapping ? 1.0f : 0.0f,
        settings.impostorBrightness,
        settings.normalStrength,
        0.0f
    );

    // atlasParams: x=enableFrameBlending, y=unused, z=unused, w=unused
    pushConstants.atlasParams = glm::vec4(
        settings.enableFrameBlending ? 1.0f : 0.0f,
        0.0f,
        0.0f,
        0.0f
    );
    pushConstants.cascadeIndex = cascadeIndex;

    vkCmdPushConstants(cmd, shadowPipelineLayout_.get(),
                      VK_SHADER_STAGE_VERTEX_BIT,
                      0, sizeof(pushConstants), &pushConstants);

    // Bind buffers
    VkBuffer vertexBuffers[] = {billboardVertexBuffer_, instanceBuffer_};
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, billboardIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);

    // Draw instanced
    vkCmdDrawIndexed(cmd, billboardIndexCount_, static_cast<uint32_t>(visibleImpostors_.size()), 0, 0, 0);
}

void TreeLODSystem::renderImpostorsGPUCulled(VkCommandBuffer cmd, uint32_t frameIndex,
                                              VkBuffer uniformBuffer, VkImageView shadowMap, VkSampler shadowSampler,
                                              VkBuffer gpuInstanceBuffer, VkBuffer indirectDrawBuffer) {
    (void)uniformBuffer; (void)shadowMap; (void)shadowSampler; (void)gpuInstanceBuffer; // Descriptors bound at initialization
    if (impostorAtlas_->getArchetypeCount() == 0) return;

    const auto& settings = getLODSettings();
    if (!settings.enableImpostors) return;

    if (impostorDescriptorSets_.empty()) return;

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
    // lodParams: x=useOctahedral, y=brightness, z=normalStrength, w=unused
    pushConstants.lodParams = glm::vec4(
        settings.useOctahedralMapping ? 1.0f : 0.0f,
        settings.impostorBrightness,
        settings.normalStrength,
        0.0f
    );

    // atlasParams: x=enableFrameBlending, y=unused, z=unused, w=unused
    pushConstants.atlasParams = glm::vec4(
        settings.enableFrameBlending ? 1.0f : 0.0f,
        0.0f,
        0.0f,
        0.0f
    );

    vkCmdPushConstants(cmd, impostorPipelineLayout_.get(),
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(pushConstants), &pushConstants);

    // Bind vertex and index buffers
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &billboardVertexBuffer_, &offset);
    vkCmdBindIndexBuffer(cmd, billboardIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);

    // Draw using indirect buffer from GPU culling
    vkCmdDrawIndexedIndirect(cmd, indirectDrawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
}

void TreeLODSystem::renderImpostorShadowsGPUCulled(VkCommandBuffer cmd, uint32_t frameIndex,
                                                   int cascadeIndex, VkBuffer uniformBuffer,
                                                   VkBuffer gpuInstanceBuffer, VkBuffer indirectDrawBuffer) {
    (void)uniformBuffer; (void)gpuInstanceBuffer; // Descriptors bound at initialization
    if (impostorAtlas_->getArchetypeCount() == 0) return;
    if (shadowPipeline_.get() == VK_NULL_HANDLE) return;

    const auto& settings = getLODSettings();
    if (!settings.enableImpostors) return;

    if (shadowDescriptorSets_.empty()) return;

    // Bind shadow pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout_.get(),
                           0, 1, &shadowDescriptorSets_[frameIndex], 0, nullptr);

    // Push constants for shadow pass - must match shader layout:
    // vec4 cameraPos (offset 0), vec4 lodParams (offset 16), int cascadeIndex (offset 32)
    struct {
        glm::vec4 cameraPos;
        glm::vec4 lodParams;
        int cascadeIndex;
        float _pad[3];
    } pushConstants;
    pushConstants.cameraPos = glm::vec4(lastCameraPos_, 0.0f);
    // lodParams.x = useOctahedral - must match main rendering mode for correct atlas UV lookup
    pushConstants.lodParams = glm::vec4(settings.useOctahedralMapping ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
    pushConstants.cascadeIndex = cascadeIndex;

    vkCmdPushConstants(cmd, shadowPipelineLayout_.get(),
                      VK_SHADER_STAGE_VERTEX_BIT,
                      0, sizeof(pushConstants), &pushConstants);

    // Bind vertex and index buffers
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &billboardVertexBuffer_, &offset);
    vkCmdBindIndexBuffer(cmd, billboardIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);

    // Draw using indirect buffer from GPU culling
    vkCmdDrawIndexedIndirect(cmd, indirectDrawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
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
    const auto& state = lodStates_[treeIndex];
    return state.currentLevel == TreeLODState::Level::FullDetail ||
           state.currentLevel == TreeLODState::Level::Blending;
}

bool TreeLODSystem::shouldRenderImpostor(uint32_t treeIndex) const {
    if (treeIndex >= lodStates_.size()) return false;
    const auto& state = lodStates_[treeIndex];
    return state.currentLevel == TreeLODState::Level::Impostor ||
           state.currentLevel == TreeLODState::Level::Blending;
}

float TreeLODSystem::getBlendFactor(uint32_t treeIndex) const {
    if (treeIndex >= lodStates_.size()) return 0.0f;
    return lodStates_[treeIndex].blendFactor;
}

bool TreeLODSystem::shouldRenderBranchShadow(uint32_t treeIndex, uint32_t cascadeIndex) const {
    const auto& shadowSettings = getLODSettings().shadow;

    // If cascade-aware LOD is disabled, use standard LOD check
    if (!shadowSettings.enableCascadeLOD) {
        return shouldRenderFullGeometry(treeIndex);
    }

    // Far cascades use impostors only - no branch geometry
    if (cascadeIndex >= shadowSettings.geometryCascadeCutoff) {
        return false;
    }

    // Near cascades use standard per-tree LOD
    return shouldRenderFullGeometry(treeIndex);
}

bool TreeLODSystem::shouldRenderLeafShadow(uint32_t treeIndex, uint32_t cascadeIndex) const {
    const auto& shadowSettings = getLODSettings().shadow;

    // If cascade-aware LOD is disabled, use standard LOD check
    if (!shadowSettings.enableCascadeLOD) {
        return shouldRenderFullGeometry(treeIndex);
    }

    // Very far cascades skip leaf shadows entirely
    if (cascadeIndex >= shadowSettings.leafCascadeCutoff) {
        return false;
    }

    // Far cascades use impostors only - no leaf geometry
    if (cascadeIndex >= shadowSettings.geometryCascadeCutoff) {
        return false;
    }

    // Near cascades use standard per-tree LOD
    return shouldRenderFullGeometry(treeIndex);
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
