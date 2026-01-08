#include "TaskScheduler.h"
#include <SDL3/SDL_log.h>

thread_local int32_t TaskScheduler::currentThreadId_ = -1;

TaskScheduler& TaskScheduler::instance() {
    static TaskScheduler instance;
    return instance;
}

TaskScheduler::~TaskScheduler() {
    shutdown();
}

void TaskScheduler::initialize(uint32_t numThreads) {
    if (running_.load()) {
        return; // Already initialized
    }

    running_.store(true);

    // Determine thread count: use hardware concurrency - 1 (reserve one for main thread)
    // Leave at least 2 worker threads
    if (numThreads == 0) {
        uint32_t hwThreads = std::thread::hardware_concurrency();
        numThreads = hwThreads > 2 ? hwThreads - 1 : 2;
    }

    SDL_Log("TaskScheduler: Initializing with %u worker threads", numThreads);

    // Create worker threads
    workers_.reserve(numThreads);
    for (uint32_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back(&TaskScheduler::workerThread, this, i);
    }

    // Create dedicated IO thread (pinned for cache affinity as mentioned in video)
    ioWorker_ = std::thread(&TaskScheduler::ioWorkerThread, this);

    SDL_Log("TaskScheduler: Initialized with %u workers + 1 IO thread", numThreads);
}

void TaskScheduler::shutdown() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);

    // Wake up all waiting threads
    queueCondition_.notify_all();
    ioCondition_.notify_all();

    // Join all worker threads
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    // Join IO thread
    if (ioWorker_.joinable()) {
        ioWorker_.join();
    }

    SDL_Log("TaskScheduler: Shutdown complete");
}

void TaskScheduler::submit(std::function<void()> task, TaskGroup* group, Priority priority) {
    if (!running_.load()) {
        // If scheduler not running, execute synchronously
        if (group) group->increment();
        task();
        if (group) group->decrement();
        return;
    }

    if (group) {
        group->increment();
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        taskQueue_.push(Task{std::move(task), group, priority});
    }
    queueCondition_.notify_one();
}

void TaskScheduler::submitIO(std::function<void()> task, TaskGroup* group) {
    if (!running_.load()) {
        // If scheduler not running, execute synchronously
        if (group) group->increment();
        task();
        if (group) group->decrement();
        return;
    }

    if (group) {
        group->increment();
    }

    {
        std::lock_guard<std::mutex> lock(ioMutex_);
        ioQueue_.push(Task{std::move(task), group, Priority::Normal});
    }
    ioCondition_.notify_one();
}

int32_t TaskScheduler::getCurrentThreadId() const {
    return currentThreadId_;
}

void TaskScheduler::workerThread(uint32_t threadId) {
    currentThreadId_ = static_cast<int32_t>(threadId);

    while (running_.load()) {
        Task task;

        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCondition_.wait(lock, [this] {
                return !taskQueue_.empty() || !running_.load();
            });

            if (!running_.load() && taskQueue_.empty()) {
                break;
            }

            if (!taskQueue_.empty()) {
                task = std::move(const_cast<Task&>(taskQueue_.top()));
                taskQueue_.pop();
            }
        }

        if (task.func) {
            task.func();
            if (task.group) {
                task.group->decrement();
            }
        }
    }

    currentThreadId_ = -1;
}

void TaskScheduler::ioWorkerThread() {
    // IO thread gets a special ID beyond worker range
    currentThreadId_ = static_cast<int32_t>(workers_.size());

    while (running_.load()) {
        Task task;

        {
            std::unique_lock<std::mutex> lock(ioMutex_);
            ioCondition_.wait(lock, [this] {
                return !ioQueue_.empty() || !running_.load();
            });

            if (!running_.load() && ioQueue_.empty()) {
                break;
            }

            if (!ioQueue_.empty()) {
                task = std::move(ioQueue_.front());
                ioQueue_.pop();
            }
        }

        if (task.func) {
            task.func();
            if (task.group) {
                task.group->decrement();
            }
        }
    }

    currentThreadId_ = -1;
}
