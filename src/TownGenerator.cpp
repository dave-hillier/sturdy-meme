#include "TownGenerator.h"
#include <algorithm>
#include <cmath>

TownGenerator::TownGenerator() {
    moduleLibrary.init();
}

float TownGenerator::hash(const glm::vec2& p) const {
    glm::vec2 offset(static_cast<float>(config.seed), static_cast<float>(config.seed * 7));
    return glm::fract(std::sin(glm::dot(p + offset, glm::vec2(127.1f, 311.7f))) * 43758.5453f);
}

glm::vec2 TownGenerator::hash2(const glm::vec2& p) const {
    return glm::vec2(
        hash(p),
        hash(p + glm::vec2(47.0f, 13.0f))
    );
}

void TownGenerator::generate(const TownConfig& cfg, TerrainHeightFunc hFunc) {
    config = cfg;
    heightFunc = hFunc;

    buildings.clear();
    roads.clear();
    zones.clear();
    placedBuildingBounds.clear();

    generateVoronoiLayout();
    assignZones();
    generateRoads();
    placeBuildings();
}

void TownGenerator::generateVoronoiLayout() {
    glm::vec2 boundsMin = config.center - glm::vec2(config.radius);
    glm::vec2 boundsMax = config.center + glm::vec2(config.radius);

    voronoi.generate(config.numCells, boundsMin, boundsMax, config.seed);
    voronoi.relax(config.relaxIterations);
}

void TownGenerator::assignZones() {
    const auto& cells = voronoi.getCells();
    zones.resize(cells.size());

    // Find the cell closest to town center
    int centerCell = voronoi.findCellContaining(config.center);

    for (size_t i = 0; i < cells.size(); ++i) {
        const VoronoiCell& cell = cells[i];
        ZoneAssignment& zone = zones[i];

        // Distance from town center determines zone type
        float distFromCenter = glm::length(cell.site - config.center);
        float normalizedDist = distFromCenter / config.radius;

        // Calculate average terrain suitability for this cell
        float avgSlope = 0.0f;
        int sampleCount = 0;
        for (const auto& v : cell.vertices) {
            avgSlope += getTerrainSlope(v);
            sampleCount++;
        }
        avgSlope += getTerrainSlope(cell.site);
        sampleCount++;
        avgSlope /= static_cast<float>(sampleCount);

        zone.suitability = 1.0f - glm::clamp(avgSlope / config.maxBuildingSlope, 0.0f, 1.0f);
        zone.hasCentralBuilding = false;

        // Assign zone type based on distance and suitability
        if (static_cast<int>(i) == centerCell) {
            zone.type = ZoneType::TownCenter;
            zone.hasCentralBuilding = true;
        } else if (normalizedDist < 0.25f && zone.suitability > 0.5f) {
            // Inner ring: commercial/residential mix
            float r = hash(cell.site);
            zone.type = r < 0.4f ? ZoneType::Commercial : ZoneType::Residential;
        } else if (normalizedDist < 0.5f && zone.suitability > 0.3f) {
            // Middle ring: mostly residential
            float r = hash(cell.site + glm::vec2(100.0f));
            zone.type = r < 0.2f ? ZoneType::Commercial : ZoneType::Residential;
        } else if (normalizedDist < 0.75f && zone.suitability > 0.4f) {
            // Outer ring: residential and agricultural
            float r = hash(cell.site + glm::vec2(200.0f));
            zone.type = r < 0.5f ? ZoneType::Agricultural : ZoneType::Residential;
        } else if (zone.suitability > 0.5f) {
            // Far outer: agricultural
            zone.type = ZoneType::Agricultural;
        } else {
            // Unsuitable terrain: wilderness
            zone.type = ZoneType::Wilderness;
        }

        // Boundary cells tend toward wilderness/agricultural
        if (cell.isBoundary && zone.type == ZoneType::Residential) {
            zone.type = ZoneType::Agricultural;
        }
    }

    // Mark special buildings in some cells
    for (size_t i = 0; i < cells.size(); ++i) {
        if (zones[i].type == ZoneType::Residential && zones[i].suitability > 0.7f) {
            float r = hash(cells[i].site + glm::vec2(500.0f));
            if (r < 0.1f) {
                zones[i].hasCentralBuilding = true; // Well, small chapel, etc.
            }
        }
    }
}

