#pragma once

#include "NPCData.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <functional>

class AnimatedCharacter;
class Renderable;

// Forward declare for height query function
using HeightQueryFunc = std::function<float(float, float)>;

// NPC Simulation System
// Handles behavior, state updates, and LOD-based scheduling for NPCs
// Separated from rendering for clean architecture
class NPCSimulation {
public:
    // Passkey for controlled construction
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit NPCSimulation(ConstructToken);

    struct InitInfo {
        VmaAllocator allocator;
        VkDevice device;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
        std::string resourcePath;
        HeightQueryFunc getTerrainHeight;  // Query terrain height for placement
        glm::vec2 sceneOrigin = glm::vec2(0.0f);  // World XZ offset for scene objects
    };

    // Spawn info for creating NPCs
    struct NPCSpawnInfo {
        float x, z;        // Position offset from scene origin
        float yawDegrees;  // Facing direction
        uint32_t templateIndex = 0;  // Which character template to use
        NPCActivity activity = NPCActivity::Idle;  // Initial activity state
    };

    /**
     * Factory: Create and initialize NPCSimulation.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<NPCSimulation> create(const InitInfo& info);

    ~NPCSimulation();

    // Non-copyable, non-movable
    NPCSimulation(const NPCSimulation&) = delete;
    NPCSimulation& operator=(const NPCSimulation&) = delete;
    NPCSimulation(NPCSimulation&&) = delete;
    NPCSimulation& operator=(NPCSimulation&&) = delete;

    // Spawn NPCs at predefined positions (called during scene setup)
    // Returns number of NPCs successfully created
    size_t spawnNPCs(const std::vector<NPCSpawnInfo>& spawnPoints);

    // Update all NPCs (call each frame)
    // cameraPos: used for LOD level calculation
    void update(float deltaTime, const glm::vec3& cameraPos);

    // Access to NPC data (read-only for renderer)
    const NPCData& getData() const { return data_; }

    // Access to NPC data (for renderable setup)
    NPCData& getData() { return data_; }

    // Get animated character for a specific NPC (for rendering)
    AnimatedCharacter* getCharacter(size_t npcIndex);
    const AnimatedCharacter* getCharacter(size_t npcIndex) const;

    // Check if NPCs are available
    bool hasNPCs() const { return data_.count() > 0; }
    size_t getNPCCount() const { return data_.count(); }

    // Build world transform matrix for an NPC
    glm::mat4 buildNPCTransform(size_t npcIndex) const;

    // Set renderable index for an NPC (called after adding to scene)
    void setRenderableIndex(size_t npcIndex, size_t renderableIndex);

    // LOD configuration (uses CharacterLODConfig thresholds)
    void setLODEnabled(bool enabled) { lodEnabled_ = enabled; }
    bool isLODEnabled() const { return lodEnabled_; }

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    // Update LOD levels based on camera distance
    void updateLODLevels(const glm::vec3& cameraPos);

    // LOD-tiered update functions
    void updateVirtualNPCs(float deltaTime);  // >50m: minimal updates
    void updateBulkNPCs(float deltaTime);     // 25-50m: reduced updates
    void updateRealNPCs(float deltaTime);     // <25m: full updates

    // Update a single NPC's animation
    void updateNPCAnimation(size_t npcIndex, float deltaTime);

    // Build character transform from position and rotation
    glm::mat4 buildCharacterTransform(const glm::vec3& position, float yawRadians) const;

    // Stored initialization data
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    std::string resourcePath_;
    HeightQueryFunc terrainHeightFunc_;
    glm::vec2 sceneOrigin_ = glm::vec2(0.0f);

    // NPC data (Structure-of-Arrays)
    NPCData data_;

    // Character instances (one per NPC for now, will become template references in Stage 4)
    std::vector<std::unique_ptr<AnimatedCharacter>> characters_;

    // LOD configuration
    bool lodEnabled_ = true;

    // LOD distance thresholds (matching CharacterLODConfig)
    static constexpr float LOD_DISTANCE_REAL = 25.0f;     // Full quality
    static constexpr float LOD_DISTANCE_BULK = 50.0f;     // Reduced quality
    // Beyond LOD_DISTANCE_BULK = Virtual (minimal updates)

    // LOD update intervals (in frames)
    static constexpr uint32_t UPDATE_INTERVAL_REAL = 1;    // Every frame
    static constexpr uint32_t UPDATE_INTERVAL_BULK = 60;   // ~1 second at 60fps
    static constexpr uint32_t UPDATE_INTERVAL_VIRTUAL = 600; // ~10 seconds at 60fps
};
