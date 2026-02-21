#pragma once

#include "calm/Controller.h"
#include "calm/LowLevelController.h"
#include "LatentSpace.h"
#include "CharacterConfig.h"
#include "../animation/AnimationArchetypeManager.h"
#include "../animation/CharacterLOD.h"
#include "../npc/NPCData.h"
#include "../physics/RagdollBuilder.h"
#include "../physics/RagdollInstance.h"
#include <memory>
#include <vector>
#include <unordered_map>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>

struct Skeleton;
class CharacterController;

namespace JPH {
    class PhysicsSystem;
}

namespace ml {

// Archetype — shared LLC and latent space for a character type.
// Multiple NPCs of the same archetype share the LLC weights and latent library
// (read-only), while each NPC owns its own controller state.
struct Archetype {
    uint32_t id = 0;
    std::string name;

    // Shared animation archetype (skeleton, clips for fallback)
    uint32_t animArchetypeId = AnimationArchetypeManager::INVALID_ARCHETYPE_ID;

    // Shared components (read-only at inference time)
    calm::LowLevelController llc;
    LatentSpace latentSpace;
    CharacterConfig config;

    // Shared ragdoll settings (ref-counted, built once per archetype)
    JPH::Ref<JPH::RagdollSettings> ragdollSettings;
    physics::RagdollConfig ragdollConfig;
};

// Per-NPC instance state — lightweight data owned by each NPC.
struct NPCInstance {
    uint32_t archetypeId = 0;
    calm::Controller controller;  // Per-NPC latent state + obs history

    // LOD control
    uint32_t lodLevel = 0;
    uint32_t framesSinceUpdate = 0;
    uint32_t lastUpdateFrame = 0;

    // Cached pose for LOD frame-skipping
    SkeletonPose cachedPose;
    std::vector<glm::mat4> cachedBoneMatrices;

    bool initialized = false;

    // Ragdoll physics (nullptr when in kinematic mode)
    std::unique_ptr<physics::RagdollInstance> ragdoll;
    bool usePhysics = false;  // Toggle kinematic vs physics-driven mode
};

// ArchetypeManager — manages character types and per-NPC instances.
//
// Workflow:
//   1. Create archetypes (loads shared LLC + latent library)
//   2. Spawn NPC instances referencing an archetype
//   3. Each frame: update all instances with LOD-aware scheduling
//
// Integrates with AnimationArchetypeManager for fallback clip animation
// and with CharacterLODConfig for update frequency control.
class ArchetypeManager {
public:
    ArchetypeManager() = default;

    // --- Archetype management ---

    // Create an archetype from components.
    // animArchetypeId: reference to the AnimationArchetypeManager archetype (for skeleton + fallback clips)
    // Returns the archetype ID.
    uint32_t createArchetype(const std::string& name,
                             uint32_t animArchetypeId,
                             calm::LowLevelController llc,
                             LatentSpace latentSpace,
                             CharacterConfig config);

    // Get archetype by ID
    const Archetype* getArchetype(uint32_t id) const;

    // Find archetype by name
    const Archetype* findArchetype(const std::string& name) const;

    size_t archetypeCount() const { return archetypes_.size(); }

    // --- Instance management ---

    // Create a new NPC instance referencing an archetype.
    // Returns instance index.
    size_t createInstance(uint32_t archetypeId);

    // Initialize an instance (called once after creation, needs skeleton reference)
    void initInstance(size_t instanceIdx, Skeleton& skeleton);

    // Get instance state
    NPCInstance* getInstance(size_t index);
    const NPCInstance* getInstance(size_t index) const;

    size_t instanceCount() const { return instances_.size(); }

    // --- Per-frame update ---

    // Update all instances with LOD-aware scheduling.
    // Instances at higher LOD levels update less frequently.
    void updateAll(float deltaTime,
                   std::vector<Skeleton>& skeletons,
                   const std::vector<CharacterController>& physics,
                   uint32_t currentFrame,
                   const CharacterLODConfig& lodConfig);

    // Update a single instance
    void updateInstance(size_t instanceIdx,
                        float deltaTime,
                        Skeleton& skeleton,
                        const CharacterController& physics);

    // --- Ragdoll physics ---

    // Build ragdoll settings for an archetype from its skeleton's bind pose.
    // Must be called after the archetype is created and skeleton is available.
    void buildArchetypeRagdoll(uint32_t archetypeId,
                                const Skeleton& skeleton,
                                const physics::RagdollConfig& config = {});

    // Create and activate a ragdoll for a specific NPC instance.
    // Requires that the archetype has ragdoll settings built.
    void enableInstanceRagdoll(size_t instanceIdx,
                                Skeleton& skeleton,
                                JPH::PhysicsSystem* physicsSystem);

    // Deactivate and destroy the ragdoll for an instance (switch back to kinematic).
    void disableInstanceRagdoll(size_t instanceIdx);

    // Update a single instance in physics mode.
    void updateInstancePhysics(size_t instanceIdx,
                                float deltaTime,
                                Skeleton& skeleton);

    // --- LOD control ---

    // Set LOD level for an instance (typically set by the LOD system)
    void setInstanceLOD(size_t instanceIdx, uint32_t lodLevel);

    // Check if an instance should update this frame based on LOD
    bool shouldUpdateInstance(size_t instanceIdx, uint32_t currentFrame,
                              const CharacterLODConfig& lodConfig) const;

    // --- Bone matrix computation ---

    // Compute bone matrices from cached pose for an instance.
    // Uses the skeleton's inverse bind matrices.
    void computeBoneMatrices(size_t instanceIdx,
                             const Skeleton& skeleton);

    // Get cached bone matrices for rendering
    const std::vector<glm::mat4>& getBoneMatrices(size_t instanceIdx) const;

    // Clear all instances (keep archetypes)
    void clearInstances();

    static constexpr uint32_t INVALID_ARCHETYPE_ID = UINT32_MAX;

private:
    std::vector<std::unique_ptr<Archetype>> archetypes_;
    std::unordered_map<std::string, uint32_t> archetypeNameMap_;
    uint32_t nextArchetypeId_ = 0;

    std::vector<NPCInstance> instances_;
};

// Utility: compute bone matrices from a SkeletonPose and a Skeleton.
// Applies parent-child hierarchy and inverse bind matrices.
void computeBoneMatricesFromPose(const SkeletonPose& pose,
                                  const Skeleton& skeleton,
                                  std::vector<glm::mat4>& outMatrices);

} // namespace ml
