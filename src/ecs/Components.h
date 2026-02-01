#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include <vector>
#include <algorithm>
#include "World.h"  // For Entity, NullEntity

// Forward declarations
struct Mesh;
struct Texture;

namespace ecs {

// =============================================================================
// Transform Component (World Space)
// =============================================================================
// Stores world-space transformation. The matrix is kept in a GPU-friendly
// layout (column-major, std140 compatible) so it can be uploaded directly
// to an SSBO for GPU-driven rendering.

struct Transform {
    glm::mat4 matrix = glm::mat4(1.0f);

    // Convenience constructors
    Transform() = default;
    explicit Transform(const glm::mat4& m) : matrix(m) {}
    Transform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale);

    // Decompose helpers
    [[nodiscard]] glm::vec3 position() const { return glm::vec3(matrix[3]); }
    void setPosition(const glm::vec3& pos) { matrix[3] = glm::vec4(pos, 1.0f); }

    // Static factory methods
    static Transform fromPosition(const glm::vec3& pos);
    static Transform fromPositionRotation(const glm::vec3& pos, const glm::quat& rot);
    static Transform fromTRS(const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale);
};

// =============================================================================
// Local Transform Component (Parent Space)
// =============================================================================
// Stores transformation relative to parent entity. Used with Parent component
// for hierarchical transforms. The world Transform is computed by the
// updateWorldTransforms() system.

struct LocalTransform {
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Identity quaternion
    glm::vec3 scale = glm::vec3(1.0f);

    LocalTransform() = default;
    LocalTransform(const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scl)
        : position(pos), rotation(rot), scale(scl) {}

    // Compute local matrix from TRS
    [[nodiscard]] glm::mat4 toMatrix() const;

    // Static factory methods
    static LocalTransform fromPosition(const glm::vec3& pos) {
        return LocalTransform(pos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
    }

    static LocalTransform fromPositionRotation(const glm::vec3& pos, const glm::quat& rot) {
        return LocalTransform(pos, rot, glm::vec3(1.0f));
    }

    static LocalTransform identity() {
        return LocalTransform();
    }
};

// =============================================================================
// Hierarchy Components
// =============================================================================
// Parent-child relationships for hierarchical transforms.
// Entities with a Parent component have their world Transform computed from
// their LocalTransform combined with their parent's world Transform.

// Parent reference - links an entity to its parent
struct Parent {
    Entity entity = NullEntity;

    Parent() = default;
    explicit Parent(Entity e) : entity(e) {}

    [[nodiscard]] bool valid() const { return entity != NullEntity; }
};

// Children list - optional component for efficient top-down traversal
// Not strictly required (can query Parent components), but speeds up
// hierarchical operations when iterating from parents to children.
struct Children {
    std::vector<Entity> entities;

    Children() = default;

    void add(Entity child) { entities.push_back(child); }

    void remove(Entity child) {
        auto it = std::find(entities.begin(), entities.end(), child);
        if (it != entities.end()) {
            entities.erase(it);
        }
    }

    [[nodiscard]] bool empty() const { return entities.empty(); }
    [[nodiscard]] size_t count() const { return entities.size(); }
};

// Hierarchy depth - cached depth for sorting during transform updates
// Root entities have depth 0, their children depth 1, etc.
struct HierarchyDepth {
    uint16_t depth = 0;

    HierarchyDepth() = default;
    explicit HierarchyDepth(uint16_t d) : depth(d) {}
};

// =============================================================================
// Mesh Reference Component
// =============================================================================
// Points to a shared Mesh resource. Multiple entities can reference the same
// mesh for instanced rendering.

struct MeshRef {
    Mesh* mesh = nullptr;

    MeshRef() = default;
    explicit MeshRef(Mesh* m) : mesh(m) {}

    [[nodiscard]] bool valid() const { return mesh != nullptr; }
};

// =============================================================================
// Material Reference Component
// =============================================================================
// References a material by ID for descriptor set lookup. Entities with the
// same material can be batched together for efficient rendering.

using MaterialId = uint32_t;
constexpr MaterialId InvalidMaterialId = UINT32_MAX;

struct MaterialRef {
    MaterialId id = InvalidMaterialId;

