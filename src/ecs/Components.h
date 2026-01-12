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

// ============================================================================
// Occlusion Culling Components (Phase 6)
// ============================================================================

// Marks entity for GPU occlusion culling via Hi-Z system
struct OcclusionCullable {
    uint32_t cullIndex{~0u};              // Index in culling system's object buffer
    bool wasVisibleLastFrame{true};       // Cached visibility result
    uint32_t invisibleFrames{0};          // Frames since last visible (for hysteresis)
};

// Bounding sphere for fast culling tests
struct CullBoundingSphere {
    glm::vec3 center{0.0f};               // Local space center offset
    float radius{1.0f};
};

// Occlusion query result (GPU async query)
struct OcclusionQueryResult {
    uint32_t queryIndex{~0u};             // Index in query pool
    bool queryPending{false};             // Waiting for GPU result
    uint32_t samplesPassed{0};            // Pixels visible (0 = occluded)
};

// Portal/occluder for visibility determination
struct OcclusionPortal {
    std::vector<glm::vec3> vertices;      // Portal polygon vertices
    glm::vec3 normal{0.0f, 0.0f, 1.0f};   // Portal facing direction
    bool twoSided{false};                 // Visible from both sides
};

// Large occluder hint (buildings, terrain features)
struct Occluder {
    enum class Shape : uint8_t {
        Box,
        ConvexHull,
        Portal
    };
    Shape shape{Shape::Box};
    bool alwaysOcclude{false};            // Force as occluder even if small
};

// Software rasterization occluder data
struct SoftwareOccluder {
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
    float conservativeExpand{0.0f};       // Expand silhouette for conservative culling
};

// Visibility cell/sector for precomputed visibility (PVS-like)
struct VisibilityCell {
    uint32_t cellId{0};
    glm::vec3 center{0.0f};
    glm::vec3 extents{10.0f};
    std::vector<uint32_t> potentiallyVisibleCells;  // Cell IDs visible from here
};

// Tag: entity should never be culled (always rendered)
struct NeverCull {};

// Tag: entity is a shadow-only object (culled from main view but not shadow)
struct ShadowOnly {};

// Tag: entity participates in occlusion culling as occluder
struct IsOccluder {};

// Culling group for batch processing
struct CullingGroup {
    uint32_t groupId{0};                  // Group ID for batch culling
    uint32_t priority{0};                 // Higher = cull first
};

// ============================================================================
// Extended Rendering Components (Phase 7)
// ============================================================================

// Handle for cubemap textures
using CubemapHandle = uint32_t;
constexpr CubemapHandle InvalidCubemap = ~0u;

// Handle for render targets
using RenderTargetHandle = uint32_t;
constexpr RenderTargetHandle InvalidRenderTarget = ~0u;

// Decal projection component - projects textures onto surfaces
struct Decal {
    MaterialHandle material{InvalidMaterial};
    glm::vec3 size{1.0f, 1.0f, 1.0f};     // Projection box size
    float fadeDistance{5.0f};              // Distance to start fading
    float angleFade{0.5f};                 // Fade based on surface angle (0=no fade, 1=aggressive)
    float depthBias{0.001f};               // Avoid z-fighting
    int sortOrder{0};                      // Draw order for overlapping decals
    bool affectsAlbedo{true};
    bool affectsNormal{true};
    bool affectsRoughness{false};
};

// Sprite renderer for billboard sprites
struct SpriteRenderer {
    TextureHandle texture{InvalidTexture};
    TextureHandle atlasTexture{InvalidTexture};  // Optional texture atlas
    glm::vec2 size{1.0f, 1.0f};            // World-space size
    glm::vec4 color{1.0f};                 // Tint color with alpha
    glm::vec4 uvRect{0.0f, 0.0f, 1.0f, 1.0f};  // UV coordinates (for atlas)

    // Billboard mode
    enum class Mode : uint8_t {
        None,           // No billboarding
        FaceCamera,     // Full billboarding (face camera)
        FaceCameraY,    // Vertical axis only (cylindrical)
        Fixed           // Fixed orientation
    };
    Mode mode{Mode::FaceCamera};

    // Animation (for sprite sheets)
    uint32_t frameCount{1};
    uint32_t currentFrame{0};
    float framesPerSecond{12.0f};
    float frameTime{0.0f};
    bool animating{false};
    bool loopAnimation{true};

    // Rendering
    bool castsShadow{false};
    bool receiveShadow{true};
    float sortOffset{0.0f};                // Depth sorting bias
};

// Render target for render-to-texture functionality
struct RenderTarget {
    RenderTargetHandle handle{InvalidRenderTarget};
    uint32_t width{512};
    uint32_t height{512};

    // Format options
    enum class Format : uint8_t {
        RGBA8,
        RGBA16F,
        R32F,
        Depth
    };
    Format colorFormat{Format::RGBA8};
    bool hasDepth{true};

