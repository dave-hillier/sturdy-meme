#include "ShaderLoader.h"
#include <SDL3/SDL.h>
#include <fstream>

namespace ShaderLoader {

std::optional<std::vector<char>> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        SDL_Log("Failed to open file: %s", filename.c_str());
        return std::nullopt;
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

std::optional<VkShaderModule> createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return std::nullopt;
    }

    return shaderModule;
}

std::optional<VkShaderModule> loadShaderModule(VkDevice device, const std::string& path) {
    auto code = readFile(path);
    if (!code) {
        return std::nullopt;
    }
    return createShaderModule(device, *code);
}

}
