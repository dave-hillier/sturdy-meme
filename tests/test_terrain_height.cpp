#include <doctest/doctest.h>
#include "terrain/TerrainHeight.h"

TEST_SUITE("TerrainHeight") {
    TEST_CASE("toWorld basic conversion") {
        // Zero height maps to zero world height
        CHECK(TerrainHeight::toWorld(0.0f, 1000.0f) == doctest::Approx(0.0f));

        // Maximum normalized height maps to heightScale
        CHECK(TerrainHeight::toWorld(1.0f, 1000.0f) == doctest::Approx(1000.0f));

        // Half height
        CHECK(TerrainHeight::toWorld(0.5f, 1000.0f) == doctest::Approx(500.0f));

        // Different height scales
        CHECK(TerrainHeight::toWorld(0.25f, 2000.0f) == doctest::Approx(500.0f));
        CHECK(TerrainHeight::toWorld(0.75f, 400.0f) == doctest::Approx(300.0f));
    }

    TEST_CASE("toWorld handles edge cases") {
        // Zero scale
        CHECK(TerrainHeight::toWorld(0.5f, 0.0f) == doctest::Approx(0.0f));

        // Very small values
        CHECK(TerrainHeight::toWorld(0.001f, 1000.0f) == doctest::Approx(1.0f));

        // Negative normalized height (though unusual)
        CHECK(TerrainHeight::toWorld(-0.1f, 1000.0f) == doctest::Approx(-100.0f));
    }

    TEST_CASE("worldToUV center of terrain") {
        float u, v;

        // Center of terrain (world 0,0) maps to UV (0.5, 0.5)
        TerrainHeight::worldToUV(0.0f, 0.0f, 4096.0f, u, v);
        CHECK(u == doctest::Approx(0.5f));
        CHECK(v == doctest::Approx(0.5f));
    }

    TEST_CASE("worldToUV corners of terrain") {
        float u, v;
        float terrainSize = 4096.0f;
        float halfSize = terrainSize / 2.0f;

        // Bottom-left corner (-halfSize, -halfSize) -> UV (0, 0)
        TerrainHeight::worldToUV(-halfSize, -halfSize, terrainSize, u, v);
        CHECK(u == doctest::Approx(0.0f));
        CHECK(v == doctest::Approx(0.0f));

        // Top-right corner (halfSize, halfSize) -> UV (1, 1)
        TerrainHeight::worldToUV(halfSize, halfSize, terrainSize, u, v);
        CHECK(u == doctest::Approx(1.0f));
        CHECK(v == doctest::Approx(1.0f));

        // Bottom-right corner (halfSize, -halfSize) -> UV (1, 0)
        TerrainHeight::worldToUV(halfSize, -halfSize, terrainSize, u, v);
        CHECK(u == doctest::Approx(1.0f));
        CHECK(v == doctest::Approx(0.0f));

        // Top-left corner (-halfSize, halfSize) -> UV (0, 1)
        TerrainHeight::worldToUV(-halfSize, halfSize, terrainSize, u, v);
        CHECK(u == doctest::Approx(0.0f));
        CHECK(v == doctest::Approx(1.0f));
    }

    TEST_CASE("worldToUV with different terrain sizes") {
        float u, v;

        // Smaller terrain
        TerrainHeight::worldToUV(512.0f, 256.0f, 2048.0f, u, v);
        CHECK(u == doctest::Approx(0.75f));  // 512/2048 + 0.5
        CHECK(v == doctest::Approx(0.625f)); // 256/2048 + 0.5

        // Larger terrain
        TerrainHeight::worldToUV(1000.0f, -2000.0f, 8192.0f, u, v);
        CHECK(u == doctest::Approx(1000.0f/8192.0f + 0.5f));
        CHECK(v == doctest::Approx(-2000.0f/8192.0f + 0.5f));
    }

    TEST_CASE("isUVInBounds") {
        // Valid UV coordinates
        CHECK(TerrainHeight::isUVInBounds(0.5f, 0.5f) == true);
        CHECK(TerrainHeight::isUVInBounds(0.0f, 0.0f) == true);
        CHECK(TerrainHeight::isUVInBounds(1.0f, 1.0f) == true);
        CHECK(TerrainHeight::isUVInBounds(0.0f, 1.0f) == true);
        CHECK(TerrainHeight::isUVInBounds(1.0f, 0.0f) == true);

        // Invalid UV coordinates (outside 0-1 range)
        CHECK(TerrainHeight::isUVInBounds(-0.01f, 0.5f) == false);
        CHECK(TerrainHeight::isUVInBounds(1.01f, 0.5f) == false);
        CHECK(TerrainHeight::isUVInBounds(0.5f, -0.01f) == false);
        CHECK(TerrainHeight::isUVInBounds(0.5f, 1.01f) == false);
        CHECK(TerrainHeight::isUVInBounds(-1.0f, -1.0f) == false);
        CHECK(TerrainHeight::isUVInBounds(2.0f, 2.0f) == false);
    }

    TEST_CASE("round trip: worldToUV then check bounds") {
        float terrainSize = 4096.0f;
        float halfSize = terrainSize / 2.0f;

        // Points inside terrain should be in bounds
        float u, v;
        TerrainHeight::worldToUV(0.0f, 0.0f, terrainSize, u, v);
        CHECK(TerrainHeight::isUVInBounds(u, v) == true);

        TerrainHeight::worldToUV(halfSize * 0.9f, -halfSize * 0.5f, terrainSize, u, v);
        CHECK(TerrainHeight::isUVInBounds(u, v) == true);

        // Points outside terrain should be out of bounds
        TerrainHeight::worldToUV(halfSize * 1.1f, 0.0f, terrainSize, u, v);
        CHECK(TerrainHeight::isUVInBounds(u, v) == false);

        TerrainHeight::worldToUV(0.0f, -halfSize * 1.5f, terrainSize, u, v);
        CHECK(TerrainHeight::isUVInBounds(u, v) == false);
    }
}
