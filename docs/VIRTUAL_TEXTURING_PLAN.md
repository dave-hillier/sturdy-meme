# Virtual Texturing Plan

## Overview

Virtual texturing (VT) enables rendering arbitrarily large terrain textures by streaming only visible texture tiles on-demand. This implementation uses a **fully pre-baked approach** where biome-based terrain splatting, roads, and riverbeds are composited into megatexture tiles at build time.

### Key Design Decisions

- **Pre-baked splatting**: Biome materials are composited at build time, not runtime
- **Baked roads/rivers**: Road and river splines are rasterized into tiles at build time
- **Single VT lookup**: Runtime shader samples one pre-composited tile (vs 4+ for runtime splatting)
- **Biome-driven materials**: Uses existing BiomeGenerator zone/sub-zone data
- **River splines from ErosionSimulator**: Leverages existing river spline data

## Current Status

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Material Library & Source Assets | Complete |
| 2 | Road Spline Generation | Complete |
| 3 | Spline Rasterization | Complete |
| 4 | Tile Compositor | Complete |
| 5 | VT Runtime Infrastructure | Pending |
| 6 | Terrain Shader Integration | Pending |
| 7 | Mip Chain & Cache Management | Pending |
| 8 | Integration & Polish | Pending |

---

## Architecture

```
BUILD TIME
==========

  +-----------+   +-----------+   +-----------+   +------------+
  | Biome Map |   | Road      |   | River     |   | Materials  |
  | (zones)   |   | Splines   |   | Splines   |   | Library    |
  +-----+-----+   +-----+-----+   +-----+-----+   +------+-----+
        |               |               |                |
        v               v               v                |
  +--------------------------------------------------+   |
  |              Tile Compositor                     |<--+
  |  1. Splat biome materials                        |
  |  2. Rasterize river splines -> riverbed texture  |
  |  3. Rasterize road splines -> road texture       |
  |  4. Composite layers (base -> riverbed -> road)  |
  +--------------------------------------------------+
                         |
                         v
               +------------------+
               | Megatexture      |
               | Tiles (PNG)      |
               +------------------+

RUNTIME
=======

  +--------------+    +--------------+    +----------------------+
  |   Feedback   |--->|   Request    |--->|    Tile Loader       |
  |   Buffer     |    |   Queue      |    |  (Async Pipeline)    |
  +--------------+    +--------------+    +----------------------+
        ^                                         |
        |                                         v
  +--------------+    +--------------+    +----------------------+
  |   Terrain    |    | Indirection  |<---|   Physical Cache     |
  |   Fragment   |--->|   Texture    |    |   (Tile Atlas)       |
  +--------------+    +--------------+    +----------------------+
```

---

## Phase 1: Material Library & Source Assets

**Goal**: Gather and organize all source textures for terrain, roads, and riverbeds

### 1.1 Material Structure

```cpp
struct TerrainMaterial {
    std::string albedoPath;
    std::string normalPath;      // Optional
    std::string roughnessPath;   // Optional
    float tilingScale = 1.0f;
    float roughnessValue = 0.8f; // Fallback if no texture
};

struct BiomeMaterialMapping {
    // Main zone materials (indexed by BiomeZone enum)
    TerrainMaterial zoneMaterials[9];

    // Sub-zone variations (4 per zone)
    TerrainMaterial subZoneMaterials[9][4];

    // Slope-based override (for cliffs)
    TerrainMaterial cliffMaterial;
    float slopeThreshold = 0.7f;
};
```

### 1.2 Biome to Material Mapping

| Biome Zone | Primary Material | Sub-zone Variations |
|------------|------------------|---------------------|
| Sea | Water (not rendered via VT) | - |
| Beach | Sand | Wet sand, pebbles, driftwood, seaweed |
| Chalk Cliff | White chalk rock | Exposed chalk, grass-topped, eroded, flint |
| Salt Marsh | Muddy grass | Mudflat, saltpan, cordgrass, creek |
| River | Riverbed gravel | (handled by spline rasterization) |
| Wetland | Wet grass/reeds | Marsh grass, reeds, muddy, flooded |
| Grassland | Chalk down grass | Open, wildflower, gorse, chalk scrape |
| Agricultural | Ploughed earth | Ploughed, pasture, crop, fallow |
| Woodland | Forest floor | Beech leaves, oak/fern, clearing, coppice |