    // Update settings
    enum class UpdateMode : uint8_t {
        EveryFrame,
        OnDemand,
        Interval
    };
    UpdateMode updateMode{UpdateMode::EveryFrame};
    float updateInterval{0.0f};            // Seconds between updates
    float timeSinceUpdate{0.0f};
    bool needsUpdate{true};

    // Associated camera (if null, uses entity's CameraComponent)
    entt::entity cameraEntity{entt::null};
};

// Reflection probe for local environment reflections
struct ReflectionProbe {
    CubemapHandle cubemap{InvalidCubemap};
    glm::vec3 extents{10.0f};              // Probe influence box size
    glm::vec3 boxProjection{0.0f};         // Box projection center offset
    float blendDistance{1.0f};             // Fade distance at edges
    float intensity{1.0f};
    int priority{0};                       // Higher = more important

    // Capture settings
    enum class Resolution : uint8_t {
        Low = 0,       // 64
        Medium = 1,    // 128
        High = 2,      // 256
        VeryHigh = 3   // 512
    };
    Resolution resolution{Resolution::Medium};

    // Update mode
    bool realtime{false};                  // Dynamic reflections
    float updateInterval{0.0f};            // Seconds between updates (if realtime)
    float timeSinceCapture{0.0f};
    bool needsCapture{true};

    // Filtering
    bool useBoxProjection{true};           // Use box projection for parallax correction
    uint32_t cullingMask{~0u};             // Layer mask for what to reflect
};

// Light probe for indirect diffuse lighting (spherical harmonics)
struct LightProbe {
    // SH9 coefficients for irradiance (3 bands = 9 coefficients per color channel)
    glm::vec3 shCoefficients[9]{
        glm::vec3(0.5f),  // L00 (ambient)
        glm::vec3(0.0f),  // L1-1
        glm::vec3(0.0f),  // L10
        glm::vec3(0.0f),  // L11
        glm::vec3(0.0f),  // L2-2
        glm::vec3(0.0f),  // L2-1
        glm::vec3(0.0f),  // L20
        glm::vec3(0.0f),  // L21
        glm::vec3(0.0f)   // L22
    };

    float influence{10.0f};                // Radius of influence
    float blendDistance{2.0f};             // Fade at edges
    int priority{0};                       // For overlapping probes

    // Capture settings
    bool needsCapture{true};
    bool realtime{false};
    float updateInterval{1.0f};
    float timeSinceCapture{0.0f};
};

// Light probe group for interpolation
struct LightProbeVolume {
    glm::vec3 extents{20.0f};              // Volume size
    glm::ivec3 probeCount{4, 2, 4};        // Probes per axis
    float probeSpacing{5.0f};              // Auto-calculated from extents/count
    bool interpolate{true};                // Trilinear interpolation between probes
};

// Tag: entity is a reflection probe (for queries)
struct IsReflectionProbe {};

// Tag: entity is a light probe
struct IsLightProbe {};

// Portal/mirror surface for render-to-texture views
struct PortalSurface {
    entt::entity targetPortal{entt::null}; // Linked portal for teleportation
    entt::entity viewCamera{entt::null};   // Camera for rendering portal view
    RenderTargetHandle renderTarget{InvalidRenderTarget};
    bool isMirror{false};                  // True = mirror, False = portal
    bool twoSided{false};
    float clipPlaneOffset{0.01f};          // Oblique near plane offset
};

// ============================================================================
// Audio Components (Phase 8)
// ============================================================================

// Handle for audio clips/samples
using AudioClipHandle = uint32_t;
constexpr AudioClipHandle InvalidAudioClip = ~0u;

// Handle for audio sources (backend-specific)
using AudioSourceHandle = uint32_t;
constexpr AudioSourceHandle InvalidAudioSource = ~0u;

// Audio clip metadata (cached from audio backend)
struct AudioClipInfo {
    AudioClipHandle handle{InvalidAudioClip};
    float duration{0.0f};                  // Duration in seconds
    uint32_t sampleRate{44100};
    uint8_t channels{2};
    bool streaming{false};                 // Large files stream from disk
};

// 3D spatial audio source
struct AudioSource {
    AudioClipHandle clip{InvalidAudioClip};
    AudioSourceHandle sourceHandle{InvalidAudioSource};  // Backend handle

    // Playback state
    bool playing{false};
    bool looping{false};
    bool paused{false};
    float playbackPosition{0.0f};          // Current position in seconds

    // Volume and pitch
    float volume{1.0f};                    // 0.0 - 1.0
    float pitch{1.0f};                     // 0.5 - 2.0 typical range
    float pan{0.0f};                       // -1.0 (left) to 1.0 (right), 2D only

    // 3D spatialization
    bool spatialize{true};                 // Enable 3D positioning
    float minDistance{1.0f};               // Distance at which volume starts to attenuate
    float maxDistance{50.0f};              // Distance at which sound is inaudible