    MaterialRef() = default;
    explicit MaterialRef(MaterialId matId) : id(matId) {}

    [[nodiscard]] bool valid() const { return id != InvalidMaterialId; }
};

// =============================================================================
// PBR Material Properties Component
// =============================================================================
// Per-entity material overrides. Only entities that need custom PBR values
// should have this component - others use material defaults.

struct PBRProperties {
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emissiveIntensity = 0.0f;
    glm::vec3 emissiveColor = glm::vec3(1.0f);
    float alphaTestThreshold = 0.0f;
    uint32_t pbrFlags = 0;

    PBRProperties() = default;
};

// =============================================================================
// Render Tag Components (Zero-size markers)
// =============================================================================
// Tag components for controlling render pass participation.
// These are sparse - only entities needing the feature have the tag.

// Entity casts shadows (participates in shadow pass)
struct CastsShadow {};

// Entity is visible (set by culling system, queried by render system)
struct Visible {};

// Entity should be rendered with transparency
struct Transparent {
    float sortKey = 0.0f;  // For back-to-front sorting
};

// Entity participates in reflection rendering
struct Reflective {};

// =============================================================================
// Bounds Components
// =============================================================================
// Used for culling. Only one bounds type needed per entity.

struct BoundingSphere {
    glm::vec3 center = glm::vec3(0.0f);
    float radius = 0.0f;

    BoundingSphere() = default;
    BoundingSphere(const glm::vec3& c, float r) : center(c), radius(r) {}
};

struct BoundingBox {
    glm::vec3 min = glm::vec3(0.0f);
    glm::vec3 max = glm::vec3(0.0f);

    BoundingBox() = default;
    BoundingBox(const glm::vec3& minPt, const glm::vec3& maxPt) : min(minPt), max(maxPt) {}

    [[nodiscard]] glm::vec3 center() const { return (min + max) * 0.5f; }
    [[nodiscard]] glm::vec3 extents() const { return (max - min) * 0.5f; }
};

// =============================================================================
// Visual Effect Components
// =============================================================================
// Sparse components for optional visual effects.

// Hue shift for tinting (used by NPCs)
struct HueShift {
    float value = 0.0f;

    HueShift() = default;
    explicit HueShift(float v) : value(v) {}
};

// Opacity for fade effects (camera occlusion)
struct Opacity {
    float value = 1.0f;

    Opacity() = default;
    explicit Opacity(float v) : value(v) {}
};

// =============================================================================
// Dynamic Effect Overlay Components (Phase 4.3)
// =============================================================================
// Sparse components for runtime visual effects. Only entities with active
// effects have these components - others render with default appearance.

// Wetness overlay - simulates wet/rain effects on surfaces
struct WetnessOverlay {
    float wetness = 0.0f;  // 0.0 = dry, 1.0 = fully soaked

    WetnessOverlay() = default;
    explicit WetnessOverlay(float w) : wetness(w) {}

    // Helper to check if effect is active
    [[nodiscard]] bool isActive() const { return wetness > 0.001f; }
};

// Damage overlay - simulates damage/wear effects on surfaces
struct DamageOverlay {
    float damage = 0.0f;   // 0.0 = pristine, 1.0 = fully destroyed

    DamageOverlay() = default;
    explicit DamageOverlay(float d) : damage(d) {}

    // Helper to check if effect is active
    [[nodiscard]] bool isActive() const { return damage > 0.001f; }
};

// Selection outline - highlights selected entities with colored outline
struct SelectionOutline {
    glm::vec3 color = glm::vec3(1.0f, 0.8f, 0.0f);  // Default: golden yellow
    float thickness = 2.0f;                          // Outline thickness in pixels
    float pulseSpeed = 0.0f;                         // 0 = no pulse, >0 = pulse frequency

    SelectionOutline() = default;
    SelectionOutline(const glm::vec3& c, float t = 2.0f, float p = 0.0f)
        : color(c), thickness(t), pulseSpeed(p) {}

