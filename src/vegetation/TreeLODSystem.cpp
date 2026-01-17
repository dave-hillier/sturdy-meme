#include "TreeLODSystem.h"
#include "TreeSystem.h"
#include "TreeOptions.h"
#include "CullCommon.h"
#include "Mesh.h"
#include "ShaderLoader.h"
#include "shaders/bindings.h"
#include "core/vulkan/PipelineLayoutBuilder.h"

#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <algorithm>
#include <limits>

std::unique_ptr<TreeLODSystem> TreeLODSystem::create(const InitInfo& info) {
    auto system = std::make_unique<TreeLODSystem>(ConstructToken{});
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
    raiiDevice_ = info.raiiDevice;
    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem requires raiiDevice");
        return false;
    }
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
    atlasInfo.raiiDevice = info.raiiDevice;
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
        {{-0.5f, 0.0f, 0.0f}, {1.0f, 1.0f}},  // Bottom-left (U mirrored for billboard facing)
        {{ 0.5f, 0.0f, 0.0f}, {0.0f, 1.0f}},  // Bottom-right (U mirrored for billboard facing)
        {{ 0.5f, 1.0f, 0.0f}, {0.0f, 0.0f}},  // Top-right (U mirrored for billboard facing)
        {{-0.5f, 1.0f, 0.0f}, {1.0f, 0.0f}},  // Top-left (U mirrored for billboard facing)
    }};

    std::array<uint32_t, 6> indices = {0, 1, 2, 2, 3, 0};
    billboardIndexCount_ = 6;

    // Create vertex buffer
    VkDeviceSize vertexSize = sizeof(vertices);
    auto vertexBufferInfo = vk::BufferCreateInfo{}
        .setSize(vertexSize)
        .setUsage(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&vertexBufferInfo), &allocInfo,
                        &billboardVertexBuffer_, &billboardVertexAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Create index buffer
    VkDeviceSize indexSize = sizeof(indices);
    auto indexBufferInfo = vk::BufferCreateInfo{}
        .setSize(indexSize)
        .setUsage(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst);

    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&indexBufferInfo), &allocInfo,
                        &billboardIndexBuffer_, &billboardIndexAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Upload data via staging buffer
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    VkDeviceSize stagingSize = vertexSize + indexSize;

    auto stagingInfo = vk::BufferCreateInfo{}
        .setSize(stagingSize)
        .setUsage(vk::BufferUsageFlagBits::eTransferSrc);

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&stagingInfo), &stagingAllocInfo,
                        &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    void* data;
    vmaMapMemory(allocator_, stagingAllocation, &data);
    memcpy(data, vertices.data(), vertexSize);
    memcpy(static_cast<char*>(data) + vertexSize, indices.data(), indexSize);
    vmaUnmapMemory(allocator_, stagingAllocation);

    // Copy to GPU buffers
    auto cmdAllocInfo = vk::CommandBufferAllocateInfo{}
        .setCommandPool(commandPool_)
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandBufferCount(1);

    vk::Device vkDevice(device_);
    auto cmdBuffers = vkDevice.allocateCommandBuffers(cmdAllocInfo);
    vk::CommandBuffer vkCmd = cmdBuffers[0];

    vkCmd.begin(vk::CommandBufferBeginInfo{}.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    vkCmd.copyBuffer(stagingBuffer, billboardVertexBuffer_,
                     vk::BufferCopy{}.setSize(vertexSize));
    vkCmd.copyBuffer(stagingBuffer, billboardIndexBuffer_,
                     vk::BufferCopy{}.setSrcOffset(vertexSize).setSize(indexSize));

    vkCmd.end();

    vk::Queue(graphicsQueue_).submit(
        vk::SubmitInfo{}.setCommandBuffers(vkCmd),
        nullptr);
    vk::Queue(graphicsQueue_).waitIdle();

    vkDevice.freeCommandBuffers(commandPool_, vkCmd);
    vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);

    return true;
}

