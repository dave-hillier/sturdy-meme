#include "LoadJobQueue.h"
#include <SDL3/SDL_log.h>

namespace Loading {

std::unique_ptr<LoadJobQueue> LoadJobQueue::create(uint32_t workerCount) {
    std::unique_ptr<LoadJobQueue> queue(new LoadJobQueue());
    if (!queue->init(workerCount)) {
        return nullptr;
    }
    return queue;
}

LoadJobQueue::~LoadJobQueue() {
    shutdown();
}

bool LoadJobQueue::init(uint32_t workerCount) {
    running_ = true;

    workers_.reserve(workerCount);
    for (uint32_t i = 0; i < workerCount; ++i) {
        workers_.emplace_back(&LoadJobQueue::workerLoop, this);
    }

    SDL_Log("LoadJobQueue initialized with %u workers", workerCount);
    return true;
}

void LoadJobQueue::submit(LoadJob job) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        jobQueue_.push(std::move(job));
    }
    queueCondition_.notify_one();
}

void LoadJobQueue::submitBatch(std::vector<LoadJob> jobs) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        for (auto& job : jobs) {
            jobQueue_.push(std::move(job));
        }
    }
    queueCondition_.notify_all();
}

void LoadJobQueue::setTotalJobs(uint32_t count) {
    totalJobs_ = count;
}

std::vector<LoadJobResult> LoadJobQueue::getCompletedJobs() {
    std::lock_guard<std::mutex> lock(resultsMutex_);
    std::vector<LoadJobResult> results = std::move(completedResults_);
    completedResults_.clear();
    return results;
}

bool LoadJobQueue::isComplete() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return jobQueue_.empty() && completedJobs_ >= totalJobs_;
}

LoadProgress LoadJobQueue::getProgress() const {
    LoadProgress progress;
    progress.completedJobs = completedJobs_.load();
    progress.totalJobs = totalJobs_.load();
    progress.bytesLoaded = bytesLoaded_.load();

    {
        std::lock_guard<std::mutex> lock(progressMutex_);
        progress.currentPhase = currentPhase_;
        progress.currentJob = currentJob_;
    }

    return progress;
}

void LoadJobQueue::waitForAll() {
    while (!isComplete()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void LoadJobQueue::shutdown() {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        running_ = false;
    }
    queueCondition_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    // Clear remaining jobs
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!jobQueue_.empty()) {
            jobQueue_.pop();
        }
    }

    SDL_Log("LoadJobQueue shutdown complete");
}

void LoadJobQueue::workerLoop() {
    while (true) {
        LoadJob job;

        {
            std::unique_lock<std::mutex> lock(queueMutex_);

            queueCondition_.wait(lock, [this] {
                return !running_ || !jobQueue_.empty();
            });

            if (!running_ && jobQueue_.empty()) {
                return;
            }

            if (jobQueue_.empty()) {
                continue;
            }

            job = std::move(const_cast<LoadJob&>(jobQueue_.top()));
            jobQueue_.pop();
        }

        // Update current job info for progress display
        {
            std::lock_guard<std::mutex> lock(progressMutex_);
            currentPhase_ = job.phase;
            currentJob_ = job.id;
        }

        // Execute job outside of lock
        LoadJobResult result;
        result.jobId = job.id;
        result.phase = job.phase;

        try {
            result.resource = job.execute();
            result.success = (result.resource != nullptr);

            if (result.resource) {
                bytesLoaded_ += result.resource->getMemorySize();
            }
        } catch (const std::exception& e) {
            result.success = false;
            result.error = e.what();
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Load job '%s' failed: %s", job.id.c_str(), e.what());
        }

        // Add to completed results
        {
            std::lock_guard<std::mutex> lock(resultsMutex_);
            completedResults_.push_back(std::move(result));
        }

        ++completedJobs_;
    }
}

} // namespace Loading