### 1.3 Road Materials

| Road Type | Width | Material |
|-----------|-------|----------|
| Footpath | 1.5m | Dirt/worn grass |
| Bridleway | 3m | Gravel/packed earth |
| Lane | 4m | Dirt/gravel |
| Road | 6m | Tarmac |
| Main Road | 8m | Tarmac with markings |

### 1.4 Riverbed Materials

- Center: Wet gravel/pebbles
- Edges: Mud/wet sand transition
- Width: 1.3x river surface width (visible bank)

**Files:**
- New: `tools/tile_generator/MaterialLibrary.h`
- New: `tools/tile_generator/MaterialLibrary.cpp`
- New: `assets/materials/terrain/` - Biome textures (from opengameart.org)
- New: `assets/materials/roads/` - Road surface textures
- New: `assets/materials/rivers/` - Riverbed textures

---

## Phase 2: Road Spline Generation

**Goal**: Generate road network connecting settlements

### 2.1 Road Data Structures

```cpp
enum class RoadType {
    Footpath,       // 1.5m wide
    BridleWay,      // 3m wide
    Lane,           // 4m wide
    Road,           // 6m wide
    MainRoad        // 8m wide
};

struct RoadControlPoint {
    glm::vec2 position;     // World XZ
    float width;            // Override width (0 = use default)
};

struct RoadSpline {
    std::vector<RoadControlPoint> controlPoints;
    RoadType type;
};
```

### 2.2 Road Generation Algorithm

```cpp
void generateRoadNetwork(const std::vector<Settlement>& settlements,
                         const HeightMap& heightmap,
                         std::vector<RoadSpline>& roads) {
    // Connect settlements with roads based on importance
    for (const Settlement& from : settlements) {
        for (const Settlement& to : nearbySettlements(from, maxDistance)) {
            RoadType type = determineRoadType(from, to);
            RoadSpline road = findPath(from.position, to.position, heightmap);
            road.type = type;
            roads.push_back(road);
        }
    }
}

// A* pathfinding with terrain-aware cost
float pathCost(vec2 from, vec2 to, const HeightMap& heightmap) {
    float slope = heightmap.getSlope(to);
    float distance = length(to - from);
    float waterPenalty = isWater(to) ? 1000.0f : 0.0f;
    float cliffPenalty = slope > 0.5f ? 500.0f : 0.0f;
    return distance * (1.0f + slope * 5.0f) + waterPenalty + cliffPenalty;
}
```

### 2.3 Road Type Selection

| Connection Type | Road Type |
|-----------------|-----------|
| Town to Town | Main Road |
| Town to Village | Road |
| Village to Village | Lane |
| Village to Hamlet | Lane or Bridleway |
| Any to FishingVillage | Lane (coastal) |

**Files:**
- New: `tools/road_generator/main.cpp`
- New: `tools/road_generator/RoadSpline.h`
- New: `tools/road_generator/RoadPathfinder.h`
- New: `tools/road_generator/RoadPathfinder.cpp`
- Output: `assets/generated/roads.bin`

---

## Phase 3: Spline Rasterization

**Goal**: Rasterize road and river splines into per-tile masks with UVs

### 3.1 Spline Rasterization Algorithm

```cpp
void rasterizeSplineToMask(const Spline& spline,
                           const TileBounds& tile,
                           Image& mask,
                           Image& uvs) {
    for (int y = 0; y < tileSize; y++) {
        for (int x = 0; x < tileSize; x++) {
            vec2 worldPos = tileTexelToWorld(x, y, tile);

            // Find closest point on spline
            float dist;
            float t;  // Parameter along spline [0, totalLength]
            float width = spline.closestPoint(worldPos, dist, t);

            // Signed distance (negative = inside)
            float sdf = dist - width * 0.5f;

            // Smooth mask with anti-aliased edge
            float alpha = 1.0f - smoothstep(-0.5f, 0.5f, sdf);
            mask.setPixel(x, y, alpha);

            // UV coordinates for texture sampling
            float u = (dist / width) + 0.5f;  // 0-1 across width
            float v = t;                       // Along spline length (repeating)
            uvs.setPixel(x, y, vec2(u, v));
        }
    }
}
```

