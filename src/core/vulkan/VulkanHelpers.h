#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <SDL3/SDL_log.h>
#include <vector>
#include <array>
#include <optional>

// ============================================================================
// Render Pass Configuration
// ============================================================================

struct RenderPassConfig {
    VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkImageLayout finalColorLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkImageLayout finalDepthLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bool clearColor = true;
    bool clearDepth = true;
    bool storeDepth = true;
    bool depthOnly = false;
};

inline std::optional<vk::raii::RenderPass> createRenderPass(
    const vk::raii::Device& device,
    const RenderPassConfig& config)
{
    try {
        if (config.depthOnly) {
            auto depthAttachment = vk::AttachmentDescription{}
                .setFormat(static_cast<vk::Format>(config.depthFormat))
                .setSamples(vk::SampleCountFlagBits::e1)
                .setLoadOp(config.clearDepth ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad)
                .setStoreOp(config.storeDepth ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare)
                .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
                .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
                .setInitialLayout(vk::ImageLayout::eUndefined)
                .setFinalLayout(static_cast<vk::ImageLayout>(config.finalDepthLayout));

            auto depthAttachmentRef = vk::AttachmentReference{}
                .setAttachment(0)
                .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

            auto subpass = vk::SubpassDescription{}
                .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
                .setColorAttachmentCount(0)
                .setPDepthStencilAttachment(&depthAttachmentRef);

            auto dependency = vk::SubpassDependency{}
                .setSrcSubpass(VK_SUBPASS_EXTERNAL)
                .setDstSubpass(0)
                .setSrcStageMask(vk::PipelineStageFlagBits::eFragmentShader)
                .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
                .setDstStageMask(vk::PipelineStageFlagBits::eEarlyFragmentTests)
                .setDstAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentWrite);

            auto renderPassInfo = vk::RenderPassCreateInfo{}
                .setAttachmentCount(1)
                .setPAttachments(&depthAttachment)
                .setSubpassCount(1)
                .setPSubpasses(&subpass)
                .setDependencyCount(1)
                .setPDependencies(&dependency);

            return vk::raii::RenderPass(device, renderPassInfo);
        }

        // Standard color + depth render pass
        auto colorAttachment = vk::AttachmentDescription{}
            .setFormat(static_cast<vk::Format>(config.colorFormat))
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(config.clearColor ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setFinalLayout(static_cast<vk::ImageLayout>(config.finalColorLayout));

        auto depthAttachment = vk::AttachmentDescription{}
            .setFormat(static_cast<vk::Format>(config.depthFormat))
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(config.clearDepth ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad)
            .setStoreOp(config.storeDepth ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setFinalLayout(static_cast<vk::ImageLayout>(config.finalDepthLayout));

        auto colorAttachmentRef = vk::AttachmentReference{}
            .setAttachment(0)
            .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

        auto depthAttachmentRef = vk::AttachmentReference{}
            .setAttachment(1)
            .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

        auto subpass = vk::SubpassDescription{}
            .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
            .setColorAttachmentCount(1)
            .setPColorAttachments(&colorAttachmentRef)
            .setPDepthStencilAttachment(&depthAttachmentRef);

        auto dependency = vk::SubpassDependency{}
            .setSrcSubpass(VK_SUBPASS_EXTERNAL)
            .setDstSubpass(0)
            .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
            .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite);

        std::array<vk::AttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

        auto renderPassInfo = vk::RenderPassCreateInfo{}
            .setAttachmentCount(static_cast<uint32_t>(attachments.size()))
            .setPAttachments(attachments.data())
            .setSubpassCount(1)
            .setPSubpasses(&subpass)
            .setDependencyCount(1)
            .setPDependencies(&dependency);

        return vk::raii::RenderPass(device, renderPassInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create render pass: %s", e.what());
        return std::nullopt;
    }
}

// ============================================================================
// Depth Resources
// ============================================================================

struct DepthResources {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_D32_SFLOAT;

    void destroy(VkDevice device, VmaAllocator allocator) {
        if (sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, sampler, nullptr);
            sampler = VK_NULL_HANDLE;
        }
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
            view = VK_NULL_HANDLE;
        }
        if (image != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, image, allocation);
            image = VK_NULL_HANDLE;
            allocation = VK_NULL_HANDLE;
        }
    }
};

