// Tests for TileGridLogic - pure terrain tile grid calculations
// No Vulkan dependencies

#include <doctest/doctest.h>
#include "terrain/TileGridLogic.h"
#include <unordered_set>

using namespace TileGrid;

// ============================================================================
// TileCoord Tests
// ============================================================================

TEST_SUITE("TileCoord") {
    TEST_CASE("default constructor creates zero coord") {
        TileCoord coord;
        CHECK(coord.x == 0);
        CHECK(coord.z == 0);
    }

    TEST_CASE("equality operator") {
        TileCoord a{5, 10};
        TileCoord b{5, 10};
        TileCoord c{5, 11};

        CHECK(a == b);
        CHECK_FALSE(a == c);
        CHECK(a != c);
        CHECK_FALSE(a != b);
    }

    TEST_CASE("hash function works for unordered containers") {
        std::unordered_set<TileCoord, TileCoordHash> coordSet;

        coordSet.insert({0, 0});
        coordSet.insert({0, 0});  // Duplicate
        coordSet.insert({1, 2});

        CHECK(coordSet.size() == 2);
        CHECK(coordSet.count({0, 0}) == 1);
        CHECK(coordSet.count({1, 2}) == 1);
        CHECK(coordSet.count({3, 3}) == 0);
    }
}

// ============================================================================
// worldToTileCoord Tests
// ============================================================================

TEST_SUITE("worldToTileCoord") {
    GridConfig makeConfig(float terrainSize = 16384.0f, uint32_t tilesX = 32, uint32_t tilesZ = 32) {
        GridConfig config;
        config.terrainSize = terrainSize;
        config.tilesX = tilesX;
        config.tilesZ = tilesZ;
        config.numLODLevels = 4;
        return config;
    }

    TEST_CASE("origin maps to tile (0, 0)") {
        auto config = makeConfig();
        TileCoord coord = worldToTileCoord(0.0f, 0.0f, 0, config);
        CHECK(coord.x == 0);
        CHECK(coord.z == 0);
    }

    TEST_CASE("position within first tile maps correctly") {
        auto config = makeConfig(16384.0f, 32, 32);
        // Tile size at LOD0: 16384/32 = 512

        TileCoord coord = worldToTileCoord(100.0f, 200.0f, 0, config);
        CHECK(coord.x == 0);
        CHECK(coord.z == 0);
    }

    TEST_CASE("position in second tile") {
        auto config = makeConfig(16384.0f, 32, 32);
        // Tile size at LOD0: 512

        TileCoord coord = worldToTileCoord(600.0f, 100.0f, 0, config);
        CHECK(coord.x == 1);  // 600 / 512 = 1.17 -> tile 1
        CHECK(coord.z == 0);
    }

    TEST_CASE("position near terrain edge") {
        auto config = makeConfig(16384.0f, 32, 32);

        TileCoord coord = worldToTileCoord(16000.0f, 16000.0f, 0, config);
        CHECK(coord.x == 31);  // 16000 / 512 = 31.25 -> tile 31 (clamped)
        CHECK(coord.z == 31);
    }

    TEST_CASE("negative positions clamp to zero") {
        auto config = makeConfig();

        TileCoord coord = worldToTileCoord(-100.0f, -100.0f, 0, config);
        CHECK(coord.x == 0);
        CHECK(coord.z == 0);
    }

    TEST_CASE("positions beyond terrain clamp to max") {
        auto config = makeConfig(16384.0f, 32, 32);

        TileCoord coord = worldToTileCoord(20000.0f, 20000.0f, 0, config);
        CHECK(coord.x == 31);  // Max tile index
        CHECK(coord.z == 31);
    }

    TEST_CASE("LOD affects tile size") {
        auto config = makeConfig(16384.0f, 32, 32);
        // LOD0: 32 tiles, tile size 512
        // LOD1: 16 tiles, tile size 1024
        // LOD2: 8 tiles, tile size 2048
        // LOD3: 4 tiles, tile size 4096

        float testX = 1500.0f;
        float testZ = 1500.0f;

        TileCoord lod0 = worldToTileCoord(testX, testZ, 0, config);
        TileCoord lod1 = worldToTileCoord(testX, testZ, 1, config);
        TileCoord lod2 = worldToTileCoord(testX, testZ, 2, config);
        TileCoord lod3 = worldToTileCoord(testX, testZ, 3, config);

        CHECK(lod0.x == 2);   // 1500 / 512 = 2.93 -> 2
        CHECK(lod1.x == 1);   // 1500 / 1024 = 1.46 -> 1
        CHECK(lod2.x == 0);   // 1500 / 2048 = 0.73 -> 0
        CHECK(lod3.x == 0);   // 1500 / 4096 = 0.36 -> 0
    }
}