### 3.2 Tile Processing

```cpp
void rasterizeSplinesToTile(TileContext& ctx) {
    // Rivers first (roads render on top)
    for (const RiverSpline& river : rivers) {
        if (river.intersectsTile(ctx.bounds)) {
            // Riverbed is wider than water surface
            RiverSpline widened = river;
            for (float& w : widened.widths) w *= 1.3f;
            rasterizeSplineToMask(widened, ctx.bounds,
                                  ctx.riverbedMask, ctx.riverbedUVs);
        }
    }

    // Roads on top
    for (const RoadSpline& road : roads) {
        if (road.intersectsTile(ctx.bounds)) {
            rasterizeSplineToMask(road, ctx.bounds,
                                  ctx.roadMask, ctx.roadUVs);
            ctx.roadTypes.fill(road.type);  // Store road type for material selection
        }
    }
}
```

**Files:**
- New: `tools/tile_generator/SplineRasterizer.h`
- New: `tools/tile_generator/SplineRasterizer.cpp`

---

## Phase 4: Tile Compositor

**Goal**: Composite all layers (biome splatting, rivers, roads) into final megatexture tiles

### 4.1 Biome Splatting

```cpp
vec4 splatBiomeMaterials(vec2 worldPos,
                         const BiomeMap& biomes,
                         const HeightMap& heightmap,
                         const MaterialLibrary& materials) {
    // Get biome data
    BiomeCell cell = biomes.sample(worldPos);
    float slope = heightmap.getSlope(worldPos);
    vec3 normal = heightmap.getNormal(worldPos);

    // Select materials
    const TerrainMaterial& primary = materials.getZoneMaterial(cell.zone);
    const TerrainMaterial& subZone = materials.getSubZoneMaterial(cell.zone, cell.subZone);
    const TerrainMaterial& cliff = materials.getCliffMaterial();

    // Calculate blend weights
    float slopeBlend = smoothstep(0.5f, 0.8f, slope);
    float subZoneBlend = perlinNoise(worldPos * 0.1f) * 0.3f;

    // Sample with triplanar mapping
    vec4 primaryColor = sampleTriplanar(primary.albedo, worldPos, normal);
    vec4 subZoneColor = sampleTriplanar(subZone.albedo, worldPos, normal);
    vec4 cliffColor = sampleTriplanar(cliff.albedo, worldPos, normal);

    // Blend layers
    vec4 result = mix(primaryColor, subZoneColor, subZoneBlend);
    result = mix(result, cliffColor, slopeBlend);

    return result;
}
```

### 4.2 Final Composition

```cpp
vec4 compositeTileTexel(int x, int y, TileContext& ctx) {
    vec2 worldPos = tileTexelToWorld(x, y, ctx);

    // 1. Base terrain from biome splatting
    vec4 color = splatBiomeMaterials(worldPos, ctx.biomeMap,
                                      ctx.heightmap, ctx.materials);

    // 2. Riverbed layer
    float riverbedMask = ctx.riverbedMask.sample(x, y);
    if (riverbedMask > 0.001f) {
        vec2 uv = ctx.riverbedUVs.sample(x, y);
        vec4 riverbed = sampleRiverbedMaterial(uv);
        color = mix(color, riverbed, riverbedMask);
    }

    // 3. Road layer (on top)
    float roadMask = ctx.roadMask.sample(x, y);
    if (roadMask > 0.001f) {
        vec2 uv = ctx.roadUVs.sample(x, y);
        RoadType type = ctx.roadTypes.sample(x, y);
        vec4 road = sampleRoadMaterial(uv, type);
        color = mix(color, road, roadMask);
    }

    return color;
}
```

### 4.3 Mip Chain Generation

Generate mip levels by downsampling with proper filtering:
- Mip 0: Full resolution (e.g., 512 tiles × 128px = 65536px virtual texture)
- Mip 1: Half resolution
- Mip N: Continue until single tile covers entire terrain

### 4.4 Output Structure

```
assets/virtual_textures/terrain/
+-- mip0/
|   +-- tile_0_0.png
|   +-- tile_0_1.png
|   +-- ...
|   +-- tile_511_511.png
+-- mip1/
|   +-- tile_0_0.png
|   +-- ...
+-- mip2/
+-- ...
+-- metadata.json
```

