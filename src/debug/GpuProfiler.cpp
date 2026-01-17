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
    , currentQueryIndex(other.currentQueryIndex.load(std::memory_order_relaxed))
    , currentZoneSlot(other.currentZoneSlot.load(std::memory_order_relaxed))
    , currentFrameIndex(other.currentFrameIndex)
    , zoneSlots_(std::move(other.zoneSlots_))
    , frameQueryCounts(std::move(other.frameQueryCounts))
    , frameZoneCounts(std::move(other.frameZoneCounts))
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
        currentQueryIndex.store(other.currentQueryIndex.load(std::memory_order_relaxed), std::memory_order_relaxed);
        currentZoneSlot.store(other.currentZoneSlot.load(std::memory_order_relaxed), std::memory_order_relaxed);
        currentFrameIndex = other.currentFrameIndex;
        zoneSlots_ = std::move(other.zoneSlots_);
        frameQueryCounts = std::move(other.frameQueryCounts);
        frameZoneCounts = std::move(other.frameZoneCounts);
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

    // Pre-allocate zone slots for lock-free recording (one array per frame in flight)
    zoneSlots_.resize(framesInFlight);
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        zoneSlots_[i] = std::make_unique<ZoneSlot[]>(maxZones);
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
    zoneSlots_.clear();
    device = VK_NULL_HANDLE;
}

void GpuProfiler::beginFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!enabled || queryPools.empty()) return;

    // Collect results from previous frame first (before reset)
    collectResults(frameIndex);

    // Reset state for this frame - no locks needed, single-threaded frame setup
    currentQueryIndex.store(0, std::memory_order_relaxed);
    currentZoneSlot.store(0, std::memory_order_relaxed);

    currentFrameIndex = frameIndex;

    // Reset zone slots for this frame (mark as unused)
    ZoneSlot* slots = zoneSlots_[frameIndex].get();
    for (uint32_t i = 0; i < maxZones; ++i) {
        slots[i].startQueryIndex.store(UINT32_MAX, std::memory_order_relaxed);
        slots[i].endQueryIndex.store(UINT32_MAX, std::memory_order_relaxed);
        slots[i].name = nullptr;
    }

    vk::CommandBuffer vkCmd(cmd);

    // Reset the query pool for this frame
    vkCmd.resetQueryPool(queryPools[frameIndex], 0, (maxZones * QUERIES_PER_ZONE) + 2);

    // Write frame start timestamp
    frameStartQuery = currentQueryIndex.fetch_add(1, std::memory_order_relaxed);
    vkCmd.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, queryPools[frameIndex], frameStartQuery);
}

void GpuProfiler::endFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!enabled || queryPools.empty()) return;

    vk::CommandBuffer vkCmd(cmd);

    // Write frame end timestamp
    frameEndQuery = currentQueryIndex.fetch_add(1, std::memory_order_relaxed);
    vkCmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, queryPools[frameIndex], frameEndQuery);

    // Store the query count and zone count for this frame for later collection
    frameQueryCounts[frameIndex] = currentQueryIndex.load(std::memory_order_relaxed);
    frameZoneCounts[frameIndex] = currentZoneSlot.load(std::memory_order_relaxed);
}

void GpuProfiler::beginZone(VkCommandBuffer cmd, const char* zoneName) {
    if (!enabled || queryPools.empty()) return;

    // Atomically allocate a zone slot - lock-free
    uint32_t slotIdx = currentZoneSlot.fetch_add(1, std::memory_order_relaxed);
    if (slotIdx >= maxZones) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "GPU Profiler: max zones exceeded");
        return;
    }

    // Atomically allocate a query index - lock-free
    uint32_t queryIdx = currentQueryIndex.fetch_add(1, std::memory_order_relaxed);

    // Store zone info - the slot is exclusively ours after fetch_add
    ZoneSlot& slot = zoneSlots_[currentFrameIndex][slotIdx];
    slot.name = zoneName;
    slot.startQueryIndex.store(queryIdx, std::memory_order_release);  // Release so endZone sees name

    vk::CommandBuffer vkCmd(cmd);

    // Write start timestamp - use ALL_COMMANDS to ensure prior work is complete
    vkCmd.writeTimestamp(vk::PipelineStageFlagBits::eAllCommands,
                         queryPools[currentFrameIndex], queryIdx);
}

