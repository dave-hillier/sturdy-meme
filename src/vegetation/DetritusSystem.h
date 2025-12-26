#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <memory>

#include "BranchGenerator.h"
#include "Mesh.h"
#include "Texture.h"
#include "RenderableBuilder.h"
#include "RAIIAdapter.h"
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

// A single detritus instance in the scene
struct DetritusInstance {
    glm::vec3 position;
    glm::vec3 rotation;     // Euler angles (x, y, z)
    float scale;            // Uniform scale factor
    int meshVariation;      // Which mesh to use
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
    const std::vector<Renderable>& getSceneObjects() const { return sceneObjects_; }
    std::vector<Renderable>& getSceneObjects() { return sceneObjects_; }

    // Access to textures for descriptor set binding
    Texture& getBarkTexture() { return **barkTexture_; }
    Texture& getBarkNormalMap() { return **barkNormalMap_; }

    // Get count for statistics
    size_t getDetritusCount() const { return instances_.size(); }
    size_t getMeshVariationCount() const { return meshes_.size(); }

    // Get instances for physics integration
    const std::vector<DetritusInstance>& getInstances() const { return instances_; }

    // Get meshes for physics collision shapes
    const std::vector<Mesh>& getMeshes() const { return meshes_; }

private:
    DetritusSystem() = default;  // Private: use factory

    bool initInternal(const InitInfo& info, const DetritusConfig& config);
    void cleanup();
    bool createBranchMeshes(const InitInfo& info);
    bool loadTextures(const InitInfo& info);
    void generatePlacements(const InitInfo& info);
    void createSceneObjects();

    // Generate mesh from branch data
    bool generateMeshFromBranches(const GeneratedBranch& branchData, Mesh& outMesh,
                                  const InitInfo& info);

    // Hash function for deterministic random placement
    float hashPosition(float x, float z, uint32_t seed) const;

    DetritusConfig config_;
    BranchGenerator generator_;

    // Stored for RAII cleanup
    VmaAllocator storedAllocator_ = VK_NULL_HANDLE;
    VkDevice storedDevice_ = VK_NULL_HANDLE;

    // Branch mesh variations
    std::vector<Mesh> meshes_;

    // Textures (RAII-managed)
    std::optional<RAIIAdapter<Texture>> barkTexture_;
    std::optional<RAIIAdapter<Texture>> barkNormalMap_;

    // Detritus instances (positions, rotations, etc.)
    std::vector<DetritusInstance> instances_;

    // Scene objects for rendering
    std::vector<Renderable> sceneObjects_;
};