**Files:**
- New: `tools/tile_generator/TileCompositor.h`
- New: `tools/tile_generator/TileCompositor.cpp`
- New: `tools/tile_generator/main.cpp`
- Modify: `CMakeLists.txt` - Add tile generator build target

---

## Phase 5: VT Runtime Infrastructure

**Goal**: Implement core virtual texture streaming system

### 5.1 Core Types

```cpp
struct VirtualTextureConfig {
    uint32_t virtualSizePixels = 65536;  // 64K virtual texture
    uint32_t tileSizePixels = 128;       // 128x128 tiles
    uint32_t cacheSizePixels = 4096;     // 4K physical cache
    uint32_t borderPixels = 4;           // Tile border for filtering
    uint32_t maxMipLevels = 9;           // log2(512) = 9 mip levels
};

struct TileId {
    uint16_t x, y;      // Virtual tile coordinates
    uint8_t mipLevel;   // Mip level (0 = highest detail)

    uint32_t pack() const {
        return (mipLevel << 20) | (y << 10) | x;
    }
};

struct PageTableEntry {
    uint16_t cacheX, cacheY;  // Physical cache location
    uint8_t valid;            // 0 = not loaded, 1 = loaded
};
```

### 5.2 Physical Tile Cache

```cpp
class VirtualTextureCache {
public:
    void init(VulkanContext& ctx, const VirtualTextureConfig& config);

    // Allocate slot for new tile, evicting LRU if needed
    CacheSlot* allocateSlot(TileId id);

    // Mark tile as used this frame
    void markUsed(TileId id);

    // Upload tile data to cache texture
    void uploadTile(TileId id, const void* data, VkCommandBuffer cmd);

    VkImageView getCacheImageView() const;

private:
    VkImage cacheImage;           // RGBA8 texture array
    VmaAllocation cacheAllocation;
    std::vector<CacheSlot> slots;
    std::unordered_map<uint32_t, size_t> tileToSlot;
    uint32_t currentFrame = 0;
};
```

### 5.3 Page Table (Indirection Texture)

```cpp
class VirtualTexturePageTable {
public:
    void init(VulkanContext& ctx, const VirtualTextureConfig& config);

    // Update entry when tile is loaded
    void setEntry(TileId id, uint16_t cacheX, uint16_t cacheY);

    // Invalidate entry when tile is evicted
    void clearEntry(TileId id);

    // Upload changes to GPU
    void upload(VkCommandBuffer cmd);

    VkImageView getPageTableImageView() const;

private:
    // One texture per mip level
    std::vector<VkImage> pageTableImages;
    std::vector<PageTableEntry> cpuData;
    bool dirty = false;
};
```

### 5.4 Feedback Buffer

```cpp
struct FeedbackEntry {
    uint32_t tileIdPacked;   // TileId::pack()
    uint32_t priority;       // Screen-space priority
};

class VirtualTextureFeedback {
public:
    void init(VulkanContext& ctx, uint32_t maxEntries);

    // Read back feedback from GPU (async)
    void readback(uint32_t frameIndex);

    // Get requested tiles sorted by priority
    std::vector<TileId> getRequestedTiles();

    // Clear for next frame
    void clear(VkCommandBuffer cmd);

    VkBuffer getFeedbackBuffer() const;

private:
    VkBuffer feedbackBuffer;
    VkBuffer readbackBuffer;  // Host-visible staging
    std::vector<FeedbackEntry> cpuEntries;
};
```

### 5.5 Async Tile Loader

```cpp
class VirtualTextureTileLoader {
public:
    void init(const std::string& tileDirectory);
    void shutdown();

    // Queue tile for loading
    void requestTile(TileId id);

    // Get completed tiles (call from main thread)
    std::vector<LoadedTile> getCompletedTiles();

private:
    void loadThreadFunc();

    std::thread loadThread;
    std::mutex queueMutex;
    std::condition_variable queueCV;
    std::queue<TileId> requestQueue;
    std::queue<LoadedTile> completedQueue;
    std::atomic<bool> running{true};
};
```

### 5.6 Main System Orchestrator

