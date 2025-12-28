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
#include "RAIIAdapter.h"
#include <optional>

// Settlement types (matches BiomeGenerator)
enum class SettlementType : uint8_t {
    Hamlet = 0,
    Village = 1,
    Town = 2,
    FishingVillage = 3
};

// A single building instance in a settlement
struct BuildingInstance {
    glm::vec3 position;
    float rotation;         // Y-axis rotation
    glm::vec3 scale;        // Building dimensions (width, height, depth)
    int meshVariation;      // Which mesh to use
    uint32_t settlementId;  // Which settlement this belongs to
};

// Settlement data loaded from BiomeGenerator output
struct SettlementData {
    uint32_t id;
    SettlementType type;
    glm::vec2 position;     // World XZ coordinates
    float score;
    std::vector<std::string> features;
};

// Configuration for settlement generation
struct SettlementConfig {
    int buildingsPerHamlet = 3;
    int buildingsPerVillage = 8;
    int buildingsPerTown = 20;
    int buildingsPerFishingVillage = 5;
    float minBuildingWidth = 4.0f;
    float maxBuildingWidth = 8.0f;
    float minBuildingHeight = 3.0f;
    float maxBuildingHeight = 8.0f;
    float minBuildingDepth = 4.0f;
    float maxBuildingDepth = 10.0f;
    float buildingSpacing = 5.0f;       // Minimum distance between buildings
    float settlementRadius = 50.0f;     // Max radius from center to place buildings
    float materialRoughness = 0.8f;
    float materialMetallic = 0.0f;
};

class SettlementSystem {
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
     * Factory: Create and initialize SettlementSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<SettlementSystem> create(const InitInfo& info, const SettlementConfig& config = {});

    ~SettlementSystem();

    // Non-copyable, non-movable
    SettlementSystem(const SettlementSystem&) = delete;
    SettlementSystem& operator=(const SettlementSystem&) = delete;
    SettlementSystem(SettlementSystem&&) = delete;
    SettlementSystem& operator=(SettlementSystem&&) = delete;

    // Get scene objects for rendering (integrated with existing pipeline)
    const std::vector<Renderable>& getSceneObjects() const { return sceneObjects_; }
    std::vector<Renderable>& getSceneObjects() { return sceneObjects_; }

    // Access to textures for descriptor set binding
    Texture& getBuildingTexture() { return **buildingTexture_; }

    // Get counts for statistics
    size_t getBuildingCount() const { return buildingInstances_.size(); }
    size_t getSettlementCount() const { return settlements_.size(); }

    // Get building instances for physics integration
    const std::vector<BuildingInstance>& getBuildingInstances() const { return buildingInstances_; }

    // Get settlement data
    const std::vector<SettlementData>& getSettlements() const { return settlements_; }

    // Load settlements from BiomeGenerator JSON output
    bool loadSettlements(const std::string& jsonPath);

private:
    SettlementSystem() = default;  // Private: use factory

    bool initInternal(const InitInfo& info, const SettlementConfig& config);
    void cleanup();
    bool createBuildingMeshes(const InitInfo& info);
    bool loadTextures(const InitInfo& info);
    void generateBuildingPlacements(const InitInfo& info);
    void createSceneObjects();

    // Hash function for deterministic random placement
    float hashPosition(float x, float z, uint32_t seed) const;

    // Get number of buildings for a settlement type
    int getBuildingCount(SettlementType type) const;

    SettlementConfig config_;

    // Stored for RAII cleanup
    VmaAllocator storedAllocator_ = VK_NULL_HANDLE;
    VkDevice storedDevice_ = VK_NULL_HANDLE;

    // Building mesh (simple cube for now)
    Mesh buildingMesh_;

    // Building texture (RAII-managed)
    std::optional<RAIIAdapter<Texture>> buildingTexture_;

    // Settlement data
    std::vector<SettlementData> settlements_;

    // Building instances (positions, rotations, etc.)
    std::vector<BuildingInstance> buildingInstances_;

    // Scene objects for rendering
    std::vector<Renderable> sceneObjects_;

    // Terrain height callback
    std::function<float(float, float)> getTerrainHeight_;
};