void TownGenerator::generateRoads() {
    const auto& edges = voronoi.getEdges();
    const auto& cells = voronoi.getCells();

    for (const auto& edge : edges) {
        // Skip very short edges
        float edgeLength = glm::length(edge.end - edge.start);
        if (edgeLength < 1.0f) continue;

        // Determine if this should be a road based on adjacent zones
        bool leftIsBuilt = false;
        bool rightIsBuilt = false;

        if (edge.leftCell >= 0 && static_cast<size_t>(edge.leftCell) < zones.size()) {
            ZoneType lt = zones[edge.leftCell].type;
            leftIsBuilt = (lt == ZoneType::TownCenter || lt == ZoneType::Residential ||
                          lt == ZoneType::Commercial);
        }
        if (edge.rightCell >= 0 && static_cast<size_t>(edge.rightCell) < zones.size()) {
            ZoneType rt = zones[edge.rightCell].type;
            rightIsBuilt = (rt == ZoneType::TownCenter || rt == ZoneType::Residential ||
                           rt == ZoneType::Commercial);
        }

        // Roads form between built-up areas
        if (leftIsBuilt || rightIsBuilt) {
            RoadSegment road;

            // Sample terrain height along road
            road.start = glm::vec3(edge.start.x, getTerrainHeight(edge.start), edge.start.y);
            road.end = glm::vec3(edge.end.x, getTerrainHeight(edge.end), edge.end.y);

            // Main roads connect to town center
            bool touchesCenter = false;
            if (edge.leftCell >= 0 && zones[edge.leftCell].type == ZoneType::TownCenter) touchesCenter = true;
            if (edge.rightCell >= 0 && zones[edge.rightCell].type == ZoneType::TownCenter) touchesCenter = true;

            road.isMainRoad = touchesCenter || (leftIsBuilt && rightIsBuilt);
            road.width = road.isMainRoad ? config.mainRoadWidth : config.roadWidth;

            roads.push_back(road);
        }
    }
}