```cpp
class VirtualTextureSystem {
public:
    struct InitInfo {
        VulkanContext* vulkanContext;
        std::string tileDirectory;
        VirtualTextureConfig config;
    };

    void init(const InitInfo& info);
    void shutdown();

    // Called each frame
    void update(uint32_t frameIndex);
    void uploadPendingTiles(VkCommandBuffer cmd);

    // Descriptors for terrain shader
    VkDescriptorSetLayout getDescriptorLayout() const;
    VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const;

private:
    VirtualTextureCache cache;
    VirtualTexturePageTable pageTable;
    VirtualTextureFeedback feedback;
    VirtualTextureTileLoader loader;
    VirtualTextureConfig config;
};
```

**Files:**
- New: `src/VirtualTextureTypes.h`
- New: `src/VirtualTextureCache.h`
- New: `src/VirtualTextureCache.cpp`
- New: `src/VirtualTexturePageTable.h`
- New: `src/VirtualTexturePageTable.cpp`
- New: `src/VirtualTextureFeedback.h`
- New: `src/VirtualTextureFeedback.cpp`
- New: `src/VirtualTextureTileLoader.h`
- New: `src/VirtualTextureTileLoader.cpp`
- New: `src/VirtualTextureSystem.h`
- New: `src/VirtualTextureSystem.cpp`

---

## Phase 6: Terrain Shader Integration

**Goal**: Replace triplanar sampling with VT lookup

### 6.1 New Bindings

```cpp
// shaders/bindings.h additions

// Virtual Texture bindings (terrain descriptor set)
#define BINDING_VT_INDIRECTION      20
#define BINDING_VT_PHYSICAL_CACHE   21
#define BINDING_VT_FEEDBACK         22
#define BINDING_VT_PARAMS_UBO       23
```

### 6.2 VT Sampling Function

```glsl
// shaders/virtual_texture.glsl

struct VTParams {
    vec4 virtualTextureSizeAndInverse;  // xy = size, zw = 1/size
    vec4 physicalCacheSizeAndInverse;
    vec4 tileSizeAndBorder;             // x = tile size, y = border, zw = unused
    uint maxMipLevel;
};

layout(std140, binding = BINDING_VT_PARAMS_UBO) uniform VTParamsUBO {
    VTParams vtParams;
};

layout(binding = BINDING_VT_INDIRECTION) uniform sampler2D vtIndirection;
layout(binding = BINDING_VT_PHYSICAL_CACHE) uniform sampler2D vtCache;
layout(binding = BINDING_VT_FEEDBACK) buffer VTFeedback {
    uint feedbackCount;
    uint feedbackEntries[];
};

vec4 sampleVirtualTexture(vec2 virtualUV, float mipLevel) {
    // Clamp mip level
    uint mip = clamp(uint(mipLevel), 0u, vtParams.maxMipLevel);

    // Calculate tile coordinates at this mip level
    float tilesAtMip = vtParams.virtualTextureSizeAndInverse.x /
                       (vtParams.tileSizeAndBorder.x * float(1u << mip));
    vec2 tileCoord = virtualUV * tilesAtMip;
    ivec2 tileId = ivec2(floor(tileCoord));
    vec2 inTileUV = fract(tileCoord);

    // Look up page table
    vec4 pageEntry = texelFetch(vtIndirection, ivec2(tileId.x, tileId.y + mipOffset), 0);

    if (pageEntry.a < 0.5) {
        // Tile not loaded - write feedback request
        uint packed = (mip << 20u) | (uint(tileId.y) << 10u) | uint(tileId.x);
        uint idx = atomicAdd(feedbackCount, 1u);
        if (idx < MAX_FEEDBACK_ENTRIES) {
            feedbackEntries[idx] = packed;
        }

        // Fallback to coarser mip
        return sampleVirtualTexture(virtualUV, float(mip + 1u));
    }

    // Transform to physical cache coordinates
    vec2 cachePos = pageEntry.xy;  // Cache tile position
    float tilePixels = vtParams.tileSizeAndBorder.x;
    float border = vtParams.tileSizeAndBorder.y;

    vec2 physicalUV = (cachePos * tilePixels + border + inTileUV * (tilePixels - 2.0 * border))
                    * vtParams.physicalCacheSizeAndInverse.zw;

    return texture(vtCache, physicalUV);
}
```