void GpuProfiler::endZone(VkCommandBuffer cmd, const char* zoneName) {
    if (!enabled || queryPools.empty()) return;

    // Find the zone slot by name - lock-free linear scan
    // This is O(n) but n is small (typically < 20 zones per frame)
    uint32_t numSlots = currentZoneSlot.load(std::memory_order_acquire);
    ZoneSlot* frameSlots = zoneSlots_[currentFrameIndex].get();
    ZoneSlot* foundSlot = nullptr;

    for (uint32_t i = 0; i < numSlots && i < maxZones; ++i) {
        ZoneSlot& slot = frameSlots[i];
        // Check if slot is initialized and name matches
        if (slot.startQueryIndex.load(std::memory_order_acquire) != UINT32_MAX &&
            slot.name != nullptr &&
            std::strcmp(slot.name, zoneName) == 0 &&
            slot.endQueryIndex.load(std::memory_order_relaxed) == UINT32_MAX) {  // Not yet ended
            foundSlot = &slot;
            break;
        }
    }

    if (!foundSlot) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "GPU Profiler: endZone called without beginZone for '%s'", zoneName);
        return;
    }

    // Atomically allocate a query index - lock-free
    uint32_t endQueryIdx = currentQueryIndex.fetch_add(1, std::memory_order_relaxed);
    foundSlot->endQueryIndex.store(endQueryIdx, std::memory_order_relaxed);

    vk::CommandBuffer vkCmd(cmd);

    // Write end timestamp - use ALL_COMMANDS to capture actual work completion
    vkCmd.writeTimestamp(vk::PipelineStageFlagBits::eAllCommands,
                         queryPools[currentFrameIndex], endQueryIdx);
}

void GpuProfiler::collectResults(uint32_t frameIndex) {
    if (!enabled || queryPools.empty()) return;

    // We collect from the frame we're about to overwrite (previous use of this pool)
    // On first few frames, there won't be valid data
    if (frameQueryCounts.find(frameIndex) == frameQueryCounts.end()) {
        return;  // No data for this frame yet
    }

    uint32_t queryCount = frameQueryCounts[frameIndex];
    uint32_t zoneCount = frameZoneCounts[frameIndex];
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

    // Calculate per-zone timings from zone slots for this frame
    const ZoneSlot* frameSlots = zoneSlots_[frameIndex].get();
    for (uint32_t i = 0; i < zoneCount && i < maxZones; ++i) {
        const ZoneSlot& slot = frameSlots[i];
        uint32_t startIdx = slot.startQueryIndex.load(std::memory_order_relaxed);
        uint32_t endIdx = slot.endQueryIndex.load(std::memory_order_relaxed);

        if (startIdx == UINT32_MAX || endIdx == UINT32_MAX || slot.name == nullptr) {
            continue;  // Invalid or incomplete zone
        }

        if (startIdx >= queryCount || endIdx >= queryCount) {
            continue;  // Out of bounds
        }

        uint64_t startTs = timestamps[startIdx];
        uint64_t endTs = timestamps[endIdx];

        float zoneTimeNs = static_cast<float>(endTs - startTs) * timestampPeriod;
        float zoneTimeMs = zoneTimeNs / 1000000.0f;

        TimingResult timing;
        timing.name = slot.name;
        timing.gpuTimeMs = zoneTimeMs;
        timing.percentOfFrame = (frameTimeMs > 0.0f) ? (zoneTimeMs / frameTimeMs * 100.0f) : 0.0f;

        lastFrameStats.zones.push_back(timing);
        zoneNames.push_back(slot.name);
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

    // Calculate sum of measured zones, excluding sub-zones from the Idle/Sync calculation
    // Sub-zones (e.g., HDR:Sky, Shadow:Terrain) are nested inside parent zones and would be double-counted
    float measuredTotal = 0.0f;
    bool hasTerrainCompute = smoothedZoneTimes.count("TerrainCompute") > 0;
    bool hasHDRPass = smoothedZoneTimes.count("HDRPass") > 0;
    bool hasShadowPass = smoothedZoneTimes.count("ShadowPass") > 0;
    bool hasAtmosphere = smoothedZoneTimes.count("Atmosphere") > 0;

    for (const auto& [name, time] : sortedZones) {
        // Skip sub-zones if we have the parent zone (they're already counted in the parent)
        if (hasTerrainCompute && name.rfind("Terrain:", 0) == 0) {
            continue;
        }
        if (hasHDRPass && name.rfind("HDR:", 0) == 0) {
            continue;
        }
        if (hasShadowPass && name.rfind("Shadow:", 0) == 0) {
            continue;
        }
        if (hasAtmosphere && name.rfind("Atmosphere:", 0) == 0) {
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