bool TreeLODSystem::createDescriptorSetLayout() {
    std::array<vk::DescriptorSetLayoutBinding, 5> bindings = {{
        // UBO
        vk::DescriptorSetLayoutBinding{}
            .setBinding(BINDING_TREE_IMPOSTOR_UBO)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment),
        // Albedo atlas
        vk::DescriptorSetLayoutBinding{}
            .setBinding(BINDING_TREE_IMPOSTOR_ALBEDO)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment),
        // Normal atlas
        vk::DescriptorSetLayoutBinding{}
            .setBinding(BINDING_TREE_IMPOSTOR_NORMAL)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment),
        // Shadow map
        vk::DescriptorSetLayoutBinding{}
            .setBinding(BINDING_TREE_IMPOSTOR_SHADOW_MAP)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment),
        // Instance buffer (SSBO for GPU-culled rendering)
        vk::DescriptorSetLayoutBinding{}
            .setBinding(BINDING_TREE_IMPOSTOR_INSTANCES)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex)
    }};

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindings(bindings);

    impostorDescriptorSetLayout_.emplace(*raiiDevice_, layoutInfo);

    return true;
}

bool TreeLODSystem::createPipeline() {
    // Pipeline layout with push constants: cameraPos, lodParams, atlasParams
    auto layoutOpt = PipelineLayoutBuilder(*raiiDevice_)
        .addDescriptorSetLayout(**impostorDescriptorSetLayout_)
        .addPushConstantRange(
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            sizeof(glm::vec4) * 3)  // cameraPos, lodParams, atlasParams
        .build();
    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem: Failed to create impostor pipeline layout");
        return false;
    }
    impostorPipelineLayout_ = std::move(layoutOpt);

    // Load shaders
    std::string shaderPath = resourcePath_ + "/shaders/";
    auto vertModule = ShaderLoader::loadShaderModule(device_, shaderPath + "tree_impostor.vert.spv");
    auto fragModule = ShaderLoader::loadShaderModule(device_, shaderPath + "tree_impostor.frag.spv");

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem: Failed to load impostor shaders");
        return false;
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {{
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setModule(*vertModule)
            .setPName("main"),
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setModule(*fragModule)
            .setPName("main")
    }};

    // Vertex input: billboard vertex + instance data
    std::array<vk::VertexInputBindingDescription, 2> bindingDescriptions = {{
        vk::VertexInputBindingDescription{}
            .setBinding(0)
            .setStride(sizeof(glm::vec3) + sizeof(glm::vec2))  // position + texcoord
            .setInputRate(vk::VertexInputRate::eVertex),
        vk::VertexInputBindingDescription{}
            .setBinding(1)
            .setStride(sizeof(ImpostorInstanceGPU))
            .setInputRate(vk::VertexInputRate::eInstance)
    }};

    std::array<vk::VertexInputAttributeDescription, 10> attributeDescriptions = {{
        // Per-vertex attributes
        vk::VertexInputAttributeDescription{}.setLocation(0).setBinding(0).setFormat(vk::Format::eR32G32B32Sfloat).setOffset(0),  // position
        vk::VertexInputAttributeDescription{}.setLocation(1).setBinding(0).setFormat(vk::Format::eR32G32Sfloat).setOffset(sizeof(glm::vec3)),  // texcoord
        // Per-instance attributes
        vk::VertexInputAttributeDescription{}.setLocation(2).setBinding(1).setFormat(vk::Format::eR32G32B32Sfloat).setOffset(offsetof(ImpostorInstanceGPU, position)),
        vk::VertexInputAttributeDescription{}.setLocation(3).setBinding(1).setFormat(vk::Format::eR32Sfloat).setOffset(offsetof(ImpostorInstanceGPU, scale)),
        vk::VertexInputAttributeDescription{}.setLocation(4).setBinding(1).setFormat(vk::Format::eR32Sfloat).setOffset(offsetof(ImpostorInstanceGPU, rotation)),
        vk::VertexInputAttributeDescription{}.setLocation(5).setBinding(1).setFormat(vk::Format::eR32Uint).setOffset(offsetof(ImpostorInstanceGPU, archetypeIndex)),
        vk::VertexInputAttributeDescription{}.setLocation(6).setBinding(1).setFormat(vk::Format::eR32Sfloat).setOffset(offsetof(ImpostorInstanceGPU, blendFactor)),
        vk::VertexInputAttributeDescription{}.setLocation(7).setBinding(1).setFormat(vk::Format::eR32Sfloat).setOffset(offsetof(ImpostorInstanceGPU, hSize)),
        vk::VertexInputAttributeDescription{}.setLocation(8).setBinding(1).setFormat(vk::Format::eR32Sfloat).setOffset(offsetof(ImpostorInstanceGPU, vSize)),
        vk::VertexInputAttributeDescription{}.setLocation(9).setBinding(1).setFormat(vk::Format::eR32Sfloat).setOffset(offsetof(ImpostorInstanceGPU, baseOffset))
    }};

    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{}
        .setVertexBindingDescriptions(bindingDescriptions)
        .setVertexAttributeDescriptions(attributeDescriptions);

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList);

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setScissorCount(1);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)  // Billboard faces camera
        .setFrontFace(vk::FrontFace::eCounterClockwise);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_TRUE)
        .setDepthCompareOp(vk::CompareOp::eLess);

    auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState{}
        .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
        .setBlendEnable(VK_FALSE);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setAttachmentCount(1)
        .setPAttachments(&colorBlendAttachment);

    std::array<vk::DynamicState, 2> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStates(dynamicStates);

    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setStages(shaderStages)
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssembly)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlending)
        .setPDynamicState(&dynamicState)
        .setLayout(**impostorPipelineLayout_)
        .setRenderPass(hdrRenderPass_)
        .setSubpass(0);

    impostorPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);

    vk::Device vkDevice(device_);
    vkDevice.destroyShaderModule(*vertModule);
    vkDevice.destroyShaderModule(*fragModule);

    return true;
}