inline bool createDepthResources(
    VkDevice device,
    VmaAllocator allocator,
    VkExtent2D extent,
    VkFormat format,
    DepthResources& outResources)
{
    outResources.format = format;

    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setExtent(vk::Extent3D{extent.width, extent.height, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setFormat(static_cast<vk::Format>(format))
        .setTiling(vk::ImageTiling::eOptimal)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &outResources.image, &outResources.allocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth image");
        return false;
    }

    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(outResources.image)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(static_cast<vk::Format>(format))
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eDepth)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo),
                          nullptr, &outResources.view) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth image view");
        outResources.destroy(device, allocator);
        return false;
    }

    // Create depth sampler for Hi-Z pyramid generation
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMinLod(0.0f)
        .setMaxLod(0.0f);

    if (vkCreateSampler(device, reinterpret_cast<const VkSamplerCreateInfo*>(&samplerInfo),
                        nullptr, &outResources.sampler) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth sampler");
        outResources.destroy(device, allocator);
        return false;
    }

    return true;
}

inline bool createDepthImageAndView(
    VkDevice device,
    VmaAllocator allocator,
    VkExtent2D extent,
    VkFormat format,
    VkImage& outImage,
    VmaAllocation& outAllocation,
    VkImageView& outView)
{
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setExtent(vk::Extent3D{extent.width, extent.height, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setFormat(static_cast<vk::Format>(format))
        .setTiling(vk::ImageTiling::eOptimal)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &outImage, &outAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth image");
        return false;
    }

    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(outImage)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(static_cast<vk::Format>(format))
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eDepth)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo),
                          nullptr, &outView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth image view");
        vmaDestroyImage(allocator, outImage, outAllocation);
        outImage = VK_NULL_HANDLE;
        outAllocation = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

// ============================================================================
// Depth Array Resources (for shadow maps)
// ============================================================================

struct DepthArrayConfig {
    VkExtent2D extent;
    VkFormat format = VK_FORMAT_D32_SFLOAT;
    uint32_t arrayLayers = 1;
    bool cubeCompatible = false;
    bool createSampler = true;
};

struct DepthArrayResources {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView arrayView = VK_NULL_HANDLE;
    std::vector<VkImageView> layerViews;
    VkSampler sampler = VK_NULL_HANDLE;

    void destroy(VkDevice device, VmaAllocator allocator) {
        if (sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, sampler, nullptr);
            sampler = VK_NULL_HANDLE;
        }
        for (auto& view : layerViews) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, view, nullptr);
            }
        }
        layerViews.clear();
        if (arrayView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, arrayView, nullptr);
            arrayView = VK_NULL_HANDLE;
        }
        if (image != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, image, allocation);
            image = VK_NULL_HANDLE;
            allocation = VK_NULL_HANDLE;
        }
    }
};

