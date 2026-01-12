#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <functional>
#include "Components.h"
#include "SceneGraphSystem.h"

// Environment-ECS Integration System
// Bridges ECS environment components with terrain, grass, water, and vegetation systems
namespace EnvironmentECS {

// ============================================================================
// Terrain Integration
// ============================================================================

// Create a terrain configuration entity (singleton)
inline entt::entity createTerrainConfig(
    entt::registry& registry,
    float totalSize = 16384.0f,
    float heightScale = 500.0f)
{
    auto entity = registry.create();

    TerrainConfig config;
    config.totalSize = totalSize;
    config.heightScale = heightScale;
    registry.emplace<TerrainConfig>(entity, config);

    EntityInfo info;
    info.name = "Terrain Config";
    info.icon = "T";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a terrain patch entity
inline entt::entity createTerrainPatch(
    entt::registry& registry,
    int32_t tileX,
    int32_t tileZ,
    uint32_t lod,
    float worldSize = 64.0f)
{
    auto entity = registry.create();

    TerrainPatch patch;
    patch.tileX = tileX;
    patch.tileZ = tileZ;
    patch.lod = lod;
    patch.worldSize = worldSize;
    registry.emplace<TerrainPatch>(entity, patch);

    // Calculate world position from tile coords
    float worldX = tileX * worldSize;
    float worldZ = tileZ * worldSize;
    registry.emplace<Transform>(entity, Transform{glm::vec3(worldX, 0.0f, worldZ), 0.0f});

    // Add bounds
    AABBBounds bounds;
    bounds.min = glm::vec3(0.0f, -100.0f, 0.0f);
    bounds.max = glm::vec3(worldSize, 500.0f, worldSize);
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = "TerrainPatch_" + std::to_string(tileX) + "_" + std::to_string(tileZ);
    info.icon = "T";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Get terrain config entity
inline entt::entity getTerrainConfig(entt::registry& registry) {
    auto view = registry.view<TerrainConfig>();
    for (auto entity : view) {
        return entity;
    }
    return entt::null;
}

// Get all terrain patches
inline std::vector<entt::entity> getTerrainPatches(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<TerrainPatch>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Find terrain patch at tile coordinates
inline entt::entity findTerrainPatch(entt::registry& registry, int32_t tileX, int32_t tileZ) {
    auto view = registry.view<TerrainPatch>();
    for (auto entity : view) {
        auto& patch = view.get<TerrainPatch>(entity);
        if (patch.tileX == tileX && patch.tileZ == tileZ) {
            return entity;
        }
    }
    return entt::null;
}

// ============================================================================
// Grass Integration
// ============================================================================

// Create a grass volume entity
inline entt::entity createGrassVolume(
    entt::registry& registry,
    const glm::vec3& center,
    const glm::vec3& extents,
    float density = 1.0f,
    const std::string& name = "GrassVolume")
{
    auto entity = registry.create();

    GrassVolume grass;
    grass.center = center;
    grass.extents = extents;
    grass.density = density;
    registry.emplace<GrassVolume>(entity, grass);

    registry.emplace<Transform>(entity, Transform{center, 0.0f});

    AABBBounds bounds;
    bounds.min = -extents;
    bounds.max = extents;
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = name;
    info.icon = "G";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a grass tile entity (for tiled system)
inline entt::entity createGrassTile(
    entt::registry& registry,
    int32_t tileX,
    int32_t tileZ,
    uint32_t lod)
{
    auto entity = registry.create();

    GrassTile tile;
    tile.tileX = tileX;
    tile.tileZ = tileZ;
    tile.lod = lod;
    registry.emplace<GrassTile>(entity, tile);

    // Calculate tile size based on LOD
    float tileSize = (lod == 0) ? 64.0f : (lod == 1) ? 128.0f : 256.0f;
    float worldX = tileX * tileSize;
    float worldZ = tileZ * tileSize;

    registry.emplace<Transform>(entity, Transform{glm::vec3(worldX, 0.0f, worldZ), 0.0f});

    EntityInfo info;
    info.name = "GrassTile_" + std::to_string(tileX) + "_" + std::to_string(tileZ) + "_LOD" + std::to_string(lod);
    info.icon = "g";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Get all grass volumes
inline std::vector<entt::entity> getGrassVolumes(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<GrassVolume>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get grass tiles in view
inline std::vector<entt::entity> getGrassTilesInView(
    entt::registry& registry,
    const glm::vec3& cameraPos,
    float viewDistance)
{
    std::vector<entt::entity> result;
    auto view = registry.view<GrassTile, Transform>();

    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        float dist = glm::length(transform.position - cameraPos);
        if (dist < viewDistance) {
            result.push_back(entity);
        }
    }
    return result;
}

// ============================================================================
// Water Integration
// ============================================================================

// Create a water surface entity
inline entt::entity createWaterSurface(
    entt::registry& registry,
    const glm::vec3& position,
    const glm::vec3& extents,
    WaterType type = WaterType::Lake,
    const std::string& name = "WaterSurface")
{
    auto entity = registry.create();

    WaterSurface water;
    water.type = type;
    water.height = position.y;
    registry.emplace<WaterSurface>(entity, water);

    registry.emplace<Transform>(entity, Transform{position, 0.0f});

    AABBBounds bounds;
    bounds.min = glm::vec3(-extents.x, -water.depth, -extents.z);
    bounds.max = glm::vec3(extents.x, 0.0f, extents.z);
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = name;
    info.icon = "W";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a river entity
inline entt::entity createRiver(
    entt::registry& registry,
    const std::vector<glm::vec3>& controlPoints,
    const std::vector<float>& widths,
    float flowSpeed = 2.0f,
    const std::string& name = "River")
{
    auto entity = registry.create();

    RiverSpline river;
    river.controlPoints = controlPoints;
    river.widths = widths;
    river.flowSpeed = flowSpeed;
    registry.emplace<RiverSpline>(entity, river);

    // Position at first control point
    if (!controlPoints.empty()) {
        registry.emplace<Transform>(entity, Transform{controlPoints[0], 0.0f});
    }

    EntityInfo info;
    info.name = name;
    info.icon = "~";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a lake entity
inline entt::entity createLake(
    entt::registry& registry,
    const glm::vec3& center,
    float radius,
    float depth = 10.0f,
    const std::string& name = "Lake")
{
    auto entity = registry.create();

    LakeBody lake;
    lake.center = center;
    lake.radius = radius;
    lake.depth = depth;
    registry.emplace<LakeBody>(entity, lake);

    registry.emplace<Transform>(entity, Transform{center, 0.0f});

    // Circular bounds approximated as AABB
    AABBBounds bounds;
    bounds.min = glm::vec3(-radius, -depth, -radius);
    bounds.max = glm::vec3(radius, 0.0f, radius);
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = name;
    info.icon = "O";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Get all water surfaces
inline std::vector<entt::entity> getWaterSurfaces(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<WaterSurface>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Check if position is underwater
inline bool isUnderwater(entt::registry& registry, const glm::vec3& position) {
    auto view = registry.view<WaterSurface, Transform>();
    for (auto entity : view) {
        auto& water = view.get<WaterSurface>(entity);
        auto& transform = view.get<Transform>(entity);

        // Simple height check
        if (position.y < transform.position.y + water.height) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Vegetation Integration
// ============================================================================

// Create a tree entity
inline entt::entity createTree(
    entt::registry& registry,
    const glm::vec3& position,
    TreeArchetype archetype = TreeArchetype::Oak,
    float scale = 1.0f,
    float rotation = 0.0f,
    const std::string& name = "Tree")
{
    auto entity = registry.create();

    TreeInstance tree;
    tree.archetype = archetype;
    tree.scale = scale;
    tree.rotation = rotation;
    registry.emplace<TreeInstance>(entity, tree);

    registry.emplace<Transform>(entity, Transform{position, rotation});

    // Add LOD state
    registry.emplace<TreeLODState>(entity);

    // Approximate bounds based on scale
    AABBBounds bounds;
    bounds.min = glm::vec3(-2.0f * scale, 0.0f, -2.0f * scale);
    bounds.max = glm::vec3(2.0f * scale, 15.0f * scale, 2.0f * scale);
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = name;
    info.icon = "Y";  // Tree-like icon
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a vegetation zone entity
inline entt::entity createVegetationZone(
    entt::registry& registry,
    const glm::vec3& center,
    const glm::vec3& extents,
    float treeDensity = 0.1f,
    const std::string& name = "VegetationZone")
{
    auto entity = registry.create();

    VegetationZone zone;
    zone.center = center;
    zone.extents = extents;
    zone.treeDensity = treeDensity;
    zone.allowedTrees = {TreeArchetype::Oak, TreeArchetype::Pine, TreeArchetype::Birch};
    registry.emplace<VegetationZone>(entity, zone);

    registry.emplace<Transform>(entity, Transform{center, 0.0f});

    AABBBounds bounds;
    bounds.min = -extents;
    bounds.max = extents;
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = name;
    info.icon = "V";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a rock entity
inline entt::entity createRock(
    entt::registry& registry,
    const glm::vec3& position,
    uint32_t variant = 0,
    float scale = 1.0f,
    const glm::vec3& rotation = glm::vec3(0.0f),
    const std::string& name = "Rock")
{
    auto entity = registry.create();

    RockInstance rock;
    rock.meshVariant = variant;
    rock.scale = scale;
    rock.rotation = rotation;
    registry.emplace<RockInstance>(entity, rock);

    registry.emplace<Transform>(entity, Transform{position, rotation.y});
    registry.emplace<StaticObject>(entity);

    AABBBounds bounds;
    bounds.min = glm::vec3(-1.0f * scale);
    bounds.max = glm::vec3(1.0f * scale);
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = name;
    info.icon = "R";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Get all tree entities
inline std::vector<entt::entity> getTrees(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<TreeInstance>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get trees by archetype
inline std::vector<entt::entity> getTreesByArchetype(entt::registry& registry, TreeArchetype archetype) {
    std::vector<entt::entity> result;
    auto view = registry.view<TreeInstance>();
    for (auto entity : view) {
        if (view.get<TreeInstance>(entity).archetype == archetype) {
            result.push_back(entity);
        }
    }
    return result;
}

// ============================================================================
// Wind Zone Integration
// ============================================================================

// Create a wind zone entity
inline entt::entity createWindZone(
    entt::registry& registry,
    const glm::vec3& center,
    const glm::vec3& direction,
    float strength = 1.0f,
    const glm::vec3& extents = glm::vec3(50.0f),
    const std::string& name = "WindZone")
{
    auto entity = registry.create();

    WindZone wind;
    wind.direction = glm::normalize(direction);
    wind.strength = strength;
    wind.extents = extents;
    registry.emplace<WindZone>(entity, wind);

    registry.emplace<Transform>(entity, Transform{center, 0.0f});

    AABBBounds bounds;
    bounds.min = -extents;
    bounds.max = extents;
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = name;
    info.icon = ">";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create global wind (affects entire scene)
inline entt::entity createGlobalWind(
    entt::registry& registry,
    const glm::vec3& direction,
    float strength = 1.0f)
{
    auto entity = registry.create();

    WindZone wind;
    wind.direction = glm::normalize(direction);
    wind.strength = strength;
    wind.isGlobal = true;
    registry.emplace<WindZone>(entity, wind);

    EntityInfo info;
    info.name = "Global Wind";
    info.icon = "W";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Get wind at position (combines all affecting wind zones)
inline glm::vec3 getWindAtPosition(entt::registry& registry, const glm::vec3& position) {
    glm::vec3 totalWind(0.0f);

    auto view = registry.view<WindZone>();
    for (auto entity : view) {
        auto& wind = view.get<WindZone>(entity);

        if (wind.isGlobal) {
            totalWind += wind.direction * wind.strength;
            continue;
        }

        // Check if position is inside wind zone
        if (registry.all_of<Transform>(entity)) {
            auto& transform = registry.get<Transform>(entity);
            glm::vec3 localPos = position - transform.position;

            if (glm::abs(localPos.x) <= wind.extents.x &&
                glm::abs(localPos.y) <= wind.extents.y &&
                glm::abs(localPos.z) <= wind.extents.z)
            {
                totalWind += wind.direction * wind.strength;
            }
        }
    }

    return totalWind;
}

// ============================================================================
// Weather Zone Integration
// ============================================================================

// Create a weather zone entity
inline entt::entity createWeatherZone(
    entt::registry& registry,
    const glm::vec3& center,
    const glm::vec3& extents,
    WeatherZone::Type type,
    float intensity = 1.0f,
    const std::string& name = "WeatherZone")
{
    auto entity = registry.create();

    WeatherZone weather;
    weather.type = type;
    weather.intensity = intensity;
    weather.extents = extents;
    registry.emplace<WeatherZone>(entity, weather);

    registry.emplace<Transform>(entity, Transform{center, 0.0f});

    AABBBounds bounds;
    bounds.min = -extents;
    bounds.max = extents;
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = name;
    info.icon = "C";  // Cloud icon
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a fog volume entity
inline entt::entity createFogVolume(
    entt::registry& registry,
    const glm::vec3& center,
    const glm::vec3& extents,
    float density = 0.05f,
    const glm::vec3& color = glm::vec3(0.5f, 0.6f, 0.7f),
    const std::string& name = "FogVolume")
{
    auto entity = registry.create();

    FogVolume fog;
    fog.extents = extents;
    fog.density = density;
    fog.color = color;
    registry.emplace<FogVolume>(entity, fog);

    registry.emplace<Transform>(entity, Transform{center, 0.0f});

    AABBBounds bounds;
    bounds.min = -extents;
    bounds.max = extents;
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = name;
    info.icon = "F";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// ============================================================================
// LOD Updates
// ============================================================================

// Update tree LOD states based on camera position
inline void updateTreeLODs(
    entt::registry& registry,
    const glm::vec3& cameraPos,
    float fullDetailDistance = 50.0f,
    float impostorDistance = 200.0f)
{
    auto view = registry.view<TreeInstance, TreeLODState, Transform>();

    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& lodState = view.get<TreeLODState>(entity);

        float dist = glm::length(transform.position - cameraPos);
        lodState.distanceToCamera = dist;

        if (dist < fullDetailDistance) {
            lodState.level = TreeLODState::Level::FullDetail;
            lodState.blendFactor = 0.0f;
        }
        else if (dist > impostorDistance) {
            lodState.level = TreeLODState::Level::Impostor;
            lodState.blendFactor = 1.0f;
        }
        else {
            // Blending zone
            lodState.level = TreeLODState::Level::Blending;
            lodState.blendFactor = (dist - fullDetailDistance) / (impostorDistance - fullDetailDistance);
        }
    }
}

// ============================================================================
// Debug Utilities
// ============================================================================

// Environment stats
struct EnvironmentStats {
    int terrainPatches;
    int grassVolumes;
    int grassTiles;
    int waterSurfaces;
    int rivers;
    int lakes;
    int trees;
    int rocks;
    int windZones;
    int weatherZones;
    int fogVolumes;
};

inline EnvironmentStats getEnvironmentStats(entt::registry& registry) {
    EnvironmentStats stats{};

    stats.terrainPatches = static_cast<int>(registry.view<TerrainPatch>().size());
    stats.grassVolumes = static_cast<int>(registry.view<GrassVolume>().size());
    stats.grassTiles = static_cast<int>(registry.view<GrassTile>().size());
    stats.waterSurfaces = static_cast<int>(registry.view<WaterSurface>().size());
    stats.rivers = static_cast<int>(registry.view<RiverSpline>().size());
    stats.lakes = static_cast<int>(registry.view<LakeBody>().size());
    stats.trees = static_cast<int>(registry.view<TreeInstance>().size());
    stats.rocks = static_cast<int>(registry.view<RockInstance>().size());
    stats.windZones = static_cast<int>(registry.view<WindZone>().size());
    stats.weatherZones = static_cast<int>(registry.view<WeatherZone>().size());
    stats.fogVolumes = static_cast<int>(registry.view<FogVolume>().size());

    return stats;
}

// Get water type name
inline const char* getWaterTypeName(WaterType type) {
    switch (type) {
        case WaterType::Ocean: return "Ocean";
        case WaterType::CoastalOcean: return "Coastal Ocean";
        case WaterType::River: return "River";
        case WaterType::MuddyRiver: return "Muddy River";
        case WaterType::ClearStream: return "Clear Stream";
        case WaterType::Lake: return "Lake";
        case WaterType::Swamp: return "Swamp";
        case WaterType::Tropical: return "Tropical";
        case WaterType::Custom: return "Custom";
        default: return "Unknown";
    }
}

// Get tree archetype name
inline const char* getTreeArchetypeName(TreeArchetype archetype) {
    switch (archetype) {
        case TreeArchetype::Oak: return "Oak";
        case TreeArchetype::Pine: return "Pine";
        case TreeArchetype::Ash: return "Ash";
        case TreeArchetype::Aspen: return "Aspen";
        case TreeArchetype::Birch: return "Birch";
        case TreeArchetype::Custom: return "Custom";
        default: return "Unknown";
    }
}

// Get weather type name
inline const char* getWeatherTypeName(WeatherZone::Type type) {
    switch (type) {
        case WeatherZone::Type::Clear: return "Clear";
        case WeatherZone::Type::Cloudy: return "Cloudy";
        case WeatherZone::Type::Rain: return "Rain";
        case WeatherZone::Type::Snow: return "Snow";
        case WeatherZone::Type::Fog: return "Fog";
        case WeatherZone::Type::Storm: return "Storm";
        default: return "Unknown";
    }
}

}  // namespace EnvironmentECS