bool TreeLODSystem::allocateDescriptorSets() {
    impostorDescriptorSets_ = descriptorPool_->allocate(**impostorDescriptorSetLayout_, maxFramesInFlight_);
    return !impostorDescriptorSets_.empty();
}

bool TreeLODSystem::createShadowDescriptorSetLayout() {
    // Shadow pass needs UBO (for cascade matrices), albedo atlas (for alpha testing),
    // and instance buffer (for GPU-culled rendering)
    std::array<vk::DescriptorSetLayoutBinding, 3> bindings = {{
        // UBO
        vk::DescriptorSetLayoutBinding{}
            .setBinding(BINDING_TREE_IMPOSTOR_UBO)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex),
        // Albedo atlas (for alpha testing in fragment shader)
        vk::DescriptorSetLayoutBinding{}
            .setBinding(BINDING_TREE_IMPOSTOR_ALBEDO)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment),
        // Instance buffer (SSBO for GPU-culled rendering)
        vk::DescriptorSetLayoutBinding{}
            .setBinding(BINDING_TREE_IMPOSTOR_SHADOW_INSTANCES)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex)
    }};

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindings(bindings);

    shadowDescriptorSetLayout_.emplace(*raiiDevice_, layoutInfo);

    return true;
}

bool TreeLODSystem::createShadowPipeline() {
    // Push constants: cameraPos, lodParams, atlasParams, cascadeIndex
    auto layoutOpt = PipelineLayoutBuilder(*raiiDevice_)
        .addDescriptorSetLayout(**shadowDescriptorSetLayout_)
        .addPushConstantRange(
            vk::ShaderStageFlagBits::eVertex,
            sizeof(glm::vec4) * 3 + sizeof(int))  // cameraPos, lodParams, atlasParams, cascadeIndex
        .build();
    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem: Failed to create shadow pipeline layout");
        return false;
    }
    shadowPipelineLayout_ = std::move(layoutOpt);

    // Load shadow shaders
    std::string shaderPath = resourcePath_ + "/shaders/";
    auto vertModule = ShaderLoader::loadShaderModule(device_, shaderPath + "tree_impostor_shadow.vert.spv");
    auto fragModule = ShaderLoader::loadShaderModule(device_, shaderPath + "tree_impostor_shadow.frag.spv");

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem: Failed to load impostor shadow shaders");
        return false;
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {{
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setModule(*vertModule)
            .setPName("main"),
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setModule(*fragModule)
            .setPName("main")
    }};

    // Vertex input: only billboard quad vertices (instances come from SSBO)
    auto bindingDescription = vk::VertexInputBindingDescription{}
        .setBinding(0)
        .setStride(sizeof(glm::vec3) + sizeof(glm::vec2))
        .setInputRate(vk::VertexInputRate::eVertex);

    std::array<vk::VertexInputAttributeDescription, 2> attributeDescriptions = {{
        vk::VertexInputAttributeDescription{}.setLocation(0).setBinding(0).setFormat(vk::Format::eR32G32B32Sfloat).setOffset(0),  // inPosition
        vk::VertexInputAttributeDescription{}.setLocation(1).setBinding(0).setFormat(vk::Format::eR32G32Sfloat).setOffset(sizeof(glm::vec3))  // inTexCoord
    }};

    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{}
        .setVertexBindingDescriptionCount(1)
        .setPVertexBindingDescriptions(&bindingDescription)
        .setVertexAttributeDescriptions(attributeDescriptions);

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList);

    // Static viewport and scissor for shadow map
    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(shadowMapSize_))
        .setHeight(static_cast<float>(shadowMapSize_))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);

    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent({shadowMapSize_, shadowMapSize_});

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setPViewports(&viewport)
        .setScissorCount(1)
        .setPScissors(&scissor);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)  // Billboard, no culling
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setDepthBiasEnable(VK_TRUE)  // Enable depth bias for shadow acne
        .setDepthBiasConstantFactor(1.25f)
        .setDepthBiasSlopeFactor(1.75f);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_TRUE)
        .setDepthCompareOp(vk::CompareOp::eLess);

    // No color attachment for shadow pass
    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setAttachmentCount(0);

    // No dynamic state - viewport and scissor are static
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStateCount(0);

    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setStages(shaderStages)
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssembly)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlending)
        .setPDynamicState(&dynamicState)
        .setLayout(**shadowPipelineLayout_)
        .setRenderPass(shadowRenderPass_)
        .setSubpass(0);

    shadowPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);

    vk::Device vkDevice(device_);
    vkDevice.destroyShaderModule(*vertModule);
    vkDevice.destroyShaderModule(*fragModule);

    return true;
}

