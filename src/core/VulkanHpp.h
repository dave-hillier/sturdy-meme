#pragma once

// Vulkan-Hpp Integration Header
// This header configures Vulkan-Hpp for use alongside the existing codebase.
// It provides type-safe C++ bindings while maintaining compatibility with VMA.

// Configure Vulkan-Hpp before including
#define VULKAN_HPP_NO_CONSTRUCTORS  // Use aggregate initialization
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1  // Dynamic dispatch for extensions

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

// Initialize the dynamic dispatcher - call once at startup after vkGetInstanceProcAddr is available
// Usage: VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
//        VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
//        VULKAN_HPP_DEFAULT_DISPATCHER.init(device);
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace vkh {

// Convenience aliases for commonly used RAII types
using Buffer = vk::raii::Buffer;
using Image = vk::raii::Image;
using ImageView = vk::raii::ImageView;
using Sampler = vk::raii::Sampler;
using Pipeline = vk::raii::Pipeline;
using PipelineLayout = vk::raii::PipelineLayout;
using DescriptorSetLayout = vk::raii::DescriptorSetLayout;
using DescriptorPool = vk::raii::DescriptorPool;
using RenderPass = vk::raii::RenderPass;
using Framebuffer = vk::raii::Framebuffer;
using CommandPool = vk::raii::CommandPool;
using CommandBuffer = vk::raii::CommandBuffer;
using Fence = vk::raii::Fence;
using Semaphore = vk::raii::Semaphore;
using ShaderModule = vk::raii::ShaderModule;
using Device = vk::raii::Device;
using Instance = vk::raii::Instance;
using PhysicalDevice = vk::raii::PhysicalDevice;
using Queue = vk::raii::Queue;
using DeviceMemory = vk::raii::DeviceMemory;

// Context for RAII objects - wraps the vk::raii::Context
// This must be created before any other RAII objects
using Context = vk::raii::Context;

// Interoperability helpers - convert between raw handles and RAII types

// Get raw handle from RAII object (for passing to existing code)
template<typename T>
auto raw(const T& raiiObject) -> decltype(*raiiObject) {
    return *raiiObject;
}

// Create info struct helpers with sensible defaults
inline vk::BufferCreateInfo bufferCreateInfo(
    vk::DeviceSize size,
    vk::BufferUsageFlags usage,
    vk::SharingMode sharingMode = vk::SharingMode::eExclusive
) {
    return vk::BufferCreateInfo{
        {},           // flags
        size,
        usage,
        sharingMode,
        0,            // queueFamilyIndexCount
        nullptr       // pQueueFamilyIndices
    };
}

inline vk::ImageCreateInfo imageCreateInfo2D(
    uint32_t width,
    uint32_t height,
    vk::Format format,
    vk::ImageUsageFlags usage,
    uint32_t mipLevels = 1,
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1
) {
    return vk::ImageCreateInfo{
        {},                          // flags
        vk::ImageType::e2D,
        format,
        vk::Extent3D{width, height, 1},
        mipLevels,
        1,                           // arrayLayers
        samples,
        vk::ImageTiling::eOptimal,
        usage,
        vk::SharingMode::eExclusive,
        0,                           // queueFamilyIndexCount
        nullptr,                     // pQueueFamilyIndices
        vk::ImageLayout::eUndefined
    };
}

inline vk::ImageViewCreateInfo imageViewCreateInfo2D(
    vk::Image image,
    vk::Format format,
    vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor,
    uint32_t baseMipLevel = 0,
    uint32_t mipLevels = 1
) {
    return vk::ImageViewCreateInfo{
        {},                          // flags
        image,
        vk::ImageViewType::e2D,
        format,
        vk::ComponentMapping{},      // identity swizzle
        vk::ImageSubresourceRange{
            aspectFlags,
            baseMipLevel,
            mipLevels,
            0,                       // baseArrayLayer
            1                        // layerCount
        }
    };
}

inline vk::SamplerCreateInfo samplerCreateInfoLinear(
    vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eClampToEdge
) {
    return vk::SamplerCreateInfo{
        {},                              // flags
        vk::Filter::eLinear,             // magFilter
        vk::Filter::eLinear,             // minFilter
        vk::SamplerMipmapMode::eLinear,
        addressMode,                     // addressModeU
        addressMode,                     // addressModeV
        addressMode,                     // addressModeW
        0.0f,                            // mipLodBias
        VK_FALSE,                        // anisotropyEnable
        1.0f,                            // maxAnisotropy
        VK_FALSE,                        // compareEnable
        vk::CompareOp::eNever,
        0.0f,                            // minLod
        VK_LOD_CLAMP_NONE,               // maxLod
        vk::BorderColor::eFloatTransparentBlack,
        VK_FALSE                         // unnormalizedCoordinates
    };
}

inline vk::PipelineShaderStageCreateInfo shaderStageCreateInfo(
    vk::ShaderStageFlagBits stage,
    vk::ShaderModule module,
    const char* entryPoint = "main"
) {
    return vk::PipelineShaderStageCreateInfo{
        {},           // flags
        stage,
        module,
        entryPoint,
        nullptr       // pSpecializationInfo
    };
}

// Descriptor set layout binding helper
inline vk::DescriptorSetLayoutBinding descriptorBinding(
    uint32_t binding,
    vk::DescriptorType type,
    vk::ShaderStageFlags stageFlags,
    uint32_t count = 1
) {
    return vk::DescriptorSetLayoutBinding{
        binding,
        type,
        count,
        stageFlags,
        nullptr  // pImmutableSamplers
    };
}

// Write descriptor set helper for uniform buffer
inline vk::WriteDescriptorSet writeDescriptorBuffer(
    vk::DescriptorSet dstSet,
    uint32_t binding,
    vk::DescriptorType type,
    const vk::DescriptorBufferInfo* bufferInfo,
    uint32_t count = 1
) {
    return vk::WriteDescriptorSet{
        dstSet,
        binding,
        0,           // dstArrayElement
        count,
        type,
        nullptr,     // pImageInfo
        bufferInfo,
        nullptr      // pTexelBufferView
    };
}

// Write descriptor set helper for image/sampler
inline vk::WriteDescriptorSet writeDescriptorImage(
    vk::DescriptorSet dstSet,
    uint32_t binding,
    vk::DescriptorType type,
    const vk::DescriptorImageInfo* imageInfo,
    uint32_t count = 1
) {
    return vk::WriteDescriptorSet{
        dstSet,
        binding,
        0,           // dstArrayElement
        count,
        type,
        imageInfo,
        nullptr,     // pBufferInfo
        nullptr      // pTexelBufferView
    };
}

} // namespace vkh