### 6.3 Updated Terrain Fragment Shader

```glsl
// shaders/terrain/terrain.frag (simplified)

#include "virtual_texture.glsl"

void main() {
    // Calculate virtual UV from world position
    vec2 vtUV = inWorldPos.xz / virtualTextureWorldSize;

    // Calculate mip level from screen-space derivatives
    vec2 dx = dFdx(vtUV * vtParams.virtualTextureSizeAndInverse.x);
    vec2 dy = dFdy(vtUV * vtParams.virtualTextureSizeAndInverse.x);
    float mipLevel = 0.5 * log2(max(dot(dx, dx), dot(dy, dy)));

    // Single VT lookup - roads/rivers already baked in
    vec4 albedo = sampleVirtualTexture(vtUV, mipLevel);

    // Normal from heightmap (unchanged)
    vec3 normal = calculateTerrainNormal();

    // PBR lighting (unchanged)
    float roughness = 0.8;
    vec3 finalColor = applyPBRLighting(albedo.rgb, normal, roughness);

    // Snow, shadows, etc. (unchanged)
    finalColor = applySnowCoverage(finalColor, inWorldPos);
    finalColor = applyShadows(finalColor);

    outColor = vec4(finalColor, 1.0);
}
```

**Files:**
- New: `shaders/virtual_texture.glsl`
- Modify: `shaders/bindings.h`
- Modify: `shaders/terrain/terrain.frag`

---

## Phase 7: Mip Chain & Cache Management

**Goal**: Smooth LOD transitions and efficient cache usage

### 7.1 LRU Cache Eviction

```cpp
CacheSlot* VirtualTextureCache::allocateSlot(TileId id) {
    // Check if already cached
    auto it = tileToSlot.find(id.pack());
    if (it != tileToSlot.end()) {
        slots[it->second].lastUsedFrame = currentFrame;
        return &slots[it->second];
    }

    // Find empty or LRU slot
    size_t bestSlot = 0;
    uint32_t oldestFrame = UINT32_MAX;

    for (size_t i = 0; i < slots.size(); i++) {
        if (!slots[i].valid) {
            bestSlot = i;
            break;
        }
        if (slots[i].lastUsedFrame < oldestFrame) {
            oldestFrame = slots[i].lastUsedFrame;
            bestSlot = i;
        }
    }

    // Evict old tile if needed
    if (slots[bestSlot].valid) {
        pageTable.clearEntry(slots[bestSlot].tileId);
        tileToSlot.erase(slots[bestSlot].tileId.pack());
    }

    // Assign new tile
    slots[bestSlot].tileId = id;
    slots[bestSlot].lastUsedFrame = currentFrame;
    slots[bestSlot].valid = true;
    tileToSlot[id.pack()] = bestSlot;

    return &slots[bestSlot];
}
```

### 7.2 Mip Level Fallback

When a tile isn't loaded, the shader automatically falls back to coarser mip levels. This ensures something is always displayed while fine detail loads in the background.

### 7.3 Trilinear Filtering

For smooth mip transitions, sample two adjacent mip levels and blend:

```glsl
vec4 sampleVirtualTextureTrilinear(vec2 virtualUV, float mipLevel) {
    float mipFloor = floor(mipLevel);
    float mipFrac = fract(mipLevel);

    vec4 mip0 = sampleVirtualTexture(virtualUV, mipFloor);
    vec4 mip1 = sampleVirtualTexture(virtualUV, mipFloor + 1.0);

    return mix(mip0, mip1, mipFrac);
}
```

**Files:**
- Modify: `src/VirtualTextureCache.cpp`
- Modify: `shaders/virtual_texture.glsl`

---

## Phase 8: Integration & Polish

**Goal**: Full system integration with debugging and optimization

### 8.1 Renderer Integration

```cpp
// In Renderer::initSystems()
VirtualTextureSystem::InitInfo vtInfo{};
vtInfo.vulkanContext = &vulkanContext;
vtInfo.tileDirectory = "assets/virtual_textures/terrain";
vtInfo.config.virtualSizePixels = 65536;
vtInfo.config.tileSizePixels = 128;
vtInfo.config.cacheSizePixels = 4096;
virtualTextureSystem.init(vtInfo);

// In Renderer::render()
virtualTextureSystem.update(frameIndex);
// ... in command buffer recording ...
virtualTextureSystem.uploadPendingTiles(cmd);
```

