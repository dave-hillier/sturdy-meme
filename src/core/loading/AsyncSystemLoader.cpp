#include "AsyncSystemLoader.h"
#include "../LoadingRenderer.h"
#include "../vulkan/VulkanContext.h"
#include "../threading/TaskScheduler.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace Loading {

std::unique_ptr<AsyncSystemLoader> AsyncSystemLoader::create(const InitInfo& info) {
    auto loader = std::make_unique<AsyncSystemLoader>(ConstructToken{});
    if (!loader->init(info)) {
        return nullptr;
    }
    return loader;
}

AsyncSystemLoader::~AsyncSystemLoader() {
    shutdown();
}

bool AsyncSystemLoader::init(const InitInfo& info) {
    loadingRenderer_ = info.loadingRenderer;

    // Determine worker count
    uint32_t workerCount = info.workerCount;
    if (workerCount == 0) {
        workerCount = std::max(1u, std::thread::hardware_concurrency() - 1);
    }

    // Start worker threads
    running_ = true;
    for (uint32_t i = 0; i < workerCount; ++i) {
        workers_.emplace_back(&AsyncSystemLoader::workerLoop, this);
    }

    SDL_Log("AsyncSystemLoader initialized with %u workers", workerCount);
    return true;
}

void AsyncSystemLoader::addTask(SystemInitTask task) {
    std::string id = task.id;

    // Validate dependencies exist
    for (const auto& dep : task.dependencies) {
        if (tasks_.find(dep) == tasks_.end()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                       "AsyncSystemLoader: Task '%s' depends on unknown task '%s'",
                       id.c_str(), dep.c_str());
        }
    }

    totalWeight_ = totalWeight_.load() + task.weight;
    tasks_[id] = std::move(task);
    taskOrder_.push_back(id);
    pendingTasks_.insert(id);
}

void AsyncSystemLoader::start() {
    SDL_Log("AsyncSystemLoader starting with %zu tasks", tasks_.size());

    // Schedule tasks whose dependencies are already satisfied
    scheduleReadyTasks();
}

void AsyncSystemLoader::scheduleReadyTasks() {
    std::lock_guard<std::mutex> lock(queueMutex_);

    // Find pending tasks with satisfied dependencies
    std::vector<std::string> readyTasks;
    for (const auto& taskId : pendingTasks_) {
        if (areDependenciesSatisfied(taskId)) {
            readyTasks.push_back(taskId);
        }
    }

    // Move to running and queue for CPU work
    for (const auto& taskId : readyTasks) {
        pendingTasks_.erase(taskId);
        cpuRunningTasks_.insert(taskId);
        cpuWorkQueue_.push(taskId);

        // Update progress display
        {
            std::lock_guard<std::mutex> progressLock(progressMutex_);
            currentPhase_ = tasks_[taskId].displayName;
        }
    }

    if (!readyTasks.empty()) {
        queueCondition_.notify_all();
    }
}

bool AsyncSystemLoader::areDependenciesSatisfied(const std::string& taskId) const {
    const auto& task = tasks_.at(taskId);
    for (const auto& dep : task.dependencies) {
        if (completeTasks_.find(dep) == completeTasks_.end()) {
            return false;
        }
    }
    return true;
}

void AsyncSystemLoader::workerLoop() {
    while (running_) {
        std::string taskId;

        // Wait for work
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCondition_.wait(lock, [this] {
                return !cpuWorkQueue_.empty() || !running_;
            });

            if (!running_ && cpuWorkQueue_.empty()) {
                return;
            }

            if (cpuWorkQueue_.empty()) {
                continue;
            }

            taskId = cpuWorkQueue_.front();
            cpuWorkQueue_.pop();
        }

        // Execute CPU work
        auto& task = tasks_[taskId];
        bool success = true;

        if (task.cpuWork) {
            SDL_Log("AsyncSystemLoader: Starting CPU work for '%s'", taskId.c_str());
            try {
                success = task.cpuWork();
            } catch (const std::exception& e) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                            "AsyncSystemLoader: CPU work for '%s' threw exception: %s",
                            taskId.c_str(), e.what());
                success = false;
            }

            if (!success) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                            "AsyncSystemLoader: CPU work failed for '%s'",
                            taskId.c_str());

                std::lock_guard<std::mutex> errorLock(errorMutex_);
                hasError_ = true;
                errorMessage_ = "CPU work failed for task: " + taskId;
                return;
            }
            SDL_Log("AsyncSystemLoader: CPU work complete for '%s'", taskId.c_str());
        }

        // Mark CPU work complete
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            cpuRunningTasks_.erase(taskId);
            cpuCompleteTasks_.insert(taskId);
        }

        // Queue for main thread GPU work
        {
            std::lock_guard<std::mutex> lock(completedMutex_);
            cpuCompletedQueue_.push(taskId);
        }
    }
}

