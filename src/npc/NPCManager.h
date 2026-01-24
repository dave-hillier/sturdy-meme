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

private:
    std::vector<NPC> npcs_;
    NPCID nextId_ = 1;
    NPCEventCallback eventCallback_;
    AnimatedCharacter* sharedCharacter_ = nullptr;  // Shared mesh/skeleton from player

    // Find NPC index by ID (returns -1 if not found)
    int findNPCIndex(NPCID id) const;

    // Fire an event
    void fireEvent(NPCID id, const std::string& event);

    // Compute bone matrices for a single NPC based on its animation state
    void computeNPCBoneMatrices(NPC& npc, std::vector<glm::mat4>& outBoneMatrices);
};