### 8.2 Debug Visualization

```glsl
#ifdef VT_DEBUG
    // Color code by mip level
    vec3 mipColors[9] = vec3[](
        vec3(1,0,0), vec3(1,0.5,0), vec3(1,1,0), vec3(0,1,0),
        vec3(0,1,1), vec3(0,0,1), vec3(0.5,0,1), vec3(1,0,1), vec3(1,1,1)
    );
    finalColor = mix(finalColor, mipColors[int(mipLevel)], 0.3);

    // Tile boundaries
    vec2 tileUV = fract(virtualUV * tilesPerAxis);
    if (tileUV.x < 0.01 || tileUV.y < 0.01) {
        finalColor = vec3(1.0);
    }
#endif
```

### 8.3 ImGui Debug Panel

- Cache occupancy (used/total slots)
- Tile load rate (tiles/second)
- Feedback buffer usage
- Pending request queue size
- Toggle debug visualization

**Files:**
- Modify: `src/Renderer.h`
- Modify: `src/Renderer.cpp`
- Modify: `src/TerrainSystem.cpp` - Pass VT descriptors

---

## Memory Budget

| Resource | Size | Notes |
|----------|------|-------|
| Physical VT Cache | 64 MB | 4096² RGBA8 texture |
| Indirection Textures | 1 MB | 512² per mip level (9 levels) |
| Feedback Buffer | 256 KB | Per-frame tile requests |
| Staging Buffers | 4 MB | Double-buffered upload |
| **Total GPU** | ~70 MB | Configurable via VirtualTextureConfig |

---

## Files Summary

### Build-time Tools

| File | Purpose |
|------|---------|
| `tools/tile_generator/main.cpp` | CLI entry point |
| `tools/tile_generator/MaterialLibrary.h/.cpp` | Material definitions |
| `tools/tile_generator/SplineRasterizer.h/.cpp` | Road/river rasterization |
| `tools/tile_generator/TileCompositor.h/.cpp` | Final tile composition |
| `tools/road_generator/main.cpp` | Road network generation |
| `tools/road_generator/RoadSpline.h` | Road data structures |
| `tools/road_generator/RoadPathfinder.h/.cpp` | A* pathfinding |

### Runtime

| File | Purpose |
|------|---------|
| `src/VirtualTextureTypes.h` | Core types and config |
| `src/VirtualTextureCache.h/.cpp` | Physical tile cache |
| `src/VirtualTexturePageTable.h/.cpp` | Indirection texture |
| `src/VirtualTextureFeedback.h/.cpp` | GPU feedback handling |
| `src/VirtualTextureTileLoader.h/.cpp` | Async loading thread |
| `src/VirtualTextureSystem.h/.cpp` | Main orchestrator |
| `shaders/virtual_texture.glsl` | VT sampling functions |

### Modified

| File | Changes |
|------|---------|
| `shaders/bindings.h` | Add VT bindings |
| `shaders/terrain/terrain.frag` | Replace triplanar with VT |
| `src/Renderer.h/.cpp` | Init VT system |
| `src/TerrainSystem.h/.cpp` | Pass VT descriptors |
| `CMakeLists.txt` | Add generator tools |

---

## Testing

### Phase 1-4 (Build Tools)
```bash
# Generate road network
./build/debug/road_generator --heightmap assets/heightmap.png \
    --settlements assets/generated/settlements.json \
    --output assets/generated/roads.bin

# Generate megatexture tiles
./build/debug/tile_generator --biome assets/generated/biome_map.png \
    --heightmap assets/heightmap.png \
    --rivers assets/generated/rivers.bin \
    --roads assets/generated/roads.bin \
    --materials assets/materials/ \
    --output assets/virtual_textures/terrain/
```

### Phase 5-8 (Runtime)
```bash
cmake --preset debug && cmake --build build/debug
./run-debug.sh
```

- Fly camera across terrain, verify tiles stream in
- Check no frame drops during loading
- Verify roads/rivers appear correctly
- Test cache eviction by flying to many areas
- Enable debug visualization to verify mip levels
