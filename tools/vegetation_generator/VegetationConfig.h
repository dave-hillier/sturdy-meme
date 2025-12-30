#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

// Biome zones (must match BiomeGenerator.h)
enum class BiomeZone : uint8_t {
    Sea = 0,
    Beach = 1,
    ChalkCliff = 2,
    SaltMarsh = 3,
    River = 4,
    Wetland = 5,
    Grassland = 6,
    Agricultural = 7,
    Woodland = 8,
    Count
};

// Types of vegetation/detritus that can be placed
enum class VegetationType : uint8_t {
    // Trees
    OakSmall = 0,
    OakMedium,
    OakLarge,
    AshSmall,
    AshMedium,
    AshLarge,
    BeechSmall,
    BeechMedium,
    BeechLarge,
    PineSmall,
    PineMedium,
    PineLarge,
    Bush1,
    Bush2,
    Bush3,

    // Detritus / ground cover
    Rock,
    FallenBranch,
    Mushroom,
    Stump,
    Log,
    Fern,
    Bramble,

    // Placeholder colored blocks for testing
    PlaceholderRed,
    PlaceholderGreen,
    PlaceholderBlue,
    PlaceholderYellow,

    Count
};

// Get the preset file name for a vegetation type
inline const char* getVegetationPreset(VegetationType type) {
    switch (type) {
        case VegetationType::OakSmall: return "oak_small";
        case VegetationType::OakMedium: return "oak_medium";
        case VegetationType::OakLarge: return "oak_large";
        case VegetationType::AshSmall: return "ash_small";
        case VegetationType::AshMedium: return "ash_medium";
        case VegetationType::AshLarge: return "ash_large";
        case VegetationType::BeechSmall: return "ash_small";  // Use ash as placeholder
        case VegetationType::BeechMedium: return "ash_medium";
        case VegetationType::BeechLarge: return "ash_large";
        case VegetationType::PineSmall: return "pine_small";
        case VegetationType::PineMedium: return "pine_medium";
        case VegetationType::PineLarge: return "pine_large";
        case VegetationType::Bush1: return "bush_1";
        case VegetationType::Bush2: return "bush_2";
        case VegetationType::Bush3: return "bush_3";
        default: return nullptr;  // Non-tree types
    }
}

inline const char* getVegetationTypeName(VegetationType type) {
    switch (type) {
        case VegetationType::OakSmall: return "oak_small";
        case VegetationType::OakMedium: return "oak_medium";
        case VegetationType::OakLarge: return "oak_large";
        case VegetationType::AshSmall: return "ash_small";
        case VegetationType::AshMedium: return "ash_medium";
        case VegetationType::AshLarge: return "ash_large";
        case VegetationType::BeechSmall: return "beech_small";
        case VegetationType::BeechMedium: return "beech_medium";
        case VegetationType::BeechLarge: return "beech_large";
        case VegetationType::PineSmall: return "pine_small";
        case VegetationType::PineMedium: return "pine_medium";
        case VegetationType::PineLarge: return "pine_large";
        case VegetationType::Bush1: return "bush_1";
        case VegetationType::Bush2: return "bush_2";
        case VegetationType::Bush3: return "bush_3";
        case VegetationType::Rock: return "rock";
        case VegetationType::FallenBranch: return "fallen_branch";
        case VegetationType::Mushroom: return "mushroom";
        case VegetationType::Stump: return "stump";
        case VegetationType::Log: return "log";
        case VegetationType::Fern: return "fern";
        case VegetationType::Bramble: return "bramble";
        case VegetationType::PlaceholderRed: return "placeholder_red";
        case VegetationType::PlaceholderGreen: return "placeholder_green";
        case VegetationType::PlaceholderBlue: return "placeholder_blue";
        case VegetationType::PlaceholderYellow: return "placeholder_yellow";
        default: return "unknown";
    }
}

inline bool isTreeType(VegetationType type) {
    return type <= VegetationType::Bush3;
}

// A single vegetation instance
struct VegetationInstance {
    glm::vec2 position;         // World XZ position
    float rotation;             // Y-axis rotation in radians
    float scale;                // Uniform scale factor
    VegetationType type;
    uint32_t seed;              // Per-instance seed for variation
};

// Tile containing vegetation instances (for paging)
struct VegetationTile {
    int32_t tileX;
    int32_t tileZ;
    glm::vec2 worldMin;         // World space bounds
    glm::vec2 worldMax;
    std::vector<VegetationInstance> instances;
};

// Density configuration per biome
struct BiomeDensityConfig {
    float treeDensity = 0.0f;           // Trees per square meter
    float bushDensity = 0.0f;           // Bushes per square meter
    float rockDensity = 0.0f;           // Rocks per square meter
    float detritusDensity = 0.0f;       // Fallen branches, mushrooms etc per sqm

    // Tree species distribution (should sum to 1.0)
    float oakProbability = 0.0f;
    float ashProbability = 0.0f;
    float beechProbability = 0.0f;
    float pineProbability = 0.0f;
};

// Main configuration for vegetation generation
struct VegetationGeneratorConfig {
    // Input files
    std::string biomemapPath;
    std::string heightmapPath;
    std::string outputDir;

    // World parameters
    float terrainSize = 16384.0f;
    float minAltitude = 0.0f;
    float maxAltitude = 200.0f;

    // Tile parameters for paging
    float tileSize = 256.0f;            // World units per tile

    // Global density multiplier
    float densityMultiplier = 1.0f;

    // Minimum spacing (Poisson disk)
    float minTreeSpacing = 4.0f;
    float minBushSpacing = 2.0f;
    float minRockSpacing = 3.0f;
    float minDetritusSpacing = 1.0f;

    // Seed for deterministic generation
    uint32_t seed = 12345;

    // Whether to generate SVG visualization
    bool generateSVG = true;
    int svgSize = 2048;

    // Default biome densities
    BiomeDensityConfig woodlandDensity = {
        .treeDensity = 0.01f,           // ~100 trees per hectare
        .bushDensity = 0.02f,
        .rockDensity = 0.001f,
        .detritusDensity = 0.05f,
        .oakProbability = 0.4f,
        .ashProbability = 0.3f,
        .beechProbability = 0.2f,
        .pineProbability = 0.1f
    };

    BiomeDensityConfig grasslandDensity = {
        .treeDensity = 0.0005f,         // Sparse trees
        .bushDensity = 0.005f,          // Some gorse/shrubs
        .rockDensity = 0.002f,
        .detritusDensity = 0.001f,
        .oakProbability = 0.6f,
        .ashProbability = 0.2f,
        .beechProbability = 0.1f,
        .pineProbability = 0.1f
    };

    BiomeDensityConfig wetlandDensity = {
        .treeDensity = 0.002f,
        .bushDensity = 0.01f,
        .rockDensity = 0.0005f,
        .detritusDensity = 0.02f,
        .oakProbability = 0.2f,
        .ashProbability = 0.5f,
        .beechProbability = 0.0f,
        .pineProbability = 0.3f
    };

    BiomeDensityConfig agriculturalDensity = {
        .treeDensity = 0.0001f,         // Very sparse (field margins)
        .bushDensity = 0.001f,
        .rockDensity = 0.0001f,
        .detritusDensity = 0.0005f,
        .oakProbability = 0.7f,
        .ashProbability = 0.2f,
        .beechProbability = 0.1f,
        .pineProbability = 0.0f
    };
};