    // Attenuation model
    enum class Rolloff : uint8_t {
        Linear,                            // Linear falloff
        Logarithmic,                       // Realistic (inverse square law)
        Custom                             // Use rolloffFactor
    };
    Rolloff rolloff{Rolloff::Logarithmic};
    float rolloffFactor{1.0f};             // Multiplier for attenuation

    // Doppler effect
    bool dopplerEnabled{true};
    float dopplerFactor{1.0f};             // Strength of doppler effect

    // Cone attenuation (directional sounds)
    float coneInnerAngle{360.0f};          // Full volume inside this angle (degrees)
    float coneOuterAngle{360.0f};          // Attenuated beyond this angle
    float coneOuterVolume{0.0f};           // Volume outside outer cone

    // Priority for voice management
    int priority{128};                     // 0 = highest, 255 = lowest

    // Playback control flags
    bool playOnAwake{false};               // Start playing when entity spawns
    bool destroyOnComplete{false};         // Destroy entity when clip finishes
};

// Audio listener (receives spatial audio, typically attached to camera/player)
// Only one listener should be active at a time
struct AudioListener {
    float volume{1.0f};                    // Master volume multiplier
    bool active{true};                     // Is this the active listener

    // Velocity for doppler calculations (set by physics/movement system)
    glm::vec3 velocity{0.0f};
};

// Tag: entity is the active audio listener
struct ActiveAudioListener {};

// Audio mixer group for volume control
struct AudioMixerGroup {
    enum class Group : uint8_t {
        Master,
        Music,
        SFX,
        Voice,
        Ambient,
        UI,
        Custom
    };
    Group group{Group::SFX};
    float groupVolume{1.0f};               // Additional volume multiplier from group
};

// One-shot audio effect (plays once then removes itself)
struct OneShotAudio {
    AudioClipHandle clip{InvalidAudioClip};
    float volume{1.0f};
    float pitch{1.0f};
    float delay{0.0f};                     // Delay before playing (seconds)
    float elapsedDelay{0.0f};
    bool started{false};
};

// Ambient sound zone (plays sounds when player enters)
struct AmbientSoundZone {
    AudioClipHandle clip{InvalidAudioClip};
    glm::vec3 extents{10.0f};              // Half-extents of zone
    float fadeDistance{5.0f};              // Distance to fade in/out
    float volume{1.0f};
    bool looping{true};
    bool currentlyInside{false};           // Tracked by audio system
    float currentVolume{0.0f};             // Faded volume
};

// Reverb zone for environmental audio effects
struct ReverbZone {
    glm::vec3 extents{10.0f};
    float fadeDistance{5.0f};

    // Reverb parameters (based on common audio APIs)
    enum class Preset : uint8_t {
        None,
        Room,
        Hallway,
        Cave,
        Arena,
        Hangar,
        Forest,
        Underwater,
        Custom
    };
    Preset preset{Preset::Room};

    // Custom reverb parameters (used when preset = Custom)
    float decayTime{1.0f};                 // Reverb decay time (seconds)
    float earlyReflections{0.5f};          // Early reflection level
    float lateReverb{0.5f};                // Late reverb level
    float diffusion{1.0f};                 // Echo density
    float density{1.0f};                   // Modal density
    float hfDecayRatio{0.5f};              // High frequency decay ratio

    float blendWeight{0.0f};               // Current blend (set by audio system)
};

// Audio occlusion for sounds blocked by geometry
struct AudioOcclusion {
    float occlusionFactor{0.0f};           // 0 = no occlusion, 1 = fully blocked
    float lowPassFilter{1.0f};             // Low-pass filter cutoff (0-1)
    bool autoCalculate{true};              // Calculate from raycasts
    float updateInterval{0.1f};            // How often to recalculate (seconds)
    float timeSinceUpdate{0.0f};
};

// Music track controller
struct MusicTrack {
    AudioClipHandle clip{InvalidAudioClip};
    AudioClipHandle nextClip{InvalidAudioClip};  // For crossfading
    float volume{1.0f};
    float fadeInDuration{2.0f};
    float fadeOutDuration{2.0f};
    float crossfadeProgress{0.0f};         // 0 = current, 1 = next
    bool playing{false};
    bool looping{true};

    enum class State : uint8_t {
        Stopped,
        FadingIn,
        Playing,
        FadingOut,
        Crossfading
    };
    State state{State::Stopped};
};

// Tag: entity is an audio emitter (for queries)
struct IsAudioSource {};

// Tag: entity is in an ambient zone
struct InAmbientZone {
    entt::entity zone{entt::null};
};

// ============================================================================
// Gameplay Components (Phase 9)
// ============================================================================

// Handle for navigation mesh data
using NavMeshHandle = uint32_t;
constexpr NavMeshHandle InvalidNavMesh = ~0u;