    // Preset styles
    static SelectionOutline selected() { return SelectionOutline(glm::vec3(1.0f, 0.8f, 0.0f), 2.0f); }
    static SelectionOutline hovered() { return SelectionOutline(glm::vec3(0.5f, 0.8f, 1.0f), 1.5f); }
    static SelectionOutline error() { return SelectionOutline(glm::vec3(1.0f, 0.2f, 0.2f), 3.0f, 2.0f); }
};

// =============================================================================
// Tree-specific Components
// =============================================================================
// Only tree entities have these components.

struct TreeData {
    int leafInstanceIndex = -1;
    int treeInstanceIndex = -1;
    glm::vec3 leafTint = glm::vec3(1.0f);
    float autumnHueShift = 0.0f;
};

struct BarkType {
    uint32_t typeIndex = 0;  // Index into bark texture array
};

struct LeafType {
    uint32_t typeIndex = 0;  // Index into leaf texture array
};

// =============================================================================
// LOD Component
// =============================================================================
// Unified LOD management across all entity types.

struct LODController {
    float thresholds[3] = {50.0f, 150.0f, 500.0f};  // Distance thresholds
    uint8_t currentLevel = 0;                        // 0=high, 1=medium, 2=low
    uint16_t updateInterval = 1;                     // Frames between updates
    uint16_t frameCounter = 0;

    LODController() = default;
    explicit LODController(float near, float mid, float far)
        : thresholds{near, mid, far} {}
};

// =============================================================================
// Bone/Skeleton Attachment Component
// =============================================================================
// Attaches an entity's transform to a bone in a skeletal hierarchy.
// The bone transform is provided externally (from AnimatedCharacter) and
// combined with the local offset to produce the entity's world transform.

struct BoneAttachment {
    int32_t boneIndex = -1;           // Index into skeleton's bone array
    glm::mat4 localOffset;            // Offset from bone (rotation, translation)

    BoneAttachment() : localOffset(glm::mat4(1.0f)) {}
    BoneAttachment(int32_t bone, const glm::mat4& offset)
        : boneIndex(bone), localOffset(offset) {}

    [[nodiscard]] bool valid() const { return boneIndex >= 0; }
};

// External transform source - for entities driven by external systems
// (physics bodies, bones, procedural animation, etc.)
// The source provides a world-space transform that becomes this entity's base.
// If the entity also has LocalTransform, it's applied as an additional offset.
struct ExternalTransformSource {
    // Pointer to external transform matrix (must remain valid!)
    // This allows entities to follow transforms managed elsewhere.
    const glm::mat4* sourceMatrix = nullptr;

    ExternalTransformSource() = default;
    explicit ExternalTransformSource(const glm::mat4* src) : sourceMatrix(src) {}

    [[nodiscard]] bool valid() const { return sourceMatrix != nullptr; }
};

// =============================================================================
// Physics Component
// =============================================================================
// Links an entity to a physics body.

using PhysicsBodyId = uint32_t;
constexpr PhysicsBodyId InvalidPhysicsBodyId = UINT32_MAX;

struct PhysicsBody {
    PhysicsBodyId bodyId = InvalidPhysicsBodyId;

    PhysicsBody() = default;
    explicit PhysicsBody(PhysicsBodyId id) : bodyId(id) {}

    [[nodiscard]] bool valid() const { return bodyId != InvalidPhysicsBodyId; }
};

// =============================================================================
// Name/Debug Component
// =============================================================================
// Optional debug name for entities (development only).

struct DebugName {
    const char* name = nullptr;

    DebugName() = default;
    explicit DebugName(const char* n) : name(n) {}
};

// =============================================================================
// Tag Components for Special Entities
// =============================================================================
// Zero-size markers for querying specific entity types.
// Eliminates need for hardcoded index tracking.

// Player character entity
struct PlayerTag {};

// Player cape (cloth simulation)
struct CapeTag {};

// Flag pole and cloth
struct FlagPoleTag {};
struct FlagClothTag {};

// Emissive orb (has corresponding light)
struct OrbTag {};

// Weapon slots
enum class WeaponSlot : uint8_t {
    RightHand = 0,  // Sword
    LeftHand = 1    // Shield
};

struct WeaponTag {
    WeaponSlot slot;

    WeaponTag() : slot(WeaponSlot::RightHand) {}
    explicit WeaponTag(WeaponSlot s) : slot(s) {}
};

// NPC entity marker
struct NPCTag {
    uint32_t templateIndex = 0;  // Index into character templates