void TownGenerator::placeBuildings() {
    const auto& cells = voronoi.getCells();

    for (size_t cellIdx = 0; cellIdx < cells.size(); ++cellIdx) {
        const VoronoiCell& cell = cells[cellIdx];
        const ZoneAssignment& zone = zones[cellIdx];

        // Skip wilderness and road zones
        if (zone.type == ZoneType::Wilderness || zone.type == ZoneType::Road) {
            continue;
        }

        // Determine number of buildings based on zone type and cell area
        int maxBuildings = 0;
        switch (zone.type) {
            case ZoneType::TownCenter:
                maxBuildings = 3 + static_cast<int>(cell.area * 0.01f * config.buildingDensity);
                break;
            case ZoneType::Commercial:
                maxBuildings = 2 + static_cast<int>(cell.area * 0.008f * config.buildingDensity);
                break;
            case ZoneType::Residential:
                maxBuildings = 1 + static_cast<int>(cell.area * 0.006f * config.buildingDensity);
                break;
            case ZoneType::Agricultural:
                maxBuildings = static_cast<int>(cell.area * 0.002f * config.buildingDensity);
                break;
            default:
                break;
        }

        maxBuildings = std::min(maxBuildings, 8); // Cap per cell

        // Place central building if designated
        if (zone.hasCentralBuilding) {
            BuildingPlacement building;
            building.cellIndex = static_cast<uint32_t>(cellIdx);

            glm::vec2 pos = cell.site;
            float terrainH = getTerrainHeight(pos);

            // Select special building type
            if (zone.type == ZoneType::TownCenter) {
                building.type = BuildingType::Well;
            } else {
                float r = hash(pos + glm::vec2(1000.0f));
                building.type = r < 0.5f ? BuildingType::Well : BuildingType::Church;
            }

            building.dimensions = getBuildingDimensions(building.type);
            building.position = glm::vec3(pos.x, terrainH, pos.y);
            building.rotation = hash(pos) * 6.28318f;
            building.scale = 0.9f + hash(pos + glm::vec2(50.0f)) * 0.2f;

            // Generate modular building structure using WFC
            uint32_t buildingSeed = static_cast<uint32_t>(pos.x * 1000 + pos.y * 1000000) ^ config.seed;
            generateModularBuilding(building, buildingSeed);

            buildings.push_back(building);
            placedBuildingBounds.push_back(glm::vec4(pos.x, pos.y,
                                                     building.dimensions.x * 0.5f,
                                                     building.dimensions.z * 0.5f));
        }

        // Place regular buildings
        int placed = 0;
        int attempts = 0;
        const int maxAttempts = maxBuildings * 10;

        while (placed < maxBuildings && attempts < maxAttempts) {
            attempts++;

            // Generate random position within cell
            glm::vec2 jitter = hash2(glm::vec2(static_cast<float>(attempts), static_cast<float>(cellIdx)));
            jitter = jitter * 2.0f - 1.0f;

            // Use cell centroid with jitter, biased toward cell interior
            glm::vec2 pos = cell.site + jitter * (std::sqrt(cell.area) * 0.3f);

            // Check if position is within cell bounds (approximately)
            int containingCell = voronoi.findCellContaining(pos);
            if (containingCell != static_cast<int>(cellIdx)) continue;

            // Check terrain suitability
            float slope = getTerrainSlope(pos);
            if (slope > config.maxBuildingSlope) continue;

            // Check if too close to roads
            if (isOnRoad(pos, config.roadWidth + 1.0f)) continue;

            // Select building type
            float r = hash(pos + glm::vec2(300.0f));
            BuildingType type = selectBuildingType(zone.type, r);
            glm::vec3 dims = getBuildingDimensions(type);

            // Check for overlap with existing buildings
            if (!canPlaceBuilding(pos, glm::vec2(dims.x, dims.z))) continue;

            // Place the building
            BuildingPlacement building;
            building.type = type;
            building.cellIndex = static_cast<uint32_t>(cellIdx);
            building.dimensions = dims;
            building.position = glm::vec3(pos.x, getTerrainHeight(pos), pos.y);
            building.rotation = hash(pos + glm::vec2(400.0f)) * 6.28318f;
            building.scale = 0.85f + hash(pos + glm::vec2(600.0f)) * 0.3f;

            // Generate modular building structure using WFC
            uint32_t buildingSeed = static_cast<uint32_t>(pos.x * 1000 + pos.y * 1000000) ^ config.seed;
            generateModularBuilding(building, buildingSeed);

            buildings.push_back(building);
            placedBuildingBounds.push_back(glm::vec4(pos.x, pos.y, dims.x * 0.5f, dims.z * 0.5f));
            placed++;
        }
    }
}

float TownGenerator::evaluateBuildingSuitability(const glm::vec2& pos) const {
    float slope = getTerrainSlope(pos);
    float distFromCenter = glm::length(pos - config.center);

    float slopeFactor = 1.0f - glm::clamp(slope / config.maxBuildingSlope, 0.0f, 1.0f);
    float distFactor = 1.0f - glm::clamp(distFromCenter / config.radius, 0.0f, 1.0f);

    return slopeFactor * 0.7f + distFactor * 0.3f;
}

float TownGenerator::getTerrainSlope(const glm::vec2& pos) const {
    if (!heightFunc) return 0.0f;

    const float sampleDist = 1.0f;
    float hCenter = heightFunc(pos.x, pos.y);
    float hLeft = heightFunc(pos.x - sampleDist, pos.y);
    float hRight = heightFunc(pos.x + sampleDist, pos.y);
    float hUp = heightFunc(pos.x, pos.y - sampleDist);
    float hDown = heightFunc(pos.x, pos.y + sampleDist);

    float dx = (hRight - hLeft) / (2.0f * sampleDist);
    float dy = (hDown - hUp) / (2.0f * sampleDist);

    return std::sqrt(dx * dx + dy * dy);
}

