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

// Street segment within a settlement (M2.5: Street Network)
struct StreetSegment {
    glm::vec2 start;
    glm::vec2 end;
    float width = 5.0f;         // Street width in meters
    uint32_t settlementId;

    glm::vec2 getDirection() const {
        glm::vec2 d = end - start;
        float len = glm::length(d);
        return len > 0.001f ? d / len : glm::vec2(1.0f, 0.0f);
    }

    glm::vec2 getNormal() const {
        glm::vec2 dir = getDirection();
        return glm::vec2(-dir.y, dir.x);  // Left-hand perpendicular
    }

    float getLength() const {
        return glm::length(end - start);
    }
};

// A building lot aligned to street frontage (M2.5: Plot Subdivision)
// Medieval burgage plot: 5-10m wide x 30-60m deep
struct BuildingLot {
    glm::vec2 frontageCenter;   // Center of frontage edge (on street)
    glm::vec2 frontageDir;      // Direction along frontage (parallel to street)
    glm::vec2 depthDir;         // Direction into lot (perpendicular to street)
    float frontageWidth;        // Width along street (5-10m typical)
    float depth;                // Depth away from street (30-60m typical)
    uint32_t settlementId;
    uint32_t streetSegmentId;   // Which street this lot fronts

    // Get the four corners of the lot (CCW from front-left)
    void getCorners(glm::vec2 corners[4]) const {
        glm::vec2 halfFront = frontageDir * (frontageWidth * 0.5f);
        glm::vec2 depthVec = depthDir * depth;
        corners[0] = frontageCenter - halfFront;              // Front-left
        corners[1] = frontageCenter + halfFront;              // Front-right
        corners[2] = frontageCenter + halfFront + depthVec;   // Back-right
        corners[3] = frontageCenter - halfFront + depthVec;   // Back-left
    }

    glm::vec2 getCenter() const {
        return frontageCenter + depthDir * (depth * 0.5f);
    }
};

// A single building instance placed on a lot
struct BuildingInstance {
    glm::vec3 position;
    float rotation;         // Y-axis rotation (aligned to lot frontage)
    glm::vec3 scale;        // Building dimensions (width, height, depth)
    int meshVariation;      // Which mesh to use
    uint32_t settlementId;  // Which settlement this belongs to
    uint32_t lotId;         // Which lot this building is on
};

// Settlement data loaded from BiomeGenerator output
struct SettlementData {
    uint32_t id;
    SettlementType type;
    glm::vec2 position;     // World XZ coordinates (center)
    float score;
    std::vector<std::string> features;

    // Entry points where external roads connect (for space colonization seeds)
    std::vector<glm::vec2> entryPoints;
};

// Configuration for settlement generation
struct SettlementConfig {
    // Lot dimensions (medieval burgage plots)
    float minLotWidth = 5.0f;       // Min frontage width
    float maxLotWidth = 10.0f;      // Max frontage width
    float minLotDepth = 30.0f;      // Min depth from street
    float maxLotDepth = 60.0f;      // Max depth from street

    // Building dimensions (within lot constraints)
    float minBuildingWidth = 4.0f;
    float maxBuildingWidth = 8.0f;
    float minBuildingHeight = 3.0f;
    float maxBuildingHeight = 8.0f;
    float minBuildingDepth = 4.0f;
    float maxBuildingDepth = 10.0f;

    // Street network
    float mainStreetWidth = 6.0f;
    float backLaneWidth = 3.0f;
    float streetSpacing = 40.0f;    // Distance between parallel streets

    // Settlement scaling
    float settlementRadius = 50.0f;
    int lotsPerHamlet = 3;
    int lotsPerVillage = 8;
    int lotsPerTown = 20;
    int lotsPerFishingVillage = 5;

    // Materials
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
    size_t getLotCount() const { return lots_.size(); }
    size_t getStreetCount() const { return streets_.size(); }

    // Get building instances for physics integration
    const std::vector<BuildingInstance>& getBuildingInstances() const { return buildingInstances_; }

    // Get settlement data
    const std::vector<SettlementData>& getSettlements() const { return settlements_; }

    // Get lots and streets for debugging/visualization
    const std::vector<BuildingLot>& getLots() const { return lots_; }
    const std::vector<StreetSegment>& getStreets() const { return streets_; }

    // Load settlements from BiomeGenerator JSON output
    bool loadSettlements(const std::string& jsonPath);

private:
    SettlementSystem() = default;  // Private: use factory

    bool initInternal(const InitInfo& info, const SettlementConfig& config);
    void cleanup();
    bool createBuildingMeshes(const InitInfo& info);
    bool loadTextures(const InitInfo& info);

    // M2.5 Layout Generation Pipeline
    void generateSettlementLayout(const SettlementData& settlement);
    void generateStreetNetwork(const SettlementData& settlement);
    void subdivideFrontageIntoLots(const StreetSegment& street, bool leftSide);
    void placeBuildingOnLot(const BuildingLot& lot);

    void createSceneObjects();

    // Hash function for deterministic random placement
    float hashPosition(float x, float z, uint32_t seed) const;

    // Get number of lots for a settlement type
    int getLotCount(SettlementType type) const;

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

    // M2.5: Street network within settlements
    std::vector<StreetSegment> streets_;

    // M2.5: Building lots (subdivided from street frontage)
    std::vector<BuildingLot> lots_;

    // Building instances (placed on lots)
    std::vector<BuildingInstance> buildingInstances_;

    // Scene objects for rendering
    std::vector<Renderable> sceneObjects_;

    // Terrain height callback
    std::function<float(float, float)> getTerrainHeight_;
};
