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

} // namespace ecs
