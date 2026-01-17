#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <SDL3/SDL_log.h>
#include <vector>
#include <array>
#include <optional>
#include "VmaResources.h"

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
// Depth Resources (RAII)
// ============================================================================

struct DepthResources {
    VmaImage image;
    std::optional<vk::raii::ImageView> view;
    std::optional<vk::raii::Sampler> sampler;
    vk::Format format = vk::Format::eD32Sfloat;

    // Get raw handles for compatibility with existing code
    VkImage getImage() const { return image.get(); }
    VkImageView getView() const { return view ? **view : VK_NULL_HANDLE; }
    VkSampler getSampler() const { return sampler ? **sampler : VK_NULL_HANDLE; }

    void reset() {
        sampler.reset();
        view.reset();
        image.reset();
    }
};

inline bool createDepthResources(
    const vk::raii::Device& device,
    VmaAllocator allocator,
    vk::Extent2D extent,
    vk::Format format,
    DepthResources& outResources)
{
    outResources.format = format;

    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setExtent(vk::Extent3D{extent.width, extent.height, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setFormat(format)
        .setTiling(vk::ImageTiling::eOptimal)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (!VmaImage::create(allocator, imageInfo, allocInfo, outResources.image)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth image");
        return false;
    }

    try {
        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(outResources.image.get())
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(format)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        outResources.view = vk::raii::ImageView(device, viewInfo);

        // Create depth sampler for Hi-Z pyramid generation using SamplerFactory
        auto samplerOpt = SamplerFactory::createSamplerNearestClamp(device);
        if (!samplerOpt) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth sampler");
            outResources.reset();
            return false;
        }
        outResources.sampler = std::move(*samplerOpt);

        return true;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth resources: %s", e.what());
        outResources.reset();
        return false;
    }
}

inline bool createDepthImageAndView(
    const vk::raii::Device& device,
    VmaAllocator allocator,
    vk::Extent2D extent,
    vk::Format format,
    VmaImage& outImage,
    std::optional<vk::raii::ImageView>& outView)
{
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setExtent(vk::Extent3D{extent.width, extent.height, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setFormat(format)
        .setTiling(vk::ImageTiling::eOptimal)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (!VmaImage::create(allocator, imageInfo, allocInfo, outImage)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth image");
        return false;
    }

    try {
        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(outImage.get())
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(format)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        outView = vk::raii::ImageView(device, viewInfo);
        return true;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth image view: %s", e.what());
        outImage.reset();
        return false;
    }
}

// ============================================================================
// Depth Array Resources (for shadow maps) - RAII
// ============================================================================

struct DepthArrayConfig {
    vk::Extent2D extent;
    vk::Format format = vk::Format::eD32Sfloat;
    uint32_t arrayLayers = 1;
    bool cubeCompatible = false;
    bool createSampler = true;
};

struct DepthArrayResources {
    VmaImage image;
    std::optional<vk::raii::ImageView> arrayView;
    std::vector<vk::raii::ImageView> layerViews;
    std::optional<vk::raii::Sampler> sampler;

    // Get raw handles for compatibility with existing code
    VkImage getImage() const { return image.get(); }
    VkImageView getArrayView() const { return arrayView ? **arrayView : VK_NULL_HANDLE; }
    VkImageView getLayerView(size_t index) const {
        return index < layerViews.size() ? *layerViews[index] : VK_NULL_HANDLE;
    }
    VkSampler getSampler() const { return sampler ? **sampler : VK_NULL_HANDLE; }

    void reset() {
        sampler.reset();
        layerViews.clear();
        arrayView.reset();
        image.reset();
    }
};

inline bool createDepthArrayResources(
    const vk::raii::Device& device,
    VmaAllocator allocator,
    const DepthArrayConfig& config,
    DepthArrayResources& outResources)
{
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setExtent(vk::Extent3D{config.extent.width, config.extent.height, 1})
        .setMipLevels(1)
        .setArrayLayers(config.arrayLayers)
        .setFormat(config.format)
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

    if (!VmaImage::create(allocator, imageInfo, allocInfo, outResources.image)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth array image");
        return false;
    }

    try {
        auto arrayViewInfo = vk::ImageViewCreateInfo{}
            .setImage(outResources.image.get())
            .setViewType(config.cubeCompatible ? vk::ImageViewType::eCubeArray : vk::ImageViewType::e2DArray)
            .setFormat(config.format)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(config.arrayLayers));

        outResources.arrayView = vk::raii::ImageView(device, arrayViewInfo);

        outResources.layerViews.reserve(config.arrayLayers);
        for (uint32_t i = 0; i < config.arrayLayers; i++) {
            auto layerViewInfo = vk::ImageViewCreateInfo{}
                .setImage(outResources.image.get())
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(config.format)
                .setSubresourceRange(vk::ImageSubresourceRange{}
                    .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                    .setBaseMipLevel(0)
                    .setLevelCount(1)
                    .setBaseArrayLayer(i)
                    .setLayerCount(1));

            outResources.layerViews.emplace_back(device, layerViewInfo);
        }

        if (config.createSampler) {
            auto samplerOpt = SamplerFactory::createSamplerShadowComparison(device);
            if (!samplerOpt) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth array sampler");
                outResources.reset();
                return false;
            }
            outResources.sampler = std::move(*samplerOpt);
        }

        return true;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth array resources: %s", e.what());
        outResources.reset();
        return false;
    }
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