// Handle for dialogue/quest data
using DialogueHandle = uint32_t;
constexpr DialogueHandle InvalidDialogue = ~0u;

// Trigger volume for gameplay events
struct TriggerVolume {
    glm::vec3 extents{5.0f};               // Half-extents of trigger box

    // Trigger shape
    enum class Shape : uint8_t {
        Box,
        Sphere,
        Capsule
    };
    Shape shape{Shape::Box};
    float radius{5.0f};                    // For sphere/capsule
    float height{2.0f};                    // For capsule

    // Filtering
    uint32_t triggerMask{1};               // Which layers can trigger
    bool triggerOnce{false};               // Only fire once then disable
    bool triggered{false};                 // Has been triggered (for triggerOnce)

    // Events (indices into event system)
    uint32_t onEnterEvent{~0u};            // Event to fire on enter
    uint32_t onExitEvent{~0u};             // Event to fire on exit
    uint32_t onStayEvent{~0u};             // Event to fire each frame while inside

    // State tracking
    std::vector<entt::entity> entitiesInside;
    float stayEventInterval{0.0f};         // How often to fire stay event (0 = every frame)
    float timeSinceStayEvent{0.0f};

    // Tags for filtering
    std::string requiredTag;               // Only entities with this tag can trigger
};

// Tag: entity can trigger TriggerVolumes
struct Triggerable {
    uint32_t triggerLayer{1};              // Which trigger masks this can activate
};

// Tag: entity is currently inside a trigger
struct InsideTrigger {
    entt::entity currentTrigger{entt::null};  // Primary trigger (most recent)
    std::vector<entt::entity> triggers;        // All triggers entity is inside
    float timeInside{0.0f};                    // Time inside current trigger
};

// Navigation mesh agent for AI pathfinding
struct NavMeshAgent {
    NavMeshHandle navMesh{InvalidNavMesh}; // Which navmesh to use

    // Movement properties
    float speed{3.5f};                     // Movement speed (m/s)
    float acceleration{8.0f};              // Acceleration (m/s²)
    float angularSpeed{120.0f};            // Rotation speed (deg/s)
    float stoppingDistance{0.1f};          // Distance to stop from destination
    float radius{0.5f};                    // Agent radius for avoidance
    float height{2.0f};                    // Agent height

    // Path state
    glm::vec3 destination{0.0f};
    std::vector<glm::vec3> currentPath;
    uint32_t currentPathIndex{0};
    bool hasPath{false};
    bool pathPending{false};               // Waiting for path calculation

    // Status
    enum class Status : uint8_t {
        Idle,
        Moving,
        Arrived,
        PathNotFound,
        Stuck
    };
    Status status{Status::Idle};

    // Avoidance
    bool avoidanceEnabled{true};
    int avoidancePriority{50};             // Higher = less likely to move aside
    float avoidanceRadius{1.0f};           // Radius for agent avoidance

    // Auto-braking
    bool autoBraking{true};                // Slow down near destination
    float autoRepath{true};                // Recalculate path if blocked
    float repathInterval{0.5f};            // How often to repath (seconds)
    float timeSinceRepath{0.0f};

    // Off-mesh links (jumping, climbing, etc.)
    bool canJump{false};
    bool canClimb{false};
    float maxJumpDistance{2.0f};
    float maxClimbHeight{1.0f};

    // Current movement
    glm::vec3 velocity{0.0f};
    glm::vec3 desiredVelocity{0.0f};
};

// Tag: entity is on a nav mesh
struct OnNavMesh {
    uint32_t polyRef{0};                   // Current polygon reference
    glm::vec3 nearestPoint{0.0f};          // Snapped position on navmesh
};

// Waypoint for patrol/movement paths
struct Waypoint {
    float waitTime{0.0f};                  // Time to wait at this point
    float speed{-1.0f};                    // Speed override (-1 = use agent speed)
    std::string action;                    // Action to perform at waypoint
};

// Waypoint path component
struct WaypointPath {
    std::vector<entt::entity> waypoints;   // Waypoint entities
    std::vector<Waypoint> waypointData;    // Optional per-point data
    uint32_t currentIndex{0};
    bool loop{true};
    bool reverseAtEnd{false};              // Ping-pong movement
    bool reversed{false};                  // Currently going backwards
    float defaultSpeed{3.0f};              // Default movement speed
    float defaultWaitTime{0.0f};           // Default wait time at waypoints
    float waitTimer{0.0f};                 // Current wait time
};

// Interactable object (can be activated by player)
struct Interactable {
    // Interaction settings
    float interactionRadius{2.0f};         // How close player must be
    float interactionAngle{120.0f};        // Field of view required (degrees, 360 = any)
    glm::vec3 interactionPoint{0.0f};      // Offset from transform for interaction focus

    // State
    bool canInteract{true};                // Currently interactable
    bool highlighted{false};               // Being looked at
    bool interacting{false};               // Currently in interaction

