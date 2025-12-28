#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <SDL3/SDL.h>

/**
 * Initialization Profiler for measuring startup/initialization time breakdown.
 *
 * Unlike the frame profiler which tracks per-frame times, this tracks cumulative
 * time spent in each initialization phase. Results are available after init
 * completes for display in the GUI.
 *
 * Usage:
 *   InitProfiler::get().beginPhase("Vulkan Init");
 *   // ... initialize Vulkan ...
 *   InitProfiler::get().endPhase("Vulkan Init");
 *
 * Or with RAII:
 *   {
 *       InitProfiler::ScopedPhase phase("Vulkan Init");
 *       // ... initialize Vulkan ...
 *   }
 */
class InitProfiler {
public:
    struct PhaseResult {
        std::string name;
        float timeMs;           // Time in milliseconds
        float percentOfTotal;   // Percentage of total init time
        int depth;              // Nesting depth for hierarchical display
    };

    struct Results {
        float totalTimeMs;
        std::vector<PhaseResult> phases;  // In order of completion
    };

    /**
     * RAII helper for scoped initialization phases.
     */
    class ScopedPhase {
    public:
        ScopedPhase(const char* phaseName)
            : name(phaseName) {
            InitProfiler::get().beginPhase(name);
        }

        ~ScopedPhase() {
            InitProfiler::get().endPhase(name);
        }

        // Non-copyable, non-movable
        ScopedPhase(const ScopedPhase&) = delete;
        ScopedPhase& operator=(const ScopedPhase&) = delete;
        ScopedPhase(ScopedPhase&&) = delete;
        ScopedPhase& operator=(ScopedPhase&&) = delete;

    private:
        const char* name;
    };

    /**
     * Get the singleton instance.
     */
    static InitProfiler& get() {
        static InitProfiler instance;
        return instance;
    }

    /**
     * Reset profiler for a new initialization run.
     */
    void reset() {
        results_.totalTimeMs = 0.0f;
        results_.phases.clear();
        activePhases_.clear();
        phaseOrder_.clear();
        phaseTimes_.clear();
        overallStartTime_ = Clock::now();
        currentDepth_ = 0;
        finalized_ = false;
    }

    /**
     * Begin a named initialization phase.
     */
    void beginPhase(const char* phaseName) {
        std::string name(phaseName);
        PhaseData data;
        data.startTime = Clock::now();
        data.depth = currentDepth_;
        activePhases_[name] = data;
        phaseOrder_.push_back(name);
        currentDepth_++;
    }

    /**
     * End a named initialization phase.
     */
    void endPhase(const char* phaseName) {
        std::string name(phaseName);
        auto it = activePhases_.find(name);
        if (it == activePhases_.end()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "InitProfiler: endPhase called without beginPhase for '%s'", phaseName);
            return;
        }

        auto endTime = Clock::now();
        float elapsedMs = std::chrono::duration<float, std::milli>(endTime - it->second.startTime).count();

        PhaseResult result;
        result.name = name;
        result.timeMs = elapsedMs;
        result.percentOfTotal = 0.0f;  // Calculated in finalize()
        result.depth = it->second.depth;

        // Store result (will be sorted by order later)
        phaseTimes_[name] = result;

        currentDepth_--;
        activePhases_.erase(it);

        // Log immediately for visibility during init
        std::string indent(result.depth * 2, ' ');
        SDL_Log("%s[Init] %s: %.1f ms", indent.c_str(), phaseName, elapsedMs);
    }

    /**
     * Finalize initialization profiling and calculate percentages.
     * Call this after all init phases complete.
     */
    void finalize() {
        if (finalized_) return;

        auto endTime = Clock::now();
        results_.totalTimeMs = std::chrono::duration<float, std::milli>(endTime - overallStartTime_).count();

        // Build results in order phases were started
        results_.phases.clear();
        for (const auto& name : phaseOrder_) {
            auto it = phaseTimes_.find(name);
            if (it != phaseTimes_.end()) {
                PhaseResult result = it->second;
                result.percentOfTotal = (results_.totalTimeMs > 0.0f)
                    ? (result.timeMs / results_.totalTimeMs * 100.0f)
                    : 0.0f;
                results_.phases.push_back(result);
            }
        }

        finalized_ = true;
        SDL_Log("[Init] Total initialization time: %.1f ms", results_.totalTimeMs);
    }

    /**
     * Get the initialization profiling results.
     */
    const Results& getResults() const { return results_; }

    /**
     * Check if profiling has been finalized.
     */
    bool isFinalized() const { return finalized_; }

private:
    InitProfiler() = default;

    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    struct PhaseData {
        TimePoint startTime;
        int depth;
    };

    Results results_;
    std::unordered_map<std::string, PhaseData> activePhases_;
    std::vector<std::string> phaseOrder_;  // Order phases were started
    std::unordered_map<std::string, PhaseResult> phaseTimes_;  // Completed phase times
    TimePoint overallStartTime_;
    int currentDepth_ = 0;
    bool finalized_ = false;
};

// Macro for convenient scoped profiling
#define INIT_PROFILE_PHASE(name) \
    InitProfiler::ScopedPhase _initPhase##__LINE__(name)
