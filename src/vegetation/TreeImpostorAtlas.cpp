#include "TreeImpostorAtlas.h"
#include "TreeSystem.h"
#include "CullCommon.h"
#include "Mesh.h"
#include "ShaderLoader.h"
#include "BufferUtils.h"
#include "shaders/bindings.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.hpp>

std::unique_ptr<TreeImpostorAtlas> TreeImpostorAtlas::create(const InitInfo& info) {
    auto atlas = std::unique_ptr<TreeImpostorAtlas>(new TreeImpostorAtlas());
    if (!atlas->initInternal(info)) {
        return nullptr;
    }
    return atlas;
}

TreeImpostorAtlas::~TreeImpostorAtlas() {
    if (device_ == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device_);

    // Cleanup leaf capture buffer (VMA-managed)
    if (leafCaptureBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, leafCaptureBuffer_, leafCaptureAllocation_);
    }

    // Cleanup leaf quad mesh (VMA-managed)
    if (leafQuadVertexBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, leafQuadVertexBuffer_, leafQuadVertexAllocation_);
    }
    if (leafQuadIndexBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, leafQuadIndexBuffer_, leafQuadIndexAllocation_);
    }

    // Cleanup array textures (VMA-managed images, RAII views)
    // Clear RAII views first, then destroy VMA images
    octaAlbedoArrayView_.reset();
    octaNormalArrayView_.reset();

    if (octaAlbedoArrayImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, octaAlbedoArrayImage_, octaAlbedoArrayAllocation_);
    }
    if (octaNormalArrayImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, octaNormalArrayImage_, octaNormalArrayAllocation_);
    }

    // Cleanup per-archetype atlas textures (VMA depth buffers, RAII views/framebuffers)
    for (auto& atlas : atlasTextures_) {
        atlas.framebuffer.reset();
        atlas.albedoView.reset();
        atlas.normalView.reset();
        atlas.depthView.reset();
        if (atlas.depthImage != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator_, atlas.depthImage, atlas.depthAllocation);
        }
    }

    // RAII types (pipelines, render pass, etc.) are automatically cleaned up
}

bool TreeImpostorAtlas::initInternal(const InitInfo& info) {
    raiiDevice_ = info.raiiDevice;
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    commandPool_ = info.commandPool;
    graphicsQueue_ = info.graphicsQueue;
    descriptorPool_ = info.descriptorPool;
    resourcePath_ = info.resourcePath;
    maxArchetypes_ = info.maxArchetypes;

    if (!createRenderPass()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create render pass");
        return false;
    }

    if (!createAtlasArrayTextures()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create atlas array textures");
        return false;
    }

    if (!createCapturePipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create capture pipeline");
        return false;
    }

    if (!createLeafCapturePipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create leaf capture pipeline");
        return false;
    }

    if (!createLeafQuadMesh()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create leaf quad mesh");
        return false;
    }

    if (!createSampler()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create sampler");
        return false;
    }

    SDL_Log("TreeImpostorAtlas: Initialized successfully");
    return true;
}

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