    // Type
    enum class Type : uint8_t {
        Generic,
        Pickup,
        Door,
        Switch,
        NPC,
        Container,
        Readable,
        Usable
    };
    Type type{Type::Generic};

    // Interaction requirements
    std::string requiredItem;              // Item needed to interact
    std::string requiredState;             // State check (e.g., "door_unlocked")

    // Events
    uint32_t onInteractEvent{~0u};
    uint32_t onHighlightEvent{~0u};
    uint32_t onUnhighlightEvent{~0u};

    // UI
    std::string promptText{"Interact"};    // Text shown to player
    std::string iconName;                  // Icon to display
    int priority{0};                       // Higher = shown first if multiple
};

// Tag: entity can interact with Interactables
struct CanInteract {
    float interactionRange{3.0f};          // Max interaction distance
    bool interactionEnabled{true};          // Can currently interact
    entt::entity currentTarget{entt::null}; // Currently highlighted interactable
    entt::entity interactingWith{entt::null}; // Currently interacting with
};

// Pickup item
struct Pickup {
    std::string itemId;                    // Item identifier
    uint32_t quantity{1};
    bool respawns{false};
    float respawnTime{30.0f};
    float timeSincePickup{0.0f};
    bool pickedUp{false};

    // Visual feedback
    bool bobbing{true};
    float bobSpeed{2.0f};
    float bobHeight{0.1f};
    bool rotating{true};
    float rotateSpeed{45.0f};
};

// Door/gate that can open/close
struct Door {
    enum class State : uint8_t {
        Closed,
        Opening,
        Open,
        Closing
    };
    State state{State::Closed};

    float openAngle{90.0f};                // Rotation when open (degrees)
    float openSpeed{90.0f};                // Speed of opening (deg/s or m/s)
    float currentAngle{0.0f};

    bool locked{false};
    std::string requiredKeyId;             // Key item needed to unlock
    bool autoClose{false};
    float autoCloseDelay{5.0f};
    float timeSinceOpened{0.0f};

    // Sliding door
    bool sliding{false};
    glm::vec3 slideDirection{1.0f, 0.0f, 0.0f};
    float slideDistance{2.0f};
    float currentSlide{0.0f};
};

// Switch/lever/button
struct Switch {
    enum class Type : uint8_t {
        Toggle,                            // On/Off
        Hold,                              // Momentary (returns to off)
        OneShot                            // Single use
    };
    Type type{Type::Toggle};

    bool isOn{false};
    float cooldown{0.0f};                  // Cooldown between uses
    float cooldownTimer{0.0f};

    entt::entity targetEntity{entt::null}; // What this switch controls
    std::string targetAction;              // Action to perform on target

    // Events
    uint32_t onActivateEvent{~0u};
    uint32_t onDeactivateEvent{~0u};
};

// Spawn point for entities
struct SpawnPoint {
    std::string entityType;                // What to spawn
    uint32_t maxEntities{1};               // Max alive at once
    float respawnDelay{10.0f};
    float spawnRadius{0.5f};               // Random offset
    bool active{true};
    bool initialSpawn{true};               // Spawn on initialization
    bool spawnOnActivate{false};           // Spawn when activated
    float nextSpawnTime{0.0f};             // Time until next spawn
    float timeSinceSpawn{0.0f};

    std::vector<entt::entity> spawnedEntities;
};

// Checkpoint/save point
struct Checkpoint {
    uint32_t checkpointId{0};
    bool activated{false};
    bool isRespawnPoint{true};
    glm::vec3 respawnOffset{0.0f, 1.0f, 0.0f};  // Offset from checkpoint position
};

// Damage zone (lava, poison, etc.)
struct DamageZone {
    float damagePerSecond{10.0f};
    glm::vec3 extents{5.0f};

    enum class DamageType : uint8_t {
        Generic,
        Fire,
        Ice,
        Poison,
        Electric,
        Fall
    };
    DamageType damageType{DamageType::Generic};

    float damageInterval{0.5f};            // Time between damage ticks
    float timeSinceDamage{0.0f};
    bool affectsPlayer{true};
    bool affectsNPCs{true};
};

// Dialogue trigger/NPC conversation
struct DialogueTrigger {
    DialogueHandle dialogue{InvalidDialogue};
    uint32_t startNode{0};                 // Starting dialogue node

    bool automatic{false};                 // Trigger dialogue automatically
    bool oneShot{false};                   // Only trigger once
    bool triggered{false};                 // Has been triggered (for oneShot)

    // Conditions (string-based for flexibility)
    std::vector<std::string> conditions;   // Conditions that must be met
};

// Quest giver/objective marker
struct QuestMarker {
    std::string questId;
    std::string objectiveId;               // Specific objective this marks

    enum class Type : uint8_t {
        Destination,
        Interact,
        Kill,
        Collect,
        Escort,
        Area,
        Hidden
    };
    Type type{Type::Destination};

