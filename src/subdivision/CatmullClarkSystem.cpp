#include "CatmullClarkSystem.h"
#include "OBJLoader.h"
#include "ShaderLoader.h"
#include "BufferUtils.h"
#include "VulkanBarriers.h"
#include "VmaResources.h"
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan.hpp>
#include <algorithm>
#include <array>
#include <cstring>

using ShaderLoader::loadShaderModule;

std::unique_ptr<CatmullClarkSystem> CatmullClarkSystem::create(const InitInfo& info, const CatmullClarkConfig& config) {
    std::unique_ptr<CatmullClarkSystem> system(new CatmullClarkSystem());
    if (!system->initInternal(info, config)) {
        return nullptr;
    }
    return system;
}

CatmullClarkSystem::~CatmullClarkSystem() {
    cleanup();
}

bool CatmullClarkSystem::initInternal(const InitInfo& info, const CatmullClarkConfig& cfg) {
    if (!info.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CatmullClarkSystem::initInternal: raiiDevice is null");
        return false;
    }
    raiiDevice_ = info.raiiDevice;
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    renderPass = info.renderPass;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    graphicsQueue = info.graphicsQueue;
    commandPool = info.commandPool;
    config = cfg;

    // Create base mesh with RAII wrapper
    CatmullClarkMesh baseMesh;
    if (!config.objPath.empty()) {
        baseMesh = OBJLoader::loadQuadMesh(config.objPath);
        if (baseMesh.vertices.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load OBJ, falling back to cube");
            baseMesh = CatmullClarkMesh::createCube();
        }
    } else {
        baseMesh = CatmullClarkMesh::createCube();
    }

    mesh = std::move(baseMesh);
    if (!mesh->uploadToGPU(allocator)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to upload Catmull-Clark mesh to GPU");
        return false;
    }

    // Initialize CBT
    CatmullClarkCBT::InitInfo cbtInfo{};
    cbtInfo.allocator = allocator;
    cbtInfo.maxDepth = config.maxDepth;
    cbtInfo.faceCount = static_cast<int>(mesh->faces.size());

    cbt = CatmullClarkCBT::create(cbtInfo);
    if (!cbt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize Catmull-Clark CBT");
        return false;
    }

    // Create buffers and pipelines
    if (!createUniformBuffers()) return false;
    if (!createIndirectBuffers()) return false;
    if (!createComputeDescriptorSetLayout()) return false;
    if (!createRenderDescriptorSetLayout()) return false;
    if (!createDescriptorSets()) return false;
    if (!createSubdivisionPipeline()) return false;
    if (!createRenderPipeline()) return false;
    if (!createWireframePipeline()) return false;

    SDL_Log("Catmull-Clark subdivision system initialized");
    return true;
}

void CatmullClarkSystem::cleanup() {
    if (device == VK_NULL_HANDLE) return;

    // RAII-managed subsystems (mesh, cbt) are destroyed automatically via std::optional reset
    mesh.reset();
    cbt.reset();

    // RAII-managed buffers are destroyed automatically via reset/clear
    indirectDispatchBuffer_.reset();
    indirectDrawBuffer_.reset();
    uniformBuffers_.clear();
    uniformMappedPtrs.clear();

    // RAII-managed pipelines are destroyed automatically via reset
    subdivisionPipeline_.reset();
    renderPipeline_.reset();
    wireframePipeline_.reset();

    // RAII-managed pipeline layouts are destroyed automatically via reset
    subdivisionPipelineLayout_.reset();
    renderPipelineLayout_.reset();

    // RAII-managed descriptor set layouts are destroyed automatically via reset
    computeDescriptorSetLayout_.reset();
    renderDescriptorSetLayout_.reset();
}

