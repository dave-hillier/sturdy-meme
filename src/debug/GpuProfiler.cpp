#include "GpuProfiler.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <algorithm>
#include <cstring>

// Private constructor
GpuProfiler::GpuProfiler() = default;

// Factory
std::optional<GpuProfiler> GpuProfiler::create(VkDevice device, VkPhysicalDevice physicalDevice,
                                                uint32_t framesInFlight, uint32_t maxZones) {
    GpuProfiler profiler;
    if (!profiler.initInternal(device, physicalDevice, framesInFlight, maxZones)) {
        return std::nullopt;
    }
    return profiler;
}

// Destructor
GpuProfiler::~GpuProfiler() {
    cleanup();
}

// Move constructor
GpuProfiler::GpuProfiler(GpuProfiler&& other) noexcept
    : device(other.device)
    , queryPools(std::move(other.queryPools))
    , timestampPeriod(other.timestampPeriod)
    , maxZones(other.maxZones)
    , framesInFlight(other.framesInFlight)
    , enabled(other.enabled)
    , currentQueryIndex(other.currentQueryIndex)
    , currentFrameIndex(other.currentFrameIndex)
    , activeZones(std::move(other.activeZones))
    , currentFrameZoneOrder(std::move(other.currentFrameZoneOrder))
    , frameQueryCounts(std::move(other.frameQueryCounts))
    , frameZoneOrders(std::move(other.frameZoneOrders))
    , lastFrameStats(std::move(other.lastFrameStats))
    , smoothedStats(std::move(other.smoothedStats))
    , smoothedZoneTimes(std::move(other.smoothedZoneTimes))
    , zoneNames(std::move(other.zoneNames))
    , smoothedFrameTimeMs(other.smoothedFrameTimeMs)
    , frameStartQuery(other.frameStartQuery)
    , frameEndQuery(other.frameEndQuery)
{
    // Null out source to prevent double-free
    other.device = VK_NULL_HANDLE;
}

// Move assignment
GpuProfiler& GpuProfiler::operator=(GpuProfiler&& other) noexcept {
    if (this != &other) {
        cleanup();

        device = other.device;
        queryPools = std::move(other.queryPools);
        timestampPeriod = other.timestampPeriod;
        maxZones = other.maxZones;
        framesInFlight = other.framesInFlight;
        enabled = other.enabled;
        currentQueryIndex = other.currentQueryIndex;
        currentFrameIndex = other.currentFrameIndex;
        activeZones = std::move(other.activeZones);
        currentFrameZoneOrder = std::move(other.currentFrameZoneOrder);
        frameQueryCounts = std::move(other.frameQueryCounts);
        frameZoneOrders = std::move(other.frameZoneOrders);
        lastFrameStats = std::move(other.lastFrameStats);
        smoothedStats = std::move(other.smoothedStats);
        smoothedZoneTimes = std::move(other.smoothedZoneTimes);
        zoneNames = std::move(other.zoneNames);
        smoothedFrameTimeMs = other.smoothedFrameTimeMs;
        frameStartQuery = other.frameStartQuery;
        frameEndQuery = other.frameEndQuery;

        other.device = VK_NULL_HANDLE;
    }
    return *this;
}

bool GpuProfiler::initInternal(VkDevice dev, VkPhysicalDevice physicalDevice,
                                uint32_t framesInFlight_, uint32_t maxZones_) {
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
            cleanup();
            return false;
        }
    }

    SDL_Log("GPU Profiler initialized: %d zones max, %d frames in flight", maxZones, framesInFlight);
    return true;
}

void GpuProfiler::cleanup() {
    if (device != VK_NULL_HANDLE) {
        for (auto& pool : queryPools) {
            if (pool != VK_NULL_HANDLE) {
                vkDestroyQueryPool(device, pool, nullptr);
                pool = VK_NULL_HANDLE;
            }
        }
    }
    queryPools.clear();
    device = VK_NULL_HANDLE;
}

void GpuProfiler::beginFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!enabled || queryPools.empty()) return;

    // Collect results from previous frame first (before reset)
    collectResults(frameIndex);

    // Reset state for this frame
    currentQueryIndex = 0;
    activeZones.clear();
    currentFrameZoneOrder.clear();
    currentFrameIndex = frameIndex;

    vk::CommandBuffer vkCmd(cmd);

    // Reset the query pool for this frame
    vkCmd.resetQueryPool(queryPools[frameIndex], 0, (maxZones * QUERIES_PER_ZONE) + 2);

    // Write frame start timestamp
    frameStartQuery = currentQueryIndex++;
    vkCmd.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, queryPools[frameIndex], frameStartQuery);
}

void GpuProfiler::endFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!enabled || queryPools.empty()) return;

    vk::CommandBuffer vkCmd(cmd);

    // Write frame end timestamp
    frameEndQuery = currentQueryIndex++;
    vkCmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, queryPools[frameIndex], frameEndQuery);

    // Store the query count and zone order for this frame for later collection
    frameQueryCounts[frameIndex] = currentQueryIndex;
    frameZoneOrders[frameIndex] = currentFrameZoneOrder;
}

