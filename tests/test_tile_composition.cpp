// Tests for terrain tile composition and boundary handling
// These tests verify that tiles sample correctly at boundaries without seams

#include <doctest/doctest.h>
#include "terrain/TerrainHeight.h"
#include <vector>
#include <cmath>

// Simulate the tile generation and sampling process to verify boundary continuity

namespace {

// Create a simple linear gradient heightmap for testing
// Returns normalized [0,1] heights
std::vector<float> createGradientHeightmap(uint32_t width, uint32_t height) {
    std::vector<float> data(width * height);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            // Linear gradient in X direction
            data[y * width + x] = static_cast<float>(x) / (width - 1);
        }
    }
    return data;
}

// Create a heightmap with known values at pixel centers
std::vector<float> createPixelValueHeightmap(uint32_t width, uint32_t height) {
    std::vector<float> data(width * height);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            // Each pixel's value is its x index (normalized)
            data[y * width + x] = static_cast<float>(x);
        }
    }
    return data;
}

// Extract a tile from a source heightmap (simulates TerrainImporter logic)
// WITHOUT overlap - the current buggy behavior
std::vector<float> extractTileNoOverlap(const std::vector<float>& source,
                                         uint32_t sourceWidth, uint32_t sourceHeight,
                                         uint32_t tileX, uint32_t tileZ,
                                         uint32_t tileRes) {
    std::vector<float> tile(tileRes * tileRes);
    uint32_t srcStartX = tileX * tileRes;
    uint32_t srcStartZ = tileZ * tileRes;

    for (uint32_t py = 0; py < tileRes; py++) {
        for (uint32_t px = 0; px < tileRes; px++) {
            uint32_t srcX = std::min(srcStartX + px, sourceWidth - 1);
            uint32_t srcZ = std::min(srcStartZ + py, sourceHeight - 1);
            tile[py * tileRes + px] = source[srcZ * sourceWidth + srcX];
        }
    }
    return tile;
}

// Extract a tile from a source heightmap WITH overlap
// This is the fixed behavior - tiles are (tileRes+1) x (tileRes+1)
std::vector<float> extractTileWithOverlap(const std::vector<float>& source,
                                           uint32_t sourceWidth, uint32_t sourceHeight,
                                           uint32_t tileX, uint32_t tileZ,
                                           uint32_t tileRes) {
    // Tile is actually tileRes+1 pixels to include overlap
    uint32_t actualRes = tileRes + 1;
    std::vector<float> tile(actualRes * actualRes);
    uint32_t srcStartX = tileX * tileRes;
    uint32_t srcStartZ = tileZ * tileRes;

    for (uint32_t py = 0; py < actualRes; py++) {
        for (uint32_t px = 0; px < actualRes; px++) {
            uint32_t srcX = std::min(srcStartX + px, sourceWidth - 1);
            uint32_t srcZ = std::min(srcStartZ + py, sourceHeight - 1);
            tile[py * actualRes + px] = source[srcZ * sourceWidth + srcX];
        }
    }
    return tile;
}

// Sample a tile at a given UV coordinate using bilinear interpolation
// This matches TerrainHeight::sampleBilinear
float sampleTile(const std::vector<float>& tile, uint32_t resolution, float u, float v) {
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    float fx = u * (resolution - 1);
    float fy = v * (resolution - 1);

    int x0 = static_cast<int>(fx);
    int y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1, static_cast<int>(resolution - 1));
    int y1 = std::min(y0 + 1, static_cast<int>(resolution - 1));

    float tx = fx - x0;
    float ty = fy - y0;

    float h00 = tile[y0 * resolution + x0];
    float h10 = tile[y0 * resolution + x1];
    float h01 = tile[y1 * resolution + x0];
    float h11 = tile[y1 * resolution + x1];

    float h0 = h00 * (1.0f - tx) + h10 * tx;
    float h1 = h01 * (1.0f - tx) + h11 * tx;

    return h0 * (1.0f - ty) + h1 * ty;
}

} // anonymous namespace

// ============================================================================
// Tile Boundary Continuity Tests
// ============================================================================

