// TreeImpostorAtlasCapture.cpp
// Pipeline creation and capture rendering for TreeImpostorAtlas
// This file contains the capture pipeline setup and rendering functions

#include "TreeImpostorAtlas.h"
#include "TreeSystem.h"
#include "Mesh.h"
#include "ShaderLoader.h"
#include "OctahedralMapping.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <vulkan/vulkan.hpp>

bool TreeImpostorAtlas::createRenderPass() {
    // Two color attachments: albedo+alpha and normal+depth+AO
    std::array<vk::AttachmentDescription, 3> attachments = {
        vk::AttachmentDescription{}
            .setFormat(vk::Format::eR8G8B8A8Unorm)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal),
        vk::AttachmentDescription{}
            .setFormat(vk::Format::eR8G8B8A8Unorm)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal),
        vk::AttachmentDescription{}
            .setFormat(vk::Format::eD32Sfloat)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
    };

    std::array<vk::AttachmentReference, 2> colorRefs = {
        vk::AttachmentReference{}.setAttachment(0).setLayout(vk::ImageLayout::eColorAttachmentOptimal),
        vk::AttachmentReference{}.setAttachment(1).setLayout(vk::ImageLayout::eColorAttachmentOptimal)
    };

    auto depthRef = vk::AttachmentReference{}
        .setAttachment(2)
        .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

    auto subpass = vk::SubpassDescription{}
        .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachments(colorRefs)
        .setPDepthStencilAttachment(&depthRef);

    auto dependency = vk::SubpassDependency{}
        .setSrcSubpass(VK_SUBPASS_EXTERNAL)
        .setDstSubpass(0)
        .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
        .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
        .setSrcAccessMask(vk::AccessFlags{})
        .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite);

    auto renderPassInfo = vk::RenderPassCreateInfo{}
        .setAttachments(attachments)
        .setSubpasses(subpass)
        .setDependencies(dependency);

    captureRenderPass_.emplace(*raiiDevice_, renderPassInfo);
    return true;
}

bool TreeImpostorAtlas::createCapturePipeline() {
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {
        vk::DescriptorSetLayoutBinding{}
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding{}
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment)
    };

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}.setBindings(bindings);
    captureDescriptorSetLayout_.emplace(*raiiDevice_, layoutInfo);

    auto pushConstant = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        .setOffset(0)
        .setSize(sizeof(glm::mat4) * 2 + sizeof(glm::vec4));

    vk::DescriptorSetLayout dsl = **captureDescriptorSetLayout_;
    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(dsl)
        .setPushConstantRanges(pushConstant);

    capturePipelineLayout_.emplace(*raiiDevice_, pipelineLayoutInfo);

    std::string shaderPath = resourcePath_ + "/shaders/";
    auto vertModule = ShaderLoader::loadShaderModule(device_, shaderPath + "tree_impostor_capture.vert.spv");
    auto fragModule = ShaderLoader::loadShaderModule(device_, shaderPath + "tree_impostor_capture.frag.spv");

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to load capture shaders");
        return false;
    }

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

    std::array<vk::VertexInputBindingDescription, 1> bindingDescriptions = {
        vk::VertexInputBindingDescription{}
            .setBinding(0)
            .setStride(sizeof(Vertex))
            .setInputRate(vk::VertexInputRate::eVertex)
    };

    std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions = {
        vk::VertexInputAttributeDescription{}.setLocation(0).setBinding(0).setFormat(vk::Format::eR32G32B32Sfloat).setOffset(offsetof(Vertex, position)),
        vk::VertexInputAttributeDescription{}.setLocation(1).setBinding(0).setFormat(vk::Format::eR32G32B32Sfloat).setOffset(offsetof(Vertex, normal)),
        vk::VertexInputAttributeDescription{}.setLocation(2).setBinding(0).setFormat(vk::Format::eR32G32Sfloat).setOffset(offsetof(Vertex, texCoord))
    };

    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{}
        .setVertexBindingDescriptions(bindingDescriptions)
        .setVertexAttributeDescriptions(attributeDescriptions);

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList);

    auto viewport = vk::Viewport{0.0f, 0.0f,
        static_cast<float>(OctahedralAtlasConfig::CELL_SIZE),
        static_cast<float>(OctahedralAtlasConfig::CELL_SIZE), 0.0f, 1.0f};
    auto scissor = vk::Rect2D{{0, 0}, {OctahedralAtlasConfig::CELL_SIZE, OctahedralAtlasConfig::CELL_SIZE}};

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewports(viewport)
        .setScissors(scissor);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(true)
        .setDepthWriteEnable(true)
        .setDepthCompareOp(vk::CompareOp::eLess);

    std::array<vk::PipelineColorBlendAttachmentState, 2> colorBlendAttachments = {
        vk::PipelineColorBlendAttachmentState{}
            .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA),
        vk::PipelineColorBlendAttachmentState{}
            .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
    };

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}.setAttachments(colorBlendAttachments);

    std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}.setDynamicStates(dynamicStates);

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
        .setLayout(**capturePipelineLayout_)
        .setRenderPass(**captureRenderPass_)
        .setSubpass(0);

    branchCapturePipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);

    vkDestroyShaderModule(device_, *vertModule, nullptr);
    vkDestroyShaderModule(device_, *fragModule, nullptr);

    return true;
}