bool TreeLODSystem::allocateShadowDescriptorSets() {
    shadowDescriptorSets_ = descriptorPool_->allocate(**shadowDescriptorSetLayout_, maxFramesInFlight_);
    return !shadowDescriptorSets_.empty();
}

bool TreeLODSystem::createInstanceBuffer(size_t maxInstances) {
    maxInstances_ = maxInstances;
    instanceBufferSize_ = maxInstances * sizeof(ImpostorInstanceGPU);

    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(instanceBufferSize_)
        .setUsage(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    return vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo), &allocInfo,
                           &instanceBuffer_, &instanceAllocation_, nullptr) == VK_SUCCESS;
}

// computeScreenError is now in CullCommon.h

void TreeLODSystem::updateTreeLODState(TreeLODState& state, float distance, float treeScale,
                                        const TreeLODSettings& settings, const ScreenParams& screenParams) {
    state.lastDistance = distance;
    TreeLODState::Level newTarget = state.targetLevel;

    if (settings.useScreenSpaceError) {
        // Screen-space error based LOD
        const auto* archetype = impostorAtlas_->getArchetype(state.archetypeIndex);
        float worldErrorFull = 0.1f * treeScale;  // ~10cm branch thickness, scaled

        float screenErrorFull = computeScreenError(worldErrorFull, distance,
                                                    screenParams.screenHeight, screenParams.tanHalfFOV);

        // Determine LOD level based on screen error
        newTarget = (screenErrorFull > settings.errorThresholdFull)
            ? TreeLODState::Level::FullDetail
            : TreeLODState::Level::Impostor;

        // Compute blend factor based on screen error
        if (screenErrorFull > settings.errorThresholdFull) {
            state.blendFactor = 0.0f;
        } else if (screenErrorFull < settings.errorThresholdImpostor) {
            state.blendFactor = 1.0f;
        } else {
            float t = (settings.errorThresholdFull - screenErrorFull) /
                      (settings.errorThresholdFull - settings.errorThresholdImpostor);
            state.blendFactor = t * t * (3.0f - 2.0f * t);  // smoothstep
        }
    } else {
        // Legacy distance-based LOD with hysteresis
        if (state.targetLevel == TreeLODState::Level::FullDetail) {
            if (distance > settings.fullDetailDistance + settings.hysteresis) {
                newTarget = TreeLODState::Level::Impostor;
            }
        } else {
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
}

void TreeLODSystem::buildImpostorInstance(ImpostorInstanceGPU& instance, const TreeInstanceData& tree,
                                           const TreeLODState& state, const TreeSystem& treeSystem) {
    instance.position = tree.position();
    instance.scale = tree.scale();
    instance.rotation = tree.getYRotation();
    instance.archetypeIndex = state.archetypeIndex;
    instance.blendFactor = state.blendFactor;

    // Use full tree bounds (branches + leaves) for accurate imposter sizing
    if (tree.meshIndex < treeSystem.getMeshCount()) {
        const auto& fullBounds = treeSystem.getFullTreeBounds(tree.meshIndex);
        glm::vec3 extent = fullBounds.max - fullBounds.min;

        float horizontalRadius = std::max(extent.x, extent.z) * 0.5f;
        float halfHeight = extent.y * 0.5f;

        instance.hSize = horizontalRadius * TreeLODConstants::IMPOSTOR_SIZE_MARGIN * tree.scale();
        instance.vSize = halfHeight * TreeLODConstants::IMPOSTOR_SIZE_MARGIN * tree.scale();
        instance.baseOffset = (fullBounds.min.y + fullBounds.max.y) * 0.5f * tree.scale();
    } else {
        // Fallback to archetype bounds
        const auto* archetype = impostorAtlas_->getArchetype(state.archetypeIndex);
        instance.hSize = (archetype ? archetype->boundingSphereRadius * TreeLODConstants::IMPOSTOR_SIZE_MARGIN : 10.0f) * tree.scale();
        instance.vSize = (archetype ? archetype->treeHeight * 0.5f * TreeLODConstants::IMPOSTOR_SIZE_MARGIN : 10.0f) * tree.scale();
        instance.baseOffset = (archetype ? archetype->centerHeight : 0.0f) * tree.scale();
    }
}

ImpostorPushConstants TreeLODSystem::buildImpostorPushConstants() const {
    const auto& settings = getLODSettings();
    ImpostorPushConstants pc;
    pc.cameraPos = glm::vec4(lastCameraPos_, settings.autumnHueShift);
    pc.lodParams = glm::vec4(1.0f, settings.impostorBrightness, settings.normalStrength, 0.0f);
    pc.atlasParams = glm::vec4(settings.enableFrameBlending ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
    return pc;
}

ImpostorShadowPushConstants TreeLODSystem::buildShadowPushConstants(int cascadeIndex) const {
    ImpostorShadowPushConstants pc;
    pc.cameraPos = glm::vec4(lastCameraPos_, 0.0f);
    pc.lodParams = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    pc.cascadeIndex = cascadeIndex;
    return pc;
}

void TreeLODSystem::bindImpostorPipeline(vk::CommandBuffer& cmd, uint32_t frameIndex) {
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **impostorPipeline_);

    auto viewport = vk::Viewport{}
        .setX(0.0f).setY(0.0f)
        .setWidth(static_cast<float>(extent_.width))
        .setHeight(static_cast<float>(extent_.height))
        .setMinDepth(0.0f).setMaxDepth(1.0f);
    cmd.setViewport(0, viewport);

    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent(vk::Extent2D{}.setWidth(extent_.width).setHeight(extent_.height));
    cmd.setScissor(0, scissor);

    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **impostorPipelineLayout_,
                           0, vk::DescriptorSet(impostorDescriptorSets_[frameIndex]), {});
}

void TreeLODSystem::bindShadowPipeline(vk::CommandBuffer& cmd, uint32_t frameIndex) {
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **shadowPipeline_);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **shadowPipelineLayout_,
                           0, vk::DescriptorSet(shadowDescriptorSets_[frameIndex]), {});
}

