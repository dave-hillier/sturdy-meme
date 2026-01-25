#pragma once

#include <type_traits>
#include <utility>
#include <vulkan/vulkan.hpp>

#include "InitContext.h"

namespace InitInfoBuilder {
namespace detail {
template <typename T, typename = void>
struct has_device : std::false_type {};
template <typename T>
struct has_device<T, std::void_t<decltype(std::declval<T>().device)>> : std::true_type {};

template <typename T, typename = void>
struct has_physical_device : std::false_type {};
template <typename T>
struct has_physical_device<T, std::void_t<decltype(std::declval<T>().physicalDevice)>> : std::true_type {};

template <typename T, typename = void>
struct has_allocator : std::false_type {};
template <typename T>
struct has_allocator<T, std::void_t<decltype(std::declval<T>().allocator)>> : std::true_type {};

template <typename T, typename = void>
struct has_command_pool : std::false_type {};
template <typename T>
struct has_command_pool<T, std::void_t<decltype(std::declval<T>().commandPool)>> : std::true_type {};

template <typename T, typename = void>
struct has_graphics_queue : std::false_type {};
template <typename T>
struct has_graphics_queue<T, std::void_t<decltype(std::declval<T>().graphicsQueue)>> : std::true_type {};

template <typename T, typename = void>
struct has_descriptor_pool : std::false_type {};
template <typename T>
struct has_descriptor_pool<T, std::void_t<decltype(std::declval<T>().descriptorPool)>> : std::true_type {};

template <typename T, typename = void>
struct has_extent : std::false_type {};
template <typename T>
struct has_extent<T, std::void_t<decltype(std::declval<T>().extent)>> : std::true_type {};

template <typename T, typename = void>
struct has_shader_path : std::false_type {};
template <typename T>
struct has_shader_path<T, std::void_t<decltype(std::declval<T>().shaderPath)>> : std::true_type {};

template <typename T, typename = void>
struct has_resource_path : std::false_type {};
template <typename T>
struct has_resource_path<T, std::void_t<decltype(std::declval<T>().resourcePath)>> : std::true_type {};

template <typename T, typename = void>
struct has_frames_in_flight : std::false_type {};
template <typename T>
struct has_frames_in_flight<T, std::void_t<decltype(std::declval<T>().framesInFlight)>> : std::true_type {};

template <typename T, typename = void>
struct has_max_frames_in_flight : std::false_type {};
template <typename T>
struct has_max_frames_in_flight<T, std::void_t<decltype(std::declval<T>().maxFramesInFlight)>> : std::true_type {};

template <typename T, typename = void>
struct has_raii_device : std::false_type {};
template <typename T>
struct has_raii_device<T, std::void_t<decltype(std::declval<T>().raiiDevice)>> : std::true_type {};

template <typename Info>
void assignDevice(Info& info, VkDevice device) {
    if constexpr (has_device<Info>::value) {
        using MemberType = std::decay_t<decltype(info.device)>;
        if constexpr (std::is_same_v<MemberType, VkDevice>) {
            info.device = device;
        } else if constexpr (std::is_same_v<MemberType, vk::Device>) {
            info.device = vk::Device(device);
        }
    }
}

template <typename Info>
void assignPhysicalDevice(Info& info, VkPhysicalDevice physicalDevice) {
    if constexpr (has_physical_device<Info>::value) {
        using MemberType = std::decay_t<decltype(info.physicalDevice)>;
        if constexpr (std::is_same_v<MemberType, VkPhysicalDevice>) {
            info.physicalDevice = physicalDevice;
        } else if constexpr (std::is_same_v<MemberType, vk::PhysicalDevice>) {
            info.physicalDevice = vk::PhysicalDevice(physicalDevice);
        }
    }
}

template <typename Info>
void assignAllocator(Info& info, VmaAllocator allocator) {
    if constexpr (has_allocator<Info>::value) {
        info.allocator = allocator;
    }
}

template <typename Info>
void assignCommandPool(Info& info, VkCommandPool commandPool) {
    if constexpr (has_command_pool<Info>::value) {
        using MemberType = std::decay_t<decltype(info.commandPool)>;
        if constexpr (std::is_same_v<MemberType, VkCommandPool>) {
            info.commandPool = commandPool;
        } else if constexpr (std::is_same_v<MemberType, vk::CommandPool>) {
            info.commandPool = vk::CommandPool(commandPool);
        }
    }
}

template <typename Info>
void assignGraphicsQueue(Info& info, VkQueue graphicsQueue) {
    if constexpr (has_graphics_queue<Info>::value) {
        using MemberType = std::decay_t<decltype(info.graphicsQueue)>;
        if constexpr (std::is_same_v<MemberType, VkQueue>) {
            info.graphicsQueue = graphicsQueue;
        } else if constexpr (std::is_same_v<MemberType, vk::Queue>) {
            info.graphicsQueue = vk::Queue(graphicsQueue);
        }
    }
}

template <typename Info>
void assignDescriptorPool(Info& info, DescriptorManager::Pool* descriptorPool) {
    if constexpr (has_descriptor_pool<Info>::value) {
        info.descriptorPool = descriptorPool;
    }
}

template <typename Info>
void assignExtent(Info& info, VkExtent2D extent) {
    if constexpr (has_extent<Info>::value) {
        using MemberType = std::decay_t<decltype(info.extent)>;
        if constexpr (std::is_same_v<MemberType, VkExtent2D>) {
            info.extent = extent;
        } else if constexpr (std::is_same_v<MemberType, vk::Extent2D>) {
            info.extent = vk::Extent2D{extent.width, extent.height};
        }
    }
}

template <typename Info>
void assignShaderPath(Info& info, const std::string& shaderPath) {
    if constexpr (has_shader_path<Info>::value) {
        info.shaderPath = shaderPath;
    }
}

template <typename Info>
void assignResourcePath(Info& info, const std::string& resourcePath) {
    if constexpr (has_resource_path<Info>::value) {
        info.resourcePath = resourcePath;
    }
}

template <typename Info>
void assignFramesInFlight(Info& info, uint32_t framesInFlight) {
    if constexpr (has_frames_in_flight<Info>::value) {
        info.framesInFlight = framesInFlight;
    }
    if constexpr (has_max_frames_in_flight<Info>::value) {
        info.maxFramesInFlight = framesInFlight;
    }
}

template <typename Info>
void assignRaiiDevice(Info& info, const vk::raii::Device* raiiDevice) {
    if constexpr (has_raii_device<Info>::value) {
        info.raiiDevice = raiiDevice;
    }
}
}  // namespace detail

template <typename Info>
Info fromContext(const InitContext& ctx) {
    Info info{};
    detail::assignDevice(info, ctx.device);
    detail::assignPhysicalDevice(info, ctx.physicalDevice);
    detail::assignAllocator(info, ctx.allocator);
    detail::assignCommandPool(info, ctx.commandPool);
    detail::assignGraphicsQueue(info, ctx.graphicsQueue);
    detail::assignDescriptorPool(info, ctx.descriptorPool);
    detail::assignExtent(info, ctx.extent);
    detail::assignShaderPath(info, ctx.shaderPath);
    detail::assignResourcePath(info, ctx.resourcePath);
    detail::assignFramesInFlight(info, ctx.framesInFlight);
    detail::assignRaiiDevice(info, ctx.raiiDevice);
    return info;
}
}  // namespace InitInfoBuilder