bool TreeImpostorAtlas::createLeafCapturePipeline() {
    std::array<vk::DescriptorSetLayoutBinding, 3> bindings = {
        vk::DescriptorSetLayoutBinding{}
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding{}
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding{}
            .setBinding(2)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex)
    };

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}.setBindings(bindings);
    leafCaptureDescriptorSetLayout_.emplace(*raiiDevice_, layoutInfo);

    auto pushConstant = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        .setOffset(0)
        .setSize(sizeof(glm::mat4) * 2 + sizeof(glm::vec4) + sizeof(int32_t));

    vk::DescriptorSetLayout dsl = **leafCaptureDescriptorSetLayout_;
    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(dsl)
        .setPushConstantRanges(pushConstant);

    leafCapturePipelineLayout_.emplace(*raiiDevice_, pipelineLayoutInfo);

    std::string shaderPath = resourcePath_ + "/shaders/";
    auto vertModule = ShaderLoader::loadShaderModule(device_, shaderPath + "tree_impostor_capture_leaf.vert.spv");
    auto fragModule = ShaderLoader::loadShaderModule(device_, shaderPath + "tree_impostor_capture.frag.spv");

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to load leaf capture shaders");
        return false;
    }

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

    std::array<vk::VertexInputBindingDescription, 1> bindingDescriptions = {
        vk::VertexInputBindingDescription{}.setBinding(0).setStride(sizeof(Vertex)).setInputRate(vk::VertexInputRate::eVertex)
    };

    std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions = {
        vk::VertexInputAttributeDescription{}.setLocation(0).setBinding(0).setFormat(vk::Format::eR32G32B32Sfloat).setOffset(offsetof(Vertex, position)),
        vk::VertexInputAttributeDescription{}.setLocation(1).setBinding(0).setFormat(vk::Format::eR32G32B32Sfloat).setOffset(offsetof(Vertex, normal)),
        vk::VertexInputAttributeDescription{}.setLocation(2).setBinding(0).setFormat(vk::Format::eR32G32Sfloat).setOffset(offsetof(Vertex, texCoord))
    };

    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{}
        .setVertexBindingDescriptions(bindingDescriptions)
        .setVertexAttributeDescriptions(attributeDescriptions);

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}.setTopology(vk::PrimitiveTopology::eTriangleList);
    auto viewportState = vk::PipelineViewportStateCreateInfo{}.setViewportCount(1).setScissorCount(1);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}.setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(true)
        .setDepthWriteEnable(true)
        .setDepthCompareOp(vk::CompareOp::eLess);

    std::array<vk::PipelineColorBlendAttachmentState, 2> colorBlendAttachments = {
        vk::PipelineColorBlendAttachmentState{}
            .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA),
        vk::PipelineColorBlendAttachmentState{}
            .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
    };

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}.setAttachments(colorBlendAttachments);

    std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}.setDynamicStates(dynamicStates);

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
        .setLayout(**leafCapturePipelineLayout_)
        .setRenderPass(**captureRenderPass_)
        .setSubpass(0);

    leafCapturePipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);

    vkDestroyShaderModule(device_, *vertModule, nullptr);
    vkDestroyShaderModule(device_, *fragModule, nullptr);

    SDL_Log("TreeImpostorAtlas: Created leaf capture pipeline");
    return true;
}