TEST_SUITE("TileComposition") {

    TEST_CASE("extractTileNoOverlap extracts correct pixels") {
        // Create a 8x8 source heightmap with known values
        auto source = createPixelValueHeightmap(8, 8);

        // Extract 4x4 tiles
        auto tile0 = extractTileNoOverlap(source, 8, 8, 0, 0, 4);
        auto tile1 = extractTileNoOverlap(source, 8, 8, 1, 0, 4);

        // Tile 0 should have pixels 0-3
        CHECK(tile0[0] == 0.0f);  // pixel 0
        CHECK(tile0[1] == 1.0f);  // pixel 1
        CHECK(tile0[2] == 2.0f);  // pixel 2
        CHECK(tile0[3] == 3.0f);  // pixel 3

        // Tile 1 should have pixels 4-7
        CHECK(tile1[0] == 4.0f);  // pixel 4
        CHECK(tile1[1] == 5.0f);  // pixel 5
        CHECK(tile1[2] == 6.0f);  // pixel 6
        CHECK(tile1[3] == 7.0f);  // pixel 7
    }

    TEST_CASE("no-overlap tiles have discontinuity at boundary") {
        // Create a 1024x1024 source heightmap with linear gradient
        uint32_t sourceRes = 1024;
        auto source = createGradientHeightmap(sourceRes, sourceRes);

        // Extract two adjacent 512x512 tiles
        uint32_t tileRes = 512;
        auto tile0 = extractTileNoOverlap(source, sourceRes, sourceRes, 0, 0, tileRes);
        auto tile1 = extractTileNoOverlap(source, sourceRes, sourceRes, 1, 0, tileRes);

        // Sample at the boundary
        // Tile 0 at UV (1.0, 0.5) should sample its rightmost pixel (511)
        // Tile 1 at UV (0.0, 0.5) should sample its leftmost pixel (512 in source)
        float sample0 = sampleTile(tile0, tileRes, 1.0f, 0.5f);
        float sample1 = sampleTile(tile1, tileRes, 0.0f, 0.5f);

        // These values should be different because tiles don't overlap!
        // Tile 0 sampled source pixel 511: value = 511/1023
        // Tile 1 sampled source pixel 512: value = 512/1023
        float expected0 = 511.0f / (sourceRes - 1);
        float expected1 = 512.0f / (sourceRes - 1);

        CHECK(sample0 == doctest::Approx(expected0).epsilon(0.001));
        CHECK(sample1 == doctest::Approx(expected1).epsilon(0.001));

        // The discontinuity!
        float discontinuity = std::abs(sample1 - sample0);
        CHECK(discontinuity > 0.0001f);  // There IS a gap
        MESSAGE("Discontinuity at tile boundary (no overlap): ", discontinuity);
    }

    TEST_CASE("extractTileWithOverlap extracts correct pixels") {
        // Create a 8x8 source heightmap with known values
        auto source = createPixelValueHeightmap(8, 8);

        // Extract 4-pixel nominal tiles (actually 5x5 with overlap)
        auto tile0 = extractTileWithOverlap(source, 8, 8, 0, 0, 4);
        auto tile1 = extractTileWithOverlap(source, 8, 8, 1, 0, 4);

        // Tile 0 should have pixels 0-4 (5 pixels)
        CHECK(tile0.size() == 25);  // 5x5
        CHECK(tile0[0] == 0.0f);  // pixel 0
        CHECK(tile0[1] == 1.0f);  // pixel 1
        CHECK(tile0[2] == 2.0f);  // pixel 2
        CHECK(tile0[3] == 3.0f);  // pixel 3
        CHECK(tile0[4] == 4.0f);  // pixel 4 (overlap!)

        // Tile 1 should have pixels 4-7 (clamped) plus overlap
        CHECK(tile1.size() == 25);  // 5x5
        CHECK(tile1[0] == 4.0f);  // pixel 4 (same as tile0's last!)
        CHECK(tile1[1] == 5.0f);  // pixel 5
        CHECK(tile1[2] == 6.0f);  // pixel 6
        CHECK(tile1[3] == 7.0f);  // pixel 7
        CHECK(tile1[4] == 7.0f);  // pixel 7 (clamped at edge)
    }

    TEST_CASE("overlap tiles have continuity at boundary") {
        // Create a 1024x1024 source heightmap with linear gradient
        uint32_t sourceRes = 1024;
        auto source = createGradientHeightmap(sourceRes, sourceRes);

        // Extract two adjacent tiles with overlap
        // Nominal resolution 512, actual resolution 513
        uint32_t nominalTileRes = 512;
        uint32_t actualTileRes = nominalTileRes + 1;  // 513

        auto tile0 = extractTileWithOverlap(source, sourceRes, sourceRes, 0, 0, nominalTileRes);
        auto tile1 = extractTileWithOverlap(source, sourceRes, sourceRes, 1, 0, nominalTileRes);

        // Both tiles are 513x513
        CHECK(tile0.size() == actualTileRes * actualTileRes);
        CHECK(tile1.size() == actualTileRes * actualTileRes);

        // Sample at the boundary
        // Tile 0 at UV (1.0, 0.5) samples its pixel 512 (source pixel 512)
        // Tile 1 at UV (0.0, 0.5) samples its pixel 0 (also source pixel 512!)
        float sample0 = sampleTile(tile0, actualTileRes, 1.0f, 0.5f);
        float sample1 = sampleTile(tile1, actualTileRes, 0.0f, 0.5f);

        // Both should sample the same source pixel!
        float expectedValue = 512.0f / (sourceRes - 1);
        CHECK(sample0 == doctest::Approx(expectedValue).epsilon(0.001));
        CHECK(sample1 == doctest::Approx(expectedValue).epsilon(0.001));

        // NO discontinuity!
        float discontinuity = std::abs(sample1 - sample0);
        CHECK(discontinuity < 0.0001f);  // No gap
        MESSAGE("Discontinuity at tile boundary (with overlap): ", discontinuity);
    }

    TEST_CASE("TerrainHeight::sampleBilinear handles boundaries correctly") {
        // Create a 4x4 heightmap with linear gradient
        uint32_t res = 4;
        std::vector<float> data(res * res);
        for (uint32_t y = 0; y < res; y++) {
            for (uint32_t x = 0; x < res; x++) {
                data[y * res + x] = static_cast<float>(x) / (res - 1);  // 0, 0.33, 0.67, 1.0
            }
        }

        // Sample at UV edges
        float sample00 = TerrainHeight::sampleBilinear(0.0f, 0.0f, data.data(), res);
        float sample10 = TerrainHeight::sampleBilinear(1.0f, 0.0f, data.data(), res);
        float sample01 = TerrainHeight::sampleBilinear(0.0f, 1.0f, data.data(), res);
        float sample11 = TerrainHeight::sampleBilinear(1.0f, 1.0f, data.data(), res);

        CHECK(sample00 == doctest::Approx(0.0f));
        CHECK(sample10 == doctest::Approx(1.0f));
        CHECK(sample01 == doctest::Approx(0.0f));
        CHECK(sample11 == doctest::Approx(1.0f));

        // Sample at center
        float sampleCenter = TerrainHeight::sampleBilinear(0.5f, 0.5f, data.data(), res);
        CHECK(sampleCenter == doctest::Approx(0.5f));
    }

    TEST_CASE("adjacent tile sampling with overlap matches at all points along boundary") {
        // Create a larger heightmap with more interesting data
        uint32_t sourceRes = 256;
        auto source = createGradientHeightmap(sourceRes, sourceRes);

        // Use smaller tiles for faster test
        uint32_t nominalTileRes = 64;
        uint32_t actualTileRes = nominalTileRes + 1;

        auto tile0 = extractTileWithOverlap(source, sourceRes, sourceRes, 0, 0, nominalTileRes);
        auto tile1 = extractTileWithOverlap(source, sourceRes, sourceRes, 1, 0, nominalTileRes);

        // Test multiple points along the boundary (U=1 for tile0, U=0 for tile1)
        for (float v = 0.0f; v <= 1.0f; v += 0.1f) {
            float sample0 = sampleTile(tile0, actualTileRes, 1.0f, v);
            float sample1 = sampleTile(tile1, actualTileRes, 0.0f, v);

            float diff = std::abs(sample1 - sample0);
            CHECK(diff < 0.0001f);
        }
    }

    TEST_CASE("vertical tile boundary also continuous with overlap") {
        // Test Z/V direction boundaries too
        uint32_t sourceRes = 256;
        auto source = createGradientHeightmap(sourceRes, sourceRes);

        uint32_t nominalTileRes = 64;
        uint32_t actualTileRes = nominalTileRes + 1;

        // Tiles stacked vertically
        auto tileTop = extractTileWithOverlap(source, sourceRes, sourceRes, 0, 0, nominalTileRes);
        auto tileBottom = extractTileWithOverlap(source, sourceRes, sourceRes, 0, 1, nominalTileRes);

        // Test boundary (V=1 for tileTop, V=0 for tileBottom)
        for (float u = 0.0f; u <= 1.0f; u += 0.1f) {
            float sampleTop = sampleTile(tileTop, actualTileRes, u, 1.0f);
            float sampleBottom = sampleTile(tileBottom, actualTileRes, u, 0.0f);

            float diff = std::abs(sampleBottom - sampleTop);
            CHECK(diff < 0.0001f);
        }
    }

    TEST_CASE("corner tiles meet at exact corner point") {
        // Test the corner where 4 tiles meet
        uint32_t sourceRes = 256;
        auto source = createGradientHeightmap(sourceRes, sourceRes);

        uint32_t nominalTileRes = 64;
        uint32_t actualTileRes = nominalTileRes + 1;

        // Four tiles meeting at a corner
        auto tile00 = extractTileWithOverlap(source, sourceRes, sourceRes, 0, 0, nominalTileRes);
        auto tile10 = extractTileWithOverlap(source, sourceRes, sourceRes, 1, 0, nominalTileRes);
        auto tile01 = extractTileWithOverlap(source, sourceRes, sourceRes, 0, 1, nominalTileRes);
        auto tile11 = extractTileWithOverlap(source, sourceRes, sourceRes, 1, 1, nominalTileRes);

        // All should sample the same value at the corner
        float s00 = sampleTile(tile00, actualTileRes, 1.0f, 1.0f);  // bottom-right of tile00
        float s10 = sampleTile(tile10, actualTileRes, 0.0f, 1.0f);  // bottom-left of tile10
        float s01 = sampleTile(tile01, actualTileRes, 1.0f, 0.0f);  // top-right of tile01
        float s11 = sampleTile(tile11, actualTileRes, 0.0f, 0.0f);  // top-left of tile11

        CHECK(s00 == doctest::Approx(s10).epsilon(0.0001));
        CHECK(s00 == doctest::Approx(s01).epsilon(0.0001));
        CHECK(s00 == doctest::Approx(s11).epsilon(0.0001));
    }
}