void TreeLODSystem::bindBillboardBuffers(vk::CommandBuffer& cmd, VkBuffer instanceBuf) {
    vk::DeviceSize offset = 0;
    if (instanceBuf != VK_NULL_HANDLE) {
        // GPU-culled path: only bind vertex buffer (instances come from SSBO)
        cmd.bindVertexBuffers(0, vk::Buffer(billboardVertexBuffer_), offset);
    } else {
        // CPU path: bind both vertex and instance buffers
        vk::Buffer vertexBuffers[] = {billboardVertexBuffer_, instanceBuffer_};
        vk::DeviceSize offsets[] = {0, 0};
        cmd.bindVertexBuffers(0, vertexBuffers, offsets);
    }
    cmd.bindIndexBuffer(billboardIndexBuffer_, 0, vk::IndexType::eUint32);
}

void TreeLODSystem::update(float deltaTime, const glm::vec3& cameraPos, const TreeSystem& treeSystem,
                           const ScreenParams& screenParams) {
    (void)deltaTime;
    const auto& settings = getLODSettings();
    const auto& instances = treeSystem.getTreeInstances();

    if (lodStates_.size() != instances.size()) {
        lodStates_.resize(instances.size());
    }

    const uint32_t numArchetypes = static_cast<uint32_t>(impostorAtlas_->getArchetypeCount());
    visibleImpostors_.clear();

    for (size_t i = 0; i < instances.size(); i++) {
        const auto& tree = instances[i];
        auto& state = lodStates_[i];

        // Set archetype index from tree
        if (numArchetypes > 0) {
            state.archetypeIndex = tree.archetypeIndex % numArchetypes;
        }

        float distance = glm::distance(cameraPos, tree.position());
        updateTreeLODState(state, distance, tree.scale(), settings, screenParams);

        // Skip CPU impostor list building when GPU culling handles it
        if (gpuCullingEnabled_) continue;

        // Collect visible impostors (CPU fallback path only)
        if (settings.enableImpostors && state.blendFactor > 0.0f &&
            state.archetypeIndex < impostorAtlas_->getArchetypeCount()) {
            ImpostorInstanceGPU instance;
            buildImpostorInstance(instance, tree, state, treeSystem);
            visibleImpostors_.push_back(instance);
        }
    }

    lastCameraPos_ = cameraPos;

    // Skip debug info calculation when GPU culling is enabled
    if (!gpuCullingEnabled_) {
        debugInfo_.cameraPos = cameraPos;
        debugInfo_.nearestTreeDistance = std::numeric_limits<float>::max();
        for (const auto& tree : instances) {
            float dist = glm::distance(cameraPos, tree.position());
            if (dist < debugInfo_.nearestTreeDistance) {
                debugInfo_.nearestTreeDistance = dist;
                debugInfo_.nearestTreePos = tree.position();

                glm::vec3 toTree = tree.position() - cameraPos;
                float toTreeDist = glm::length(toTree);
                if (toTreeDist > 0.001f) {
                    debugInfo_.calculatedElevation = glm::degrees(std::asin(glm::clamp(-toTree.y / toTreeDist, -1.0f, 1.0f)));
                }
            }
        }

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

    vk::Device vkDevice(device_);

    // Initialize main impostor descriptor sets for all frames
    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight_; ++frameIndex) {
        auto uboInfo = vk::DescriptorBufferInfo{}
            .setBuffer(uniformBuffers[frameIndex])
            .setOffset(0)
            .setRange(VK_WHOLE_SIZE);

        auto albedoInfo = vk::DescriptorImageInfo{}
            .setSampler(atlasSampler)
            .setImageView(albedoView)
            .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

        auto normalInfo = vk::DescriptorImageInfo{}
            .setSampler(atlasSampler)
            .setImageView(normalView)
            .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

        auto shadowInfo = vk::DescriptorImageInfo{}
            .setSampler(shadowSampler)
            .setImageView(shadowMap)
            .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

        auto instanceInfo = vk::DescriptorBufferInfo{}
            .setBuffer(instanceBuffer_)
            .setOffset(0)
            .setRange(VK_WHOLE_SIZE);

        std::array<vk::WriteDescriptorSet, 5> writes = {{
            vk::WriteDescriptorSet{}
                .setDstSet(impostorDescriptorSets_[frameIndex])
                .setDstBinding(BINDING_TREE_IMPOSTOR_UBO)
                .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                .setBufferInfo(uboInfo),
            vk::WriteDescriptorSet{}
                .setDstSet(impostorDescriptorSets_[frameIndex])
                .setDstBinding(BINDING_TREE_IMPOSTOR_ALBEDO)
                .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                .setImageInfo(albedoInfo),
            vk::WriteDescriptorSet{}
                .setDstSet(impostorDescriptorSets_[frameIndex])
                .setDstBinding(BINDING_TREE_IMPOSTOR_NORMAL)
                .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                .setImageInfo(normalInfo),
            vk::WriteDescriptorSet{}
                .setDstSet(impostorDescriptorSets_[frameIndex])
                .setDstBinding(BINDING_TREE_IMPOSTOR_SHADOW_MAP)
                .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                .setImageInfo(shadowInfo),
            // Instance buffer (CPU instance buffer - will be overwritten by initializeGPUCulledDescriptors for GPU path)
            vk::WriteDescriptorSet{}
                .setDstSet(impostorDescriptorSets_[frameIndex])
                .setDstBinding(BINDING_TREE_IMPOSTOR_INSTANCES)
                .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                .setBufferInfo(instanceInfo)
        }};

        vkDevice.updateDescriptorSets(writes, nullptr);
    }

    // Initialize shadow descriptor sets for all frames
    if (!shadowDescriptorSets_.empty()) {
        for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight_; ++frameIndex) {
            auto uboInfo = vk::DescriptorBufferInfo{}
                .setBuffer(uniformBuffers[frameIndex])
                .setOffset(0)
                .setRange(VK_WHOLE_SIZE);

            auto albedoInfo = vk::DescriptorImageInfo{}
                .setSampler(atlasSampler)
                .setImageView(albedoView)
                .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

            auto instanceInfo = vk::DescriptorBufferInfo{}
                .setBuffer(instanceBuffer_)
                .setOffset(0)
                .setRange(VK_WHOLE_SIZE);

            std::array<vk::WriteDescriptorSet, 3> writes = {{
                vk::WriteDescriptorSet{}
                    .setDstSet(shadowDescriptorSets_[frameIndex])
                    .setDstBinding(BINDING_TREE_IMPOSTOR_UBO)
                    .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                    .setBufferInfo(uboInfo),
                vk::WriteDescriptorSet{}
                    .setDstSet(shadowDescriptorSets_[frameIndex])
                    .setDstBinding(BINDING_TREE_IMPOSTOR_ALBEDO)
                    .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                    .setImageInfo(albedoInfo),
                // Instance buffer (CPU instance buffer - will be overwritten by initializeGPUCulledDescriptors for GPU path)
                vk::WriteDescriptorSet{}
                    .setDstSet(shadowDescriptorSets_[frameIndex])
                    .setDstBinding(BINDING_TREE_IMPOSTOR_SHADOW_INSTANCES)
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                    .setBufferInfo(instanceInfo)
            }};

            vkDevice.updateDescriptorSets(writes, nullptr);
        }
    }

    SDL_Log("TreeLODSystem: Descriptor sets initialized");
}

