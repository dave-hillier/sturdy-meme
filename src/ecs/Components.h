#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <entt/entt.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include "PhysicsSystem.h"

// Core transform component - position and rotation for entities
struct Transform {
    glm::vec3 position{0.0f};
    float yaw{0.0f};  // Horizontal rotation in degrees

    glm::vec3 getForward() const {
        float rad = glm::radians(yaw);
        return glm::vec3(sin(rad), 0.0f, cos(rad));
    }

    glm::vec3 getRight() const {
        float rad = glm::radians(yaw + 90.0f);
        return glm::vec3(sin(rad), 0.0f, cos(rad));
    }

    void normalizeYaw() {
        while (yaw > 360.0f) yaw -= 360.0f;
        while (yaw < 0.0f) yaw += 360.0f;
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
        float effectiveYaw = orientationLocked ? lockedYaw : transform.yaw;
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
struct SpotLight : LightBase {
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    float innerConeAngle{30.0f};  // Degrees
    float outerConeAngle{45.0f};  // Degrees
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

// ============================================================================
// Environment Components (Phase 5)
// ============================================================================

// Terrain patch/tile component - represents a terrain tile in the LOD system
struct TerrainPatch {
    int32_t tileX{0};
    int32_t tileZ{0};
    uint32_t lod{0};                      // Level of detail (0 = highest)
    float worldSize{64.0f};               // Tile size in world units
    float heightScale{1.0f};
    bool hasHoles{false};                 // Cave/well holes
    bool visible{true};
    int32_t arrayLayerIndex{-1};          // GPU tile array index
};

// Terrain configuration entity component (singleton-like)
struct TerrainConfig {
    float totalSize{16384.0f};            // World terrain size
    uint32_t maxDepth{20};                // Max LOD depth
    uint32_t minDepth{6};
    float heightScale{500.0f};            // Height multiplier
    bool useMeshlets{true};
    bool causticsEnabled{true};
};

// Grass volume/region component
struct GrassVolume {
    glm::vec3 center{0.0f};
    glm::vec3 extents{32.0f};             // Half-extents of grass region
    float density{1.0f};                  // Density multiplier
    float heightMin{0.03f};
    float heightMax{0.15f};
    float spacing{0.35f};
    uint32_t lod{0};                      // LOD level (affects tile size)
    bool windEnabled{true};
    bool snowMaskEnabled{true};
};

// Individual grass tile component (for tiled grass system)
struct GrassTile {
    int32_t tileX{0};
    int32_t tileZ{0};
    uint32_t lod{0};                      // 0=64m, 1=128m, 2=256m
    uint32_t instanceCount{0};
    bool active{true};
    float fadeProgress{1.0f};             // For fade-in/out
};

// Water type enumeration
enum class WaterType : uint8_t {
    Ocean,
    CoastalOcean,
    River,
    MuddyRiver,
    ClearStream,
    Lake,
    Swamp,
    Tropical,
    Custom
};

// Water surface/body component
struct WaterSurface {
    WaterType type{WaterType::Lake};
    float height{0.0f};                   // Water level Y position
    float depth{10.0f};                   // Average depth
    glm::vec4 color{0.02f, 0.08f, 0.15f, 0.8f};  // RGBA

    // Wave parameters
    float waveAmplitude{0.5f};
    float waveLength{20.0f};
    float waveSteepness{0.5f};
    float waveSpeed{1.0f};

    // Material properties
    float specularRoughness{0.1f};
    float absorptionScale{1.0f};
    float scatteringScale{1.0f};
    float fresnelPower{5.0f};

    // Features
    bool hasFFT{false};                   // FFT ocean simulation
    bool hasCaustics{true};
    bool hasFoam{true};
    bool hasFlowMap{false};
    float flowStrength{0.5f};
    float flowSpeed{1.0f};

    // Tidal (for oceans)
    bool tidalEnabled{false};
    float tidalRange{2.0f};
};

// River spline component (for flowing water)
struct RiverSpline {
    std::vector<glm::vec3> controlPoints;  // Spline path
    std::vector<float> widths;             // Width at each control point
    float flowSpeed{2.0f};
    float depth{2.0f};
    WaterType type{WaterType::River};
};

// Lake component (enclosed body of water)
struct LakeBody {
    glm::vec3 center{0.0f};
    float radius{50.0f};
    float depth{10.0f};
    std::vector<glm::vec3> shoreline;      // Optional irregular shoreline
    WaterType type{WaterType::Lake};
};

// Tree archetype enumeration
enum class TreeArchetype : uint8_t {
    Oak,
    Pine,
    Ash,
    Aspen,
    Birch,
    Custom
};

// Tree instance component (for individual trees)
struct TreeInstance {
    TreeArchetype archetype{TreeArchetype::Oak};
    float scale{1.0f};
    float rotation{0.0f};                  // Y-axis rotation
    uint32_t meshIndex{0};                 // Which mesh variant
    uint32_t impostorIndex{0};             // Impostor atlas index
    bool hasCollision{true};
    bool castsShadow{true};
};

// Tree LOD state component
struct TreeLODState {
    enum class Level : uint8_t {
        FullDetail,
        Impostor,
        Blending
    };
    Level level{Level::FullDetail};
    float blendFactor{0.0f};               // 0=full detail, 1=impostor
    float distanceToCamera{0.0f};
};

// Vegetation zone component (region with multiple vegetation types)
struct VegetationZone {
    glm::vec3 center{0.0f};
    glm::vec3 extents{100.0f};
    float treeDensity{0.1f};               // Trees per unit area
    float bushDensity{0.2f};
    float grassDensity{1.0f};
    std::vector<TreeArchetype> allowedTrees;
    bool autoPopulate{false};              // Generate vegetation on spawn
};

// Rock instance component
struct RockInstance {
    uint32_t meshVariant{0};
    float scale{1.0f};
    glm::vec3 rotation{0.0f};              // Euler angles
    bool hasCollision{true};
    bool castsShadow{true};
};

// Detritus (fallen branches, debris) component
struct DetritusInstance {
    uint32_t meshVariant{0};
    float scale{1.0f};
    glm::vec3 rotation{0.0f};
    entt::entity sourceTree{entt::null};   // Which tree it came from
};

// Wind zone component (local wind effects)
struct WindZone {
    glm::vec3 direction{1.0f, 0.0f, 0.0f};
    float strength{1.0f};
    float turbulence{0.3f};
    float gustFrequency{0.5f};
    float gustStrength{2.0f};
    glm::vec3 extents{50.0f};              // Half-extents of zone
    bool isGlobal{false};                  // Affects entire scene
};

// Weather zone component (local weather effects)
struct WeatherZone {
    enum class Type : uint8_t {
        Clear,
        Cloudy,
        Rain,
        Snow,
        Fog,
        Storm
    };
    Type type{Type::Clear};
    float intensity{1.0f};
    float transitionRadius{20.0f};         // Blend distance at edges
    glm::vec3 extents{100.0f};
};

// Fog volume component
struct FogVolume {
    glm::vec3 extents{50.0f};
    float density{0.05f};
    glm::vec3 color{0.5f, 0.6f, 0.7f};
    float heightFalloff{0.01f};            // Fog density falloff with height
    bool isGlobal{false};
};