    bool showOnMap{true};
    bool showOnCompass{true};
    bool showDistance{true};
    float visibilityDistance{100.0f};
    bool active{true};

    glm::vec3 color{1.0f, 0.8f, 0.0f};     // Yellow by default
};

// Tag: entity is a gameplay trigger (for queries)
struct IsTrigger {};

// Tag: entity is an interactable (for queries)
struct IsInteractable {};

// Tag: entity is a spawn point
struct IsSpawnPoint {};

// Tag: entity is an NPC that can be talked to
struct IsDialogueNPC {};

// ============================================================================
// Composable Utility Components (Phase 10)
// ============================================================================

// Generic timer for any timed behavior
struct Timer {
    float duration{1.0f};              // Total duration
    float elapsed{0.0f};               // Time elapsed
    bool paused{false};
    bool looping{false};               // Restart automatically
    bool finished{false};              // Set when complete (for non-looping)
    bool autoRemove{false};            // Remove component when finished
    std::string tag;                   // Optional identifier for multiple timers
    uint32_t onCompleteEvent{~0u};     // Event to fire on completion

    float progress() const { return duration > 0.0f ? elapsed / duration : 1.0f; }
    float remaining() const { return duration - elapsed; }
    bool isComplete() const { return elapsed >= duration; }
};

// Cooldown for abilities, attacks, interactions
struct Cooldown {
    float baseDuration{1.0f};          // Base cooldown time
    float currentCooldown{0.0f};       // Time remaining
    float reductionMultiplier{1.0f};   // For cooldown reduction effects
    bool ready{true};                  // Can be used
    int charges{1};                    // Number of uses before cooldown
    int maxCharges{1};
    float chargeRegenRate{0.0f};       // Charges regained per second (0 = only on cooldown end)

    float effectiveDuration() const { return baseDuration * reductionMultiplier; }
    bool canUse() const { return ready && charges > 0; }
    void use() {
        if (charges > 0) {
            charges--;
            if (charges == 0) {
                ready = false;
                currentCooldown = effectiveDuration();
            }
        }
    }
};

// Status effect (buff/debuff)
struct StatusEffect {
    enum class Type : uint8_t {
        Buff,
        Debuff,
        Neutral
    };
    Type type{Type::Buff};

    std::string effectId;              // Identifier for this effect type
    float duration{-1.0f};             // -1 = permanent until removed
    float elapsed{0.0f};
    float tickInterval{0.0f};          // For periodic effects (0 = no ticks)
    float timeSinceTick{0.0f};

    // Stacking behavior
    enum class StackMode : uint8_t {
        Unique,                        // Only one of this effect
        Stack,                         // Multiple instances stack
        Refresh,                       // Reset duration on reapply
        StackRefresh                   // Stack and reset duration
    };
    StackMode stackMode{StackMode::Unique};
    int stacks{1};
    int maxStacks{1};

    // Source tracking
    entt::entity source{entt::null};   // Who applied this effect

    // Modifier values (interpreted by game systems)
    float magnitude{1.0f};             // Effect strength
    float bonusFlat{0.0f};             // Flat bonus
    float bonusPercent{0.0f};          // Percentage bonus (0.1 = 10%)

    // Visual feedback
    bool showIcon{true};
    std::string iconName;
    glm::vec4 tintColor{1.0f};

    bool isExpired() const { return duration >= 0.0f && elapsed >= duration; }
};

// Container for multiple status effects on an entity
struct StatusEffects {
    std::vector<StatusEffect> effects;

    void add(const StatusEffect& effect) {
        // Handle stacking based on effect's stack mode
        for (auto& existing : effects) {
            if (existing.effectId == effect.effectId) {
                switch (existing.stackMode) {
                    case StatusEffect::StackMode::Unique:
                        return; // Don't add
                    case StatusEffect::StackMode::Refresh:
                        existing.elapsed = 0.0f;
                        return;
                    case StatusEffect::StackMode::Stack:
                        if (existing.stacks < existing.maxStacks) {
                            existing.stacks++;
                        }
                        return;
                    case StatusEffect::StackMode::StackRefresh:
                        if (existing.stacks < existing.maxStacks) {
                            existing.stacks++;
                        }
                        existing.elapsed = 0.0f;
                        return;
                }
            }
        }
        effects.push_back(effect);
    }

    void removeByTag(const std::string& effectId) {
        effects.erase(
            std::remove_if(effects.begin(), effects.end(),
                [&](const StatusEffect& e) { return e.effectId == effectId; }),
            effects.end()
        );
    }

    void clearExpired() {
        effects.erase(
            std::remove_if(effects.begin(), effects.end(),
                [](const StatusEffect& e) { return e.isExpired(); }),
            effects.end()
        );
    }
};

