#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <glm/glm.hpp>
#include "../road_generator/RoadSpline.h"
#include "WaterPlacementData.h"

namespace VirtualTexture {

// Bounds of a tile in world coordinates
struct TileBounds {
    glm::vec2 min;      // Min corner (world XZ)
    glm::vec2 max;      // Max corner (world XZ)

    float width() const { return max.x - min.x; }
    float height() const { return max.y - min.y; }
    glm::vec2 center() const { return (min + max) * 0.5f; }

    bool contains(glm::vec2 point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y;
    }

    // Expand bounds by a margin
    TileBounds expanded(float margin) const {
        return TileBounds{min - glm::vec2(margin), max + glm::vec2(margin)};
    }
};

// Result of rasterizing splines to a tile
struct RasterizedTile {
    uint32_t tileX = 0;             // Tile coordinate X
    uint32_t tileY = 0;             // Tile coordinate Y
    uint32_t resolution = 128;       // Tile resolution in pixels
    TileBounds bounds;               // World bounds of tile

    // Road mask and UVs
    std::vector<float> roadMask;     // Alpha mask [0,1] for roads
    std::vector<glm::vec2> roadUVs;  // UV coordinates for road texture
    std::vector<uint8_t> roadTypes;  // RoadType at each pixel

    // Riverbed mask and UVs
    std::vector<float> riverbedMask; // Alpha mask [0,1] for riverbeds
    std::vector<glm::vec2> riverbedUVs; // UV coordinates for riverbed texture

    // Check if tile has any spline coverage
    bool hasRoads() const;
    bool hasRiverbeds() const;

    // Get pixel index
    size_t pixelIndex(uint32_t x, uint32_t y) const {
        return y * resolution + x;
    }

    // Sample at pixel coordinates
    float sampleRoadMask(uint32_t x, uint32_t y) const;
    glm::vec2 sampleRoadUV(uint32_t x, uint32_t y) const;
    RoadGen::RoadType sampleRoadType(uint32_t x, uint32_t y) const;
    float sampleRiverbedMask(uint32_t x, uint32_t y) const;
    glm::vec2 sampleRiverbedUV(uint32_t x, uint32_t y) const;
};

// Configuration for spline rasterization
struct SplineRasterizerConfig {
    float terrainSize = 16384.0f;       // World terrain size
    uint32_t tileResolution = 128;       // Pixels per tile
    uint32_t tilesPerAxis = 512;         // Number of tiles per axis

    // Anti-aliasing settings
    float edgeSmoothness = 0.5f;         // Edge softness in world units

    // Riverbed settings
    float riverbedWidthMultiplier = 1.3f; // Riverbed is wider than water
    float minRiverWidth = 2.0f;           // Minimum river width to rasterize

    // UV settings
    float roadUVScale = 0.1f;            // UV scale along road length
    float riverUVScale = 0.05f;          // UV scale along river length

    // Get tile size in world units
    float getTileSize() const {
        return terrainSize / tilesPerAxis;
    }

    // Get tile bounds for a tile coordinate
    TileBounds getTileBounds(uint32_t tileX, uint32_t tileY) const {
        float tileSize = getTileSize();
        return TileBounds{
            glm::vec2(tileX * tileSize, tileY * tileSize),
            glm::vec2((tileX + 1) * tileSize, (tileY + 1) * tileSize)
        };
    }
};

// Result of finding closest point on a spline
struct SplineQueryResult {
    glm::vec2 closestPoint;     // Closest point on spline
    float distance;             // Distance from query point to closest point
    float t;                    // Parameter along spline [0, totalLength]
    float width;                // Spline width at closest point
    int segmentIndex;           // Index of segment containing closest point
};

// Spline rasterizer class
class SplineRasterizer {
public:
    SplineRasterizer();
    ~SplineRasterizer() = default;

    // Initialize with configuration
    void init(const SplineRasterizerConfig& config);

    // Set spline data
    void setRoads(const std::vector<RoadGen::RoadSpline>& roads);
    void setRivers(const std::vector<RiverSpline>& rivers);

    // Check if a tile intersects any splines
    bool tileHasRoads(uint32_t tileX, uint32_t tileY) const;
    bool tileHasRivers(uint32_t tileX, uint32_t tileY) const;

    // Rasterize all splines to a single tile
    void rasterizeTile(uint32_t tileX, uint32_t tileY, RasterizedTile& outTile) const;

    // Query functions for splines
    SplineQueryResult queryRoadSpline(const RoadGen::RoadSpline& road, glm::vec2 point) const;
    SplineQueryResult queryRiverSpline(const RiverSpline& river, glm::vec2 point) const;

    // Get statistics
    size_t getRoadCount() const { return roads.size(); }
    size_t getRiverCount() const { return rivers.size(); }

private:
    // Internal spline segment for efficient queries
    struct SplineSegment {
        glm::vec2 p0, p1;       // Segment endpoints
        float w0, w1;           // Widths at endpoints
        float t0, t1;           // Parameter values at endpoints
        TileBounds bounds;       // Bounding box of segment
    };

    // Precomputed data for a road
    struct RoadData {
        std::vector<SplineSegment> segments;
        TileBounds bounds;
        RoadGen::RoadType type;
        float totalLength;
    };

    // Precomputed data for a river
    struct RiverData {
        std::vector<SplineSegment> segments;
        TileBounds bounds;
        float totalLength;
    };

    // Build spatial data for roads and rivers
    void buildRoadData();
    void buildRiverData();

    // Find closest point on a segment
    SplineQueryResult querySegment(const SplineSegment& seg, glm::vec2 point) const;

    // Rasterize a single road to tile
    void rasterizeRoadToTile(const RoadData& road, RasterizedTile& tile) const;

    // Rasterize a single river to tile
    void rasterizeRiverToTile(const RiverData& river, RasterizedTile& tile) const;

    // Smoothstep for anti-aliased edges
    static float smoothstep(float edge0, float edge1, float x);

    SplineRasterizerConfig config;

    // Source spline data
    std::vector<RoadGen::RoadSpline> roads;
    std::vector<RiverSpline> rivers;

    // Precomputed data
    std::vector<RoadData> roadData;
    std::vector<RiverData> riverData;
};

} // namespace VirtualTexture
