#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

/**
 * TaskGroup allows waiting for a group of related tasks to complete.
 * Similar to enkiTS task sets.
 */
class TaskGroup {
public:
    TaskGroup() : pendingCount_(0) {}

    void increment() {
        ++pendingCount_;
    }

    void decrement() {
        if (--pendingCount_ == 0) {
            cv_.notify_all();
        }
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return pendingCount_.load() == 0; });
    }

    bool isComplete() const {
        return pendingCount_.load() == 0;
    }

private:
    std::atomic<uint32_t> pendingCount_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

/**
 * Task-based threading system inspired by enkiTS.
 *
 * Key features:
 * - Thread pool with work stealing
 * - Thread affinity for IO operations (cache benefits)
 * - TaskGroup support for synchronization
 * - Priority-based task scheduling
 *
 * Usage:
 *   TaskScheduler& scheduler = TaskScheduler::instance();
 *   scheduler.initialize();
 *
 *   TaskGroup group;
 *   scheduler.submit([&]{ doWork(); }, &group);
 *   scheduler.submit([&]{ doMoreWork(); }, &group);
 *   group.wait();
 */
class TaskScheduler {
public:
    enum class Priority {
        Low = 0,
        Normal = 1,
        High = 2
    };

    static TaskScheduler& instance();

    // Initialize with specified thread count (0 = hardware concurrency - 1)
    void initialize(uint32_t numThreads = 0);
    void shutdown();

    // Submit a task for parallel execution
    void submit(std::function<void()> task, TaskGroup* group = nullptr, Priority priority = Priority::Normal);

    // Submit IO task to pinned thread for cache affinity
    void submitIO(std::function<void()> task, TaskGroup* group = nullptr);

    // Get thread ID for current worker (0 to threadCount-1, or -1 if not a worker thread)
    int32_t getCurrentThreadId() const;

    // Get total worker thread count
    uint32_t getThreadCount() const { return static_cast<uint32_t>(workers_.size()); }

    // Check if scheduler is running
    bool isRunning() const { return running_.load(); }

private:
    TaskScheduler() = default;
    ~TaskScheduler();

    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    struct Task {
        std::function<void()> func;
        TaskGroup* group;
        Priority priority;

        bool operator<(const Task& other) const {
            return priority < other.priority; // Higher priority = processed first
        }
    };

    void workerThread(uint32_t threadId);
    void ioWorkerThread();

    std::vector<std::thread> workers_;
    std::thread ioWorker_;

    // General task queue (priority queue)
    std::priority_queue<Task> taskQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;

    // IO-specific task queue (FIFO, pinned to one thread)
    std::queue<Task> ioQueue_;
    std::mutex ioMutex_;
    std::condition_variable ioCondition_;

    std::atomic<bool> running_{false};

    // Thread-local storage for thread IDs
    static thread_local int32_t currentThreadId_;
};

/**
 * RAII helper to submit a task group and wait on scope exit.
 */
class ScopedTaskGroup {
public:
    explicit ScopedTaskGroup(TaskScheduler& scheduler) : scheduler_(scheduler) {}
    ~ScopedTaskGroup() { group_.wait(); }

    void submit(std::function<void()> task, TaskScheduler::Priority priority = TaskScheduler::Priority::Normal) {
        scheduler_.submit(std::move(task), &group_, priority);
    }

    void wait() { group_.wait(); }
    bool isComplete() const { return group_.isComplete(); }

private:
    TaskScheduler& scheduler_;
    TaskGroup group_;
};