// Team/Faction membership for AI and combat
struct Team {
    uint32_t teamId{0};                // Team identifier
    uint32_t hostileMask{~0u};         // Bitmask of hostile teams (all by default)
    uint32_t friendlyMask{0};          // Bitmask of friendly teams
    uint32_t neutralMask{0};           // Bitmask of neutral teams

    bool isHostile(uint32_t otherTeam) const { return (hostileMask & (1u << otherTeam)) != 0; }
    bool isFriendly(uint32_t otherTeam) const { return teamId == otherTeam || (friendlyMask & (1u << otherTeam)) != 0; }
    bool isNeutral(uint32_t otherTeam) const { return (neutralMask & (1u << otherTeam)) != 0; }
};

// Targeting system - tracks what entity is focused on
struct Target {
    entt::entity current{entt::null};  // Current target
    entt::entity previous{entt::null}; // Previous target
    float timeSinceAcquired{0.0f};     // How long we've had this target
    float maxRange{50.0f};             // Max targeting range
    bool autoTarget{false};            // Automatically acquire targets
    bool lockOn{false};                // Maintain target even if lost LOS

    // Target priority settings
    enum class Priority : uint8_t {
        Nearest,
        LowestHealth,
        HighestThreat,
        LastAttacker
    };
    Priority priority{Priority::Nearest};

    bool hasTarget() const { return current != entt::null; }
};

// Following behavior - entity follows another
struct FollowTarget {
    entt::entity target{entt::null};
    float followDistance{2.0f};        // Ideal distance to maintain
    float minDistance{1.0f};           // Stop if closer than this
    float maxDistance{20.0f};          // Stop following if further
    float speed{3.0f};                 // Follow speed
    bool active{true};
    bool maintainOffset{false};        // Keep fixed offset instead of distance
    glm::vec3 offset{0.0f};            // Offset from target (if maintainOffset)

    // Smoothing
    float smoothTime{0.2f};            // Position smoothing
    glm::vec3 velocity{0.0f};          // Current velocity (for smoothing)
};

// Orbit around target
struct OrbitTarget {
    entt::entity target{entt::null};
    float distance{5.0f};              // Orbit radius
    float speed{45.0f};                // Degrees per second
    float currentAngle{0.0f};
    float heightOffset{0.0f};          // Vertical offset
    bool clockwise{true};
    bool faceTarget{true};             // Rotate to face target
};

// Entity lifetime - auto-destroy after time
struct Lifetime {
    float duration{5.0f};
    float elapsed{0.0f};
    bool paused{false};

    // Optional callbacks
    uint32_t onExpireEvent{~0u};
    bool fadeOut{false};               // Fade alpha before destroy
    float fadeTime{0.5f};

    float remaining() const { return duration - elapsed; }
    float progress() const { return duration > 0.0f ? elapsed / duration : 1.0f; }
    bool isExpired() const { return elapsed >= duration; }
};

// Delayed action - execute something after delay
struct DelayedAction {
    float delay{1.0f};
    float elapsed{0.0f};
    bool triggered{false};
    bool autoRemove{true};             // Remove component after triggering
    uint32_t actionEvent{~0u};         // Event to fire
    std::string actionTag;             // String identifier for handlers
};

// Spawn on destroy - create entities when this one dies
struct SpawnOnDestroy {
    std::string entityType;            // What to spawn
    int count{1};                      // Number to spawn
    float spreadRadius{1.0f};          // Random spawn spread
    bool inheritVelocity{false};
    float inheritVelocityMult{0.5f};
};

// Damage dealing component (weapons, projectiles, hazards)
struct DamageDealer {
    float baseDamage{10.0f};
    float damageMultiplier{1.0f};

    enum class DamageType : uint8_t {
        Physical,
        Fire,
        Ice,
        Electric,
        Poison,
        Holy,
        Dark,
        True                           // Ignores armor/resistance
    };
    DamageType damageType{DamageType::Physical};

    // Timing
    bool canDamage{true};
    float hitCooldown{0.1f};           // Minimum time between hits to same target
    std::vector<entt::entity> recentlyHit;  // For hit cooldown tracking
    std::vector<float> hitTimers;

    // Knockback
    bool appliesKnockback{false};
    float knockbackForce{5.0f};

    // Critical hits
    float critChance{0.0f};            // 0.0 - 1.0
    float critMultiplier{2.0f};

    // Status effects
    std::string appliesEffect;         // StatusEffect ID to apply
    float effectChance{1.0f};

    float totalDamage() const { return baseDamage * damageMultiplier; }
};

// Damage receiving component (how entity responds to damage)
struct DamageReceiver {
    bool canReceiveDamage{true};
    float damageMultiplier{1.0f};      // Global damage modifier

    // Resistances per damage type (0.0 = no resistance, 1.0 = immune, negative = vulnerability)
    float resistPhysical{0.0f};
    float resistFire{0.0f};
    float resistIce{0.0f};
    float resistElectric{0.0f};
    float resistPoison{0.0f};
    float resistHoly{0.0f};
    float resistDark{0.0f};