bool CatmullClarkSystem::createUniformBuffers() {
    uniformBuffers_.resize(framesInFlight);
    uniformMappedPtrs.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; ++i) {
        if (!VmaBufferFactory::createUniformBuffer(allocator, sizeof(UniformBufferObject), uniformBuffers_[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create Catmull-Clark uniform buffer %u", i);
            return false;
        }
        uniformMappedPtrs[i] = uniformBuffers_[i].map();
    }

    return true;
}

bool CatmullClarkSystem::createIndirectBuffers() {
    // Indirect dispatch buffer
    if (!VmaBufferFactory::createIndirectBuffer(allocator, sizeof(VkDispatchIndirectCommand), indirectDispatchBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create indirect dispatch buffer");
        return false;
    }

    // Indirect draw buffer
    if (!VmaBufferFactory::createIndirectBuffer(allocator, sizeof(VkDrawIndirectCommand), indirectDrawBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create indirect draw buffer");
        return false;
    }

    return true;
}

bool CatmullClarkSystem::createComputeDescriptorSetLayout() {
    std::array<vk::DescriptorSetLayoutBinding, 5> bindings = {
        // Binding 0: Scene UBO
        vk::DescriptorSetLayoutBinding{}
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute),
        // Binding 1: CBT buffer
        vk::DescriptorSetLayoutBinding{}
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute),
        // Binding 2: Mesh vertices
        vk::DescriptorSetLayoutBinding{}
            .setBinding(2)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute),
        // Binding 3: Mesh halfedges
        vk::DescriptorSetLayoutBinding{}
            .setBinding(3)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute),
        // Binding 4: Mesh faces
        vk::DescriptorSetLayoutBinding{}
            .setBinding(4)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute)
    };

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindings(bindings);

    try {
        computeDescriptorSetLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create compute descriptor set layout: %s", e.what());
        return false;
    }

    return true;
}

bool CatmullClarkSystem::createRenderDescriptorSetLayout() {
    std::array<vk::DescriptorSetLayoutBinding, 5> bindings = {
        // Binding 0: Scene UBO
        vk::DescriptorSetLayoutBinding{}
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment),
        // Binding 1: CBT buffer
        vk::DescriptorSetLayoutBinding{}
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex),
        // Binding 2: Mesh vertices
        vk::DescriptorSetLayoutBinding{}
            .setBinding(2)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex),
        // Binding 3: Mesh halfedges
        vk::DescriptorSetLayoutBinding{}
            .setBinding(3)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex),
        // Binding 4: Mesh faces
        vk::DescriptorSetLayoutBinding{}
            .setBinding(4)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex)
    };

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindings(bindings);

    try {
        renderDescriptorSetLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create render descriptor set layout: %s", e.what());
        return false;
    }

    return true;
}

bool CatmullClarkSystem::createDescriptorSets() {
    // Allocate compute descriptor sets using managed pool
    computeDescriptorSets = descriptorPool->allocate(**computeDescriptorSetLayout_, framesInFlight);
    if (computeDescriptorSets.size() != framesInFlight) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate compute descriptor sets");
        return false;
    }

    // Allocate render descriptor sets using managed pool
    renderDescriptorSets = descriptorPool->allocate(**renderDescriptorSetLayout_, framesInFlight);
    if (renderDescriptorSets.size() != framesInFlight) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate render descriptor sets");
        return false;
    }

    return true;
}