bool TreeImpostorAtlas::createAtlasArrayTextures() {
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setExtent(vk::Extent3D{OctahedralAtlasConfig::ATLAS_WIDTH, OctahedralAtlasConfig::ATLAS_HEIGHT, 1})
        .setMipLevels(1)
        .setArrayLayers(maxArchetypes_)
        .setFormat(vk::Format::eR8G8B8A8Unorm)
        .setTiling(vk::ImageTiling::eOptimal)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator_, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &octaAlbedoArrayImage_, &octaAlbedoArrayAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral albedo array image");
        return false;
    }

    if (vmaCreateImage(allocator_, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &octaNormalArrayImage_, &octaNormalArrayAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral normal array image");
        return false;
    }

    // Create image views using vk::raii
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setViewType(vk::ImageViewType::e2DArray)
        .setFormat(vk::Format::eR8G8B8A8Unorm)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(maxArchetypes_));

    viewInfo.setImage(octaAlbedoArrayImage_);
    octaAlbedoArrayView_.emplace(*raiiDevice_, viewInfo);

    viewInfo.setImage(octaNormalArrayImage_);
    octaNormalArrayView_.emplace(*raiiDevice_, viewInfo);

    // Transition both array images to shader read optimal layout
    vk::CommandBuffer cmd = (*raiiDevice_).allocateCommandBuffers(
        vk::CommandBufferAllocateInfo{}
            .setCommandPool(commandPool_)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1)
    )[0];

    cmd.begin(vk::CommandBufferBeginInfo{}.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    auto barrier = vk::ImageMemoryBarrier{}
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(maxArchetypes_))
        .setSrcAccessMask(vk::AccessFlags{})
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

    std::array<vk::ImageMemoryBarrier, 2> barriers = {barrier, barrier};
    barriers[0].setImage(octaAlbedoArrayImage_);
    barriers[1].setImage(octaNormalArrayImage_);

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eFragmentShader,
        vk::DependencyFlags{},
        nullptr, nullptr, barriers
    );

    cmd.end();

    vk::Queue queue(graphicsQueue_);
    queue.submit(vk::SubmitInfo{}.setCommandBuffers(cmd));
    queue.waitIdle();

    vk::Device(device_).freeCommandBuffers(commandPool_, cmd);

    SDL_Log("TreeImpostorAtlas: Created octahedral array textures (%dx%d, %d layers, %d views)",
            OctahedralAtlasConfig::ATLAS_WIDTH, OctahedralAtlasConfig::ATLAS_HEIGHT,
            maxArchetypes_, OctahedralAtlasConfig::TOTAL_CELLS);

    return true;
}

glm::vec2 TreeImpostorAtlas::hemiOctaEncode(glm::vec3 dir) {
    dir.y = glm::max(dir.y, 0.001f);
    float sum = glm::abs(dir.x) + glm::abs(dir.y) + glm::abs(dir.z);
    dir /= sum;
    glm::vec2 enc(dir.x, dir.z);
    glm::vec2 result;
    result.x = enc.x + enc.y;
    result.y = enc.y - enc.x;
    return result * 0.5f + 0.5f;
}

glm::vec3 TreeImpostorAtlas::hemiOctaDecode(glm::vec2 uv) {
    uv = uv * 2.0f - 1.0f;
    glm::vec2 enc;
    enc.x = (uv.x - uv.y) * 0.5f;
    enc.y = (uv.x + uv.y) * 0.5f;
    float y = 1.0f - glm::abs(enc.x) - glm::abs(enc.y);
    return glm::normalize(glm::vec3(enc.x, glm::max(y, 0.0f), enc.y));
}

bool TreeImpostorAtlas::createAtlasResources(uint32_t archetypeIndex) {
    if (archetypeIndex >= atlasTextures_.size()) {
        atlasTextures_.resize(archetypeIndex + 1);
    }

    if (archetypeIndex >= maxArchetypes_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TreeImpostorAtlas: Archetype index %u exceeds max %u", archetypeIndex, maxArchetypes_);
        return false;
    }

    auto& atlas = atlasTextures_[archetypeIndex];

    // Create per-layer views into the shared array textures
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Unorm)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(archetypeIndex)
            .setLayerCount(1));

    viewInfo.setImage(octaAlbedoArrayImage_);
    atlas.albedoView.emplace(*raiiDevice_, viewInfo);

    viewInfo.setImage(octaNormalArrayImage_);
    atlas.normalView.emplace(*raiiDevice_, viewInfo);

    // Create depth image (VMA-managed)
    auto depthImageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setExtent(vk::Extent3D{OctahedralAtlasConfig::ATLAS_WIDTH, OctahedralAtlasConfig::ATLAS_HEIGHT, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setFormat(vk::Format::eD32Sfloat)
        .setTiling(vk::ImageTiling::eOptimal)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator_, reinterpret_cast<const VkImageCreateInfo*>(&depthImageInfo), &allocInfo,
                       &atlas.depthImage, &atlas.depthAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create depth image");
        return false;
    }

    viewInfo.setImage(atlas.depthImage)
        .setFormat(vk::Format::eD32Sfloat)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eDepth)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));
    atlas.depthView.emplace(*raiiDevice_, viewInfo);

    // Create framebuffer
    std::array<vk::ImageView, 3> attachments = {
        **atlas.albedoView,
        **atlas.normalView,
        **atlas.depthView
    };

    auto fbInfo = vk::FramebufferCreateInfo{}
        .setRenderPass(**captureRenderPass_)
        .setAttachments(attachments)
        .setWidth(OctahedralAtlasConfig::ATLAS_WIDTH)
        .setHeight(OctahedralAtlasConfig::ATLAS_HEIGHT)
        .setLayers(1);

    atlas.framebuffer.emplace(*raiiDevice_, fbInfo);

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

