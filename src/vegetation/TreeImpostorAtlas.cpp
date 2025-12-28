// TreeImpostorAtlas.cpp
// Core atlas management and archetype generation for TreeImpostorAtlas
// Pipeline and capture rendering are in TreeImpostorAtlasCapture.cpp

#include "TreeImpostorAtlas.h"
#include "TreeSystem.h"
#include "Mesh.h"
#include "OctahedralMapping.h"

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
            glm::vec3 viewDir = OctahedralMapping::hemiOctaDecode(cellCenter);

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
