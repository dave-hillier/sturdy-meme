#include "GrassTileTracker.h"

GrassTileTracker::GrassTileTracker() {
    // Initialize with default LOD strategy
    lodStrategy_ = createDefaultGrassLODStrategy();
}

void GrassTileTracker::setLODStrategy(std::unique_ptr<IGrassLODStrategy> strategy) {
    if (strategy) {
        lodStrategy_ = std::move(strategy);
    } else {
        lodStrategy_ = createDefaultGrassLODStrategy();
    }
}

GrassTileTracker::UpdateResult GrassTileTracker::update(const glm::vec3& cameraPos,
                                                         uint64_t currentFrame,
                                                         uint32_t framesInFlight) {
    UpdateResult result;
    glm::vec2 cameraXZ(cameraPos.x, cameraPos.z);

    // Build desired tile set across all LOD levels
    std::unordered_set<TileCoord, TileCoordHash> desiredTiles;
    uint32_t numLODs = lodStrategy_->getNumLODLevels();

    for (uint32_t lod = 0; lod < numLODs; ++lod) {
        auto tilesForLod = getDesiredTilesForLod(cameraXZ, lod);
        for (const auto& coord : tilesForLod) {
            // For LOD 1+: Skip tiles covered by higher LOD
            if (lod > 0) {
                float tileSize = lodStrategy_->getTileSize(lod);
                glm::vec2 tileCenter(
                    static_cast<float>(coord.x) * tileSize + tileSize * 0.5f,
                    static_cast<float>(coord.z) * tileSize + tileSize * 0.5f
                );
                if (isCoveredByHigherLod(tileCenter, lod, cameraXZ)) {
                    continue;
                }
            }
            desiredTiles.insert(coord);
        }
    }

    // Determine load requests (desired but not loaded)
    for (const auto& coord : desiredTiles) {
        if (loadedTiles_.find(coord) == loadedTiles_.end()) {
            TileRequest req;
            req.coord = coord;
            req.load = true;
            req.priority = calculateTilePriority(coord, cameraXZ);
            result.loadRequests.push_back(req);
        }
    }

    // Sort load requests by priority (highest first)
    std::sort(result.loadRequests.begin(), result.loadRequests.end(),
        [](const TileRequest& a, const TileRequest& b) {
            return a.priority > b.priority;
        });

    // Determine unload requests (loaded but not desired and safe to unload)
    for (const auto& [coord, info] : loadedTiles_) {
        if (desiredTiles.find(coord) == desiredTiles.end()) {
            // Check distance-based unloading with hysteresis
            float unloadRadius = getUnloadRadiusForLod(coord.lod);
            float unloadRadiusSq = unloadRadius * unloadRadius;

            float tileSize = lodStrategy_->getTileSize(coord.lod);
            glm::vec2 tileCenter(
                static_cast<float>(coord.x) * tileSize + tileSize * 0.5f,
                static_cast<float>(coord.z) * tileSize + tileSize * 0.5f
            );
            float distSq = glm::dot(tileCenter - cameraXZ, tileCenter - cameraXZ);

            // Only unload if beyond unload radius AND safe (not in use by GPU)
            if (distSq > unloadRadiusSq && canUnloadTile(coord, currentFrame, framesInFlight)) {
                TileRequest req;
                req.coord = coord;
                req.load = false;
                req.priority = 0.0f;  // Unload priority not used
                result.unloadRequests.push_back(req);
            }
        }
    }

    // Update active tile set
    activeTileSet_ = desiredTiles;

    // Update tracking for tiles that remain active
    for (const auto& coord : desiredTiles) {
        if (loadedTiles_.find(coord) != loadedTiles_.end()) {
            loadedTiles_[coord].lastUsedFrame = currentFrame;
        }
    }

    // Update camera tile (LOD 0)
    currentCameraTile_ = worldToTileCoordWithStrategy(cameraXZ, 0);

    // Build sorted active tile list for result
    result.activeTiles.reserve(desiredTiles.size());
    for (const auto& coord : desiredTiles) {
        // Only include tiles that are actually loaded
        if (loadedTiles_.find(coord) != loadedTiles_.end()) {
            result.activeTiles.push_back(coord);
        }
    }

    // Sort by LOD (lower = higher detail = render first), then by distance
    std::sort(result.activeTiles.begin(), result.activeTiles.end(),
        [&cameraXZ, this](const TileCoord& a, const TileCoord& b) {
            if (a.lod != b.lod) {
                return a.lod < b.lod;
            }
            // Within same LOD, sort by distance
            float tileSize_a = lodStrategy_->getTileSize(a.lod);
            float tileSize_b = lodStrategy_->getTileSize(b.lod);
            glm::vec2 center_a(static_cast<float>(a.x) * tileSize_a + tileSize_a * 0.5f,
                               static_cast<float>(a.z) * tileSize_a + tileSize_a * 0.5f);
            glm::vec2 center_b(static_cast<float>(b.x) * tileSize_b + tileSize_b * 0.5f,
                               static_cast<float>(b.z) * tileSize_b + tileSize_b * 0.5f);
            return glm::dot(center_a - cameraXZ, center_a - cameraXZ) <
                   glm::dot(center_b - cameraXZ, center_b - cameraXZ);
        });

    return result;
}