bool TreeImpostorAtlas::createLeafQuadMesh() {
    std::array<Vertex, 4> vertices = {{
        {glm::vec3(-0.5f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(0.0f, 1.0f)},
        {glm::vec3( 0.5f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(1.0f, 1.0f)},
        {glm::vec3( 0.5f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(1.0f, 0.0f)},
        {glm::vec3(-0.5f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(0.0f, 0.0f)},
    }};

    std::array<uint32_t, 6> indices = {0, 1, 2, 2, 3, 0};
    leafQuadIndexCount_ = 6;

    VkDeviceSize vertexSize = sizeof(vertices);
    VkDeviceSize indexSize = sizeof(indices);
    VkDeviceSize stagingSize = vertexSize + indexSize;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    auto stagingInfo = vk::BufferCreateInfo{}
        .setSize(stagingSize)
        .setUsage(vk::BufferUsageFlagBits::eTransferSrc);

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&stagingInfo), &stagingAllocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    void* data;
    vmaMapMemory(allocator_, stagingAllocation, &data);
    memcpy(data, vertices.data(), vertexSize);
    memcpy(static_cast<char*>(data) + vertexSize, indices.data(), indexSize);
    vmaUnmapMemory(allocator_, stagingAllocation);

    auto vertexBufferInfo = vk::BufferCreateInfo{}
        .setSize(vertexSize)
        .setUsage(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst);

    VmaAllocationCreateInfo gpuAllocInfo{};
    gpuAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&vertexBufferInfo), &gpuAllocInfo, &leafQuadVertexBuffer_, &leafQuadVertexAllocation_, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
        return false;
    }

    auto indexBufferInfo = vk::BufferCreateInfo{}
        .setSize(indexSize)
        .setUsage(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst);

    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&indexBufferInfo), &gpuAllocInfo, &leafQuadIndexBuffer_, &leafQuadIndexAllocation_, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
        return false;
    }

    vk::CommandBuffer cmd = (*raiiDevice_).allocateCommandBuffers(
        vk::CommandBufferAllocateInfo{}
            .setCommandPool(commandPool_)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1)
    )[0];

    cmd.begin(vk::CommandBufferBeginInfo{}.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    cmd.copyBuffer(stagingBuffer, leafQuadVertexBuffer_, vk::BufferCopy{0, 0, vertexSize});
    cmd.copyBuffer(stagingBuffer, leafQuadIndexBuffer_, vk::BufferCopy{vertexSize, 0, indexSize});
    cmd.end();

    vk::Queue queue(graphicsQueue_);
    queue.submit(vk::SubmitInfo{}.setCommandBuffers(cmd));
    queue.waitIdle();

    vk::Device(device_).freeCommandBuffers(commandPool_, cmd);
    vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);

    SDL_Log("TreeImpostorAtlas: Created leaf quad mesh");
    return true;
}