bool TreeImpostorAtlas::createSampler() {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setAnisotropyEnable(true)
        .setMaxAnisotropy(4.0f)
        .setBorderColor(vk::BorderColor::eFloatTransparentBlack)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear);

    atlasSampler_.emplace(*raiiDevice_, samplerInfo);
    return true;
}

int32_t TreeImpostorAtlas::generateArchetype(
    const std::string& name,
    const TreeOptions& options,
    const Mesh& branchMesh,
    const std::vector<LeafInstanceGPU>& leafInstances,
    VkImageView barkAlbedo,
    VkImageView barkNormal,
    VkImageView leafAlbedo,
    VkSampler sampler) {

    uint32_t archetypeIndex = static_cast<uint32_t>(archetypes_.size());

    if (!createAtlasResources(archetypeIndex)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create atlas resources for %s", name.c_str());
        return -1;
    }

    glm::vec3 minBounds(FLT_MAX);
    glm::vec3 maxBounds(-FLT_MAX);

    for (const auto& vertex : branchMesh.getVertices()) {
        minBounds = glm::min(minBounds, vertex.position);
        maxBounds = glm::max(maxBounds, vertex.position);
    }

    for (const auto& leaf : leafInstances) {
        glm::vec3 leafPos = glm::vec3(leaf.positionAndSize);
        float leafSize = leaf.positionAndSize.w;
        minBounds = glm::min(minBounds, leafPos - glm::vec3(leafSize));
        maxBounds = glm::max(maxBounds, leafPos + glm::vec3(leafSize));
    }

    glm::vec3 treeCenter = (minBounds + maxBounds) * 0.5f;
    glm::vec3 treeExtent = maxBounds - minBounds;
    float horizontalRadius = glm::max(treeExtent.x, treeExtent.z) * 0.5f;
    float boundingSphereRadius = glm::length(treeExtent) * 0.5f;
    float centerHeight = treeCenter.y;
    float halfHeight = treeExtent.y * 0.5f;

    SDL_Log("TreeImpostorAtlas: Tree bounds X=[%.2f, %.2f], Y=[%.2f, %.2f], Z=[%.2f, %.2f]",
            minBounds.x, maxBounds.x, minBounds.y, maxBounds.y, minBounds.z, maxBounds.z);

    VkDescriptorSet leafCaptureDescSet = VK_NULL_HANDLE;
    if (!leafInstances.empty()) {
        VkDeviceSize requiredSize = leafInstances.size() * sizeof(LeafInstanceGPU);

        if (requiredSize > leafCaptureBufferSize_) {
            if (leafCaptureBuffer_ != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator_, leafCaptureBuffer_, leafCaptureAllocation_);
            }

            auto bufferInfo = vk::BufferCreateInfo{}
                .setSize(requiredSize)
                .setUsage(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

            if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo), &allocInfo, &leafCaptureBuffer_, &leafCaptureAllocation_, nullptr) != VK_SUCCESS) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create leaf capture buffer");
                return -1;
            }
            leafCaptureBufferSize_ = requiredSize;
        }

        void* data;
        vmaMapMemory(allocator_, leafCaptureAllocation_, &data);
        memcpy(data, leafInstances.data(), requiredSize);
        vmaUnmapMemory(allocator_, leafCaptureAllocation_);

        leafCaptureDescSet = descriptorPool_->allocateSingle(**leafCaptureDescriptorSetLayout_);
        if (leafCaptureDescSet != VK_NULL_HANDLE) {
            auto leafImageInfo = vk::DescriptorImageInfo{}
                .setSampler(sampler)
                .setImageView(leafAlbedo)
                .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

            auto normalInfo = vk::DescriptorImageInfo{}
                .setSampler(sampler)
                .setImageView(barkNormal)
                .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

            auto ssboInfo = vk::DescriptorBufferInfo{}
                .setBuffer(leafCaptureBuffer_)
                .setOffset(0)
                .setRange(VK_WHOLE_SIZE);

            std::array<vk::WriteDescriptorSet, 3> writes = {
                vk::WriteDescriptorSet{}.setDstSet(leafCaptureDescSet).setDstBinding(0)
                    .setDescriptorType(vk::DescriptorType::eCombinedImageSampler).setImageInfo(leafImageInfo),
                vk::WriteDescriptorSet{}.setDstSet(leafCaptureDescSet).setDstBinding(1)
                    .setDescriptorType(vk::DescriptorType::eCombinedImageSampler).setImageInfo(normalInfo),
                vk::WriteDescriptorSet{}.setDstSet(leafCaptureDescSet).setDstBinding(2)
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer).setBufferInfo(ssboInfo)
            };

            vk::Device(device_).updateDescriptorSets(writes, nullptr);
        }
    }

    VkDescriptorSet captureDescSet = descriptorPool_->allocateSingle(**captureDescriptorSetLayout_);
    if (captureDescSet == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to allocate descriptor set");
        return -1;
    }

    auto albedoInfo = vk::DescriptorImageInfo{}
        .setSampler(sampler)
        .setImageView(barkAlbedo)
        .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

    auto normalInfo = vk::DescriptorImageInfo{}
        .setSampler(sampler)
        .setImageView(barkNormal)
        .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

    std::array<vk::WriteDescriptorSet, 2> writes = {
        vk::WriteDescriptorSet{}.setDstSet(captureDescSet).setDstBinding(0)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler).setImageInfo(albedoInfo),
        vk::WriteDescriptorSet{}.setDstSet(captureDescSet).setDstBinding(1)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler).setImageInfo(normalInfo)
    };

    vk::Device(device_).updateDescriptorSets(writes, nullptr);

    vk::CommandBuffer cmd = (*raiiDevice_).allocateCommandBuffers(
        vk::CommandBufferAllocateInfo{}
            .setCommandPool(commandPool_)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1)
    )[0];

    cmd.begin(vk::CommandBufferBeginInfo{}.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    auto preBarrier = vk::ImageMemoryBarrier{}
        .setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(archetypeIndex)
            .setLayerCount(1))
        .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
        .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);

    std::array<vk::ImageMemoryBarrier, 2> preBarriers = {preBarrier, preBarrier};
    preBarriers[0].setImage(octaAlbedoArrayImage_);
    preBarriers[1].setImage(octaNormalArrayImage_);

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eFragmentShader,
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::DependencyFlags{}, nullptr, nullptr, preBarriers
    );

    std::array<vk::ClearValue, 3> clearValues = {
        vk::ClearValue{}.setColor(vk::ClearColorValue{}.setFloat32({0.0f, 0.0f, 0.0f, 0.0f})),
        vk::ClearValue{}.setColor(vk::ClearColorValue{}.setFloat32({0.5f, 0.5f, 0.5f, 1.0f})),
        vk::ClearValue{}.setDepthStencil({1.0f, 0})
    };

    cmd.beginRenderPass(
        vk::RenderPassBeginInfo{}
            .setRenderPass(**captureRenderPass_)
            .setFramebuffer(**atlasTextures_[archetypeIndex].framebuffer)
            .setRenderArea({{0, 0}, {OctahedralAtlasConfig::ATLAS_WIDTH, OctahedralAtlasConfig::ATLAS_HEIGHT}})
            .setClearValues(clearValues),
        vk::SubpassContents::eInline
    );

    int cellCount = 0;
    for (int y = 0; y < OctahedralAtlasConfig::GRID_SIZE; y++) {
        for (int x = 0; x < OctahedralAtlasConfig::GRID_SIZE; x++) {
            glm::vec2 cellCenter = (glm::vec2(x, y) + 0.5f) / static_cast<float>(OctahedralAtlasConfig::GRID_SIZE);
            glm::vec3 viewDir = hemiOctaDecode(cellCenter);

            renderOctahedralCell(cmd, x, y, viewDir, branchMesh, leafInstances,
                                 horizontalRadius, boundingSphereRadius, halfHeight,
                                 centerHeight, minBounds.y, captureDescSet, leafCaptureDescSet);
            cellCount++;
        }
    }

    cmd.endRenderPass();
    cmd.end();

    vk::Queue queue(graphicsQueue_);
    queue.submit(vk::SubmitInfo{}.setCommandBuffers(cmd));
    queue.waitIdle();

    vk::Device(device_).freeCommandBuffers(commandPool_, cmd);

    TreeImpostorArchetype archetype;
    archetype.name = name;
    archetype.treeType = options.bark.type;
    archetype.boundingSphereRadius = horizontalRadius;
    archetype.centerHeight = centerHeight;
    archetype.treeHeight = treeExtent.y;
    archetype.baseOffset = minBounds.y;
    archetype.albedoAlphaView = **atlasTextures_[archetypeIndex].albedoView;
    archetype.normalDepthAOView = **atlasTextures_[archetypeIndex].normalView;
    archetype.atlasIndex = archetypeIndex;

    archetypes_.push_back(archetype);

    SDL_Log("TreeImpostorAtlas: Generated archetype '%s' (%dx%d grid = %d views, hRadius=%.2f, height=%.2f)",
            name.c_str(), OctahedralAtlasConfig::GRID_SIZE, OctahedralAtlasConfig::GRID_SIZE, cellCount,
            horizontalRadius, treeExtent.y);

    return static_cast<int32_t>(archetypeIndex);
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

