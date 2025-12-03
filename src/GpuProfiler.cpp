#include "GpuProfiler.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>

bool GpuProfiler::init(VkDevice dev, VkPhysicalDevice physicalDevice, uint32_t framesInFlight_, uint32_t maxZones_) {
    device = dev;
    framesInFlight = framesInFlight_;
    maxZones = maxZones_;

    // Query timestamp period from physical device
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    timestampPeriod = props.limits.timestampPeriod;

    if (timestampPeriod == 0.0f) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "GPU timestamps not supported on this device");
        enabled = false;
        return true;  // Not a fatal error, just disable profiling
    }

    SDL_Log("GPU Profiler: timestamp period = %.2f ns", timestampPeriod);

    // Create query pools (one per frame in flight)
    // Each zone needs 2 queries (start + end), plus 2 for frame start/end
    uint32_t queriesPerFrame = (maxZones * QUERIES_PER_ZONE) + 2;

    queryPools.resize(framesInFlight);
    for (uint32_t i = 0; i < framesInFlight; i++) {
        VkQueryPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        poolInfo.queryCount = queriesPerFrame;

        VkResult result = vkCreateQueryPool(device, &poolInfo, nullptr, &queryPools[i]);
        if (result != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create GPU profiler query pool %d", i);
            shutdown();
            return false;
        }
    }

    initialized = true;
    SDL_Log("GPU Profiler initialized: %d zones max, %d frames in flight", maxZones, framesInFlight);
    return true;
}

void GpuProfiler::shutdown() {
    if (device != VK_NULL_HANDLE) {
        for (auto& pool : queryPools) {
            if (pool != VK_NULL_HANDLE) {
                vkDestroyQueryPool(device, pool, nullptr);
                pool = VK_NULL_HANDLE;
            }
        }
    }
    queryPools.clear();
    initialized = false;
}

void GpuProfiler::beginFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!enabled || !initialized) return;

    // Collect results from previous frame first (before reset)
    collectResults(frameIndex);

    // Reset state for this frame
    currentQueryIndex = 0;
    activeZones.clear();
    currentFrameZoneOrder.clear();
    currentFrameIndex = frameIndex;

    // Reset the query pool for this frame
    vkCmdResetQueryPool(cmd, queryPools[frameIndex], 0, (maxZones * QUERIES_PER_ZONE) + 2);

    // Write frame start timestamp
    frameStartQuery = currentQueryIndex++;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPools[frameIndex], frameStartQuery);
}

void GpuProfiler::endFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!enabled || !initialized) return;

    // Write frame end timestamp
    frameEndQuery = currentQueryIndex++;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPools[frameIndex], frameEndQuery);

    // Store the query count and zone order for this frame for later collection
    frameQueryCounts[frameIndex] = currentQueryIndex;
    frameZoneOrders[frameIndex] = currentFrameZoneOrder;
}

void GpuProfiler::beginZone(VkCommandBuffer cmd, const char* zoneName) {
    if (!enabled || !initialized) return;

    if (currentQueryIndex >= (maxZones * QUERIES_PER_ZONE)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "GPU Profiler: max zones exceeded");
        return;
    }

    ZoneInfo zone;
    zone.startQueryIndex = currentQueryIndex++;
    activeZones[zoneName] = zone;
    currentFrameZoneOrder.push_back(zoneName);

    // Write start timestamp
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        queryPools[currentFrameIndex], zone.startQueryIndex);
}

void GpuProfiler::endZone(VkCommandBuffer cmd, const char* zoneName) {
    if (!enabled || !initialized) return;

    auto it = activeZones.find(zoneName);
    if (it == activeZones.end()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "GPU Profiler: endZone called without beginZone for '%s'", zoneName);
        return;
    }

    it->second.endQueryIndex = currentQueryIndex++;

    // Write end timestamp
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        queryPools[currentFrameIndex], it->second.endQueryIndex);
}

void GpuProfiler::collectResults(uint32_t frameIndex) {
    if (!enabled || !initialized) return;

    // We collect from the frame we're about to overwrite (previous use of this pool)
    // On first few frames, there won't be valid data
    if (frameQueryCounts.find(frameIndex) == frameQueryCounts.end()) {
        return;  // No data for this frame yet
    }

    uint32_t queryCount = frameQueryCounts[frameIndex];
    if (queryCount < 2) return;  // Need at least frame start/end

    // Allocate buffer for results
    std::vector<uint64_t> timestamps(queryCount);

    // Get query results - use VK_QUERY_RESULT_64_BIT for timestamps
    // Don't use WAIT_BIT since we're in the middle of frame setup
    VkResult result = vkGetQueryPoolResults(
        device,
        queryPools[frameIndex],
        0,
        queryCount,
        timestamps.size() * sizeof(uint64_t),
        timestamps.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT
    );

    if (result != VK_SUCCESS) {
        // Results not ready or error - this is normal for first frames
        return;
    }

    // Calculate frame total time
    uint64_t frameStartTs = timestamps[0];
    uint64_t frameEndTs = timestamps[queryCount - 1];
    float frameTimeNs = static_cast<float>(frameEndTs - frameStartTs) * timestampPeriod;
    float frameTimeMs = frameTimeNs / 1000000.0f;

    lastFrameStats.totalGpuTimeMs = frameTimeMs;
    lastFrameStats.zones.clear();
    zoneNames.clear();

    // Calculate per-zone timings using stored zone order
    const auto& zoneOrder = frameZoneOrders[frameIndex];
    uint32_t queryIdx = 1;  // Skip frame start query

    for (const auto& zoneName : zoneOrder) {
        if (queryIdx + 1 >= queryCount - 1) break;  // Don't go past frame end

        uint64_t startTs = timestamps[queryIdx];
        uint64_t endTs = timestamps[queryIdx + 1];

        float zoneTimeNs = static_cast<float>(endTs - startTs) * timestampPeriod;
        float zoneTimeMs = zoneTimeNs / 1000000.0f;

        TimingResult timing;
        timing.name = zoneName;
        timing.gpuTimeMs = zoneTimeMs;
        timing.percentOfFrame = (frameTimeMs > 0.0f) ? (zoneTimeMs / frameTimeMs * 100.0f) : 0.0f;

        lastFrameStats.zones.push_back(timing);
        zoneNames.push_back(zoneName);

        queryIdx += 2;  // Each zone has start + end query
    }
}