float TownGenerator::getTerrainHeight(const glm::vec2& pos) const {
    if (!heightFunc) return 0.0f;
    return heightFunc(pos.x, pos.y);
}

bool TownGenerator::canPlaceBuilding(const glm::vec2& pos, const glm::vec2& size) const {
    glm::vec2 halfSize = size * 0.5f + glm::vec2(config.minBuildingSpacing);

    for (const auto& existing : placedBuildingBounds) {
        glm::vec2 existingPos(existing.x, existing.y);
        glm::vec2 existingHalf(existing.z, existing.w);

        // AABB overlap test
        glm::vec2 diff = glm::abs(pos - existingPos);
        glm::vec2 combined = halfSize + existingHalf;

        if (diff.x < combined.x && diff.y < combined.y) {
            return false;
        }
    }
    return true;
}

BuildingType TownGenerator::selectBuildingType(ZoneType zone, float random) const {
    switch (zone) {
        case ZoneType::TownCenter:
            if (random < 0.2f) return BuildingType::Market;
            if (random < 0.4f) return BuildingType::Tavern;
            if (random < 0.6f) return BuildingType::Workshop;
            return BuildingType::MediumHouse;

        case ZoneType::Commercial:
            if (random < 0.3f) return BuildingType::Workshop;
            if (random < 0.5f) return BuildingType::Tavern;
            if (random < 0.7f) return BuildingType::Market;
            return BuildingType::MediumHouse;

        case ZoneType::Residential:
            if (random < 0.7f) return BuildingType::SmallHouse;
            if (random < 0.9f) return BuildingType::MediumHouse;
            return BuildingType::Workshop;

        case ZoneType::Agricultural:
            if (random < 0.4f) return BuildingType::Barn;
            if (random < 0.6f) return BuildingType::SmallHouse;
            if (random < 0.8f) return BuildingType::Windmill;
            return BuildingType::SmallHouse;

        default:
            return BuildingType::SmallHouse;
    }
}

glm::vec3 TownGenerator::getBuildingDimensions(BuildingType type) const {
    // Returns width (X), height (Y), depth (Z) based on grid size and module size
    glm::ivec3 grid = getBuildingGridSize(type);
    float moduleSize = 2.0f;  // MODULE_SIZE from ModuleMeshGenerator
    return glm::vec3(grid) * moduleSize;
}

glm::ivec3 TownGenerator::getBuildingGridSize(BuildingType type) const {
    // Returns grid dimensions in modules (X, Y, Z)
    switch (type) {
        case BuildingType::SmallHouse:   return glm::ivec3(2, 2, 2);   // 4x4x4m
        case BuildingType::MediumHouse:  return glm::ivec3(3, 2, 3);   // 6x4x6m
        case BuildingType::Tavern:       return glm::ivec3(4, 3, 4);   // 8x6x8m
        case BuildingType::Workshop:     return glm::ivec3(3, 2, 3);   // 6x4x6m
        case BuildingType::Church:       return glm::ivec3(4, 4, 5);   // 8x8x10m
        case BuildingType::WatchTower:   return glm::ivec3(2, 5, 2);   // 4x10x4m
        case BuildingType::Well:         return glm::ivec3(1, 1, 1);   // 2x2x2m
        case BuildingType::Market:       return glm::ivec3(2, 2, 2);   // 4x4x4m
        case BuildingType::Barn:         return glm::ivec3(4, 2, 5);   // 8x4x10m
        case BuildingType::Windmill:     return glm::ivec3(2, 4, 2);   // 4x8x4m
        default:                         return glm::ivec3(2, 2, 2);
    }
}