    NPCTag() = default;
    explicit NPCTag(uint32_t idx) : templateIndex(idx) {}
};

// Well entrance (terrain hole marker)
struct WellEntranceTag {};

// =============================================================================
// NPC Animation Components
// =============================================================================
// Components for NPC skeletal animation, enabling ECS-driven animation updates.

// NPC activity states for animation variety
enum class NPCActivity : uint8_t {
    Idle = 0,       // Standing still
    Walking = 1,    // Slow movement (walk animation)
    Running = 2     // Fast movement (run animation)
};

// Animation playback state - per-NPC animation control
struct NPCAnimationState {
    size_t clipIndex = 0;          // Index into template's animation clips
    float currentTime = 0.0f;      // Current playback position in seconds
    float playbackSpeed = 1.0f;    // Speed multiplier
    float blendWeight = 1.0f;      // Blend weight for transitions
    bool looping = true;           // Whether to loop at end
    NPCActivity activity = NPCActivity::Idle;  // Current activity state

    NPCAnimationState() = default;
};

// Cached bone matrices for LOD-based update skipping
// When an NPC is far away, we skip animation updates and reuse cached matrices
struct NPCBoneCache {
    std::vector<glm::mat4> matrices;
    uint32_t lastUpdateFrame = 0;

    NPCBoneCache() = default;

    void resize(size_t boneCount) {
        if (matrices.size() != boneCount) {
            matrices.resize(boneCount, glm::mat4(1.0f));
        }
    }
};

// NPC LOD level (mirrors NPCLODLevel from NPCData.h for ECS)
enum class NPCLODLevel : uint8_t {
    Virtual = 0,  // >50m: No rendering, minimal updates (every 10 seconds)
    Bulk = 1,     // 25-50m: Simplified animation, reduced updates (every 1 second)
    Real = 2      // <25m: Full animation every frame
};

// NPC-specific LOD controller with tiered update scheduling
struct NPCLODController {
    NPCLODLevel level = NPCLODLevel::Real;
    uint32_t framesSinceUpdate = 0;

    // LOD distance thresholds
    static constexpr float DISTANCE_REAL = 25.0f;
    static constexpr float DISTANCE_BULK = 50.0f;

    // Update intervals (in frames) per LOD level
    static constexpr uint32_t INTERVAL_REAL = 1;      // Every frame
    static constexpr uint32_t INTERVAL_BULK = 60;     // ~1 second at 60fps
    static constexpr uint32_t INTERVAL_VIRTUAL = 600; // ~10 seconds at 60fps

    NPCLODController() = default;

    // Check if this NPC should update this frame
    [[nodiscard]] bool shouldUpdate() const {
        uint32_t interval = INTERVAL_REAL;
        switch (level) {
            case NPCLODLevel::Bulk: interval = INTERVAL_BULK; break;
            case NPCLODLevel::Virtual: interval = INTERVAL_VIRTUAL; break;
            default: break;
        }
        return framesSinceUpdate >= interval;
    }

    // Get the movement speed for animation based on activity
    [[nodiscard]] static float getMovementSpeed(NPCActivity activity) {
        switch (activity) {
            case NPCActivity::Walking: return 1.5f;  // Walk speed (m/s)
            case NPCActivity::Running: return 5.0f;  // Run speed (m/s)
            default: return 0.0f;  // Idle
        }
    }
};

// Skinned mesh reference - links NPC to its AnimatedCharacter instance
// For Phase 2, each NPC still has its own AnimatedCharacter
// Phase 2.2 will introduce shared archetypes
struct SkinnedMeshRef {
    void* character = nullptr;  // Pointer to AnimatedCharacter (type-erased to avoid header dependency)
    size_t npcIndex = 0;        // Index into NPCSimulation's characters array

    SkinnedMeshRef() = default;
    SkinnedMeshRef(void* c, size_t idx) : character(c), npcIndex(idx) {}

    [[nodiscard]] bool valid() const { return character != nullptr; }
};

// NPC facing direction (yaw in degrees)
struct NPCFacing {
    float yawDegrees = 0.0f;

    NPCFacing() = default;
    explicit NPCFacing(float yaw) : yawDegrees(yaw) {}