void CatmullClarkSystem::updateDescriptorSets(VkDevice device, const std::vector<VkBuffer>& sceneUniformBuffers) {
    VkDeviceSize vertexBufferSize = mesh->vertices.size() * sizeof(CatmullClarkMesh::Vertex);
    VkDeviceSize halfedgeBufferSize = mesh->halfedges.size() * sizeof(CatmullClarkMesh::Halfedge);
    VkDeviceSize faceBufferSize = mesh->faces.size() * sizeof(CatmullClarkMesh::Face);

    for (uint32_t i = 0; i < framesInFlight; ++i) {
        // Compute descriptor set
        DescriptorManager::SetWriter(device, computeDescriptorSets[i])
            .writeBuffer(0, sceneUniformBuffers[i], 0, sizeof(UniformBufferObject))
            .writeBuffer(1, cbt->getBuffer(), 0, cbt->getBufferSize(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(2, mesh->getVertexBuffer(), 0, vertexBufferSize, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(3, mesh->getHalfedgeBuffer(), 0, halfedgeBufferSize, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(4, mesh->getFaceBuffer(), 0, faceBufferSize, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();

        // Render descriptor set (same buffers)
        DescriptorManager::SetWriter(device, renderDescriptorSets[i])
            .writeBuffer(0, sceneUniformBuffers[i], 0, sizeof(UniformBufferObject))
            .writeBuffer(1, cbt->getBuffer(), 0, cbt->getBufferSize(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(2, mesh->getVertexBuffer(), 0, vertexBufferSize, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(3, mesh->getHalfedgeBuffer(), 0, halfedgeBufferSize, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(4, mesh->getFaceBuffer(), 0, faceBufferSize, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();
    }
}

bool CatmullClarkSystem::createSubdivisionPipeline() {
    auto shaderModule = loadShaderModule(device, shaderPath + "/catmullclark_subdivision.comp.spv");
    if (!shaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load Catmull-Clark subdivision compute shader");
        return false;
    }

    vk::Device vkDevice(device);

    // Push constants for subdivision parameters
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(CatmullClarkSubdivisionPushConstants));

    vk::DescriptorSetLayout setLayout(**computeDescriptorSetLayout_);
    auto layoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(setLayout)
        .setPushConstantRanges(pushConstantRange);

    try {
        subdivisionPipelineLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create subdivision pipeline layout: %s", e.what());
        vkDevice.destroyShaderModule(*shaderModule);
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*shaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(**subdivisionPipelineLayout_);

    try {
        subdivisionPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create subdivision compute pipeline: %s", e.what());
        vkDevice.destroyShaderModule(*shaderModule);
        return false;
    }

    vkDevice.destroyShaderModule(*shaderModule);
    return true;
}

bool CatmullClarkSystem::createRenderPipeline() {
    auto vertModule = loadShaderModule(device, shaderPath + "/catmullclark_render.vert.spv");
    auto fragModule = loadShaderModule(device, shaderPath + "/catmullclark_render.frag.spv");
    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load Catmull-Clark render shaders");
        vk::Device vkDev(device);
        if (vertModule) vkDev.destroyShaderModule(*vertModule);
        if (fragModule) vkDev.destroyShaderModule(*fragModule);
        return false;
    }

    vk::Device vkDevice(device);

    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setModule(*vertModule)
            .setPName("main"),
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setModule(*fragModule)
            .setPName("main")
    };

    // No vertex input - all vertex data comes from buffers bound as storage buffers
    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{};

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList);

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setScissorCount(1);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eBack)
        .setFrontFace(vk::FrontFace::eCounterClockwise);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_TRUE)
        .setDepthCompareOp(vk::CompareOp::eLess);

    auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState{}
        .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setAttachments(colorBlendAttachment);

    std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStates(dynamicStates);

    // Push constants for model matrix
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex)
        .setOffset(0)
        .setSize(sizeof(CatmullClarkPushConstants));

    vk::DescriptorSetLayout setLayout(**renderDescriptorSetLayout_);
    auto layoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(setLayout)
        .setPushConstantRanges(pushConstantRange);

    try {
        renderPipelineLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create render pipeline layout: %s", e.what());
        vkDevice.destroyShaderModule(*vertModule);
        vkDevice.destroyShaderModule(*fragModule);
        return false;
    }

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
        .setLayout(**renderPipelineLayout_)
        .setRenderPass(renderPass)
        .setSubpass(0);

    try {
        renderPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create render graphics pipeline: %s", e.what());
        vkDevice.destroyShaderModule(*vertModule);
        vkDevice.destroyShaderModule(*fragModule);
        return false;
    }

    vkDevice.destroyShaderModule(*vertModule);
    vkDevice.destroyShaderModule(*fragModule);

    return true;
}

bool CatmullClarkSystem::createWireframePipeline() {
    auto vertModule = loadShaderModule(device, shaderPath + "/catmullclark_render.vert.spv");
    auto fragModule = loadShaderModule(device, shaderPath + "/catmullclark_render.frag.spv");
    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load Catmull-Clark wireframe shaders");
        vk::Device vkDev(device);
        if (vertModule) vkDev.destroyShaderModule(*vertModule);
        if (fragModule) vkDev.destroyShaderModule(*fragModule);
        return false;
    }

    vk::Device vkDevice(device);

    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setModule(*vertModule)
            .setPName("main"),
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setModule(*fragModule)
            .setPName("main")
    };

    // No vertex input - all vertex data comes from buffers bound as storage buffers
    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{};

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList);

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setScissorCount(1);

    // Wireframe rasterization
    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eLine)  // Wireframe mode
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)  // No culling for wireframe
        .setFrontFace(vk::FrontFace::eCounterClockwise);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_TRUE)
        .setDepthCompareOp(vk::CompareOp::eLess);

    auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState{}
        .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setAttachments(colorBlendAttachment);

    std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
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
        .setLayout(**renderPipelineLayout_)  // Reuse render pipeline layout
        .setRenderPass(renderPass)
        .setSubpass(0);

    try {
        wireframePipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create wireframe graphics pipeline: %s", e.what());
        vkDevice.destroyShaderModule(*vertModule);
        vkDevice.destroyShaderModule(*fragModule);
        return false;
    }

    vkDevice.destroyShaderModule(*vertModule);
    vkDevice.destroyShaderModule(*fragModule);

    return true;
}

void CatmullClarkSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                                         const glm::mat4& view, const glm::mat4& proj) {
    // The CatmullClarkSystem uses the shared scene UBO which is updated by the main renderer.
    // This method is provided for API consistency and future Catmull-Clark specific uniforms.
    // Currently no additional uniforms need updating here.
}

void CatmullClarkSystem::recordCompute(VkCommandBuffer cmd, uint32_t frameIndex) {
    vk::CommandBuffer vkCmd(cmd);

    // Bind the subdivision compute pipeline
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **subdivisionPipeline_);

    // Bind the compute descriptor set
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **subdivisionPipelineLayout_,
                             0, vk::DescriptorSet(computeDescriptorSets[frameIndex]), {});

    // Push subdivision parameters
    CatmullClarkSubdivisionPushConstants pc{};
    pc.targetEdgePixels = config.targetEdgePixels;
    pc.splitThreshold = config.splitThreshold;
    pc.mergeThreshold = config.mergeThreshold;
    pc.padding = 0;
    vkCmd.pushConstants<CatmullClarkSubdivisionPushConstants>(
        **subdivisionPipelineLayout_, vk::ShaderStageFlagBits::eCompute, 0, pc);

    // Dispatch compute shader - one workgroup per face at base level
    uint32_t workgroupCount = (cbt->getFaceCount() + SUBDIVISION_WORKGROUP_SIZE - 1) / SUBDIVISION_WORKGROUP_SIZE;
    vkCmd.dispatch(std::max(1u, workgroupCount), 1, 1);

    // Memory barrier: compute shader writes -> graphics shader reads
    auto memoryBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eIndirectCommandRead | vk::AccessFlagBits::eVertexAttributeRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                          vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eVertexInput,
                          {}, memoryBarrier, {}, {});
}

void CatmullClarkSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    vk::CommandBuffer vkCmd(cmd);

    // Select pipeline based on wireframe mode
    vk::Pipeline pipeline = wireframeMode ? **wireframePipeline_ : **renderPipeline_;
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

    // Bind descriptor set for this frame
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **renderPipelineLayout_,
                             0, vk::DescriptorSet(renderDescriptorSets[frameIndex]), {});

    // Set dynamic viewport
    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(extent.width))
        .setHeight(static_cast<float>(extent.height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    vkCmd.setViewport(0, viewport);

    // Set dynamic scissor
    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent(vk::Extent2D{extent.width, extent.height});
    vkCmd.setScissor(0, scissor);

    // Push model matrix
    CatmullClarkPushConstants pc{};
    pc.model = glm::translate(glm::mat4(1.0f), config.position) *
               glm::scale(glm::mat4(1.0f), config.scale);
    vkCmd.pushConstants<CatmullClarkPushConstants>(
        **renderPipelineLayout_, vk::ShaderStageFlagBits::eVertex, 0, pc);

    // Indirect draw - vertex count populated by subdivision compute shader
    vkCmd.drawIndirect(indirectDrawBuffer_.get(), 0, 1, sizeof(VkDrawIndirectCommand));
}
