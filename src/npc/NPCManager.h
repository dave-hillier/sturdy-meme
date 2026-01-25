#pragma once

#include "NPC.h"
#include <vector>
#include <memory>
#include <functional>

class PhysicsWorld;
class AnimatedCharacter;
class SkinnedMeshRenderer;

// Callback for NPC events
using NPCEventCallback = std::function<void(NPCID npcId, const std::string& event)>;

// Manager class for all NPCs in the scene
class NPCManager {
public:
    NPCManager() = default;
    ~NPCManager() = default;

    // Non-copyable, movable
    NPCManager(const NPCManager&) = delete;
    NPCManager& operator=(const NPCManager&) = delete;
    NPCManager(NPCManager&&) = default;
    NPCManager& operator=(NPCManager&&) = default;

    // Spawn a new NPC and return its ID
    NPCID spawn(const NPCSpawnInfo& info);

    // Remove an NPC
    void remove(NPCID id);

    // Update all NPCs (call each frame)
    void update(float deltaTime, const glm::vec3& playerPosition, PhysicsWorld* physics);

    // Get NPC by ID (returns nullptr if not found)
    NPC* getNPC(NPCID id);
    const NPC* getNPC(NPCID id) const;

    // Get all NPCs
    std::vector<NPC>& getNPCs() { return npcs_; }
    const std::vector<NPC>& getNPCs() const { return npcs_; }

    // Get NPCs within a radius of a position
    std::vector<NPC*> getNPCsInRadius(const glm::vec3& position, float radius);

    // Get hostile NPCs that can see the player
    std::vector<NPC*> getActiveHostiles();

    // Apply damage to all NPCs within a radius (e.g., from explosion)
    void applyAreaDamage(const glm::vec3& center, float radius, float damage,
                         const glm::vec3& attackerPosition);

    // Apply damage to a single NPC
    void applyDamage(NPC& npc, float damage, const glm::vec3& attackerPosition);

    // Set event callback
    void setEventCallback(NPCEventCallback callback) { eventCallback_ = std::move(callback); }

    // Get count of NPCs
    size_t getCount() const { return npcs_.size(); }
    size_t getAliveCount() const;
    size_t getHostileCount() const;

    // Clear all NPCs
    void clear();

    // Debug: Get summary string
    std::string getDebugSummary() const;

    // Animation/Rendering support
    // Set the shared character reference (uses player's AnimatedCharacter for mesh/skeleton)
    void setSharedCharacter(AnimatedCharacter* character) { sharedCharacter_ = character; }
    AnimatedCharacter* getSharedCharacter() const { return sharedCharacter_; }

    // Update animation and compute bone matrices for all NPCs
    // Call this after behavior update, before rendering
    void updateAnimations(float deltaTime, SkinnedMeshRenderer& renderer, uint32_t frameIndex);

    // Render all NPCs using the skinned mesh renderer
    void render(VkCommandBuffer cmd, uint32_t frameIndex, SkinnedMeshRenderer& renderer);

    // LOD system configuration
    void setLODConfig(const NPCLODConfig& config) { lodConfig_ = config; }
    const NPCLODConfig& getLODConfig() const { return lodConfig_; }

    // Time of day for schedule system (0-24 hours)
    void setTimeOfDay(float hour) { timeOfDay_ = hour; }
    float getTimeOfDay() const { return timeOfDay_; }

    // Game time scale (how fast time passes, 1.0 = real time)
    void setTimeScale(float scale) { timeScale_ = scale; }

    // Systemic events
    const std::vector<SystemicEvent>& getActiveEvents() const { return activeEvents_; }
    SystemicEvent* getEventAt(const glm::vec3& position, float radius);

    // LOD statistics
    size_t getVirtualCount() const;
    size_t getBulkCount() const;
    size_t getRealCount() const;

private:
    std::vector<NPC> npcs_;
    NPCID nextId_ = 1;
    NPCEventCallback eventCallback_;
    AnimatedCharacter* sharedCharacter_ = nullptr;  // Shared mesh/skeleton from player

    // LOD system
    NPCLODConfig lodConfig_;
    float timeOfDay_ = 12.0f;      // Current hour (0-24)
    float timeScale_ = 60.0f;      // Game minutes per real second (default: 1 hour = 1 minute)

    // Systemic events
    std::vector<SystemicEvent> activeEvents_;
    float eventSpawnTimer_ = 0.0f;
    static constexpr float EVENT_CHECK_INTERVAL = 5.0f;  // Check for new events every 5s

    // Find NPC index by ID (returns -1 if not found)
    int findNPCIndex(NPCID id) const;

    // Fire an event
    void fireEvent(NPCID id, const std::string& event);

    // Compute bone matrices for a single NPC based on its animation state
    void computeNPCBoneMatrices(NPC& npc, std::vector<glm::mat4>& outBoneMatrices);

    // LOD system helpers
    void updateLODStates(const glm::vec3& playerPosition);
    void updateVirtualNPCs(float deltaTime, float gameHours);
    void updateBulkNPCs(float deltaTime, float gameHours, const glm::vec3& playerPosition);
    void updateRealNPCs(float deltaTime, float gameHours, const glm::vec3& playerPosition, PhysicsWorld* physics);

    // Needs and schedule helpers
    void updateNPCNeeds(NPC& npc, float gameHours);
    void updateNPCSchedule(NPC& npc);

    // Systemic event helpers
    void updateSystemicEvents(float deltaTime);
    void trySpawnSystemicEvent();
    bool canSpawnEvent(SystemicEventType type, const NPC& instigator) const;
};
