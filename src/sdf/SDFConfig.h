#pragma once

#include <cstdint>

/**
 * SDFConfig - Configuration for Signed Distance Field generation and rendering
 *
 * Memory usage per mesh at different resolutions:
 * - 32³  = 32KB  per mesh (low quality, ~40MB for 100 buildings)
 * - 64³  = 256KB per mesh (medium quality, ~300MB for 100 buildings)
 * - 128³ = 2MB   per mesh (high quality, ~2.4GB for 100 buildings)
 *
 * Default is 64³ which provides good sub-meter detail for buildings.
 */
struct SDFConfig {
    // SDF resolution per mesh (must be power of 2: 32, 64, or 128)
    uint32_t resolution = 64;

    // Padding around mesh bounds (fraction of bounds size)
    float boundsPadding = 0.1f;

    // Maximum number of SDF entries in the atlas
    uint32_t maxAtlasEntries = 256;

    // AO cone tracing parameters
    int numCones = 4;           // Number of AO cones (4-8 typical)
    int maxSteps = 16;          // Max steps per cone trace
    float coneAngle = 0.5f;     // Cone half-angle in radians (~30 degrees)
    float maxDistance = 10.0f;  // Max trace distance in meters
    float aoIntensity = 1.0f;   // AO intensity multiplier

    // Quality presets
    enum class Quality {
        Low,    // 32³, 4 cones, 8 steps - ~40MB, fastest
        Medium, // 64³, 4 cones, 16 steps - ~300MB, balanced
        High    // 128³, 6 cones, 24 steps - ~2.4GB, best quality
    };

    static SDFConfig fromQuality(Quality q) {
        SDFConfig cfg;
        switch (q) {
            case Quality::Low:
                cfg.resolution = 32;
                cfg.numCones = 4;
                cfg.maxSteps = 8;
                break;
            case Quality::Medium:
                cfg.resolution = 64;
                cfg.numCones = 4;
                cfg.maxSteps = 16;
                break;
            case Quality::High:
                cfg.resolution = 128;
                cfg.numCones = 6;
                cfg.maxSteps = 24;
                break;
        }
        return cfg;
    }

    // Estimate memory usage for a given number of meshes
    size_t estimateMemoryMB(uint32_t numMeshes) const {
        size_t bytesPerVoxel = 2; // R16F
        size_t voxelsPerMesh = resolution * resolution * resolution;
        size_t bytesPerMesh = voxelsPerMesh * bytesPerVoxel;
        return (bytesPerMesh * numMeshes) / (1024 * 1024);
    }
};