    // Armor (flat damage reduction)
    float armor{0.0f};

    // Invulnerability frames
    float iFrameDuration{0.0f};
    float iFrameTimer{0.0f};

    // Events
    uint32_t onDamageEvent{~0u};
    uint32_t onDeathEvent{~0u};

    // Last damage info
    entt::entity lastDamager{entt::null};
    float lastDamageAmount{0.0f};
    DamageDealer::DamageType lastDamageType{DamageDealer::DamageType::Physical};

    float getResistance(DamageDealer::DamageType type) const {
        switch (type) {
            case DamageDealer::DamageType::Physical: return resistPhysical;
            case DamageDealer::DamageType::Fire: return resistFire;
            case DamageDealer::DamageType::Ice: return resistIce;
            case DamageDealer::DamageType::Electric: return resistElectric;
            case DamageDealer::DamageType::Poison: return resistPoison;
            case DamageDealer::DamageType::Holy: return resistHoly;
            case DamageDealer::DamageType::Dark: return resistDark;
            case DamageDealer::DamageType::True: return 0.0f;
        }
        return 0.0f;
    }

    bool isInvulnerable() const { return iFrameTimer > 0.0f || !canReceiveDamage; }
};

// Hit reaction (knockback, stun, etc.)
struct HitReaction {
    glm::vec3 knockbackVelocity{0.0f};
    float stunDuration{0.0f};
    float stunTimer{0.0f};
    float hitStopDuration{0.0f};       // Frame freeze
    float hitStopTimer{0.0f};

    bool isStunned() const { return stunTimer > 0.0f; }
    bool inHitStop() const { return hitStopTimer > 0.0f; }
};

// Projectile component
struct Projectile {
    entt::entity owner{entt::null};    // Who fired this
    float speed{20.0f};
    float lifetime{5.0f};              // Auto-destroy after this time
    float elapsed{0.0f};
    bool homing{false};
    entt::entity homingTarget{entt::null};
    float homingStrength{5.0f};        // Turn rate for homing
    float gravity{0.0f};               // Gravity effect (for arcing projectiles)
    bool destroyOnHit{true};
    bool piercing{false};              // Pass through targets
    int maxPierceCount{0};
    int pierceCount{0};

    // Trail
    bool hasTrail{false};
    glm::vec4 trailColor{1.0f};
};

// Generic state machine for composable behaviors
struct StateMachine {
    uint32_t currentState{0};
    uint32_t previousState{0};
    float stateTime{0.0f};             // Time in current state
    float transitionTime{0.0f};        // Time since transition started
    bool transitioning{false};
    float transitionDuration{0.0f};

    // State names for debugging/editor
    std::vector<std::string> stateNames;

    void setState(uint32_t newState, float blendTime = 0.0f) {
        if (newState == currentState) return;
        previousState = currentState;
        currentState = newState;
        stateTime = 0.0f;
        transitioning = blendTime > 0.0f;
        transitionDuration = blendTime;
        transitionTime = 0.0f;
    }
};

// Aggro/Threat table for AI
struct ThreatTable {
    struct Entry {
        entt::entity entity{entt::null};
        float threat{0.0f};
        float lastDamageTime{0.0f};
    };
    std::vector<Entry> entries;
    float threatDecayRate{1.0f};       // Threat lost per second
    float maxRange{50.0f};             // Drop threat if further

    void addThreat(entt::entity entity, float amount) {
        for (auto& e : entries) {
            if (e.entity == entity) {
                e.threat += amount;
                e.lastDamageTime = 0.0f;
                return;
            }
        }
        entries.push_back({entity, amount, 0.0f});
    }

    entt::entity getHighestThreat() const {
        entt::entity best = entt::null;
        float highestThreat = 0.0f;
        for (const auto& e : entries) {
            if (e.threat > highestThreat) {
                highestThreat = e.threat;
                best = e.entity;
            }
        }
        return best;
    }
};

// Loot table for drops
struct LootTable {
    struct Drop {
        std::string itemId;
        float chance{1.0f};            // 0.0 - 1.0
        int minQuantity{1};
        int maxQuantity{1};
    };
    std::vector<Drop> drops;
    bool guaranteedDrop{true};         // At least one item drops
    int maxDrops{-1};                  // -1 = unlimited
};

// Tag: entity is a projectile
struct IsProjectile {};

// Tag: entity has active status effects
struct HasStatusEffects {};

// Tag: entity is invulnerable
struct Invulnerable {};

// Tag: entity is stunned
struct Stunned {};

// Tag: entity is dead (pending removal)
struct Dead {};

// Tag: entity is a team member (for team queries)
struct IsTeamMember {};

// Tag: entity is targetable by AI/player
struct Targetable {};

// Tag: entity ignores gravity
struct NoGravity {};

// Tag: entity has custom physics (handled by game code, not physics engine)
struct CustomPhysics {};
