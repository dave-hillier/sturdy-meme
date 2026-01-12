#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include "PhysicsSystem.h"
#include "scene/RotationUtils.h"

// Core transform component - position and rotation for entities
// Supports both quaternion (full 3D) and yaw-only (Y-axis) rotation
struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // Identity quaternion

    // ========================================================================
    // Factory methods
    // ========================================================================

    // Create with position only (identity rotation)
    static Transform withPosition(const glm::vec3& pos) {
        Transform t;
        t.position = pos;
        return t;
    }

    // Create with position and yaw (degrees)
    static Transform withYaw(const glm::vec3& pos, float yawDegrees) {
        Transform t;
        t.position = pos;
        t.rotation = glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
        return t;
    }

    // Create with position and quaternion rotation
    static Transform withRotation(const glm::vec3& pos, const glm::quat& rot) {
        Transform t;
        t.position = pos;
        t.rotation = rot;
        return t;
    }

    // ========================================================================
    // Quaternion-based rotation (preferred for full 3D rotation)
    // ========================================================================

    void setRotation(const glm::quat& rot) { rotation = rot; }
    const glm::quat& getRotation() const { return rotation; }

    // ========================================================================
    // Yaw-based rotation (backward compatibility for Y-axis only rotation)
    // ========================================================================

    // Get yaw in degrees (extracted from quaternion)
    float getYaw() const {
        glm::vec3 euler = glm::eulerAngles(rotation);
        return glm::degrees(euler.y);
    }

    // Set yaw in degrees (creates Y-axis rotation quaternion)
    void setYaw(float yawDegrees) {
        rotation = glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    void normalizeYaw() {
        // Quaternions are already normalized, but we can re-normalize if needed
        rotation = glm::normalize(rotation);
    }

    // ========================================================================
    // Direction vectors
    // ========================================================================

    glm::vec3 getForward() const {
        return glm::vec3(glm::mat4_cast(rotation) * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    }

    glm::vec3 getRight() const {
        return glm::vec3(glm::mat4_cast(rotation) * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    }

    glm::vec3 getUp() const {
        return glm::vec3(glm::mat4_cast(rotation) * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    }

    // ========================================================================
    // Transform matrix
    // ========================================================================

    glm::mat4 getMatrix() const {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = transform * glm::mat4_cast(rotation);
        return transform;
    }

    glm::mat4 getMatrixWithScale(float scale) const {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = transform * glm::mat4_cast(rotation);
        transform = glm::scale(transform, glm::vec3(scale));
        return transform;
    }
};

// Velocity for physics-driven entities
struct Velocity {
    glm::vec3 linear{0.0f};
};

// Physics body reference - links entity to Jolt physics body
struct PhysicsBody {
    PhysicsBodyID id = INVALID_BODY_ID;
};

// Renderable reference - links entity to scene object index (during migration)
// Eventually this could hold mesh/texture references directly
struct RenderableRef {
    size_t sceneIndex = 0;
};

// Tag: marks entity as the player
struct PlayerTag {};

// Tag: marks entity as grounded (on floor/terrain)
struct Grounded {};

// Player-specific components
struct PlayerMovement {
    static constexpr float CAPSULE_HEIGHT = 1.8f;
    static constexpr float CAPSULE_RADIUS = 0.3f;
    bool orientationLocked = false;
    float lockedYaw = 0.0f;

    glm::vec3 getFocusPoint(const glm::vec3& position) const {
        return position + glm::vec3(0.0f, CAPSULE_HEIGHT * 0.85f, 0.0f);
    }

    glm::mat4 getModelMatrix(const Transform& transform) const {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, transform.position + glm::vec3(0.0f, CAPSULE_HEIGHT * 0.5f, 0.0f));
        float effectiveYaw = orientationLocked ? lockedYaw : transform.getYaw();
        model = glm::rotate(model, glm::radians(effectiveYaw), glm::vec3(0.0f, 1.0f, 0.0f));
        return model;
    }
};

// Dynamic scene object that has physics simulation
struct DynamicObject {};

// Emissive light source that follows an entity
struct EmissiveLight {
    glm::vec3 color{1.0f};
    float intensity{1.0f};
};

// ============================================================================
// Light Components (Extended)
// ============================================================================

// Base light component - common properties for all light types
struct LightBase {
    glm::vec3 color{1.0f};
    float intensity{1.0f};
    float radius{10.0f};  // Falloff radius
    bool castsShadows{true};
    float priority{1.0f};  // Higher = more important (less likely to be culled)
};

// Point light - omnidirectional light source
struct PointLight : LightBase {
    // Point lights only need the base properties
};

// Spot light - directional cone light
// Direction is derived from the entity's Transform rotation
// Default rotation points light downward (-Y axis)
struct SpotLight : LightBase {
    float innerConeAngle{30.0f};  // Degrees
    float outerConeAngle{45.0f};  // Degrees

    // Get light direction from transform
    static glm::vec3 getDirection(const Transform& transform) {
        return RotationUtils::directionFromRotation(transform.rotation);
    }

    // Create rotation quaternion to point light in a specific direction
    static glm::quat rotationFromDirection(const glm::vec3& direction) {
        return RotationUtils::rotationFromDirection(direction);
    }
};

// Tag for enabled lights (removed when light is disabled)
struct LightEnabled {};

// Shadow caster tag - lights that cast shadows
struct ShadowCaster {
    int32_t shadowMapIndex{-1};  // Index in shadow map array
};

// Light attachment - attaches light to another entity's transform
struct LightAttachment {
    entt::entity parent{entt::null};
    glm::vec3 offset{0.0f};  // Offset from parent transform
};

// ============================================================================
// Dynamic Object Components (Extended)
// ============================================================================

// Tag for objects that sync transform from physics
struct PhysicsDriven {};

// Tag for objects that are rendered (links to scene renderables)
struct SceneRenderable {
    size_t renderableIndex{0};
};

// Bounding sphere for culling
struct BoundingSphere {
    float radius{1.0f};
};

// Model matrix cache - computed from transform for rendering
struct ModelMatrix {
    glm::mat4 matrix{1.0f};
};

// ============================================================================
// NPC/AI Components
// ============================================================================

// Tag for NPC entities
struct NPCTag {};

// Simple AI state component
struct AIState {
    enum class State {
        Idle,
        Patrol,
        Chase,
        Flee
    };
    State current{State::Idle};
    float stateTimer{0.0f};
};

// Patrol waypoint data
struct PatrolPath {
    std::vector<glm::vec3> waypoints;
    size_t currentWaypoint{0};
    bool loop{true};
    float waypointRadius{0.5f};  // How close to get before moving to next
};

// Movement speed settings for characters/NPCs
struct MovementSettings {
    float walkSpeed{2.0f};
    float runSpeed{5.0f};
    float turnSpeed{180.0f};  // Degrees per second
};

// Health component for damageable entities
struct Health {
    float current{100.0f};
    float maximum{100.0f};
    bool invulnerable{false};
};

// Name/identifier component
struct NameTag {
    std::string name;
};

// ============================================================================
// Scene Graph Hierarchy Components
// ============================================================================

// Hierarchy component - tracks parent/child relationships for scene graph
struct Hierarchy {
    entt::entity parent{entt::null};
    std::vector<entt::entity> children;

    // Local transform (relative to parent)
    glm::vec3 localPosition{0.0f};
    glm::vec3 localScale{1.0f};
    float localYaw{0.0f};  // Local rotation around Y axis

    bool isRoot() const { return parent == entt::null; }
    bool hasChildren() const { return !children.empty(); }
};

// World transform cache - computed from hierarchy
struct WorldTransform {
    glm::mat4 matrix{1.0f};
    glm::vec3 position{0.0f};
    glm::vec3 scale{1.0f};
    float yaw{0.0f};
    bool dirty{true};  // Needs recalculation
};

// Entity metadata for scene graph display
struct EntityInfo {
    std::string name{"Entity"};
    std::string icon{"?"};  // Single char icon for tree view
    bool visible{true};
    bool locked{false};  // Prevent selection/modification
    uint32_t layer{0};   // Layer mask for filtering
};

// Tag for selected entities in the scene graph
struct Selected {};

// Tag for entities that should be expanded in tree view
struct TreeExpanded {};

// ============================================================================
// Renderer Components (for future ECS-based rendering)
// ============================================================================

// Resource handles - typed indices into resource registries
using MeshHandle = uint32_t;
using MaterialHandle = uint32_t;
using TextureHandle = uint32_t;
using SkeletonHandle = uint32_t;

constexpr MeshHandle InvalidMesh = ~0u;
constexpr MaterialHandle InvalidMaterial = ~0u;
constexpr TextureHandle InvalidTexture = ~0u;

// Render layer flags for culling
enum class RenderLayer : uint32_t {
    Default     = 1 << 0,
    Terrain     = 1 << 1,
    Water       = 1 << 2,
    Vegetation  = 1 << 3,
    Character   = 1 << 4,
    UI          = 1 << 5,
    Debug       = 1 << 6,
    All         = ~0u
};

// Mesh renderer - links entity to GPU mesh and material
struct MeshRenderer {
    MeshHandle mesh{InvalidMesh};
    MaterialHandle material{InvalidMaterial};
    uint32_t submeshIndex{0};
    bool castsShadow{true};
    bool receiveShadow{true};
    RenderLayer layer{RenderLayer::Default};
};

// Skinned mesh for animated characters
struct SkinnedMeshRenderer {
    MeshHandle mesh{InvalidMesh};
    MaterialHandle material{InvalidMaterial};
    SkeletonHandle skeleton{~0u};
    float animationTime{0.0f};
};

// Camera component for rendering viewpoints
struct CameraComponent {
    float fov{60.0f};
    float nearPlane{0.1f};
    float farPlane{1000.0f};
    int priority{0};  // Higher priority cameras render first/on top
    uint32_t cullingMask{static_cast<uint32_t>(RenderLayer::All)};
};

// Tag for the main camera used for rendering
struct MainCamera {};

// Bounding box for frustum culling
struct AABBBounds {
    glm::vec3 min{-0.5f};
    glm::vec3 max{0.5f};

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 extents() const { return (max - min) * 0.5f; }
};

// LOD (Level of Detail) group
struct LODGroup {
    std::vector<float> switchDistances;  // Distance to switch LODs
    std::vector<MeshHandle> lodMeshes;   // One mesh per LOD level
    int currentLOD{0};
};

// Billboard that always faces camera
enum class BillboardMode { None, FaceCamera, FaceCameraY };
struct Billboard {
    BillboardMode mode{BillboardMode::FaceCamera};
};

// Static flag - entity transform won't change (enables optimizations)
struct StaticObject {};

// Culling result tag - entity was visible last frame
struct WasVisible {};

// ============================================================================
// Animation Components (Phase 4)
// ============================================================================

// Handle for animation clips
using AnimationHandle = uint32_t;
constexpr AnimationHandle InvalidAnimation = ~0u;

// Animation playback state
struct AnimationState {
    AnimationHandle currentAnimation{InvalidAnimation};
    AnimationHandle nextAnimation{InvalidAnimation};     // For crossfade
    float time{0.0f};                // Current playback time
    float speed{1.0f};               // Playback speed multiplier
    float blendWeight{0.0f};         // Crossfade progress [0-1]
    float blendDuration{0.2f};       // Crossfade duration
    bool looping{true};
    bool playing{true};
};

// Animator controller for state machine-driven animation
struct Animator {
    enum class State : uint8_t {
        Idle,
        Walk,
        Run,
        Jump,
        Fall,
        Land,
        Custom
    };
    State currentState{State::Idle};
    State previousState{State::Idle};
    float stateTime{0.0f};           // Time in current state
    float transitionTime{0.0f};      // Blend progress
    float movementSpeed{0.0f};       // Input for blend space
    bool grounded{true};
    bool jumping{false};
};

// IK target for procedural animation
struct IKTarget {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};  // Surface normal for foot alignment
    float weight{1.0f};                   // Blend weight
    bool active{false};
};

// Foot IK data for ground adaptation
struct FootIK {
    IKTarget leftFoot;
    IKTarget rightFoot;
    float pelvisOffset{0.0f};  // Vertical adjustment for uneven terrain
    bool enabled{true};
};

// Look-at IK for head/eye tracking
struct LookAtIK {
    entt::entity target{entt::null};     // Entity to look at (if set)
    glm::vec3 targetPosition{0.0f};      // World position to look at
    float weight{1.0f};
    float maxYaw{60.0f};                 // Degrees
    float maxPitch{30.0f};               // Degrees
    bool enabled{false};
};

// ============================================================================
// Particle System Components (Phase 4)
// ============================================================================

// Handle for particle system assets
using ParticleSystemHandle = uint32_t;
constexpr ParticleSystemHandle InvalidParticleSystem = ~0u;

// Particle emitter component
struct ParticleEmitter {
    ParticleSystemHandle system{InvalidParticleSystem};
    bool playing{true};
    bool looping{true};
    float playbackSpeed{1.0f};
    float elapsedTime{0.0f};
    uint32_t maxParticles{1000};

    // Emission shape
    enum class Shape : uint8_t { Point, Sphere, Box, Cone };
    Shape emitShape{Shape::Point};
    float emitRadius{1.0f};
    glm::vec3 emitSize{1.0f};  // For box/cone

    // Emission rate
    float emitRate{10.0f};     // Particles per second
    float burstCount{0.0f};    // Instant burst (triggers when > 0)
};

// Particle system parameters (can be shared or per-emitter)
struct ParticleParams {
    // Lifetime
    float minLifetime{1.0f};
    float maxLifetime{2.0f};

    // Initial velocity
    glm::vec3 minVelocity{-1.0f, 1.0f, -1.0f};
    glm::vec3 maxVelocity{1.0f, 3.0f, 1.0f};

    // Physics
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    float drag{0.1f};

    // Size over lifetime
    float startSize{0.1f};
    float endSize{0.0f};

    // Color over lifetime
    glm::vec4 startColor{1.0f};
    glm::vec4 endColor{1.0f, 1.0f, 1.0f, 0.0f};

    // Rendering
    TextureHandle texture{InvalidTexture};
    bool additive{false};  // Additive blending for fire/sparks
};

// ============================================================================
// Physics Integration Tags (Phase 4)
// ============================================================================

// Tag: entity receives physics forces but doesn't sync position
struct PhysicsKinematic {};

// Tag: entity is a physics trigger (collision events only)
struct PhysicsTrigger {};

// Collision event data (added when collision occurs)
struct CollisionEvent {
    entt::entity other{entt::null};
    glm::vec3 contactPoint{0.0f};
    glm::vec3 contactNormal{0.0f};
    float impulse{0.0f};
};

// Physics material properties
struct PhysicsMaterial {
    float friction{0.5f};
    float restitution{0.3f};  // Bounciness
    float density{1.0f};
};
