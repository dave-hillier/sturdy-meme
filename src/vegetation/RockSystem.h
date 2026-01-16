#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <memory>

#include "Mesh.h"
#include "Texture.h"
#include "RenderableBuilder.h"
#include "scene/SceneMaterial.h"
#include "scene/SceneObjectInstance.h"
#include "scene/DeterministicRandom.h"
#include <optional>

// Configuration for rock generation and placement
struct RockConfig {
    int rockVariations = 5;           // Number of unique rock mesh variations
    int rocksPerVariation = 8;        // How many instances of each variation
    float minRadius = 0.3f;           // Minimum rock base radius
    float maxRadius = 1.5f;           // Maximum rock base radius
    float placementRadius = 80.0f;    // Radius from center to place rocks
    glm::vec2 placementCenter = glm::vec2(0.0f);  // Center point for rock placement (world coords)
    float minDistanceBetween = 3.0f;  // Minimum distance between rocks
    float roughness = 0.35f;          // Surface roughness for rock generation
    float asymmetry = 0.25f;          // How non-spherical rocks should be
    int subdivisions = 3;             // Icosphere subdivision level (3 = ~320 triangles)
    float materialRoughness = 0.7f;   // PBR roughness for rendering
    float materialMetallic = 0.0f;    // PBR metallic for rendering
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

    /**
     * Factory: Create and initialize RockSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<RockSystem> create(const InitInfo& info, const RockConfig& config = {});

    ~RockSystem();

    // Non-copyable, non-movable
    RockSystem(const RockSystem&) = delete;
    RockSystem& operator=(const RockSystem&) = delete;
    RockSystem(RockSystem&&) = delete;
    RockSystem& operator=(RockSystem&&) = delete;

    // Get scene objects for rendering (integrated with existing pipeline)
    const std::vector<Renderable>& getSceneObjects() const { return material_.getSceneObjects(); }
    std::vector<Renderable>& getSceneObjects() { return material_.getSceneObjects(); }

    // Access the underlying material for unified scene collection
    SceneMaterial& getMaterial() { return material_; }
    const SceneMaterial& getMaterial() const { return material_; }

    // Access to textures for descriptor set binding
    Texture& getRockTexture() { return *material_.getDiffuseTexture(); }
    Texture& getRockNormalMap() { return *material_.getNormalTexture(); }

    // Get rock count for statistics
    size_t getRockCount() const { return material_.getInstanceCount(); }
    size_t getMeshVariationCount() const { return material_.getMeshVariationCount(); }

    // Get rock instances for physics integration (returns unified SceneObjectInstance)
    const std::vector<SceneObjectInstance>& getRockInstances() const { return material_.getInstances(); }

    // Get rock meshes for physics collision shapes
    const std::vector<Mesh>& getRockMeshes() const { return material_.getMeshes(); }

private:
    RockSystem() = default;  // Private: use factory

    bool initInternal(const InitInfo& info, const RockConfig& config);
    void cleanup();
    bool createRockMeshes(const InitInfo& info);
    bool loadTextures(const InitInfo& info);
    void generateRockPlacements(const InitInfo& info);
    void createSceneObjects();

    RockConfig config_;

    // Scene material (composition pattern)
    SceneMaterial material_;
};