// ============================================================================
// getTileWorldBounds Tests
// ============================================================================

TEST_SUITE("getTileWorldBounds") {
    TEST_CASE("first tile at LOD0") {
        GridConfig config;
        config.terrainSize = 16384.0f;
        config.tilesX = 32;
        config.tilesZ = 32;

        float minX, minZ, maxX, maxZ;
        getTileWorldBounds({0, 0}, 0, config, minX, minZ, maxX, maxZ);

        CHECK(minX == doctest::Approx(0.0f));
        CHECK(minZ == doctest::Approx(0.0f));
        CHECK(maxX == doctest::Approx(512.0f));  // 16384/32
        CHECK(maxZ == doctest::Approx(512.0f));
    }

    TEST_CASE("middle tile") {
        GridConfig config;
        config.terrainSize = 16384.0f;
        config.tilesX = 32;
        config.tilesZ = 32;

        float minX, minZ, maxX, maxZ;
        getTileWorldBounds({10, 15}, 0, config, minX, minZ, maxX, maxZ);

        CHECK(minX == doctest::Approx(10 * 512.0f));
        CHECK(minZ == doctest::Approx(15 * 512.0f));
        CHECK(maxX == doctest::Approx(11 * 512.0f));
        CHECK(maxZ == doctest::Approx(16 * 512.0f));
    }

    TEST_CASE("tile at higher LOD is larger") {
        GridConfig config;
        config.terrainSize = 16384.0f;
        config.tilesX = 32;
        config.tilesZ = 32;

        float minX0, minZ0, maxX0, maxZ0;
        float minX1, minZ1, maxX1, maxZ1;

        getTileWorldBounds({0, 0}, 0, config, minX0, minZ0, maxX0, maxZ0);
        getTileWorldBounds({0, 0}, 1, config, minX1, minZ1, maxX1, maxZ1);

        float size0 = maxX0 - minX0;
        float size1 = maxX1 - minX1;

        CHECK(size1 == doctest::Approx(size0 * 2));  // LOD1 tiles are twice as large
    }
}

// ============================================================================
// getLODForDistance Tests
// ============================================================================

TEST_SUITE("getLODForDistance") {
    TEST_CASE("close distance returns LOD0") {
        LODThresholds thresholds;

        CHECK(getLODForDistance(0.0f, thresholds) == 0);
        CHECK(getLODForDistance(500.0f, thresholds) == 0);
        CHECK(getLODForDistance(999.0f, thresholds) == 0);
    }

    TEST_CASE("medium distance returns LOD1") {
        LODThresholds thresholds;

        CHECK(getLODForDistance(1000.0f, thresholds) == 1);
        CHECK(getLODForDistance(1500.0f, thresholds) == 1);
        CHECK(getLODForDistance(1999.0f, thresholds) == 1);
    }

    TEST_CASE("far distance returns LOD2") {
        LODThresholds thresholds;

        CHECK(getLODForDistance(2000.0f, thresholds) == 2);
        CHECK(getLODForDistance(3000.0f, thresholds) == 2);
        CHECK(getLODForDistance(3999.0f, thresholds) == 2);
    }

    TEST_CASE("very far distance returns LOD3") {
        LODThresholds thresholds;

        CHECK(getLODForDistance(4000.0f, thresholds) == 3);
        CHECK(getLODForDistance(6000.0f, thresholds) == 3);
        CHECK(getLODForDistance(7999.0f, thresholds) == 3);
    }

    TEST_CASE("beyond max distance still returns highest LOD") {
        LODThresholds thresholds;

        CHECK(getLODForDistance(10000.0f, thresholds) == 3);
        CHECK(getLODForDistance(100000.0f, thresholds) == 3);
    }

    TEST_CASE("custom thresholds are respected") {
        LODThresholds thresholds;
        thresholds.lod0Max = 100.0f;
        thresholds.lod1Max = 200.0f;
        thresholds.lod2Max = 300.0f;
        thresholds.lod3Max = 400.0f;

        CHECK(getLODForDistance(50.0f, thresholds) == 0);
        CHECK(getLODForDistance(150.0f, thresholds) == 1);
        CHECK(getLODForDistance(250.0f, thresholds) == 2);
        CHECK(getLODForDistance(350.0f, thresholds) == 3);
    }
}

