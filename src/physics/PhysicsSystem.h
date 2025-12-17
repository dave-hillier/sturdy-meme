#pragma once

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
    class Character;
    class CharacterVirtual;
}

// Forward declaration for Jolt runtime RAII wrapper
struct JoltRuntime;

// Physics body handle
using PhysicsBodyID = uint32_t;
constexpr PhysicsBodyID INVALID_BODY_ID = 0xFFFFFFFF;

// Collision layers
namespace PhysicsLayers {
    constexpr uint8_t NON_MOVING = 0;
    constexpr uint8_t MOVING = 1;
    constexpr uint8_t CHARACTER = 2;
    constexpr uint8_t NUM_LAYERS = 3;
}

// Broad phase layers
namespace BroadPhaseLayers {
    constexpr uint8_t NON_MOVING = 0;
    constexpr uint8_t MOVING = 1;
    constexpr uint8_t NUM_LAYERS = 2;
}

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
    PhysicsBodyID createTerrainHeightfieldAtPosition(const float* samples, uint32_t sampleCount,
                                                      float tileWorldSize, float heightScale,
                                                      const glm::vec3& worldPosition);

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

    // Character controller
    bool createCharacter(const glm::vec3& position, float height, float radius);
    void updateCharacter(float deltaTime, const glm::vec3& desiredVelocity, bool jump);
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

#ifdef JPH_DEBUG_RENDERER
    // Access to physics system for debug rendering (non-const because DrawBodies is non-const)
    JPH::PhysicsSystem* getPhysicsSystem() { return physicsSystem.get(); }
#endif

private:
    PhysicsWorld();  // Private: only factory can construct
    bool initInternal();

    // Jolt runtime (ref-counted for multiple worlds)
    std::shared_ptr<JoltRuntime> joltRuntime_;

    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem;

    // Character
    std::unique_ptr<JPH::CharacterVirtual> character;
    float characterHeight = 1.8f;
    float characterRadius = 0.3f;
    glm::vec3 characterDesiredVelocity{0.0f};
    bool characterWantsJump = false;

    // Accumulated time for fixed timestep
    float accumulatedTime = 0.0f;
    static constexpr float FIXED_TIMESTEP = 1.0f / 60.0f;
    static constexpr int MAX_SUBSTEPS = 4;
};