void TreeLODSystem::initializeGPUCulledDescriptors(VkBuffer gpuInstanceBuffer) {
    vk::Device vkDevice(device_);

    // Update the instance buffer binding to use GPU-culled buffer instead of CPU buffer
    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight_; ++frameIndex) {
        auto instanceInfo = vk::DescriptorBufferInfo{}
            .setBuffer(gpuInstanceBuffer)
            .setOffset(0)
            .setRange(VK_WHOLE_SIZE);

        // Update main descriptor set
        auto mainWrite = vk::WriteDescriptorSet{}
            .setDstSet(impostorDescriptorSets_[frameIndex])
            .setDstBinding(BINDING_TREE_IMPOSTOR_INSTANCES)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setBufferInfo(instanceInfo);

        vkDevice.updateDescriptorSets(mainWrite, nullptr);

        // Update shadow descriptor set
        if (!shadowDescriptorSets_.empty()) {
            auto shadowWrite = vk::WriteDescriptorSet{}
                .setDstSet(shadowDescriptorSets_[frameIndex])
                .setDstBinding(BINDING_TREE_IMPOSTOR_SHADOW_INSTANCES)
                .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                .setBufferInfo(instanceInfo);

            vkDevice.updateDescriptorSets(shadowWrite, nullptr);
        }
    }

    SDL_Log("TreeLODSystem: GPU-culled descriptor sets initialized");
}