void TreeImpostorAtlas::renderOctahedralCell(
    VkCommandBuffer cmd,
    int cellX, int cellY,
    glm::vec3 viewDirection,
    const Mesh& branchMesh,
    const std::vector<LeafInstanceGPU>& leafInstances,
    float horizontalRadius,
    float boundingSphereRadius,
    float halfHeight,
    float centerHeight,
    float baseY,
    VkDescriptorSet branchDescSet,
    VkDescriptorSet leafDescSet) {

    vk::CommandBuffer vkCmd(cmd);

    vk::Viewport viewport{
        static_cast<float>(cellX * OctahedralAtlasConfig::CELL_SIZE),
        static_cast<float>(cellY * OctahedralAtlasConfig::CELL_SIZE),
        static_cast<float>(OctahedralAtlasConfig::CELL_SIZE),
        static_cast<float>(OctahedralAtlasConfig::CELL_SIZE),
        0.0f, 1.0f
    };

    vk::Rect2D scissor{
        {cellX * OctahedralAtlasConfig::CELL_SIZE, cellY * OctahedralAtlasConfig::CELL_SIZE},
        {static_cast<uint32_t>(OctahedralAtlasConfig::CELL_SIZE),
         static_cast<uint32_t>(OctahedralAtlasConfig::CELL_SIZE)}
    };

    vkCmd.setViewport(0, viewport);
    vkCmd.setScissor(0, scissor);

    float camDist = boundingSphereRadius * 3.0f;
    glm::vec3 target(0.0f, centerHeight, 0.0f);
    glm::vec3 camPos = target + viewDirection * camDist;

    float elevation = glm::degrees(glm::asin(glm::clamp(viewDirection.y, -1.0f, 1.0f)));
    glm::vec3 up = (elevation > 80.0f) ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

    glm::mat4 view = glm::lookAt(camPos, target, up);

    float elevationFactor = glm::abs(elevation) / 90.0f;
    float blendFactor = elevationFactor * elevationFactor;
    float effectiveHSize = glm::mix(horizontalRadius, boundingSphereRadius, blendFactor) * TreeLODConstants::IMPOSTOR_SIZE_MARGIN;
    float effectiveVSize = halfHeight * TreeLODConstants::IMPOSTOR_SIZE_MARGIN;
    float projSize = glm::max(effectiveHSize, effectiveVSize);

    glm::mat4 proj = glm::ortho(-projSize, projSize, -projSize, projSize, 0.1f, camDist + boundingSphereRadius * 2.0f);
    proj[1][1] *= -1;
    proj[3][1] *= -1;

    glm::mat4 viewProj = proj * view;

    // Draw branches
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **branchCapturePipeline_);
    vk::DescriptorSet branchDescSetVk(branchDescSet);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **capturePipelineLayout_, 0, branchDescSetVk, {});

    vk::Buffer branchVertexBuffers[] = {vk::Buffer(branchMesh.getVertexBuffer())};
    vk::DeviceSize offsets[] = {0};
    vkCmd.bindVertexBuffers(0, 1, branchVertexBuffers, offsets);
    vkCmd.bindIndexBuffer(vk::Buffer(branchMesh.getIndexBuffer()), 0, vk::IndexType::eUint32);

    struct {
        glm::mat4 viewProj;
        glm::mat4 model;
        glm::vec4 captureParams;
    } branchPush;

    branchPush.viewProj = viewProj;
    branchPush.model = glm::mat4(1.0f);
    branchPush.captureParams = glm::vec4(
        static_cast<float>(cellX + cellY * OctahedralAtlasConfig::GRID_SIZE),
        0.0f, boundingSphereRadius, 0.1f
    );

    vkCmd.pushConstants<decltype(branchPush)>(
        **capturePipelineLayout_,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, branchPush
    );

    vkCmd.drawIndexed(branchMesh.getIndexCount(), 1, 0, 0, 0);

    // Draw leaves
    if (leafDescSet != VK_NULL_HANDLE && !leafInstances.empty() && leafQuadIndexCount_ > 0) {
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **leafCapturePipeline_);
        vk::DescriptorSet leafDescSetVk(leafDescSet);
        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **leafCapturePipelineLayout_, 0, leafDescSetVk, {});

        vk::Buffer leafVertexBuffers[] = {vk::Buffer(leafQuadVertexBuffer_)};
        vkCmd.bindVertexBuffers(0, 1, leafVertexBuffers, offsets);
        vkCmd.bindIndexBuffer(vk::Buffer(leafQuadIndexBuffer_), 0, vk::IndexType::eUint32);

        vkCmd.setViewport(0, viewport);
        vkCmd.setScissor(0, scissor);

        struct {
            glm::mat4 viewProj;
            glm::mat4 model;
            glm::vec4 captureParams;
            int32_t firstInstance;
        } leafPush;

        leafPush.viewProj = viewProj;
        leafPush.model = glm::mat4(1.0f);
        leafPush.captureParams = glm::vec4(
            static_cast<float>(cellX + cellY * OctahedralAtlasConfig::GRID_SIZE),
            1.0f, boundingSphereRadius, 0.3f
        );
        leafPush.firstInstance = 0;

        vkCmd.pushConstants<decltype(leafPush)>(
            **leafCapturePipelineLayout_,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, leafPush
        );

        vkCmd.drawIndexed(leafQuadIndexCount_, static_cast<uint32_t>(leafInstances.size()), 0, 0, 0);
    }
}
