#include "CpuProfiler.h"
#include <SDL3/SDL.h>
#include <algorithm>

void CpuProfiler::beginFrame() {
    if (!enabled) return;

    frameStartTime = Clock::now();
    activeZones.clear();
    currentFrameZoneOrder.clear();
}

void CpuProfiler::endFrame() {
    if (!enabled) return;

    auto frameEndTime = Clock::now();
    float frameTimeMs = std::chrono::duration<float, std::milli>(frameEndTime - frameStartTime).count();

    // Build results for this frame
    lastFrameStats.totalCpuTimeMs = frameTimeMs;
    lastFrameStats.zones.clear();
    zoneNames.clear();

    for (const auto& zoneName : currentFrameZoneOrder) {
        auto it = activeZones.find(zoneName);
        if (it != activeZones.end()) {
            TimingResult result;
            result.name = zoneName;
            result.cpuTimeMs = it->second.accumulatedMs;
            result.percentOfFrame = (frameTimeMs > 0.0f) ? (result.cpuTimeMs / frameTimeMs * 100.0f) : 0.0f;

            lastFrameStats.zones.push_back(result);
            zoneNames.push_back(zoneName);
        }
    }

    // Update smoothed stats
    if (smoothedStats.zones.empty()) {
        // First frame - initialize with current values
        smoothedStats = lastFrameStats;
    } else {
        // Exponential moving average
        smoothedStats.totalCpuTimeMs = smoothedStats.totalCpuTimeMs * SMOOTHING_FACTOR +
                                        lastFrameStats.totalCpuTimeMs * (1.0f - SMOOTHING_FACTOR);

        // Update zones (match by name)
        for (auto& smoothedZone : smoothedStats.zones) {
            for (const auto& currentZone : lastFrameStats.zones) {
                if (smoothedZone.name == currentZone.name) {
                    smoothedZone.cpuTimeMs = smoothedZone.cpuTimeMs * SMOOTHING_FACTOR +
                                              currentZone.cpuTimeMs * (1.0f - SMOOTHING_FACTOR);
                    smoothedZone.percentOfFrame = smoothedZone.percentOfFrame * SMOOTHING_FACTOR +
                                                   currentZone.percentOfFrame * (1.0f - SMOOTHING_FACTOR);
                    break;
                }
            }
        }

        // Add any new zones that don't exist in smoothed stats yet
        for (const auto& currentZone : lastFrameStats.zones) {
            bool found = false;
            for (const auto& smoothedZone : smoothedStats.zones) {
                if (smoothedZone.name == currentZone.name) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                smoothedStats.zones.push_back(currentZone);
            }
        }
    }
}

void CpuProfiler::beginZone(const char* zoneName) {
    if (!enabled) return;

    auto it = activeZones.find(zoneName);
    if (it == activeZones.end()) {
        // New zone for this frame
        ZoneData data;
        data.startTime = Clock::now();
        data.accumulatedMs = 0.0f;
        activeZones[zoneName] = data;
        currentFrameZoneOrder.push_back(zoneName);
    } else {
        // Zone already exists (nested or repeated call)
        it->second.startTime = Clock::now();
    }
}

void CpuProfiler::endZone(const char* zoneName) {
    if (!enabled) return;

    auto it = activeZones.find(zoneName);
    if (it == activeZones.end()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "CPU Profiler: endZone called without beginZone for '%s'", zoneName);
        return;
    }

    auto endTime = Clock::now();
    float elapsedMs = std::chrono::duration<float, std::milli>(endTime - it->second.startTime).count();
    it->second.accumulatedMs += elapsedMs;
}