const TreeImpostorArchetype* TreeImpostorAtlas::getArchetype(const std::string& name) const {
    for (const auto& archetype : archetypes_) {
        if (archetype.name == name) {
            return &archetype;
        }
    }
    return nullptr;
}

const TreeImpostorArchetype* TreeImpostorAtlas::getArchetype(uint32_t index) const {
    if (index < archetypes_.size()) {
        return &archetypes_[index];
    }
    return nullptr;
}

VkDescriptorSet TreeImpostorAtlas::getPreviewDescriptorSet(uint32_t archetypeIndex) {
    if (archetypeIndex >= atlasTextures_.size()) {
        return VK_NULL_HANDLE;
    }

    if (atlasTextures_[archetypeIndex].previewDescriptorSet == VK_NULL_HANDLE) {
        if (atlasTextures_[archetypeIndex].albedoView) {
            atlasTextures_[archetypeIndex].previewDescriptorSet = ImGui_ImplVulkan_AddTexture(
                **atlasSampler_,
                **atlasTextures_[archetypeIndex].albedoView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
        }
    }

    return atlasTextures_[archetypeIndex].previewDescriptorSet;
}

VkDescriptorSet TreeImpostorAtlas::getNormalPreviewDescriptorSet(uint32_t archetypeIndex) {
    if (archetypeIndex >= atlasTextures_.size()) {
        return VK_NULL_HANDLE;
    }

    if (atlasTextures_[archetypeIndex].normalPreviewDescriptorSet == VK_NULL_HANDLE) {
        if (atlasTextures_[archetypeIndex].normalView) {
            atlasTextures_[archetypeIndex].normalPreviewDescriptorSet = ImGui_ImplVulkan_AddTexture(
                **atlasSampler_,
                **atlasTextures_[archetypeIndex].normalView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
        }
    }

    return atlasTextures_[archetypeIndex].normalPreviewDescriptorSet;
}
