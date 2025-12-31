#pragma once
// Parallel processing utilities with progress reporting
// Uses std::thread and std::atomic for portable multithreading

#include <thread>
#include <vector>
#include <atomic>
#include <functional>
#include <algorithm>
#include <mutex>
#include <string>
#include <cstdio>

namespace ParallelProgress {

// Progress callback: (progress 0.0-1.0, message)
using ProgressCallback = std::function<void(float, const std::string&)>;

// Thread-safe progress tracker for parallel operations
class ProgressTracker {
public:
    ProgressTracker(size_t totalItems, ProgressCallback callback = nullptr,
                    const std::string& taskName = "Processing",
                    size_t reportInterval = 0)
        : total(totalItems)
        , completed(0)
        , callback(callback)
        , taskName(taskName)
        , lastReportedPercent(-1) {
        // Default: report every ~5% or at least every 100 items
        if (reportInterval == 0) {
            interval = std::max(size_t(1), total / 20);
        } else {
            interval = reportInterval;
        }
    }

    // Call when an item is completed (thread-safe)
    void itemCompleted() {
        size_t current = ++completed;

        // Only report at intervals to avoid overhead
        if (current == total || (current % interval == 0)) {
            report(current);
        }
    }

    // Call when multiple items are completed at once
    void itemsCompleted(size_t count) {
        size_t current = completed.fetch_add(count) + count;
        if (current >= total || (current / interval) > ((current - count) / interval)) {
            report(current);
        }
    }

    // Force a progress report
    void report(size_t current) {
        float progress = static_cast<float>(current) / static_cast<float>(total);
        int percent = static_cast<int>(progress * 100.0f);

        // Avoid duplicate reports for same percentage
        int expected = lastReportedPercent.load();
        if (percent > expected) {
            if (lastReportedPercent.compare_exchange_weak(expected, percent)) {
                std::string msg = taskName + " " + std::to_string(current) + "/" + std::to_string(total);
                if (callback) {
                    callback(progress, msg);
                } else {
                    std::fprintf(stderr, "  Progress: %d%% - %s\n", percent, msg.c_str());
                }
            }
        }
    }

    // Get current progress (0.0 - 1.0)
    float getProgress() const {
        return static_cast<float>(completed.load()) / static_cast<float>(total);
    }

    size_t getCompleted() const { return completed.load(); }
    size_t getTotal() const { return total; }

private:
    size_t total;
    std::atomic<size_t> completed;
    ProgressCallback callback;
    std::string taskName;
    size_t interval;
    std::atomic<int> lastReportedPercent;
};

// Get the number of threads to use (respects hardware concurrency)
inline unsigned int getThreadCount() {
    unsigned int n = std::thread::hardware_concurrency();
    return std::max(1u, n);
}

// Parallel for loop over a range [start, end)
// Each thread processes a contiguous chunk
template<typename Func>
void parallel_for(int start, int end, Func&& func) {
    if (start >= end) return;

    unsigned int numThreads = getThreadCount();
    int total = end - start;

    // Don't spawn more threads than items
    numThreads = std::min(numThreads, static_cast<unsigned int>(total));

    if (numThreads <= 1) {
        // Single-threaded fallback
        for (int i = start; i < end; ++i) {
            func(i);
        }
        return;
    }

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    int chunkSize = (total + numThreads - 1) / numThreads;

    for (unsigned int t = 0; t < numThreads; ++t) {
        int chunkStart = start + t * chunkSize;
        int chunkEnd = std::min(chunkStart + chunkSize, end);

        if (chunkStart < end) {
            threads.emplace_back([=, &func] {
                for (int i = chunkStart; i < chunkEnd; ++i) {
                    func(i);
                }
            });
        }
    }

    for (auto& t : threads) {
        t.join();
    }
}

// Parallel for with progress tracking
// Func signature: void(int index)
template<typename Func>
void parallel_for_progress(int start, int end, Func&& func,
                           ProgressCallback progressCallback = nullptr,
                           const std::string& taskName = "Processing") {
    if (start >= end) return;

    int total = end - start;
    ProgressTracker tracker(total, progressCallback, taskName);

    unsigned int numThreads = getThreadCount();
    numThreads = std::min(numThreads, static_cast<unsigned int>(total));

    if (numThreads <= 1) {
        for (int i = start; i < end; ++i) {
            func(i);
            tracker.itemCompleted();
        }
        return;
    }

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    int chunkSize = (total + numThreads - 1) / numThreads;

    for (unsigned int t = 0; t < numThreads; ++t) {
        int chunkStart = start + t * chunkSize;
        int chunkEnd = std::min(chunkStart + chunkSize, end);

        if (chunkStart < end) {
            threads.emplace_back([=, &func, &tracker] {
                for (int i = chunkStart; i < chunkEnd; ++i) {
                    func(i);
                    tracker.itemCompleted();
                }
            });
        }
    }

    for (auto& t : threads) {
        t.join();
    }
}

// Parallel for over 2D range (row-major parallelization)
// Parallelizes over rows, each thread processes complete rows
template<typename Func>
void parallel_for_2d(int width, int height, Func&& func) {
    parallel_for(0, height, [&](int y) {
        for (int x = 0; x < width; ++x) {
            func(x, y);
        }
    });
}

// Parallel for 2D with progress (reports per-row progress)
template<typename Func>
void parallel_for_2d_progress(int width, int height, Func&& func,
                               ProgressCallback progressCallback = nullptr,
                               const std::string& taskName = "Processing") {
    parallel_for_progress(0, height, [&](int y) {
        for (int x = 0; x < width; ++x) {
            func(x, y);
        }
    }, progressCallback, taskName);
}

// Parallel map-reduce pattern
// Applies func to each element and collects results
template<typename T, typename Func>
std::vector<T> parallel_map(int start, int end, Func&& func) {
    if (start >= end) return {};

    int total = end - start;
    std::vector<T> results(total);

    parallel_for(start, end, [&](int i) {
        results[i - start] = func(i);
    });

    return results;
}

// Parallel for over a collection of items
// Each thread processes a subset of items
template<typename Container, typename Func>
void parallel_for_each(Container& items, Func&& func) {
    if (items.empty()) return;

    parallel_for(0, static_cast<int>(items.size()), [&](int i) {
        func(items[i]);
    });
}

// Parallel for-each with progress tracking
template<typename Container, typename Func>
void parallel_for_each_progress(Container& items, Func&& func,
                                ProgressCallback progressCallback = nullptr,
                                const std::string& taskName = "Processing") {
    if (items.empty()) return;

    parallel_for_progress(0, static_cast<int>(items.size()), [&](int i) {
        func(items[i]);
    }, progressCallback, taskName);
}

// Thread-safe accumulator for min/max finding during parallel processing
template<typename T>
class MinMaxAccumulator {
public:
    MinMaxAccumulator(T initialMin, T initialMax)
        : minVal(initialMin), maxVal(initialMax) {}

    void update(T value) {
        std::lock_guard<std::mutex> lock(mutex);
        if (value < minVal) minVal = value;
        if (value > maxVal) maxVal = value;
    }

    void update(T minValue, T maxValue) {
        std::lock_guard<std::mutex> lock(mutex);
        if (minValue < minVal) minVal = minValue;
        if (maxValue > maxVal) maxVal = maxValue;
    }

    T getMin() const { return minVal; }
    T getMax() const { return maxVal; }

private:
    T minVal;
    T maxVal;
    std::mutex mutex;
};

} // namespace ParallelProgress
