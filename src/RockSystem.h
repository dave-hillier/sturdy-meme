#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>

#include "Mesh.h"
#include "Texture.h"
#include "RenderableBuilder.h"

// Configuration for rock generation and placement
struct RockConfig {
    int rockVariations = 5;           // Number of unique rock mesh variations
    int rocksPerVariation = 8;        // How many instances of each variation
    float minRadius = 0.3f;           // Minimum rock base radius
    float maxRadius = 1.5f;           // Maximum rock base radius
    float placementRadius = 80.0f;    // Radius from origin to place rocks
    float minDistanceBetween = 3.0f;  // Minimum distance between rocks
    float roughness = 0.35f;          // Surface roughness for rock generation
    float asymmetry = 0.25f;          // How non-spherical rocks should be
    int subdivisions = 3;             // Icosphere subdivision level (3 = ~320 triangles)
    float materialRoughness = 0.7f;   // PBR roughness for rendering
    float materialMetallic = 0.0f;    // PBR metallic for rendering
};

// A single rock instance in the scene
struct RockInstance {
    glm::vec3 position;
    float rotation;         // Y-axis rotation
    float scale;            // Uniform scale factor
    int meshVariation;      // Which mesh to use
};

class RockSystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
        VkPhysicalDevice physicalDevice;
        std::string resourcePath;
        std::function<float(float, float)> getTerrainHeight;  // Terrain height query
        float terrainSize;
    };

    RockSystem() = default;
    ~RockSystem() = default;

    bool init(const InitInfo& info, const RockConfig& config = {});
    void destroy(VmaAllocator allocator, VkDevice device);

    // Get scene objects for rendering (integrated with existing pipeline)
    const std::vector<Renderable>& getSceneObjects() const { return sceneObjects; }
    std::vector<Renderable>& getSceneObjects() { return sceneObjects; }

    // Access to textures for descriptor set binding
    Texture& getRockTexture() { return rockTexture; }
    Texture& getRockNormalMap() { return rockNormalMap; }

    // Get rock count for statistics
    size_t getRockCount() const { return rockInstances.size(); }
    size_t getMeshVariationCount() const { return rockMeshes.size(); }

private:
    bool createRockMeshes(const InitInfo& info);
    bool loadTextures(const InitInfo& info);
    void generateRockPlacements(const InitInfo& info);
    void createSceneObjects();

    // Hash function for deterministic random placement
    float hashPosition(float x, float z, uint32_t seed) const;

    RockConfig config;

    // Rock mesh variations
    std::vector<Mesh> rockMeshes;

    // Rock textures
    Texture rockTexture;
    Texture rockNormalMap;

    // Rock instances (positions, rotations, etc.)
    std::vector<RockInstance> rockInstances;

    // Scene objects for rendering
    std::vector<Renderable> sceneObjects;
};