// ============================================================================
// makeTileKey / unpackTileKey Tests
// ============================================================================

TEST_SUITE("makeTileKey") {
    TEST_CASE("round-trip preserves values") {
        TileCoord original{100, 200};
        uint32_t lod = 2;

        uint64_t key = makeTileKey(original, lod);

        TileCoord unpacked;
        uint32_t unpackedLod;
        unpackTileKey(key, unpacked, unpackedLod);

        CHECK(unpacked.x == 100);
        CHECK(unpacked.z == 200);
        CHECK(unpackedLod == 2);
    }

    TEST_CASE("different tiles produce different keys") {
        uint64_t key1 = makeTileKey({0, 0}, 0);
        uint64_t key2 = makeTileKey({1, 0}, 0);
        uint64_t key3 = makeTileKey({0, 1}, 0);
        uint64_t key4 = makeTileKey({0, 0}, 1);

        CHECK(key1 != key2);
        CHECK(key1 != key3);
        CHECK(key1 != key4);
        CHECK(key2 != key3);
        CHECK(key2 != key4);
        CHECK(key3 != key4);
    }

    TEST_CASE("handles large coordinate values") {
        TileCoord large{1000000, 2000000};
        uint32_t lod = 5;

        uint64_t key = makeTileKey(large, lod);

        TileCoord unpacked;
        uint32_t unpackedLod;
        unpackTileKey(key, unpacked, unpackedLod);

        CHECK(unpacked.x == 1000000);
        CHECK(unpacked.z == 2000000);
        CHECK(unpackedLod == 5);
    }
}

// ============================================================================
// distanceToTile Tests
// ============================================================================

TEST_SUITE("distanceToTile") {
    GridConfig makeConfig() {
        GridConfig config;
        config.terrainSize = 16384.0f;
        config.tilesX = 32;
        config.tilesZ = 32;
        return config;
    }

    TEST_CASE("point inside tile has zero distance") {
        auto config = makeConfig();
        // Tile (0,0) covers 0-512 in X and Z

        float dist = distanceToTile(256.0f, 256.0f, {0, 0}, 0, config);
        CHECK(dist == doctest::Approx(0.0f));
    }

    TEST_CASE("point at tile edge has zero distance") {
        auto config = makeConfig();

        float dist = distanceToTile(0.0f, 0.0f, {0, 0}, 0, config);
        CHECK(dist == doctest::Approx(0.0f));

        dist = distanceToTile(512.0f, 512.0f, {0, 0}, 0, config);
        CHECK(dist == doctest::Approx(0.0f));
    }

    TEST_CASE("point outside tile has correct distance") {
        auto config = makeConfig();
        // Tile (0,0) covers 0-512

        // Point 100 units to the right of tile
        float dist = distanceToTile(612.0f, 256.0f, {0, 0}, 0, config);
        CHECK(dist == doctest::Approx(100.0f));

        // Point 100 units above tile
        dist = distanceToTile(256.0f, 612.0f, {0, 0}, 0, config);
        CHECK(dist == doctest::Approx(100.0f));
    }

    TEST_CASE("point at corner has diagonal distance") {
        auto config = makeConfig();
        // Tile (0,0) covers 0-512

        // Point 100 units right and 100 units up from corner
        float dist = distanceToTile(612.0f, 612.0f, {0, 0}, 0, config);
        CHECK(dist == doctest::Approx(std::sqrt(100.0f * 100.0f + 100.0f * 100.0f)));
    }
}

// ============================================================================
// isPointInHole Tests
// ============================================================================

