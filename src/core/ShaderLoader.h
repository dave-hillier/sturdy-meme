#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <string>
#include <optional>
#include <string_view>

namespace ShaderLoader {

struct RaiiTag {
    explicit RaiiTag() = default;
};

class ScopedShaderModule {
public:
    ScopedShaderModule() = default;
    ScopedShaderModule(VkDevice device, VkShaderModule module);
    ~ScopedShaderModule();

    ScopedShaderModule(const ScopedShaderModule&) = delete;
    ScopedShaderModule& operator=(const ScopedShaderModule&) = delete;
    ScopedShaderModule(ScopedShaderModule&& other) noexcept;
    ScopedShaderModule& operator=(ScopedShaderModule&& other) noexcept;

    VkShaderModule get() const { return module_; }
    explicit operator bool() const { return module_ != VK_NULL_HANDLE; }

private:
    void reset();

    VkDevice device_ = VK_NULL_HANDLE;
    VkShaderModule module_ = VK_NULL_HANDLE;
};

std::optional<std::vector<char>> readFile(const std::string& filename);
std::optional<vk::ShaderModule> createShaderModule(vk::Device device, const std::vector<char>& code);
std::optional<vk::ShaderModule> loadShaderModule(vk::Device device, const std::string& path);
std::optional<vk::raii::ShaderModule> createShaderModule(const vk::raii::Device& device, const std::vector<char>& code);
std::optional<vk::raii::ShaderModule> loadShaderModule(const vk::raii::Device& device, const std::string& path);
std::optional<ScopedShaderModule> createShaderModule(VkDevice device, const std::vector<char>& code, RaiiTag);
std::optional<ScopedShaderModule> loadShaderModule(VkDevice device, std::string_view path, RaiiTag);

// Convenience overloads for raw VkDevice (for gradual migration)
inline std::optional<VkShaderModule> createShaderModule(VkDevice device, const std::vector<char>& code) {
    auto result = createShaderModule(vk::Device(device), code);
    if (result) return static_cast<VkShaderModule>(*result);
    return std::nullopt;
}
inline std::optional<VkShaderModule> loadShaderModule(VkDevice device, const std::string& path) {
    auto result = loadShaderModule(vk::Device(device), path);
    if (result) return static_cast<VkShaderModule>(*result);
    return std::nullopt;
}

}