uint32_t AsyncSystemLoader::pollCompletions() {
    uint32_t completed = 0;

    // Process all completed CPU tasks
    while (true) {
        std::string taskId;

        {
            std::lock_guard<std::mutex> lock(completedMutex_);
            if (cpuCompletedQueue_.empty()) {
                break;
            }
            taskId = cpuCompletedQueue_.front();
            cpuCompletedQueue_.pop();
        }

        // Execute GPU work on main thread
        auto& task = tasks_[taskId];
        bool success = true;

        if (task.gpuWork) {
            SDL_Log("AsyncSystemLoader: Starting GPU work for '%s'", taskId.c_str());
            try {
                success = task.gpuWork();
            } catch (const std::exception& e) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                            "AsyncSystemLoader: GPU work for '%s' threw exception: %s",
                            taskId.c_str(), e.what());
                success = false;
            }

            if (!success) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                            "AsyncSystemLoader: GPU work failed for '%s'",
                            taskId.c_str());

                std::lock_guard<std::mutex> errorLock(errorMutex_);
                hasError_ = true;
                errorMessage_ = "GPU work failed for task: " + taskId;
                return completed;
            }
            SDL_Log("AsyncSystemLoader: GPU work complete for '%s'", taskId.c_str());
        }

        // Mark fully complete
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            cpuCompleteTasks_.erase(taskId);
            completeTasks_.insert(taskId);
        }

        completedWeight_ = completedWeight_.load() + task.weight;
        ++completed;

        // Schedule any newly-ready tasks
        scheduleReadyTasks();
    }

    return completed;
}

bool AsyncSystemLoader::isComplete() const {
    if (hasError_) {
        return true;  // Stop on error
    }

    std::lock_guard<std::mutex> lock(queueMutex_);
    return pendingTasks_.empty() &&
           cpuRunningTasks_.empty() &&
           cpuCompleteTasks_.empty();
}

bool AsyncSystemLoader::hasError() const {
    return hasError_.load();
}

std::string AsyncSystemLoader::getErrorMessage() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return errorMessage_;
}

SystemLoadProgress AsyncSystemLoader::getProgress() const {
    SystemLoadProgress progress;

    {
        std::lock_guard<std::mutex> lock(progressMutex_);
        progress.currentPhase = currentPhase_;
    }

    progress.totalTasks = static_cast<uint32_t>(tasks_.size());

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        progress.completedTasks = static_cast<uint32_t>(completeTasks_.size());
    }

    float total = totalWeight_.load();
    if (total > 0.0f) {
        progress.progress = completedWeight_.load() / total;
    }

    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        progress.hasError = hasError_.load();
        progress.errorMessage = errorMessage_;
    }

    return progress;
}

void AsyncSystemLoader::runLoadingLoop() {
    SDL_Log("AsyncSystemLoader: Starting loading loop");

    while (!isComplete()) {
        // Process completed CPU work (GPU uploads on main thread)
        pollCompletions();

        // Render loading screen frame
        if (loadingRenderer_) {
            auto progress = getProgress();
            loadingRenderer_->setProgress(progress.progress);
            loadingRenderer_->render();
        }

        // Keep window responsive
        SDL_PumpEvents();

        // Small sleep to avoid spinning too fast
        SDL_Delay(1);
    }

    // Process any remaining completed jobs
    pollCompletions();

    auto finalProgress = getProgress();
    if (finalProgress.hasError) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "AsyncSystemLoader: Loading failed - %s",
                    finalProgress.errorMessage.c_str());
    } else {
        SDL_Log("AsyncSystemLoader: Loading complete (%u tasks)",
                finalProgress.completedTasks);
    }
}

void AsyncSystemLoader::shutdown() {
    running_ = false;
    queueCondition_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    tasks_.clear();
    taskOrder_.clear();
    pendingTasks_.clear();
    cpuRunningTasks_.clear();
    cpuCompleteTasks_.clear();
    completeTasks_.clear();
}

} // namespace Loading