void TreeLODSystem::renderImpostors(VkCommandBuffer cmd, uint32_t frameIndex,
                                     VkBuffer uniformBuffer, VkImageView shadowMap, VkSampler shadowSampler) {
    (void)uniformBuffer; (void)shadowMap; (void)shadowSampler;
    if (visibleImpostors_.empty() || impostorAtlas_->getArchetypeCount() == 0) return;
    if (!getLODSettings().enableImpostors) return;

    vk::CommandBuffer vkCmd(cmd);
    bindImpostorPipeline(vkCmd, frameIndex);

    auto pc = buildImpostorPushConstants();
    vkCmd.pushConstants<ImpostorPushConstants>(
        **impostorPipelineLayout_,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, pc);

    bindBillboardBuffers(vkCmd);
    vkCmd.drawIndexed(billboardIndexCount_, static_cast<uint32_t>(visibleImpostors_.size()), 0, 0, 0);
}

void TreeLODSystem::renderImpostorShadows(VkCommandBuffer cmd, uint32_t frameIndex,
                                           int cascadeIndex, VkBuffer uniformBuffer) {
    (void)uniformBuffer;
    if (visibleImpostors_.empty() || impostorAtlas_->getArchetypeCount() == 0) return;
    if (!shadowPipeline_ || !getLODSettings().enableImpostors) return;

    vk::CommandBuffer vkCmd(cmd);
    bindShadowPipeline(vkCmd, frameIndex);

    auto pc = buildShadowPushConstants(cascadeIndex);
    vkCmd.pushConstants<ImpostorShadowPushConstants>(
        **shadowPipelineLayout_,
        vk::ShaderStageFlagBits::eVertex,
        0, pc);

    bindBillboardBuffers(vkCmd);
    vkCmd.drawIndexed(billboardIndexCount_, static_cast<uint32_t>(visibleImpostors_.size()), 0, 0, 0);
}

