# City Generator Engine Integration Plan

This document outlines the comprehensive plan for integrating the MFCG-based city generator into the 3D engine, enabling runtime rendering of procedurally generated medieval cities.

## Current State

**City Generator (`tools/town_generator/`):**
- Full MFCG (Medieval Fantasy City Generator) port in C++
- Voronoi-based city layout with wards (Castle, Market, Cathedral, Farm, Harbour, Alleys, Park, Wilderness)
- Street network via A* pathfinding from gates to plaza
- Block subdivision into lots then buildings (4-stage: bisector → LIRA → buildings → courtyard)
- Walls with gates and towers
- Coastlines and canals with bridges
- **Output: SVG only** (no intermediate format)

**Engine:**
- Vulkan rendering with SceneManager, MaterialRegistry, and Renderable system
- Terrain with height queries via `TerrainSystem::getHeightAt()`
- Procedural vegetation (grass, trees, scatter)
- glTF/FBX/OBJ mesh loading
- Build-time procedural content generation
- **Virtual Texture System** - 65536×65536 megatexture with 128×128 tiles, streaming
- **RoadNetworkLoader** - Already loads GeoJSON roads (but doesn't render them)
- **MaterialLayerStack** - Composable terrain material blending (height/slope/distance modes)

---

## Recommended Implementation Order

The plan is structured to provide **incremental visual progress**:

| Phase | Deliverable | Visual Result |
|-------|-------------|---------------|
| 1 | GeoJSON Export | Data viewable in GIS tools |
| 2 | **Virtual Texture Streets** | Streets visible on terrain from any distance |
| 3 | **Ward Ground Materials** | Colored ward areas on terrain |
| 4 | Basic 3D Buildings | White box buildings |
| 5 | Building Detail | Doors, windows, roofs |
| 6 | Walls & Infrastructure | Fortifications, bridges |
| 7 | LOD & Polish | Performance, decorations |

**Start with Phase 2 (Virtual Textures)** for fastest visual payoff with existing infrastructure.

---

## Phase 1: Intermediate Data Format (GeoJSON Export)

### Goal
Replace SVG output with structured GeoJSON that preserves all semantic data for 3D conversion.

### Output Files

```
generated/city/
├── city_metadata.json       # City-level config (seed, dimensions, ward colors)
├── city_streets.geojson     # Road network with width/type
├── city_buildings.geojson   # Building footprints with ward type, lot info
├── city_walls.geojson       # Wall segments with gates and towers
├── city_water.geojson       # Coastline, canals, bridges
├── city_wards.geojson       # Ward boundaries for ground materials
└── city_blocks.geojson      # Block outlines (optional, for debugging)
```

### Building Feature Properties

```json
{
  "type": "Feature",
  "geometry": { "type": "Polygon", "coordinates": [...] },
  "properties": {
    "id": "building_0042",
    "ward_type": "Alleys",
    "block_id": "block_012",
    "lot_index": 3,
    "frontage_edge": [[x1, y1], [x2, y2]],
    "area_sqm": 145.2,
    "is_corner": true,
    "touches_street": true,
    "touches_courtyard": false,
    "building_class": "residential",
    "floors": 2
  }
}
```

### Implementation

1. Add `src/export/GeoJSONWriter.h` to town_generator
2. Traverse City data structure, emitting features with semantic properties
3. Preserve frontage edge info (critical for door placement)
4. Add building classification heuristics based on ward type and size:
   - Castle ward → keep, great hall
   - Cathedral ward → church
   - Market ward → shop, tavern, warehouse
   - Alleys ward → residential, small shop
   - Harbour ward → warehouse, dock building

---

## Phase 2: Virtual Texture Integration (Priority)

### Goal
Bake city streets, plazas, and ward ground materials into the terrain virtual texture system for immediate visual results at all distances.

### Why First?
- **Existing infrastructure**: VirtualTextureSystem and RoadNetworkLoader already exist
- **Immediate payoff**: Streets visible from world map to close-up without new 3D systems
- **Natural LOD**: VT mip levels provide automatic distance-based detail reduction
- **Foundation**: Ground materials establish city footprint before adding 3D buildings

### Architecture

```
City Generator → GeoJSON → CityTextureBaker → VT Tiles → Runtime Streaming
     ↓
  streets.geojson
  wards.geojson
  buildings.geojson (footprints only)
```

### New Tool: `city_texture_baker`

Renders city 2D layout to virtual texture tiles at build time.

```cpp
// tools/city_texture_baker/main.cpp
class CityTextureBaker {
public:
    void loadCity(const std::string& cityDir);
    void bakeTiles(const std::string& outputDir, int tileSize = 128);

private:
    // Rasterize vector data to tiles
    void rasterizeStreets(TileBuffer& tile, const glm::ivec2& tileCoord);
    void rasterizeWards(TileBuffer& tile, const glm::ivec2& tileCoord);
    void rasterizeBuildingFootprints(TileBuffer& tile, const glm::ivec2& tileCoord);

    // Material assignment
    uint32_t getStreetMaterialId(StreetType type);
    uint32_t getWardMaterialId(WardType type);
};
```

### Tile Output Format

Match existing VT tile format (128×128 with 4-pixel border = 136×136):

```cpp
struct CityTileData {
    // Material ID per pixel (indexes into terrain material palette)
    uint8_t materialId[136][136];

    // Optional: blend weight for soft edges
    uint8_t blendWeight[136][136];
};
```

### Material Palette Extension

Add city materials to terrain material system:

```cpp
// Extend existing terrain materials
enum TerrainMaterialId {
    // Existing
    GRASS = 0,
    ROCK = 1,
    DIRT = 2,
    SAND = 3,

    // City materials (new)
    COBBLESTONE = 10,
    DIRT_ROAD = 11,
    PLAZA_STONE = 12,
    MARKET_GROUND = 13,
    CASTLE_COURTYARD = 14,
    HARBOUR_WOOD = 15,

    // Ward ground colors
    WARD_RESIDENTIAL = 20,
    WARD_COMMERCIAL = 21,
    WARD_INDUSTRIAL = 22,
};
```

### Street Rasterization

```cpp
void CityTextureBaker::rasterizeStreets(TileBuffer& tile, const glm::ivec2& tileCoord) {
    AABB tileBounds = getTileBounds(tileCoord);

    for (const auto& street : streets) {
        if (!street.intersects(tileBounds)) continue;

        // Clip street segment to tile
        auto clipped = clipToTile(street.path, tileBounds);

        // Rasterize as thick line
        float width = street.isArtery ? 6.0f : 3.0f;
        uint8_t material = getStreetMaterialId(street.type);

        for (const auto& segment : clipped) {
            rasterizeThickLine(tile, segment.start, segment.end, width, material);
        }
    }
}

void rasterizeThickLine(TileBuffer& tile, glm::vec2 p0, glm::vec2 p1,
                        float width, uint8_t material) {
    // Bresenham with perpendicular expansion
    // Or: SDF-based for anti-aliased edges

    glm::vec2 dir = glm::normalize(p1 - p0);
    glm::vec2 perp = glm::vec2(-dir.y, dir.x) * (width * 0.5f);

    // Quad vertices
    std::array<glm::vec2, 4> quad = {
        p0 - perp, p0 + perp, p1 + perp, p1 - perp
    };

    // Rasterize quad to tile pixels
    rasterizeConvexPolygon(tile, quad, material);
}
```

### Ward Ground Rasterization

```cpp
void CityTextureBaker::rasterizeWards(TileBuffer& tile, const glm::ivec2& tileCoord) {
    AABB tileBounds = getTileBounds(tileCoord);

    for (const auto& ward : wards) {
        if (!ward.boundary.intersects(tileBounds)) continue;

        uint8_t material = getWardMaterialId(ward.type);

        // Rasterize ward polygon (may be complex/concave)
        rasterizePolygon(tile, ward.boundary, material);
    }
}
```

### Building Footprint Shadows

For distant LOD, building footprints can appear as darker patches:

```cpp
void CityTextureBaker::rasterizeBuildingFootprints(TileBuffer& tile,
                                                    const glm::ivec2& tileCoord) {
    // Only for coarser mip levels (distant view)
    if (currentMipLevel < 4) return;

    for (const auto& building : buildings) {
        // Darken building footprint area slightly
        // Creates visual density without 3D geometry
        rasterizePolygon(tile, building.footprint, BUILDING_SHADOW_TINT);
    }
}
```

### CMake Integration

```cmake
# tools/city_texture_baker/CMakeLists.txt
add_executable(city_texture_baker
    main.cpp
    CityTextureBaker.cpp
    TileRasterizer.cpp
)
target_link_libraries(city_texture_baker PRIVATE nlohmann_json stb_image_write)

# Build pipeline integration
add_custom_command(
    OUTPUT ${GENERATED_DIR}/city_tiles/tile_manifest.json
    COMMAND city_texture_baker
        --city-dir ${GENERATED_DIR}/city
        --output-dir ${GENERATED_DIR}/city_tiles
        --tile-size 128
        --world-size 65536
    DEPENDS city_texture_baker
            ${GENERATED_DIR}/city/city_streets.geojson
            ${GENERATED_DIR}/city/city_wards.geojson
    COMMENT "Baking city to virtual texture tiles"
)
```

### VT Tile Merging

City tiles need to merge with terrain tiles:

```cpp
// Option A: Layer compositing at bake time
void mergeCityWithTerrain(const std::string& terrainTilePath,
                          const std::string& cityTilePath,
                          const std::string& outputPath) {
    auto terrainTile = loadTile(terrainTilePath);
    auto cityTile = loadTile(cityTilePath);

    // City overwrites terrain where city material is non-zero
    for (int y = 0; y < TILE_SIZE; y++) {
        for (int x = 0; x < TILE_SIZE; x++) {
            if (cityTile.materialId[y][x] != 0) {
                terrainTile.materialId[y][x] = cityTile.materialId[y][x];
            }
        }
    }

    saveTile(outputPath, terrainTile);
}

// Option B: Runtime blending in shader (more flexible)
// terrain.frag: sample both terrain VT and city VT, blend based on city alpha
```

### Shader Modification (Option B)

```glsl
// terrain.frag - add city overlay sampling
uniform sampler2D cityMaterialTexture;  // City material ID texture
uniform sampler2D cityBlendTexture;     // City blend weights

vec4 sampleTerrainWithCity(vec2 worldPos) {
    vec4 terrainColor = sampleVirtualTextureAuto(worldPos / VT_WORLD_SIZE);

    // Sample city layer
    vec2 cityUV = (worldPos - cityOrigin) / citySize;
    if (cityUV.x >= 0.0 && cityUV.x <= 1.0 && cityUV.y >= 0.0 && cityUV.y <= 1.0) {
        float cityMaterial = texture(cityMaterialTexture, cityUV).r;
        float cityBlend = texture(cityBlendTexture, cityUV).r;

        if (cityBlend > 0.0) {
            vec4 cityColor = getMaterialColor(uint(cityMaterial * 255.0));
            terrainColor = mix(terrainColor, cityColor, cityBlend);
        }
    }

    return terrainColor;
}
```

### Testing Phase 2

1. Generate city GeoJSON (Phase 1)
2. Run city_texture_baker
3. Verify tiles in image viewer (should see street patterns)
4. Load in engine, fly over city area
5. Check streets visible at all distances
6. Check ward colors differentiate areas

**Expected Result**: City layout visible on terrain as painted streets and colored ward areas, before any 3D buildings are added.

---

## Phase 3: Partial/Progressive Generation

### Goal
Enable generation at different levels of detail for LOD and streaming.

### Generation Levels

| Level | Contents | Use Case |
|-------|----------|----------|
| 0 | Ward boundaries only | World map, far LOD |
| 1 | + Street network, wall outline | Medium distance |
| 2 | + Block outlines | Approaching city |
| 3 | + Building footprints (no subdivision) | Near city |
| 4 | + Full building detail (LIRA rectangles) | In city |
| 5 | + Architectural features (doors, windows) | Close-up |

### API Changes

```cpp
class City {
public:
    void build(int maxLevel = 5);  // Partial build to specified level
    int getCurrentLevel() const;
    void continueToLevel(int level);  // Resume from current level

    // Level-specific accessors
    const std::vector<WardBoundary>& getWardBoundaries() const;  // Level 0+
    const std::vector<StreetSegment>& getStreets() const;        // Level 1+
    const std::vector<BlockOutline>& getBlocks() const;          // Level 2+
    const std::vector<BuildingFootprint>& getBuildings() const;  // Level 3+
    const std::vector<BuildingDetail>& getDetailedBuildings() const; // Level 4+
};
```

### Implementation

1. Refactor `City::build()` to checkpoint after each major phase
2. Store intermediate state for resumption
3. Add level parameter to GeoJSON export
4. Each level file is self-contained (no dependencies on higher levels)

---

## Phase 3: 3D Building Generation

### Goal
Convert 2D building footprints to 3D meshes with procedural architectural detail.

### Building Components

```
BuildingMesh
├── Foundation      (slight inset, stone material)
├── Walls           (per-floor, material varies by ward)
├── Roof            (gabled, hipped, flat based on footprint shape)
├── Doors           (front door required, back door optional)
├── Windows         (procedural placement on non-door walls)
├── Chimneys        (optional, residential buildings)
└── Decorations     (signs for shops, crosses for churches)
```

### Door Placement Algorithm

```cpp
struct DoorPlacement {
    glm::vec2 position;      // 2D position on footprint
    glm::vec2 direction;     // Outward normal
    DoorType type;           // FRONT, BACK, SIDE
    float width;
};

std::vector<DoorPlacement> placeDoors(const BuildingFootprint& footprint) {
    std::vector<DoorPlacement> doors;

    // Front door: center of frontage edge (faces street)
    Edge frontage = footprint.frontageEdge;
    doors.push_back({
        .position = frontage.midpoint(),
        .direction = frontage.outwardNormal(),
        .type = DoorType::FRONT,
        .width = 1.2f
    });

    // Back door: opposite side if building depth > threshold
    if (footprint.depth > 8.0f) {
        Edge backEdge = findOppositeEdge(footprint, frontage);
        if (!backEdge.touchesCourtyardOrNeighbor()) {
            doors.push_back({
                .position = backEdge.midpoint(),
                .direction = backEdge.outwardNormal(),
                .type = DoorType::BACK,
                .width = 1.0f
            });
        }
    }

    return doors;
}
```

### Window Placement

```cpp
struct WindowPlacement {
    glm::vec2 position;
    int floor;
    WindowType type;  // SMALL, LARGE, ARCHED, SHUTTERED
};

std::vector<WindowPlacement> placeWindows(
    const BuildingFootprint& footprint,
    int numFloors,
    const std::vector<DoorPlacement>& doors
) {
    std::vector<WindowPlacement> windows;

    for (const Edge& edge : footprint.edges) {
        if (edge.length < 3.0f) continue;  // Too short for windows

        // Skip door zones
        float availableLength = edge.length;
        for (const auto& door : doors) {
            if (edge.contains(door.position)) {
                availableLength -= door.width + 1.0f;  // Buffer
            }
        }

        // Place windows with spacing
        int windowCount = static_cast<int>(availableLength / 2.5f);
        float spacing = availableLength / (windowCount + 1);

        for (int floor = 0; floor < numFloors; floor++) {
            for (int w = 0; w < windowCount; w++) {
                windows.push_back({
                    .position = edge.pointAt(spacing * (w + 1)),
                    .floor = floor,
                    .type = selectWindowType(footprint.wardType, floor)
                });
            }
        }
    }

    return windows;
}
```

### Roof Generation

```cpp
enum class RoofStyle { GABLED, HIPPED, FLAT, MANSARD };

RoofStyle selectRoofStyle(const BuildingFootprint& footprint) {
    float aspectRatio = footprint.boundingBox.width / footprint.boundingBox.height;

    if (footprint.wardType == WardType::Castle) return RoofStyle::FLAT;
    if (footprint.vertices.size() > 6) return RoofStyle::HIPPED;
    if (aspectRatio > 2.0f) return RoofStyle::GABLED;
    return RoofStyle::HIPPED;
}

Mesh generateRoof(const BuildingFootprint& footprint, float baseHeight, RoofStyle style) {
    switch (style) {
        case RoofStyle::GABLED:
            return generateGabledRoof(footprint, baseHeight);
        case RoofStyle::HIPPED:
            return generateHippedRoof(footprint, baseHeight);
        case RoofStyle::FLAT:
            return generateFlatRoof(footprint, baseHeight);
        case RoofStyle::MANSARD:
            return generateMansardRoof(footprint, baseHeight);
    }
}
```

### Implementation Files

```
src/city/
├── CityLoader.h/cpp           # Loads GeoJSON city data
├── BuildingGenerator.h/cpp    # 2D → 3D building conversion
├── RoofGenerator.h/cpp        # Procedural roof meshes
├── WallGenerator.h/cpp        # Procedural wall meshes with openings
├── DoorPlacer.h/cpp           # Door placement algorithm
├── WindowPlacer.h/cpp         # Window placement algorithm
├── BuildingDecorator.h/cpp    # Signs, chimneys, details
└── CityMaterialSet.h/cpp      # Ward-based material assignment
```

---

## Phase 4: Street and Infrastructure

### Goal
Generate 3D geometry for streets, walls, and water features.

### Street Generation

```cpp
struct StreetMesh {
    Mesh surface;          // Cobblestone/dirt road
    Mesh gutters;          // Edge drainage channels
    Mesh curbs;            // Raised edges (if paved)
};

StreetMesh generateStreet(const StreetSegment& segment, float terrainHeight) {
    float width = segment.isArtery ? 6.0f : 3.0f;

    // Extrude path into quad strip
    // Sample terrain height along path
    // Add UV coordinates for tiling cobblestone texture
    // Generate curb geometry for urban streets
}
```

### Wall Generation

```cpp
struct WallMesh {
    Mesh wallSegments;     // Main wall body
    Mesh battlements;      // Crenellations on top
    Mesh towers;           // Cylindrical towers at corners
    Mesh gates;            // Arched gate openings
    Mesh walkway;          // Top walkway for guards
};

WallMesh generateCityWall(const CurtainWall& wall, float terrainHeightFunc) {
    // Follow wall path, sampling terrain height
    // Wall height: 8-12 units above terrain
    // Tower radius: 1.9-2.5 units (from MFCG constants)
    // Gate width: 4-6 units
    // Add battlements every 1.5 units
}
```

### Bridge Generation

```cpp
Mesh generateBridge(const Bridge& bridge, float waterLevel) {
    // Arch bridge for canals
    // Supports on either bank
    // Deck surface with railings
    // UV for stone texture
}
```

---

## Phase 5: Terrain Integration

### Goal
Place city on terrain with proper height adaptation.

### Height Sampling

```cpp
class CityPlacer {
public:
    void placeOnTerrain(CityData& city, TerrainSystem& terrain) {
        // Find base height (average of city center area)
        float baseHeight = terrain.getHeightAt(city.center.x, city.center.z);

        // For each building:
        for (auto& building : city.buildings) {
            // Sample height at footprint corners
            float minHeight = FLT_MAX;
            for (const auto& corner : building.footprint) {
                float h = terrain.getHeightAt(corner.x, corner.z);
                minHeight = std::min(minHeight, h);
            }

            // Set building foundation height
            building.baseHeight = minHeight;

            // Add foundation if terrain varies significantly
            float heightVariance = maxHeight - minHeight;
            if (heightVariance > 0.5f) {
                building.needsFoundation = true;
                building.foundationHeight = heightVariance + 0.2f;
            }
        }

        // Streets follow terrain with slight smoothing
        for (auto& street : city.streets) {
            for (auto& point : street.path) {
                point.y = terrain.getHeightAt(point.x, point.z) + 0.1f;
            }
            smoothStreetPath(street);  // Avoid jarring height changes
        }
    }
};
```

### Terrain Flattening (Optional)

For cities that need flat ground:

```cpp
void flattenTerrainForCity(TerrainSystem& terrain, const CityData& city) {
    // Create height modification mask
    // Blend city area to average height
    // Smooth edges to avoid harsh transitions
    // Update terrain heightmap
}
```

---

## Phase 6: LOD System

### Goal
Render cities efficiently at varying distances.

### LOD Levels

| Distance | Representation | Vertex Count |
|----------|---------------|--------------|
| > 2000m | Billboard/impostor | 4 |
| 1000-2000m | Ward blocks (solid colors) | ~100 |
| 500-1000m | Building boxes (no detail) | ~2000 |
| 200-500m | Simple buildings (flat roofs) | ~10000 |
| 50-200m | Detailed buildings (roofs, doors) | ~50000 |
| < 50m | Full detail (windows, decorations) | ~200000 |

### Implementation

```cpp
class CityLODManager {
public:
    struct LODLevel {
        float maxDistance;
        std::vector<Renderable> renderables;
        bool loaded = false;
    };

    void update(const glm::vec3& cameraPos) {
        float distance = glm::distance(cameraPos, cityCenter);

        int targetLevel = calculateLODLevel(distance);

        // Async load higher detail if approaching
        if (targetLevel > currentLevel) {
            loadLevelAsync(targetLevel);
        }

        // Unload distant detail
        if (targetLevel < currentLevel - 1) {
            unloadLevel(currentLevel);
        }
    }

private:
    std::array<LODLevel, 6> lodLevels;
    int currentLevel = 0;
};
```

### Mesh Merging for Performance

```cpp
// Merge all buildings in a ward into single draw call
Mesh mergeWardBuildings(const std::vector<BuildingMesh>& buildings) {
    MeshBuilder builder;
    for (const auto& building : buildings) {
        builder.append(building.mesh, building.transform);
    }
    return builder.build();
}
```

---

## Phase 7: Material System

### Goal
Create appropriate materials for medieval city elements.

### Material Categories

```cpp
enum class CityMaterial {
    // Walls
    STONE_WALL,
    TIMBER_FRAME,
    PLASTER_WHITE,
    PLASTER_COLORED,
    BRICK,

    // Roofs
    THATCH,
    CLAY_TILES,
    SLATE,
    WOODEN_SHINGLES,

    // Ground
    COBBLESTONE,
    DIRT_ROAD,
    GRASS_YARD,

    // Details
    WOODEN_DOOR,
    IRON_DOOR,
    GLASS_WINDOW,
    SHUTTERS,
    SIGN_WOOD
};
```

### Ward-Based Material Selection

```cpp
MaterialSet selectMaterials(WardType ward, int buildingClass) {
    switch (ward) {
        case WardType::Castle:
            return { STONE_WALL, SLATE, IRON_DOOR };
        case WardType::Cathedral:
            return { STONE_WALL, SLATE, WOODEN_DOOR };
        case WardType::Market:
            return { TIMBER_FRAME, CLAY_TILES, WOODEN_DOOR };
        case WardType::Alleys:
            return { PLASTER_WHITE, THATCH, WOODEN_DOOR };
        case WardType::Harbour:
            return { TIMBER_FRAME, WOODEN_SHINGLES, WOODEN_DOOR };
        default:
            return { PLASTER_WHITE, THATCH, WOODEN_DOOR };
    }
}
```

### Texture Sources

Per CLAUDE.md, use opengameart.org for placeholder textures:
- Wall textures: Stone, timber, plaster variations
- Roof textures: Thatch, tiles, slate
- Ground textures: Cobblestone, dirt
- Detail textures: Door, window, sign atlases

---

## Phase 8: Runtime Integration

### Goal
Integrate city rendering into the engine's render pipeline.

### New System: CitySystem

```cpp
class CitySystem {
public:
    void init(InitContext& ctx, const CitySystemInitInfo& info);
    void loadCity(const std::string& cityPath);

    void update(const FrameData& frameData);
    void recordCommands(vk::CommandBuffer cmd, const RenderContext& ctx);

    // Terrain integration
    void setTerrainSystem(TerrainSystem* terrain);

private:
    std::unique_ptr<CityLoader> loader;
    std::unique_ptr<BuildingGenerator> buildingGen;
    std::unique_ptr<CityLODManager> lodManager;

    std::vector<Renderable> cityRenderables;
    MaterialRegistry cityMaterials;
};
```

### Integration Points

1. **RendererSystems**: Add CitySystem to geometry tier
2. **HDRStage Slot 1**: Render city alongside scene objects
3. **ShadowStage**: City casts shadows (merged meshes for performance)
4. **SceneManager**: Track city objects for collision/interaction

---

## Phase 9: Build Pipeline Integration

### Goal
Generate city data at build time.

### CMake Integration

```cmake
# tools/town_generator/CMakeLists.txt
add_executable(town_generator ...)

# Generate city data at build time
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/generated/city/city_buildings.geojson
    COMMAND town_generator
        --seed ${CITY_SEED}
        --output-dir ${CMAKE_BINARY_DIR}/generated/city
        --format geojson
        --level 5
    DEPENDS town_generator
    COMMENT "Generating city data"
)

add_custom_target(generate_city
    DEPENDS ${CMAKE_BINARY_DIR}/generated/city/city_buildings.geojson
)
```

### Generated File Handling

```cpp
// At runtime, load pre-generated city data
CitySystem::loadCity("generated/city/");
```

---

## Implementation Order

### Milestone 1: Data Export
1. Add GeoJSONWriter to town_generator
2. Export building footprints with frontage edges
3. Export streets, walls, water features
4. Export ward boundaries

**Test**: Visualize exported GeoJSON in QGIS or geojson.io

### Milestone 2: Virtual Texture Streets (Priority)
1. Create `city_texture_baker` tool
2. Implement street line rasterization to tiles
3. Implement ward polygon rasterization
4. Add city materials to terrain material palette
5. Merge city tiles with terrain VT (or add shader overlay)
6. Integrate into CMake build pipeline

**Test**: Fly over city area in engine - streets and ward colors visible on terrain at all distances

### Milestone 3: Building Footprint Shadows
1. Add building footprint rasterization to city_texture_baker
2. Darken building areas slightly in VT
3. Test visual density from distance

**Test**: City area looks "built up" from far away without 3D geometry

### Milestone 4: Basic 3D Buildings
1. Create CityLoader to parse GeoJSON
2. Create BuildingGenerator with simple extrusion
3. Generate flat-roofed box buildings
4. Render in engine with single material
5. Sample terrain height for placement

**Test**: See white box buildings sitting on textured streets

### Milestone 5: Architectural Detail
1. Add door placement algorithm (frontage edge detection)
2. Add window placement algorithm
3. Add procedural roof generation (gabled, hipped, flat)
4. Apply ward-based materials

**Test**: Buildings have doors facing streets, varied roofs

### Milestone 6: Walls & Infrastructure
1. Generate wall meshes with towers
2. Generate gate openings
3. Generate bridge meshes
4. Add 3D street curbs/gutters (optional detail)

**Test**: Complete walled city with gates and bridges

### Milestone 7: LOD and Performance
1. Implement building LOD levels
2. Add mesh merging per ward
3. Add async LOD streaming
4. Optimize draw calls
5. Tune VT/3D transition distance

**Test**: Maintain 60fps approaching large city, smooth VT→3D transition

### Milestone 8: Polish
1. Add chimneys, signs, decorations
2. Add point lights (lanterns, torches) via LightManager
3. Add ambient props (laundry, carts, market stalls)
4. Suppress grass/vegetation in city bounds

---

## Testing Approach

### Visual Testing
- Load city in engine, fly around to inspect
- Check door placement faces streets
- Check roof variety across wards
- Check material assignment by ward
- Check terrain conformance

### Performance Testing
- Profile draw calls at various distances
- Measure vertex counts per LOD level
- Test LOD transitions for popping
- Verify async loading doesn't stall

### Validation Testing
- Compare GeoJSON export to SVG output
- Verify all buildings have front doors
- Verify no floating buildings
- Verify no building intersections

---

## Open Questions

1. **Interior spaces**: Should buildings have interior geometry for future walkthroughs?
2. **Population**: Add NPC spawn points based on building type?
3. **Destruction**: Support partial building damage/destruction?
4. **Seasonal**: Different appearances (snow-covered roofs, etc.)?
5. **Growth**: Dynamic city expansion over time?

---

## Dependencies

- **Existing Systems**:
  - `VirtualTextureSystem` - Terrain megatexture streaming
  - `RoadNetworkLoader` - GeoJSON road loading (already exists, unused)
  - `TerrainSystem` - Height queries for building placement
  - `SceneManager` - Renderable management
  - `MaterialRegistry` - Material/texture management

- **New Tool Dependencies** (city_texture_baker):
  - nlohmann/json - GeoJSON parsing (already in project)
  - stb_image_write - PNG tile output (already in project)
  - glm - Vector math (already in project)

- **New Textures** (from opengameart.org):
  - Cobblestone, plaza stone, dirt road (street materials)
  - Wall textures: Stone, timber frame, plaster
  - Roof textures: Thatch, clay tiles, slate
  - Detail textures: Door, window atlases

---

## File Structure Summary

```
tools/town_generator/
├── src/export/
│   └── GeoJSONWriter.cpp         # New: GeoJSON export

tools/city_texture_baker/         # New: VT tile generator
├── CMakeLists.txt
├── main.cpp
├── CityTextureBaker.h/cpp        # Main orchestrator
├── TileRasterizer.h/cpp          # Vector→raster conversion
├── StreetRasterizer.h/cpp        # Street line rendering
├── PolygonRasterizer.h/cpp       # Ward/building polygon fill
└── CityMaterialPalette.h/cpp     # Material ID assignments

src/city/
├── CitySystem.h/cpp              # Main rendering system
├── CityLoader.h/cpp              # GeoJSON loading
├── BuildingGenerator.h/cpp       # 2D → 3D conversion
├── RoofGenerator.h/cpp           # Procedural roofs
├── WallGenerator.h/cpp           # Building walls with openings
├── DoorPlacer.h/cpp              # Door placement algorithm
├── WindowPlacer.h/cpp            # Window placement algorithm
├── CityWallGenerator.h/cpp       # Fortification meshes
├── CityLODManager.h/cpp          # LOD management
└── CityMaterialSet.h/cpp         # Ward-based material assignment

generated/city/                    # City generator output
├── city_metadata.json
├── city_buildings.geojson
├── city_streets.geojson
├── city_walls.geojson
├── city_water.geojson
└── city_wards.geojson

generated/city_tiles/              # VT tile output
├── tile_manifest.json            # Tile index
├── mip0/                         # Full resolution tiles
│   ├── tile_x_y.png
│   └── ...
├── mip1/                         # Half resolution
├── mip2/                         # Quarter resolution
└── ...
```
