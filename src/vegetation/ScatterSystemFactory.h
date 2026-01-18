#pragma once

#include "ScatterSystem.h"
#include "scene/DeterministicRandom.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <memory>

/**
 * ScatterSystemFactory - Factory functions for creating ScatterSystem instances
 *
 * Provides pre-configured factory methods for common decoration types:
 * - Rocks: Procedural icosphere-based rock meshes with radius placement
 * - Detritus: Fallen branches placed near tree positions
 *
 * Each factory generates the meshes and placements, then creates a ScatterSystem.
 */
namespace ScatterSystemFactory {

// ============================================================================
// Rock System Configuration
// ============================================================================

struct RockConfig {
    int rockVariations = 5;           // Number of unique rock mesh variations
    int rocksPerVariation = 8;        // How many instances of each variation
    float minRadius = 0.3f;           // Minimum rock base radius
    float maxRadius = 1.5f;           // Maximum rock base radius
    float placementRadius = 80.0f;    // Radius from center to place rocks
    glm::vec2 placementCenter = glm::vec2(0.0f);  // Center point for placement
    float minDistanceBetween = 3.0f;  // Minimum distance between rocks
    float roughness = 0.35f;          // Surface roughness for mesh generation
    float asymmetry = 0.25f;          // How non-spherical rocks should be
    int subdivisions = 3;             // Icosphere subdivision level
    float materialRoughness = 0.7f;   // PBR roughness for rendering
    float materialMetallic = 0.0f;    // PBR metallic for rendering
};

/**
 * Create a rock scatter system.
 *
 * Generates procedural rock meshes and places them in a circular area.
 */
std::unique_ptr<ScatterSystem> createRocks(
    const ScatterSystem::InitInfo& info,
    const RockConfig& config = {});

// ============================================================================
// Detritus System Configuration
// ============================================================================

struct DetritusConfig {
    int branchVariations = 8;         // Number of unique fallen branch variations
    int forkedVariations = 4;         // Number of Y-shaped forked branch variations
    int branchesPerVariation = 4;     // How many instances of each variation
    float minLength = 0.5f;           // Minimum branch length
    float maxLength = 4.0f;           // Maximum branch length
    float minRadius = 0.03f;          // Minimum branch radius
    float maxRadius = 0.25f;          // Maximum branch radius
    float placementRadius = 8.0f;     // Max distance from tree to place debris
    float minDistanceBetween = 1.0f;  // Minimum distance between pieces
    float materialRoughness = 0.85f;  // PBR roughness for rendering
    float materialMetallic = 0.0f;    // PBR metallic for rendering
    int maxTotal = 100;               // Maximum total detritus pieces
    float minElevation = 24.0f;       // Minimum terrain elevation for placement
};

/**
 * Create a detritus scatter system (fallen branches).
 *
 * Generates branch meshes and places them near provided tree positions.
 */
std::unique_ptr<ScatterSystem> createDetritus(
    const ScatterSystem::InitInfo& info,
    const DetritusConfig& config,
    const std::vector<glm::vec3>& treePositions);

} // namespace ScatterSystemFactory