    [[nodiscard]] float yawRadians() const { return glm::radians(yawDegrees); }
};

// =============================================================================
// Animation Archetype Components (Phase 2.2)
// =============================================================================
// Components for shared animation archetypes, enabling O(character types)
// memory usage instead of O(n NPCs).

// Invalid archetype ID constant
constexpr uint32_t InvalidArchetypeId = UINT32_MAX;

// Reference to a shared animation archetype
// NPCs with the same character type share skeleton and animation clip data
struct AnimationArchetypeRef {
    uint32_t archetypeId = InvalidArchetypeId;

    AnimationArchetypeRef() = default;
    explicit AnimationArchetypeRef(uint32_t id) : archetypeId(id) {}

    [[nodiscard]] bool valid() const { return archetypeId != InvalidArchetypeId; }
};

// Per-NPC animation instance state when using shared archetypes
// Contains only the per-instance data (time, blend state, cached matrices)
struct NPCAnimationInstance {
    // Current animation playback
    size_t currentClipIndex = 0;
    float currentTime = 0.0f;
    float playbackSpeed = 1.0f;
    bool looping = true;

    // Transition/blend state
    size_t previousClipIndex = 0;
    float previousTime = 0.0f;
    float blendWeight = 1.0f;
    float blendDuration = 0.0f;
    float blendElapsed = 0.0f;
    bool isBlending = false;

    // LOD level for bone detail
    uint32_t lodLevel = 0;

    // Cached bone matrices (computed during update)
    std::vector<glm::mat4> boneMatrices;
    uint32_t lastUpdateFrame = 0;

    NPCAnimationInstance() = default;

    // Start blending to a new animation
    void startBlend(size_t newClipIndex, float duration) {
        if (newClipIndex == currentClipIndex && !isBlending) return;

        previousClipIndex = currentClipIndex;
        previousTime = currentTime;
        currentClipIndex = newClipIndex;
        currentTime = 0.0f;
        blendWeight = 0.0f;
        blendDuration = duration;
        blendElapsed = 0.0f;
        isBlending = true;
    }

    // Update blend progress
    void updateBlend(float deltaTime) {
        if (!isBlending) return;

        blendElapsed += deltaTime;
        if (blendElapsed >= blendDuration) {
            blendWeight = 1.0f;
            isBlending = false;
        } else {
            blendWeight = blendElapsed / blendDuration;
        }
    }

    // Resize bone matrix buffer
    void resizeBoneMatrices(uint32_t boneCount) {
        if (boneMatrices.size() != boneCount) {
            boneMatrices.resize(boneCount, glm::mat4(1.0f));
        }
    }
};

// =============================================================================
// Light Components
// =============================================================================
// Light components for ECS-driven lighting. Position/direction comes from the
// entity's Transform component. Multiple light types can be attached to entities
// to create complex lighting setups.

// Light type enum (matches shader LightType)
enum class LightType : uint32_t {
    Point = 0,
    Spot = 1,
    Directional = 2
};

// Base light properties shared across light types
struct LightProperties {
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    float priority = 1.0f;        // Higher = more important for culling
    int32_t shadowMapIndex = -1;  // -1 = no shadow, >= 0 = shadow map index
    bool castsShadows = true;
    bool enabled = true;

    LightProperties() = default;
    LightProperties(const glm::vec3& col, float intens)
        : color(col), intensity(intens) {}
};

// Point light component - omnidirectional light
// Position comes from entity's Transform
struct PointLightComponent {
    LightProperties properties;
    float radius = 10.0f;         // Falloff radius

    PointLightComponent() = default;
    PointLightComponent(const glm::vec3& color, float intensity, float rad = 10.0f)
        : properties(color, intensity), radius(rad) {}

    // Factory for common light types
    static PointLightComponent torch(float intensity = 2.0f) {
        return PointLightComponent(glm::vec3(1.0f, 0.7f, 0.4f), intensity, 8.0f);
    }

    static PointLightComponent orb(const glm::vec3& color, float intensity = 1.5f) {
        return PointLightComponent(color, intensity, 12.0f);
    }
};

// Spot light component - directional cone light
// Position comes from entity's Transform
// Direction comes from entity's Transform rotation (default = -Y)
struct SpotLightComponent {
    LightProperties properties;
    float radius = 15.0f;         // Falloff radius
    float innerConeAngle = 30.0f; // Degrees - full intensity inside this cone
    float outerConeAngle = 45.0f; // Degrees - light fades to zero at this angle

