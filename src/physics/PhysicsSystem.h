#pragma once

#include "CharacterController.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>

// Forward declarations for Jolt types
namespace JPH {
    class PhysicsSystem;
    class TempAllocatorImpl;
    class JobSystemThreadPool;
    class BodyInterface;
    class Body;
}

// Forward declaration for Jolt runtime RAII wrapper
struct JoltRuntime;

// Physics body handle
using PhysicsBodyID = uint32_t;
constexpr PhysicsBodyID INVALID_BODY_ID = 0xFFFFFFFF;

struct PhysicsBodyInfo {
    PhysicsBodyID bodyID = INVALID_BODY_ID;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 linearVelocity{0.0f};
    bool isAwake = false;
};

struct RaycastHit {
    bool hit = false;
    float distance = 0.0f;
    PhysicsBodyID bodyId = INVALID_BODY_ID;
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

class PhysicsWorld {
public:
    // Factory: returns nullopt on failure
    static std::optional<PhysicsWorld> create();

    // Move-only (RAII handles are non-copyable)
    PhysicsWorld(PhysicsWorld&&) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Destructor handles cleanup
    ~PhysicsWorld();

    // Simulation
    void update(float deltaTime);

    // Terrain - creates a heightfield from the disc (flat ground)
    PhysicsBodyID createTerrainDisc(float radius, float heightOffset = 0.0f);

    // Terrain - creates a heightfield shape from height samples
    // samples: row-major height values, sampleCount x sampleCount grid
    // worldSize: terrain extent in world units (centered at origin)
    // heightScale: multiplier for height values
    PhysicsBodyID createTerrainHeightfield(const float* samples, uint32_t sampleCount,
                                           float worldSize, float heightScale);

    // Terrain with hole mask - holes have no collision (for caves/wells)
    // holeMask: row-major uint8_t values, same dimensions as height samples
    //           values > 127 indicate holes (no collision)
    PhysicsBodyID createTerrainHeightfield(const float* samples, const uint8_t* holeMask,
                                           uint32_t sampleCount, float worldSize, float heightScale);

    // Terrain heightfield positioned at a specific world location (for tiled terrain)
    // samples: row-major height values, sampleCount x sampleCount grid
    // tileWorldSize: tile extent in world units
    // heightScale: multiplier for height values
    // worldPosition: center of tile in world coordinates
    // useHalfTexelOffset: apply half-texel offset for GPU texture alignment (default true)
    //                     set to false for tiles with overlap pixels that align to boundaries
    PhysicsBodyID createTerrainHeightfieldAtPosition(const float* samples, uint32_t sampleCount,
                                                      float tileWorldSize, float heightScale,
                                                      const glm::vec3& worldPosition,
                                                      bool useHalfTexelOffset = true);

    // Terrain heightfield at position with hole mask support
    // holeMask: row-major uint8_t values, same dimensions as height samples
    //           values > 127 indicate holes (no collision)
    PhysicsBodyID createTerrainHeightfieldAtPosition(const float* samples, const uint8_t* holeMask,
                                                      uint32_t sampleCount, float tileWorldSize,
                                                      float heightScale, const glm::vec3& worldPosition,
                                                      bool useHalfTexelOffset = true);

    // Dynamic rigid bodies
    PhysicsBodyID createBox(const glm::vec3& position, const glm::vec3& halfExtents,
                            float mass = 1.0f, float friction = 0.5f, float restitution = 0.3f);
    PhysicsBodyID createSphere(const glm::vec3& position, float radius,
                               float mass = 1.0f, float friction = 0.5f, float restitution = 0.3f);

    // Static rigid bodies
    PhysicsBodyID createStaticBox(const glm::vec3& position, const glm::vec3& halfExtents,
                                  const glm::quat& rotation = glm::quat(1, 0, 0, 0));

    // Static convex hull from vertex positions (for irregular shapes like rocks)
    // vertices: array of 3D positions, scale: uniform scale applied to all vertices
    PhysicsBodyID createStaticConvexHull(const glm::vec3& position, const glm::vec3* vertices,
                                         size_t vertexCount, float scale = 1.0f,
                                         const glm::quat& rotation = glm::quat(1, 0, 0, 0));

    // Static capsule (for tree trunks, poles, etc.)
    // halfHeight: half the height of the cylindrical part (total height = 2*halfHeight + 2*radius)
    PhysicsBodyID createStaticCapsule(const glm::vec3& position, float halfHeight, float radius,
                                      const glm::quat& rotation = glm::quat(1, 0, 0, 0));

    // Capsule data for compound shapes (e.g., tree branches)
    struct CapsuleData {
        glm::vec3 localPosition;  // Position relative to compound body origin
        glm::quat localRotation;  // Rotation relative to compound body
        float halfHeight;         // Half the cylindrical part height
        float radius;             // Capsule radius
    };

    // Static compound shape from multiple capsules (for trees with multiple branches)
    // Creates a single physics body containing all capsules
    PhysicsBodyID createStaticCompoundCapsules(const glm::vec3& position,
                                               const std::vector<CapsuleData>& capsules,
                                               const glm::quat& rotation = glm::quat(1, 0, 0, 0));

    // Character controller
    bool createCharacter(const glm::vec3& position, float height, float radius);
    void updateCharacter(float deltaTime, const glm::vec3& desiredVelocity, bool jump);
    void setCharacterPosition(const glm::vec3& position);
    glm::vec3 getCharacterPosition() const;
    glm::vec3 getCharacterVelocity() const;
    bool isCharacterOnGround() const;

    // Body queries
    PhysicsBodyInfo getBodyInfo(PhysicsBodyID bodyID) const;
    void setBodyPosition(PhysicsBodyID bodyID, const glm::vec3& position);
    void setBodyVelocity(PhysicsBodyID bodyID, const glm::vec3& velocity);
    void applyImpulse(PhysicsBodyID bodyID, const glm::vec3& impulse);

    // Convert between GLM and physics transforms
    glm::mat4 getBodyTransform(PhysicsBodyID bodyID) const;

    // Raycast queries
    std::vector<RaycastHit> castRayAllHits(const glm::vec3& from, const glm::vec3& to) const;

    // Body management
    void removeBody(PhysicsBodyID bodyID);

    // Debug
    int getActiveBodyCount() const;

    // Access to the underlying Jolt physics system.
    // Used by ragdoll instances and debug rendering.
    JPH::PhysicsSystem* getPhysicsSystem() { return physicsSystem_.get(); }

#ifdef JPH_DEBUG_RENDERER
    // Legacy alias for debug rendering
    JPH::PhysicsSystem* getPhysicsSystemForDebug() { return physicsSystem_.get(); }
#endif

private:
    PhysicsWorld();  // Private: only factory can construct
    bool initInternal();

    // Internal helper for heightfield creation
    PhysicsBodyID createHeightfieldInternal(const float* samples, const uint8_t* holeMask,
                                             uint32_t sampleCount, float worldSize,
                                             float heightScale, const glm::vec3& worldPosition,
                                             bool useHalfTexelOffset);

    // Jolt runtime (ref-counted for multiple worlds)
    std::shared_ptr<JoltRuntime> joltRuntime_;

    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem_;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem_;

    // Character controller
    CharacterController character_;

    // Accumulated time for fixed timestep
    float accumulatedTime_ = 0.0f;
    static constexpr float FIXED_TIMESTEP = 1.0f / 60.0f;
    static constexpr int MAX_SUBSTEPS = 4;
};