TEST_SUITE("isPointInHole") {
    TEST_CASE("point inside hole returns true") {
        std::vector<TerrainHole> holes = {
            {100.0f, 100.0f, 50.0f}  // Center at (100,100), radius 50
        };

        CHECK(isPointInHole(100.0f, 100.0f, holes));  // Center
        CHECK(isPointInHole(120.0f, 100.0f, holes));  // Inside
        CHECK(isPointInHole(100.0f, 140.0f, holes));  // Inside
    }

    TEST_CASE("point on edge is inside") {
        std::vector<TerrainHole> holes = {
            {100.0f, 100.0f, 50.0f}
        };

        CHECK(isPointInHole(150.0f, 100.0f, holes));  // Exactly on edge
    }

    TEST_CASE("point outside hole returns false") {
        std::vector<TerrainHole> holes = {
            {100.0f, 100.0f, 50.0f}
        };

        CHECK_FALSE(isPointInHole(200.0f, 100.0f, holes));  // Too far right
        CHECK_FALSE(isPointInHole(0.0f, 0.0f, holes));      // Way off
    }

    TEST_CASE("empty holes list returns false") {
        std::vector<TerrainHole> holes;
        CHECK_FALSE(isPointInHole(100.0f, 100.0f, holes));
    }

    TEST_CASE("multiple holes are checked") {
        std::vector<TerrainHole> holes = {
            {100.0f, 100.0f, 10.0f},  // Small hole at (100,100)
            {500.0f, 500.0f, 20.0f}   // Larger hole at (500,500)
        };

        CHECK(isPointInHole(100.0f, 100.0f, holes));  // In first hole
        CHECK(isPointInHole(510.0f, 500.0f, holes));  // In second hole
        CHECK_FALSE(isPointInHole(300.0f, 300.0f, holes));  // In neither
    }
}

// ============================================================================
// rasterizeHolesForTile Tests
// ============================================================================

TEST_SUITE("rasterizeHolesForTile") {
    TEST_CASE("empty holes produces all-zero mask") {
        std::vector<TerrainHole> holes;

        auto mask = rasterizeHolesForTile(0.0f, 0.0f, 100.0f, 100.0f, 16, holes);

        CHECK(mask.size() == 16 * 16);
        for (auto val : mask) {
            CHECK(val == 0);
        }
    }

    TEST_CASE("hole covering entire tile produces all-255 mask") {
        std::vector<TerrainHole> holes = {
            {50.0f, 50.0f, 100.0f}  // Hole larger than tile
        };

        auto mask = rasterizeHolesForTile(0.0f, 0.0f, 100.0f, 100.0f, 8, holes);

        CHECK(mask.size() == 8 * 8);
        for (auto val : mask) {
            CHECK(val == 255);
        }
    }

    TEST_CASE("small hole in center produces partial mask") {
        std::vector<TerrainHole> holes = {
            {50.0f, 50.0f, 10.0f}  // Small hole in center of 0-100 tile
        };

        auto mask = rasterizeHolesForTile(0.0f, 0.0f, 100.0f, 100.0f, 32, holes);

        CHECK(mask.size() == 32 * 32);

        int holePixels = 0;
        int solidPixels = 0;
        for (auto val : mask) {
            if (val == 255) holePixels++;
            else solidPixels++;
        }

        // Should have some hole pixels and some solid pixels
        CHECK(holePixels > 0);
        CHECK(solidPixels > 0);
        CHECK(holePixels < solidPixels);  // Hole is small
    }

    TEST_CASE("hole outside tile produces all-zero mask") {
        std::vector<TerrainHole> holes = {
            {500.0f, 500.0f, 10.0f}  // Hole way outside tile bounds
        };

        auto mask = rasterizeHolesForTile(0.0f, 0.0f, 100.0f, 100.0f, 16, holes);

        for (auto val : mask) {
            CHECK(val == 0);
        }
    }

    TEST_CASE("small hole is inflated for GPU bilinear sampling") {
        // Simulates the well hole scenario: 5m radius hole on 16384m terrain with 2048 resolution
        // Texel size = 16384 / 2048 = 8m, inflation = 4m (half texel)
        // Effective radius = 5m + 4m = 9m, which spans ~2 texels from center
        const float terrainSize = 16384.0f;
        const uint32_t resolution = 2048;
        const float halfTerrain = terrainSize * 0.5f;

        // Hole at center of terrain, radius smaller than texel size
        std::vector<TerrainHole> holes = {
            {0.0f, 0.0f, 5.0f}  // 5m radius, less than 8m texel size
        };

        auto mask = rasterizeHolesForTile(-halfTerrain, -halfTerrain, halfTerrain, halfTerrain,
                                          resolution, holes);

        // Find center texel (resolution/2, resolution/2)
        const uint32_t centerCol = resolution / 2;
        const uint32_t centerRow = resolution / 2;

        // Count marked texels around the center
        int markedCount = 0;
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                uint32_t col = centerCol + dx;
                uint32_t row = centerRow + dy;
                if (mask[row * resolution + col] == 255) {
                    markedCount++;
                }
            }
        }

        // With half-texel inflation, multiple texels should be marked
        // Effective radius 9m with 8m texels means ~2x2 to 3x3 texels
        CHECK(markedCount >= 2);  // At minimum, should mark more than 1 texel

        // Verify center texel is marked
        CHECK(mask[centerRow * resolution + centerCol] == 255);
    }
}