std::vector<GrassTileTracker::TileCoord> GrassTileTracker::getActiveTilesAtLod(uint32_t lod) const {
    std::vector<TileCoord> tiles;
    for (const auto& coord : activeTileSet_) {
        if (coord.lod == lod) {
            tiles.push_back(coord);
        }
    }
    return tiles;
}

bool GrassTileTracker::isCoveredByHigherLod(const glm::vec2& worldPos, uint32_t currentLod,
                                             const glm::vec2& cameraXZ) const {
    for (uint32_t higherLod = 0; higherLod < currentLod; ++higherLod) {
        float tileSize = lodStrategy_->getTileSize(higherLod);
        uint32_t tilesPerAxis = lodStrategy_->getTilesPerAxis(higherLod);
        int halfExtent = static_cast<int>(tilesPerAxis) / 2;

        TileCoord cameraTile = worldToTileCoordWithStrategy(cameraXZ, higherLod);
        float minX = static_cast<float>(cameraTile.x - halfExtent) * tileSize;
        float maxX = static_cast<float>(cameraTile.x + halfExtent + 1) * tileSize;
        float minZ = static_cast<float>(cameraTile.z - halfExtent) * tileSize;
        float maxZ = static_cast<float>(cameraTile.z + halfExtent + 1) * tileSize;

        if (worldPos.x >= minX && worldPos.x < maxX &&
            worldPos.y >= minZ && worldPos.y < maxZ) {
            return true;
        }
    }
    return false;
}

std::vector<GrassTileTracker::TileCoord> GrassTileTracker::getDesiredTilesForLod(
    const glm::vec2& cameraXZ, uint32_t lod) const {

    std::vector<TileCoord> tiles;

    uint32_t tilesPerAxis = lodStrategy_->getTilesPerAxis(lod);
    int halfExtent = static_cast<int>(tilesPerAxis) / 2;

    TileCoord cameraTile = worldToTileCoordWithStrategy(cameraXZ, lod);

    for (int dz = -halfExtent; dz <= halfExtent; ++dz) {
        for (int dx = -halfExtent; dx <= halfExtent; ++dx) {
            tiles.push_back({cameraTile.x + dx, cameraTile.z + dz, lod});
        }
    }

    return tiles;
}

float GrassTileTracker::getUnloadRadiusForLod(uint32_t lod) const {
    float tileSize = lodStrategy_->getTileSize(lod);
    uint32_t tilesPerAxis = lodStrategy_->getTilesPerAxis(lod);
    float halfExtent = static_cast<float>(tilesPerAxis) / 2.0f;
    float activeRadius = (halfExtent + 0.5f) * tileSize;
    return activeRadius + lodStrategy_->getTileUnloadMargin();
}

GrassTileTracker::TileCoord GrassTileTracker::worldToTileCoordWithStrategy(
    const glm::vec2& worldPos, uint32_t lod) const {
    float tileSize = lodStrategy_->getTileSize(lod);
    return {
        static_cast<int>(std::floor(worldPos.x / tileSize)),
        static_cast<int>(std::floor(worldPos.y / tileSize)),
        lod
    };
}
