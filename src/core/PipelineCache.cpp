#include "PipelineCache.h"
#include <SDL3/SDL.h>
#include <fstream>
#include <vector>

bool PipelineCache::init(VkDevice dev, const std::string& filePath) {
    device = dev;
    cacheFilePath = filePath;

    // Try to load existing cache data from file
    std::vector<char> cacheData;
    if (loadFromFile()) {
        std::ifstream file(cacheFilePath, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            size_t fileSize = static_cast<size_t>(file.tellg());
            cacheData.resize(fileSize);
            file.seekg(0);
            file.read(cacheData.data(), fileSize);
            file.close();
            SDL_Log("PipelineCache: Loaded %zu bytes from %s", fileSize, cacheFilePath.c_str());
        }
    }

    VkPipelineCacheCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    createInfo.initialDataSize = cacheData.size();
    createInfo.pInitialData = cacheData.empty() ? nullptr : cacheData.data();

    VkResult result = vkCreatePipelineCache(device, &createInfo, nullptr, &pipelineCache);
    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "PipelineCache: Failed to create pipeline cache (VkResult=%d)", static_cast<int>(result));

        // Try again without initial data in case cache is corrupted
        if (!cacheData.empty()) {
            SDL_Log("PipelineCache: Retrying without initial data");
            createInfo.initialDataSize = 0;
            createInfo.pInitialData = nullptr;
            result = vkCreatePipelineCache(device, &createInfo, nullptr, &pipelineCache);
            if (result != VK_SUCCESS) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "PipelineCache: Failed to create empty pipeline cache (VkResult=%d)", static_cast<int>(result));
                return false;
            }
        } else {
            return false;
        }
    }

    SDL_Log("PipelineCache: Initialized successfully");
    return true;
}

void PipelineCache::shutdown() {
    if (pipelineCache != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        saveToFile();
        vkDestroyPipelineCache(device, pipelineCache, nullptr);
        pipelineCache = VK_NULL_HANDLE;
    }
    device = VK_NULL_HANDLE;
}

bool PipelineCache::loadFromFile() {
    std::ifstream file(cacheFilePath, std::ios::binary);
    return file.good();
}

bool PipelineCache::saveToFile() {
    if (pipelineCache == VK_NULL_HANDLE || device == VK_NULL_HANDLE) {
        return false;
    }

    // Get cache data size
    size_t cacheSize = 0;
    VkResult result = vkGetPipelineCacheData(device, pipelineCache, &cacheSize, nullptr);
    if (result != VK_SUCCESS || cacheSize == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "PipelineCache: No cache data to save (VkResult=%d, size=%zu)",
            static_cast<int>(result), cacheSize);
        return false;
    }

    // Get cache data
    std::vector<char> cacheData(cacheSize);
    result = vkGetPipelineCacheData(device, pipelineCache, &cacheSize, cacheData.data());
    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "PipelineCache: Failed to get cache data (VkResult=%d)", static_cast<int>(result));
        return false;
    }

    // Write to file
    std::ofstream file(cacheFilePath, std::ios::binary);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "PipelineCache: Failed to open %s for writing", cacheFilePath.c_str());
        return false;
    }

    file.write(cacheData.data(), cacheSize);
    file.close();

    SDL_Log("PipelineCache: Saved %zu bytes to %s", cacheSize, cacheFilePath.c_str());
    return true;
}
