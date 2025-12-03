#include "StreamingManager.h"

StreamingManager::~StreamingManager() {
    shutdown();
}

bool StreamingManager::init(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    graphicsQueue = info.graphicsQueue;
    commandPool = info.commandPool;
    budget = info.budget;

    shutdownRequested.store(false);
    currentGPUMemory.store(0);
    activeLoads.store(0);

    // Create worker threads
    uint32_t numThreads = std::max(1u, info.numWorkerThreads);
    workerThreads.reserve(numThreads);

    for (uint32_t i = 0; i < numThreads; i++) {
        workerThreads.emplace_back(&StreamingManager::workerThreadFunc, this);
    }

    return true;
}

void StreamingManager::shutdown() {
    // Signal shutdown
    shutdownRequested.store(true);
    workQueueCV.notify_all();

    // Wait for worker threads to finish
    for (auto& thread : workerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    workerThreads.clear();

    // Clear work queue
    {
        std::lock_guard<std::mutex> lock(workQueueMutex);
        while (!workQueue.empty()) {
            workQueue.pop();
        }
    }
}

void StreamingManager::workerThreadFunc() {
    while (!shutdownRequested.load()) {
        PrioritizedWork work;
        bool hasWork = false;

        {
            std::unique_lock<std::mutex> lock(workQueueMutex);

            // Wait for work or shutdown
            workQueueCV.wait(lock, [this] {
                return shutdownRequested.load() || !workQueue.empty();
            });

            if (shutdownRequested.load()) {
                break;
            }

            if (!workQueue.empty()) {
                // Check if we're under the concurrent load limit
                if (activeLoads.load() < budget.maxConcurrentLoads) {
                    work = workQueue.top();
                    workQueue.pop();
                    hasWork = true;
                    activeLoads.fetch_add(1);
                }
            }
        }

        if (hasWork) {
            // Execute work item
            work.work();
            activeLoads.fetch_sub(1);
        }
    }
}

void StreamingManager::submitWork(WorkItem work, LoadPriority priority) {
    {
        std::lock_guard<std::mutex> lock(workQueueMutex);
        workQueue.push(PrioritizedWork{std::move(work), priority});
    }
    workQueueCV.notify_one();
}

uint32_t StreamingManager::getPendingLoadCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(workQueueMutex));
    return static_cast<uint32_t>(workQueue.size());
}