// ============================================================================
// getTilesInRadius Tests
// ============================================================================

TEST_SUITE("getTilesInRadius") {
    GridConfig makeConfig() {
        GridConfig config;
        config.terrainSize = 1600.0f;  // 1600m terrain
        config.tilesX = 16;            // 100m tiles at LOD0
        config.tilesZ = 16;
        return config;
    }

    TEST_CASE("small radius gets single tile") {
        auto config = makeConfig();
        // Tile size is 100m

        auto tiles = getTilesInRadius(150.0f, 150.0f, 10.0f, 0, config);

        CHECK(tiles.size() == 1);
        CHECK(tiles[0].x == 1);  // 150/100 = 1.5 -> tile 1
        CHECK(tiles[0].z == 1);
    }

    TEST_CASE("larger radius gets multiple tiles") {
        auto config = makeConfig();

        auto tiles = getTilesInRadius(500.0f, 500.0f, 200.0f, 0, config);

        // Should include tiles around (5,5) within 200m radius
        CHECK(tiles.size() > 1);

        // Center tile should be included
        bool foundCenter = false;
        for (const auto& tile : tiles) {
            if (tile.x == 5 && tile.z == 5) {
                foundCenter = true;
                break;
            }
        }
        CHECK(foundCenter);
    }

    TEST_CASE("radius at origin respects bounds") {
        auto config = makeConfig();

        auto tiles = getTilesInRadius(0.0f, 0.0f, 200.0f, 0, config);

        // Should only include tiles with non-negative coords
        for (const auto& tile : tiles) {
            CHECK(tile.x >= 0);
            CHECK(tile.z >= 0);
        }
    }

    TEST_CASE("radius at edge respects bounds") {
        auto config = makeConfig();

        auto tiles = getTilesInRadius(1600.0f, 1600.0f, 200.0f, 0, config);

        // Should only include valid tiles
        for (const auto& tile : tiles) {
            CHECK(tile.x < 16);
            CHECK(tile.z < 16);
        }
    }
}

// ============================================================================
// isValidTileCoord Tests
// ============================================================================

TEST_SUITE("isValidTileCoord") {
    TEST_CASE("valid coordinates at LOD0") {
        GridConfig config;
        config.tilesX = 32;
        config.tilesZ = 32;

        CHECK(isValidTileCoord({0, 0}, 0, config));
        CHECK(isValidTileCoord({15, 15}, 0, config));
        CHECK(isValidTileCoord({31, 31}, 0, config));
    }

    TEST_CASE("invalid coordinates at LOD0") {
        GridConfig config;
        config.tilesX = 32;
        config.tilesZ = 32;

        CHECK_FALSE(isValidTileCoord({-1, 0}, 0, config));
        CHECK_FALSE(isValidTileCoord({0, -1}, 0, config));
        CHECK_FALSE(isValidTileCoord({32, 0}, 0, config));
        CHECK_FALSE(isValidTileCoord({0, 32}, 0, config));
    }

    TEST_CASE("valid range shrinks with LOD") {
        GridConfig config;
        config.tilesX = 32;
        config.tilesZ = 32;

        // At LOD1, only 16 tiles
        CHECK(isValidTileCoord({15, 15}, 1, config));
        CHECK_FALSE(isValidTileCoord({16, 16}, 1, config));

        // At LOD2, only 8 tiles
        CHECK(isValidTileCoord({7, 7}, 2, config));
        CHECK_FALSE(isValidTileCoord({8, 8}, 2, config));

        // At LOD3, only 4 tiles
        CHECK(isValidTileCoord({3, 3}, 3, config));
        CHECK_FALSE(isValidTileCoord({4, 4}, 3, config));
    }
}
