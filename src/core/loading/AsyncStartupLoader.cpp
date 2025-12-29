#include "AsyncStartupLoader.h"
#include "../LoadingRenderer.h"
#include "../vulkan/VulkanContext.h"
#include <SDL3/SDL.h>
#include <lodepng.h>
#include <fstream>
#include <filesystem>

namespace Loading {

std::unique_ptr<AsyncStartupLoader> AsyncStartupLoader::create(const InitInfo& info) {
    std::unique_ptr<AsyncStartupLoader> loader(new AsyncStartupLoader());
    if (!loader->init(info)) {
        return nullptr;
    }
    return loader;
}

AsyncStartupLoader::~AsyncStartupLoader() {
    shutdown();
}

bool AsyncStartupLoader::init(const InitInfo& info) {
    vulkanContext_ = info.vulkanContext;
    loadingRenderer_ = info.loadingRenderer;
    resourcePath_ = info.resourcePath;

    jobQueue_ = LoadJobQueue::create(info.workerCount);
    if (!jobQueue_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create LoadJobQueue");
        return false;
    }

    SDL_Log("AsyncStartupLoader initialized");
    return true;
}

std::string AsyncStartupLoader::buildPath(const std::string& relativePath) const {
    if (relativePath.empty() || relativePath[0] == '/') {
        return relativePath;  // Already absolute
    }
    return resourcePath_ + "/" + relativePath;
}

void AsyncStartupLoader::queueTextureLoad(const std::string& id, const std::string& path,
                                          bool srgb, int priority) {
    std::string fullPath = buildPath(path);

    LoadJob job;
    job.id = id;
    job.phase = "Textures";
    job.priority = priority;
    job.execute = [fullPath, srgb, id]() -> std::unique_ptr<StagedResource> {
        std::vector<unsigned char> pixels;
        unsigned width, height;

        unsigned error = lodepng::decode(pixels, width, height, fullPath);
        if (error != 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to load texture '%s': %s",
                        fullPath.c_str(), lodepng_error_text(error));
            return nullptr;
        }

        auto staged = std::make_unique<StagedTexture>();
        staged->pixels = std::move(pixels);
        staged->width = width;
        staged->height = height;
        staged->channels = 4;
        staged->srgb = srgb;
        staged->name = id;

        return staged;
    };

    jobQueue_->submit(std::move(job));
    ++queuedJobCount_;
    jobQueue_->setTotalJobs(queuedJobCount_);
}

void AsyncStartupLoader::queueHeightmapLoad(const std::string& id, const std::string& path,
                                            int priority) {
    std::string fullPath = buildPath(path);

    LoadJob job;
    job.id = id;
    job.phase = "Terrain";
    job.priority = priority;
    job.execute = [fullPath, id]() -> std::unique_ptr<StagedResource> {
        std::vector<unsigned char> rawPixels;
        unsigned width, height;

        // Load as grayscale or RGBA
        unsigned error = lodepng::decode(rawPixels, width, height, fullPath);
        if (error != 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to load heightmap '%s': %s",
                        fullPath.c_str(), lodepng_error_text(error));
            return nullptr;
        }

        auto staged = std::make_unique<StagedHeightmap>();
        staged->width = width;
        staged->height = height;
        staged->name = id;

        // Convert RGBA to 16-bit heights (use red channel, or average)
        staged->heights.resize(width * height);
        for (size_t i = 0; i < width * height; ++i) {
            // Assuming 8-bit per channel, scale to 16-bit
            staged->heights[i] = static_cast<uint16_t>(rawPixels[i * 4] << 8);
        }

        SDL_Log("Loaded heightmap '%s': %ux%u", id.c_str(), width, height);
        return staged;
    };

    jobQueue_->submit(std::move(job));
    ++queuedJobCount_;
    jobQueue_->setTotalJobs(queuedJobCount_);
}

void AsyncStartupLoader::queueFileLoad(const std::string& id, const std::string& path,
                                       const std::string& phase, int priority) {
    std::string fullPath = buildPath(path);

    LoadJob job;
    job.id = id;
    job.phase = phase;
    job.priority = priority;
    job.execute = [fullPath, id]() -> std::unique_ptr<StagedResource> {
        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to open file '%s'", fullPath.c_str());
            return nullptr;
        }

        size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        auto staged = std::make_unique<StagedBuffer>();
        staged->data.resize(size);
        staged->name = id;

        if (!file.read(reinterpret_cast<char*>(staged->data.data()), size)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to read file '%s'", fullPath.c_str());
            return nullptr;
        }

        return staged;
    };

    jobQueue_->submit(std::move(job));
    ++queuedJobCount_;
    jobQueue_->setTotalJobs(queuedJobCount_);
}

void AsyncStartupLoader::queueCustomJob(const std::string& id, const std::string& phase,
                                        std::function<std::unique_ptr<StagedResource>()> execute,
                                        int priority) {
    LoadJob job;
    job.id = id;
    job.phase = phase;
    job.priority = priority;
    job.execute = std::move(execute);

    jobQueue_->submit(std::move(job));
    ++queuedJobCount_;
    jobQueue_->setTotalJobs(queuedJobCount_);
}

void AsyncStartupLoader::setJobCompleteCallback(JobCompleteCallback callback) {
    jobCompleteCallback_ = std::move(callback);
}

void AsyncStartupLoader::runLoadingLoop() {
    SDL_Log("Starting async loading loop with %u jobs", queuedJobCount_);

    while (!isComplete()) {
        // Process completed jobs (GPU uploads on main thread)
        processCompletedJobs();

        // Render loading screen frame
        if (loadingRenderer_) {
            LoadProgress progress = getProgress();
            loadingRenderer_->setProgress(progress.getProgress());
            loadingRenderer_->render();
        }

        // Keep window responsive
        SDL_PumpEvents();

        // Small sleep to avoid spinning too fast
        SDL_Delay(1);
    }

    // Process any remaining completed jobs
    processCompletedJobs();

    SDL_Log("Async loading complete: %llu bytes loaded",
            static_cast<unsigned long long>(jobQueue_->getProgress().bytesLoaded));
}

uint32_t AsyncStartupLoader::processCompletedJobs() {
    auto results = jobQueue_->getCompletedJobs();
    uint32_t count = static_cast<uint32_t>(results.size());

    for (auto& result : results) {
        if (jobCompleteCallback_) {
            jobCompleteCallback_(result);
        }

        // Store result for later retrieval if not consumed by callback
        if (result.resource) {
            collectedResults_.push_back(std::move(result));
        }
    }

    return count;
}

bool AsyncStartupLoader::isComplete() const {
    return jobQueue_->isComplete();
}

LoadProgress AsyncStartupLoader::getProgress() const {
    return jobQueue_->getProgress();
}

std::vector<LoadJobResult> AsyncStartupLoader::getAllResults() {
    return std::move(collectedResults_);
}

void AsyncStartupLoader::shutdown() {
    if (jobQueue_) {
        jobQueue_->shutdown();
        jobQueue_.reset();
    }
    collectedResults_.clear();
}

} // namespace Loading