// ============================================================================
// UV Mapping Tests
// ============================================================================

TEST_SUITE("TileUVMapping") {
    TEST_CASE("UV mapping formula for tiles with overlap") {
        // With overlap tiles (res+1 pixels), the UV mapping is:
        // pixel = UV * res  (not UV * (res-1))
        // because we want:
        //   UV 0.0 -> pixel 0
        //   UV 1.0 -> pixel res (which is the overlap pixel)

        uint32_t nominalRes = 512;
        uint32_t actualRes = nominalRes + 1;  // 513

        // Using the standard formula: pixel = UV * (actualRes - 1) = UV * 512
        // UV 0.0 -> pixel 0
        // UV 1.0 -> pixel 512
        float pixel_at_0 = 0.0f * (actualRes - 1);
        float pixel_at_1 = 1.0f * (actualRes - 1);

        CHECK(pixel_at_0 == 0.0f);
        CHECK(pixel_at_1 == 512.0f);  // This is the overlap pixel
    }

    TEST_CASE("world-to-UV mapping for adjacent tiles") {
        // Simulate world coordinate to UV conversion
        float terrainSize = 4096.0f;
        uint32_t tilesPerEdge = 4;
        float tileWorldSize = terrainSize / tilesPerEdge;  // 1024

        // Tile 0 covers world [0, 1024)
        // Tile 1 covers world [1024, 2048)

        float worldBoundary = 1024.0f;  // Boundary between tile 0 and tile 1

        // For tile 0 (worldMin=0, worldMax=1024):
        float tile0MinX = 0.0f;
        float tile0MaxX = 1024.0f;
        float u0 = (worldBoundary - tile0MinX) / (tile0MaxX - tile0MinX);
        CHECK(u0 == doctest::Approx(1.0f));

        // For tile 1 (worldMin=1024, worldMax=2048):
        float tile1MinX = 1024.0f;
        float tile1MaxX = 2048.0f;
        float u1 = (worldBoundary - tile1MinX) / (tile1MaxX - tile1MinX);
        CHECK(u1 == doctest::Approx(0.0f));

        // At the boundary, tile 0 samples at UV=1.0 and tile 1 samples at UV=0.0
        // With overlapping tiles, both will sample the same source pixel
    }
}