void TreeLODSystem::renderImpostorsGPUCulled(VkCommandBuffer cmd, uint32_t frameIndex,
                                              VkBuffer uniformBuffer, VkImageView shadowMap, VkSampler shadowSampler,
                                              VkBuffer gpuInstanceBuffer, VkBuffer indirectDrawBuffer) {
    (void)uniformBuffer; (void)shadowMap; (void)shadowSampler; (void)gpuInstanceBuffer;
    if (impostorAtlas_->getArchetypeCount() == 0) return;
    if (!getLODSettings().enableImpostors || impostorDescriptorSets_.empty()) return;

    vk::CommandBuffer vkCmd(cmd);
    bindImpostorPipeline(vkCmd, frameIndex);

    auto pc = buildImpostorPushConstants();
    vkCmd.pushConstants<ImpostorPushConstants>(
        **impostorPipelineLayout_,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, pc);

    bindBillboardBuffers(vkCmd, gpuInstanceBuffer);
    vkCmd.drawIndexedIndirect(indirectDrawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
}

void TreeLODSystem::renderImpostorShadowsGPUCulled(VkCommandBuffer cmd, uint32_t frameIndex,
                                                   int cascadeIndex, VkBuffer uniformBuffer,
                                                   VkBuffer gpuInstanceBuffer, VkBuffer indirectDrawBuffer) {
    (void)uniformBuffer; (void)gpuInstanceBuffer;
    if (impostorAtlas_->getArchetypeCount() == 0) return;
    if (!shadowPipeline_ || !getLODSettings().enableImpostors || shadowDescriptorSets_.empty()) return;

    vk::CommandBuffer vkCmd(cmd);
    bindShadowPipeline(vkCmd, frameIndex);

    auto pc = buildShadowPushConstants(cascadeIndex);
    vkCmd.pushConstants<ImpostorShadowPushConstants>(
        **shadowPipelineLayout_,
        vk::ShaderStageFlagBits::eVertex,
        0, pc);

    bindBillboardBuffers(vkCmd, gpuInstanceBuffer);
    vkCmd.drawIndexedIndirect(indirectDrawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
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