void GpuProfiler::beginZone(VkCommandBuffer cmd, const char* zoneName) {
    if (!enabled || queryPools.empty()) return;

    if (currentQueryIndex >= (maxZones * QUERIES_PER_ZONE)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "GPU Profiler: max zones exceeded");
        return;
    }

    ZoneInfo zone;
    zone.startQueryIndex = currentQueryIndex++;
    activeZones[zoneName] = zone;
    currentFrameZoneOrder.push_back(zoneName);

    vk::CommandBuffer vkCmd(cmd);

    // Write start timestamp - use ALL_COMMANDS to ensure prior work is complete
    vkCmd.writeTimestamp(vk::PipelineStageFlagBits::eAllCommands,
                         queryPools[currentFrameIndex], zone.startQueryIndex);
}

void GpuProfiler::endZone(VkCommandBuffer cmd, const char* zoneName) {
    if (!enabled || queryPools.empty()) return;

    auto it = activeZones.find(zoneName);
    if (it == activeZones.end()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "GPU Profiler: endZone called without beginZone for '%s'", zoneName);
        return;
    }

    it->second.endQueryIndex = currentQueryIndex++;

    vk::CommandBuffer vkCmd(cmd);

    // Write end timestamp - use ALL_COMMANDS to capture actual work completion
    vkCmd.writeTimestamp(vk::PipelineStageFlagBits::eAllCommands,
                         queryPools[currentFrameIndex], it->second.endQueryIndex);
}

void GpuProfiler::collectResults(uint32_t frameIndex) {
    if (!enabled || queryPools.empty()) return;

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

    // Update smoothed frame time
    smoothedFrameTimeMs = SMOOTHING_FACTOR * smoothedFrameTimeMs + (1.0f - SMOOTHING_FACTOR) * frameTimeMs;

    // Update smoothed stats using exponential moving average
    // Track which zones were seen this frame
    std::unordered_map<std::string, bool> seenThisFrame;

    // Update smoothed values for zones in this frame
    for (const auto& zone : lastFrameStats.zones) {
        seenThisFrame[zone.name] = true;

        auto it = smoothedZoneTimes.find(zone.name);
        if (it == smoothedZoneTimes.end()) {
            // New zone - initialize with current value
            smoothedZoneTimes[zone.name] = zone.gpuTimeMs;
        } else {
            // Existing zone - apply EMA
            it->second = SMOOTHING_FACTOR * it->second + (1.0f - SMOOTHING_FACTOR) * zone.gpuTimeMs;
        }
    }

    // Decay zones not seen this frame (they may be intermittent like FrustumCull)
    for (auto& [name, time] : smoothedZoneTimes) {
        if (!seenThisFrame[name]) {
            // Decay toward zero but keep it in the map for a while
            time *= SMOOTHING_FACTOR;
        }
    }

    // Remove zones that have decayed to near-zero
    for (auto it = smoothedZoneTimes.begin(); it != smoothedZoneTimes.end(); ) {
        if (it->second < 0.001f) {
            it = smoothedZoneTimes.erase(it);
        } else {
            ++it;
        }
    }

    // Compute TerrainCompute as sum of Terrain:* sub-zones (since nested zones don't work)
    float terrainTotal = 0.0f;
    for (const auto& [name, time] : smoothedZoneTimes) {
        if (name.rfind("Terrain:", 0) == 0) {
            terrainTotal += time;
        }
    }
    // Replace the broken TerrainCompute measurement with the computed sum
    if (terrainTotal > 0.0f) {
        smoothedZoneTimes["TerrainCompute"] = terrainTotal;
    }

    // Build smoothed stats from the map
    smoothedStats.totalGpuTimeMs = smoothedFrameTimeMs;
    smoothedStats.zones.clear();

    // Sort by time descending for stable display order
    std::vector<std::pair<std::string, float>> sortedZones(smoothedZoneTimes.begin(), smoothedZoneTimes.end());
    std::sort(sortedZones.begin(), sortedZones.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    // Calculate sum of measured zones (excluding Terrain:* sub-zones if TerrainCompute is present)
    float measuredTotal = 0.0f;
    bool hasTerrainCompute = smoothedZoneTimes.count("TerrainCompute") > 0;

    for (const auto& [name, time] : sortedZones) {
        // Skip Terrain:* sub-zones if we have the aggregated TerrainCompute
        if (hasTerrainCompute && name.rfind("Terrain:", 0) == 0) {
            continue;
        }

        TimingResult result;
        result.name = name;
        result.gpuTimeMs = time;
        result.percentOfFrame = (smoothedFrameTimeMs > 0.0f) ? (time / smoothedFrameTimeMs * 100.0f) : 0.0f;
        smoothedStats.zones.push_back(result);
        measuredTotal += time;
    }

    // Add "Idle/Sync" zone for unaccounted time
    // This is typically GPU idle time waiting for vsync or semaphores
    float idleTime = smoothedFrameTimeMs - measuredTotal;
    if (idleTime > 0.01f) {  // Only show if > 0.01ms
        TimingResult idle;
        idle.name = "Idle/Sync";
        idle.gpuTimeMs = idleTime;
        idle.percentOfFrame = (smoothedFrameTimeMs > 0.0f) ? (idleTime / smoothedFrameTimeMs * 100.0f) : 0.0f;
        smoothedStats.zones.push_back(idle);
    }
}
