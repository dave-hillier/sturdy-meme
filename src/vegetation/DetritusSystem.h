#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <entt/entt.hpp>

#include "BranchGenerator.h"
#include "Mesh.h"
#include "Texture.h"
#include "RenderableBuilder.h"
#include "scene/SceneObjectCollection.h"
#include "scene/SceneObjectInstance.h"
#include "scene/DeterministicRandom.h"
#include <optional>

// Configuration for detritus generation and placement
struct DetritusConfig {
    int branchVariations = 8;         // Number of unique fallen branch mesh variations
    int forkedVariations = 4;         // Number of Y-shaped forked branch variations
    int branchesPerVariation = 4;     // How many instances of each variation
    float minLength = 0.5f;           // Minimum branch length
    float maxLength = 4.0f;           // Maximum branch length (larger for variation)
    float minRadius = 0.03f;          // Minimum branch radius
    float maxRadius = 0.25f;          // Maximum branch radius (thicker branches)
    float placementRadius = 60.0f;    // Radius from origin to place detritus
    float minDistanceBetween = 1.0f;  // Minimum distance between pieces
    float breakChance = 0.7f;         // Chance for branch to have a break point
    int maxChildren = 3;              // Maximum child branches
    float materialRoughness = 0.85f;  // PBR roughness for rendering
    float materialMetallic = 0.0f;    // PBR metallic for rendering
};

class DetritusSystem {
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
        std::vector<glm::vec3> treePositions;  // Tree positions to scatter detritus near
        entt::registry* registry = nullptr;  // ECS registry for detritus entities
    };

    /**
     * Factory: Create and initialize DetritusSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<DetritusSystem> create(const InitInfo& info, const DetritusConfig& config = {});

    ~DetritusSystem();

    // Non-copyable, non-movable
    DetritusSystem(const DetritusSystem&) = delete;
    DetritusSystem& operator=(const DetritusSystem&) = delete;
    DetritusSystem(DetritusSystem&&) = delete;
    DetritusSystem& operator=(DetritusSystem&&) = delete;

    // Get scene objects for rendering (integrated with existing pipeline)
    const std::vector<Renderable>& getSceneObjects() const { return collection_.getSceneObjects(); }
    std::vector<Renderable>& getSceneObjects() { return collection_.getSceneObjects(); }

    // Access to textures for descriptor set binding
    Texture& getBarkTexture() { return *collection_.getDiffuseTexture(); }
    Texture& getBarkNormalMap() { return *collection_.getNormalTexture(); }

    // Get count for statistics
    size_t getDetritusCount() const { return collection_.getInstanceCount(); }
    size_t getMeshVariationCount() const { return collection_.getMeshVariationCount(); }

    // Get instances for physics integration (returns unified SceneObjectInstance)
    const std::vector<SceneObjectInstance>& getInstances() const { return collection_.getInstances(); }

    // Get meshes for physics collision shapes
    const std::vector<Mesh>& getMeshes() const { return collection_.getMeshes(); }

private:
    DetritusSystem() = default;  // Private: use factory

    bool initInternal(const InitInfo& info, const DetritusConfig& config);
    void cleanup();
    bool createBranchMeshes(const InitInfo& info);
    bool loadTextures(const InitInfo& info);
    void generatePlacements(const InitInfo& info);
    void createSceneObjects();

    DetritusConfig config_;
    BranchGenerator generator_;

    // Scene object collection (composition pattern)
    SceneObjectCollection collection_;
};