void TownGenerator::generateModularBuilding(BuildingPlacement& building, uint32_t seed) {
    glm::ivec3 gridSize = getBuildingGridSize(building.type);
    building.gridSize = gridSize;

    // Create WFC solver for this building
    BuildingWFC wfc;
    wfc.init(moduleLibrary, gridSize.x, gridSize.y, gridSize.z);

    // Create footprint (all cells in XZ plane)
    std::vector<glm::ivec2> footprint;
    for (int z = 0; z < gridSize.z; ++z) {
        for (int x = 0; x < gridSize.x; ++x) {
            footprint.push_back(glm::ivec2(x, z));
        }
    }
    wfc.setFootprint(footprint);
    wfc.setHeight(1, gridSize.y);

    // Solve WFC
    if (wfc.solve(seed)) {
        // Store the result in the building
        building.moduleGrid.resize(gridSize.x * gridSize.y * gridSize.z);
        for (int z = 0; z < gridSize.z; ++z) {
            for (int y = 0; y < gridSize.y; ++y) {
                for (int x = 0; x < gridSize.x; ++x) {
                    const WFCCell& cell = wfc.getCell(x, y, z);
                    size_t idx = x + y * gridSize.x + z * gridSize.x * gridSize.y;
                    building.moduleGrid[idx] = cell.collapsed ? cell.chosenModule : 0;
                }
            }
        }
    } else {
        // Fallback: fill with simple default modules
        building.moduleGrid.resize(gridSize.x * gridSize.y * gridSize.z);
        for (int z = 0; z < gridSize.z; ++z) {
            for (int y = 0; y < gridSize.y; ++y) {
                for (int x = 0; x < gridSize.x; ++x) {
                    size_t idx = x + y * gridSize.x + z * gridSize.x * gridSize.y;

                    // Simple fallback: foundation at y=0, walls above, roof at top
                    if (y == 0) {
                        // Ground level - foundation
                        bool isCorner = (x == 0 || x == gridSize.x - 1) &&
                                       (z == 0 || z == gridSize.z - 1);
                        bool isEdge = (x == 0 || x == gridSize.x - 1 || z == 0 || z == gridSize.z - 1);

                        if (isCorner) {
                            building.moduleGrid[idx] = static_cast<size_t>(ModuleType::FoundationCorner);
                        } else if (isEdge) {
                            building.moduleGrid[idx] = static_cast<size_t>(ModuleType::FoundationWall);
                        } else {
                            building.moduleGrid[idx] = static_cast<size_t>(ModuleType::FloorPlain);
                        }
                    } else if (y == gridSize.y - 1) {
                        // Top level - roof
                        building.moduleGrid[idx] = static_cast<size_t>(ModuleType::RoofFlat);
                    } else {
                        // Middle levels - walls
                        bool isEdge = (x == 0 || x == gridSize.x - 1 || z == 0 || z == gridSize.z - 1);
                        if (isEdge) {
                            building.moduleGrid[idx] = static_cast<size_t>(ModuleType::WallPlain);
                        } else {
                            building.moduleGrid[idx] = static_cast<size_t>(ModuleType::FloorPlain);
                        }
                    }
                }
            }
        }
    }
}

ZoneType TownGenerator::getZoneAt(const glm::vec2& worldPos) const {
    int cell = voronoi.findCellContaining(worldPos);
    if (cell >= 0 && static_cast<size_t>(cell) < zones.size()) {
        return zones[cell].type;
    }
    return ZoneType::Wilderness;
}

bool TownGenerator::isOnRoad(const glm::vec2& worldPos, float tolerance) const {
    for (const auto& road : roads) {
        glm::vec2 start(road.start.x, road.start.z);
        glm::vec2 end(road.end.x, road.end.z);

        // Distance to line segment
        glm::vec2 ab = end - start;
        glm::vec2 ap = worldPos - start;

        float t = glm::clamp(glm::dot(ap, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
        glm::vec2 closest = start + t * ab;
        float dist = glm::length(worldPos - closest);

        if (dist < tolerance + road.width * 0.5f) {
            return true;
        }
    }
    return false;
}
