#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <optional>

namespace ShaderLoader {

std::optional<std::vector<char>> readFile(const std::string& filename);
std::optional<VkShaderModule> createShaderModule(VkDevice device, const std::vector<char>& code);
std::optional<VkShaderModule> loadShaderModule(VkDevice device, const std::string& path);

}