    SpotLightComponent() = default;
    SpotLightComponent(const glm::vec3& color, float intensity,
                       float rad = 15.0f, float innerAngle = 30.0f, float outerAngle = 45.0f)
        : properties(color, intensity), radius(rad),
          innerConeAngle(innerAngle), outerConeAngle(outerAngle) {}

    // Factory for common spot light types
    static SpotLightComponent lantern(float intensity = 3.0f) {
        return SpotLightComponent(glm::vec3(1.0f, 0.9f, 0.7f), intensity, 20.0f, 25.0f, 40.0f);
    }

    static SpotLightComponent streetLight(float intensity = 4.0f) {
        return SpotLightComponent(glm::vec3(1.0f, 0.95f, 0.8f), intensity, 25.0f, 35.0f, 55.0f);
    }
};

// Directional light component - parallel rays (sun/moon)
// Direction comes from entity's Transform rotation (default = -Y pointing down)
// No position falloff - affects entire scene
struct DirectionalLightComponent {
    LightProperties properties;
    float angularDiameter = 0.53f;  // Degrees - sun is ~0.53 degrees

    DirectionalLightComponent() = default;
    DirectionalLightComponent(const glm::vec3& color, float intensity, float angularDiam = 0.53f)
        : properties(color, intensity), angularDiameter(angularDiam) {
        // Directional lights usually cast shadows
        properties.castsShadows = true;
        properties.priority = 10.0f;  // High priority - always rendered
    }

    // Factory for common directional lights
    static DirectionalLightComponent sun(float intensity = 1.0f) {
        return DirectionalLightComponent(glm::vec3(1.0f, 0.98f, 0.95f), intensity, 0.53f);
    }

    static DirectionalLightComponent moon(float intensity = 0.15f) {
        return DirectionalLightComponent(glm::vec3(0.8f, 0.85f, 1.0f), intensity, 0.52f);
    }
};

// =============================================================================
// Light Flicker Component
// =============================================================================
// Adds flickering effect to lights (torches, candles, fires).
// Modulates the light's intensity over time using noise-based animation.

struct LightFlickerComponent {
    // Flicker parameters
    float baseIntensity = 1.0f;       // Original intensity (set from light component)
    float flickerAmount = 0.3f;       // How much to vary (0.0 = no flicker, 1.0 = full off-on)
    float flickerSpeed = 8.0f;        // Oscillations per second
    float noiseScale = 2.0f;          // Noise frequency multiplier

    // Animation state
    float phase = 0.0f;               // Current phase offset (randomized per light)
    float currentModifier = 1.0f;     // Current intensity multiplier (0.0 to 1.0)

    LightFlickerComponent() = default;
    LightFlickerComponent(float amount, float speed, float noise = 2.0f)
        : flickerAmount(amount), flickerSpeed(speed), noiseScale(noise) {}

    // Factory methods for common flicker patterns
    static LightFlickerComponent torch() {
        LightFlickerComponent flicker;
        flicker.flickerAmount = 0.25f;
        flicker.flickerSpeed = 10.0f;
        flicker.noiseScale = 3.0f;
        return flicker;
    }

    static LightFlickerComponent candle() {
        LightFlickerComponent flicker;
        flicker.flickerAmount = 0.4f;
        flicker.flickerSpeed = 12.0f;
        flicker.noiseScale = 4.0f;
        return flicker;
    }

    static LightFlickerComponent fire() {
        LightFlickerComponent flicker;
        flicker.flickerAmount = 0.35f;
        flicker.flickerSpeed = 6.0f;
        flicker.noiseScale = 2.5f;
        return flicker;
    }

    static LightFlickerComponent subtle() {
        LightFlickerComponent flicker;
        flicker.flickerAmount = 0.1f;
        flicker.flickerSpeed = 4.0f;
        flicker.noiseScale = 1.5f;
        return flicker;
    }
};

// Tag component for entities that are light sources
// Used for quick identification without checking each light component type
struct LightSourceTag {};
} // namespace ecs