inline bool createDepthArrayResources(
    VkDevice device,
    VmaAllocator allocator,
    const DepthArrayConfig& config,
    DepthArrayResources& outResources)
{
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setExtent(vk::Extent3D{config.extent.width, config.extent.height, 1})
        .setMipLevels(1)
        .setArrayLayers(config.arrayLayers)
        .setFormat(static_cast<vk::Format>(config.format))
        .setTiling(vk::ImageTiling::eOptimal)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setSharingMode(vk::SharingMode::eExclusive);
    if (config.cubeCompatible) {
        imageInfo.setFlags(vk::ImageCreateFlagBits::eCubeCompatible);
    }

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &outResources.image, &outResources.allocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth array image");
        return false;
    }

    auto arrayViewInfo = vk::ImageViewCreateInfo{}
        .setImage(outResources.image)
        .setViewType(config.cubeCompatible ? vk::ImageViewType::eCubeArray : vk::ImageViewType::e2DArray)
        .setFormat(static_cast<vk::Format>(config.format))
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eDepth)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(config.arrayLayers));

    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&arrayViewInfo),
                          nullptr, &outResources.arrayView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth array view");
        outResources.destroy(device, allocator);
        return false;
    }

    outResources.layerViews.resize(config.arrayLayers);
    for (uint32_t i = 0; i < config.arrayLayers; i++) {
        auto layerViewInfo = vk::ImageViewCreateInfo{}
            .setImage(outResources.image)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(static_cast<vk::Format>(config.format))
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(i)
                .setLayerCount(1));

        if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&layerViewInfo),
                              nullptr, &outResources.layerViews[i]) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth layer view %u", i);
            outResources.destroy(device, allocator);
            return false;
        }
    }

    if (config.createSampler) {
        auto samplerInfo = vk::SamplerCreateInfo{}
            .setMagFilter(vk::Filter::eLinear)
            .setMinFilter(vk::Filter::eLinear)
            .setMipmapMode(vk::SamplerMipmapMode::eNearest)
            .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
            .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
            .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
            .setBorderColor(vk::BorderColor::eFloatOpaqueWhite)
            .setCompareEnable(vk::True)
            .setCompareOp(vk::CompareOp::eLess);

        if (vkCreateSampler(device, reinterpret_cast<const VkSamplerCreateInfo*>(&samplerInfo),
                            nullptr, &outResources.sampler) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth array sampler");
            outResources.destroy(device, allocator);
            return false;
        }
    }

    return true;
}

// ============================================================================
// Framebuffers
// ============================================================================

inline std::optional<std::vector<vk::raii::Framebuffer>> createFramebuffers(
    const vk::raii::Device& device,
    const vk::raii::RenderPass& renderPass,
    const std::vector<VkImageView>& swapchainImageViews,
    VkImageView depthImageView,
    VkExtent2D extent)
{
    std::vector<vk::raii::Framebuffer> framebuffers;
    framebuffers.reserve(swapchainImageViews.size());

    try {
        for (size_t i = 0; i < swapchainImageViews.size(); i++) {
            std::array<vk::ImageView, 2> attachments = {
                static_cast<vk::ImageView>(swapchainImageViews[i]),
                static_cast<vk::ImageView>(depthImageView)
            };

            auto framebufferInfo = vk::FramebufferCreateInfo{}
                .setRenderPass(*renderPass)
                .setAttachmentCount(static_cast<uint32_t>(attachments.size()))
                .setPAttachments(attachments.data())
                .setWidth(extent.width)
                .setHeight(extent.height)
                .setLayers(1);

            framebuffers.emplace_back(device, framebufferInfo);
        }
        return framebuffers;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create framebuffer: %s", e.what());
        return std::nullopt;
    }
}

inline std::optional<std::vector<vk::raii::Framebuffer>> createDepthOnlyFramebuffers(
    const vk::raii::Device& device,
    VkRenderPass renderPass,
    const std::vector<VkImageView>& depthImageViews,
    VkExtent2D extent)
{
    std::vector<vk::raii::Framebuffer> framebuffers;
    framebuffers.reserve(depthImageViews.size());

    try {
        for (size_t i = 0; i < depthImageViews.size(); i++) {
            vk::ImageView attachment = static_cast<vk::ImageView>(depthImageViews[i]);

            auto framebufferInfo = vk::FramebufferCreateInfo{}
                .setRenderPass(renderPass)
                .setAttachmentCount(1)
                .setPAttachments(&attachment)
                .setWidth(extent.width)
                .setHeight(extent.height)
                .setLayers(1);

            framebuffers.emplace_back(device, framebufferInfo);
        }
        return framebuffers;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth-only framebuffer: %s", e.what());
        return std::nullopt;
    }
}
