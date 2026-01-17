#include "PipelineCache.h"
#include <SDL3/SDL.h>
#include <fstream>
#include <vector>

PipelineCache::~PipelineCache() {
    shutdown();
}

bool PipelineCache::init(const vk::raii::Device& raiiDevice, const std::string& filePath) {
    device_ = &raiiDevice;
    cacheFilePath_ = filePath;

    // Try to load existing cache data from file
    std::vector<char> cacheData;
    if (loadFromFile()) {
        std::ifstream file(cacheFilePath_, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            size_t fileSize = static_cast<size_t>(file.tellg());
            cacheData.resize(fileSize);
            file.seekg(0);
            file.read(cacheData.data(), fileSize);
            file.close();
            SDL_Log("PipelineCache: Loaded %zu bytes from %s", fileSize, cacheFilePath_.c_str());
        }
    }

    auto createInfo = vk::PipelineCacheCreateInfo{}
        .setInitialDataSize(cacheData.size())
        .setPInitialData(cacheData.empty() ? nullptr : cacheData.data());

    try {
        pipelineCache_.emplace(*device_, createInfo);
        SDL_Log("PipelineCache: Initialized successfully");
        return true;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "PipelineCache: Failed to create pipeline cache: %s", e.what());

        // Try again without initial data in case cache is corrupted
        if (!cacheData.empty()) {
            SDL_Log("PipelineCache: Retrying without initial data");
            try {
                auto emptyCreateInfo = vk::PipelineCacheCreateInfo{};
                pipelineCache_.emplace(*device_, emptyCreateInfo);
                SDL_Log("PipelineCache: Initialized successfully (with empty cache)");
                return true;
            } catch (const vk::SystemError& e2) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "PipelineCache: Failed to create empty pipeline cache: %s", e2.what());
                return false;
            }
        }
        return false;
    }
}

void PipelineCache::shutdown() {
    if (pipelineCache_) {
        saveToFile();
        pipelineCache_.reset();  // RAII handles destruction
    }
    device_ = nullptr;
}

bool PipelineCache::loadFromFile() {
    std::ifstream file(cacheFilePath_, std::ios::binary);
    return file.good();
}

bool PipelineCache::saveToFile() {
    if (!pipelineCache_ || !device_) {
        return false;
    }

    try {
        // Get cache data using vulkan-hpp
        auto cacheData = pipelineCache_->getData();
        if (cacheData.empty()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "PipelineCache: No cache data to save");
            return false;
        }

        // Write to file
        std::ofstream file(cacheFilePath_, std::ios::binary);
        if (!file.is_open()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "PipelineCache: Failed to open %s for writing", cacheFilePath_.c_str());
            return false;
        }

        file.write(reinterpret_cast<const char*>(cacheData.data()), cacheData.size());
        file.close();

        SDL_Log("PipelineCache: Saved %zu bytes to %s", cacheData.size(), cacheFilePath_.c_str());
        return true;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "PipelineCache: Failed to get cache data: %s", e.what());
        return false;
    }
}
