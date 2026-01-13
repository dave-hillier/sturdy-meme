# ECS Scene Graph Architecture

This document outlines the architecture for integrating the EnTT ECS with the rendering system to create a Unity-style scene graph.

## Current State

### What We Have
- **EnTT ECS** with components for Transform, Velocity, Physics, Lights, AI, etc.
- **ImGui Scene Graph** with hierarchy tree view and inspector
- **SceneBuilder** with `std::vector<Renderable>` for rendered objects
- **Multiple Render Systems** (~30 subsystems for terrain, water, grass, etc.)

### The Gap
The ECS and rendering systems are currently loosely coupled:
- `RenderableRef` links entities to SceneBuilder indices (migration path)
- Most renderable objects aren't ECS entities
- Render systems don't query the ECS registry

## Target Architecture

### Core Principle: ECS as Single Source of Truth

All scene objects should be ECS entities. The renderer queries the registry for what to render.

```
ECS Registry (Source of Truth)
    │
    ├── Query: entities with (Transform, MeshRenderer)
    │         → Main geometry pass
    │
    ├── Query: entities with (Transform, PointLight, LightEnabled)
    │         → Light culling & shadow pass
    │
    ├── Query: entities with (Transform, Camera, ActiveCamera)
    │         → Determine view matrix
    │
    └── Query: entities with (Transform, SkinnedMeshRenderer)
              → Animated geometry pass
```

## Proposed Components

### Rendering Components

```cpp
// Mesh reference - links to GPU resources
struct MeshRenderer {
    MeshHandle mesh;           // Handle to GPU mesh buffer
    MaterialHandle material;   // Handle to material/textures
    uint32_t submeshIndex{0};  // For multi-part meshes
    bool castsShadow{true};
    bool receiveShadow{true};
    RenderLayer layer{RenderLayer::Default};
};

// For animated meshes
struct SkinnedMeshRenderer {
    MeshHandle mesh;
    MaterialHandle material;
    SkeletonHandle skeleton;
    AnimationHandle currentAnim;
    float animTime{0.0f};
};

// Particle system attachment
struct ParticleEmitter {
    ParticleSystemHandle system;
    bool playing{true};
    float playbackSpeed{1.0f};
};

// Billboard/sprite
struct SpriteRenderer {
    TextureHandle texture;
    glm::vec2 size{1.0f};
    glm::vec4 color{1.0f};
    BillboardMode mode{BillboardMode::FaceCamera};
};

// Decal projection
struct Decal {
    MaterialHandle material;
    glm::vec3 size{1.0f};
    float fadeDistance{5.0f};
};
```

### Camera Components

```cpp
struct Camera {
    float fov{60.0f};
    float nearPlane{0.1f};
    float farPlane{1000.0f};
    int priority{0};           // Higher = more likely to be main camera
    uint32_t cullingMask{~0u}; // Layer mask for culling
};

// Tag for the active rendering camera
struct MainCamera {};

// For render-to-texture
struct RenderTarget {
    TextureHandle targetTexture;
    uint32_t width, height;
};
```

### Culling Components

```cpp
// Bounding volume for frustum culling
struct BoundingBox {
    glm::vec3 min, max;
};

struct BoundingSphere {
    glm::vec3 center;  // Local space offset
    float radius;
};

// LOD control
struct LODGroup {
    std::vector<float> distances;    // Switch distances
    std::vector<MeshHandle> meshes;  // Mesh per LOD level
    int currentLOD{0};
};

// Occlusion culling
struct OcclusionCullable {
    bool wasVisible{true};
    uint32_t queryId{0};
};
```

### Environment Components

```cpp
// Terrain chunk attachment
struct TerrainPatch {
    uint32_t tileX, tileZ;
    uint32_t lod;
};

// Grass instance group
struct GrassVolume {
    glm::vec3 extents;
    float density{1.0f};
};

// Water body
struct WaterSurface {
    float height;
    WaterType type;
};
```

## Rendering Pipeline Integration

### Approach 1: Query-Based Rendering (Recommended)

The renderer queries ECS each frame:

```cpp
void RenderSystem::renderScene(entt::registry& registry, const Camera& mainCam) {
    // Frustum cull all renderables
    auto frustum = mainCam.getFrustum();

    // Gather opaque geometry
    std::vector<RenderItem> opaqueItems;
    auto meshView = registry.view<WorldTransform, MeshRenderer, BoundingSphere>();
    for (auto [entity, transform, mesh, bounds] : meshView.each()) {
        if (!frustumTest(frustum, transform.position + bounds.center, bounds.radius))
            continue;
        opaqueItems.push_back({
            .transform = transform.matrix,
            .mesh = mesh.mesh,
            .material = mesh.material,
            .distance = glm::distance(mainCam.position, transform.position)
        });
    }

    // Sort front-to-back for depth optimization
    std::sort(opaqueItems.begin(), opaqueItems.end(),
              [](auto& a, auto& b) { return a.distance < b.distance; });

    // Submit draw calls
    for (auto& item : opaqueItems) {
        drawMesh(item.transform, item.mesh, item.material);
    }
}
```

### Approach 2: Cached Render List

For performance, maintain a sorted list that updates incrementally:

```cpp
struct RenderList {
    std::vector<entt::entity> opaque;
    std::vector<entt::entity> transparent;
    std::vector<entt::entity> shadowCasters;
    bool dirty{true};
};

// System that maintains render list
void RenderListSystem::update(entt::registry& registry, RenderList& list) {
    if (!list.dirty) return;

    list.opaque.clear();
    list.transparent.clear();
    list.shadowCasters.clear();

    auto view = registry.view<WorldTransform, MeshRenderer>();
    for (auto entity : view) {
        auto& mesh = view.get<MeshRenderer>(entity);

        if (mesh.isTransparent) {
            list.transparent.push_back(entity);
        } else {
            list.opaque.push_back(entity);
        }

        if (mesh.castsShadow) {
            list.shadowCasters.push_back(entity);
        }
    }

    list.dirty = false;
}
```

### Light Integration

Lights are already ECS entities with PointLight/SpotLight. Connect to renderer:

```cpp
void LightSystem::gatherLights(entt::registry& registry, LightBuffer& buffer) {
    buffer.clear();

    // Point lights
    auto pointView = registry.view<WorldTransform, PointLight, LightEnabled>();
    for (auto [entity, transform, light] : pointView.each()) {
        buffer.addPointLight({
            .position = transform.position,
            .color = light.color,
            .intensity = light.intensity,
            .radius = light.radius
        });
    }

    // Spot lights
    auto spotView = registry.view<WorldTransform, SpotLight, LightEnabled>();
    for (auto [entity, transform, light] : spotView.each()) {
        buffer.addSpotLight({
            .position = transform.position,
            .direction = light.direction,
            .color = light.color,
            .intensity = light.intensity,
            .innerCone = light.innerConeAngle,
            .outerCone = light.outerConeAngle
        });
    }
}
```

## Migration Strategy

### Phase 1: Foundation (Current)
- [x] Hierarchy component and SceneGraphSystem
- [x] ImGui scene graph and inspector
- [x] Selection and entity creation in UI

### Phase 2: Camera & Lights
- [x] Camera component with priority system
- [x] Light system queries ECS instead of LightManager
- [x] Shadow caster selection via ECS

### Phase 3: Static Geometry
- [x] MeshRenderer component
- [x] Resource handles (MeshHandle, MaterialHandle)
- [x] Convert SceneBuilder renderables to entities
- [x] Basic frustum culling via ECS

### Phase 4: Dynamic Objects
- [x] Physics-driven entities use ECS transforms (PhysicsIntegration.h)
- [x] Animated meshes as entities (AnimationIntegration.h, Animator, AnimationState)
- [x] Particle emitters as entities (ParticleEmitter, ParticleParams)

### Phase 5: Environment
- [x] Terrain as ECS (TerrainPatch, TerrainConfig components)
- [x] Grass volumes as entities (GrassVolume, GrassTile components)
- [x] Water bodies as entities (WaterSurface, RiverSpline, LakeBody components)
- [x] Tree instances as entities (TreeInstance, TreeLODState, VegetationZone)
- [x] Wind/Weather zones (WindZone, WeatherZone, FogVolume)
- [x] EnvironmentIntegration.h with factory functions and utilities

### Phase 6: Advanced
- [x] LOD groups (LODGroup component, TreeLODState for trees)
- [x] Occlusion culling (OcclusionCullingIntegration.h, Hi-Z system integration)
- [x] Render layers/masks (RenderLayer enum, cullingMask in CameraComponent)
- [x] Multi-camera support (CameraComponent with priority)

### Phase 7: Extended Rendering
- [x] Decal component (projected textures on surfaces)
- [x] SpriteRenderer (billboard sprites with atlas support)
- [x] RenderTarget (render-to-texture for cameras, mirrors, portals)
- [x] ReflectionProbe (local cubemap reflections)
- [x] LightProbe (spherical harmonics for indirect lighting)
- [x] LightProbeVolume (grid of probes for interpolation)
- [x] PortalSurface (portals and mirrors)
- [x] RenderingIntegration.h with factory functions and utilities

### Phase 8: Audio
- [x] AudioSource (spatial 3D audio emitter with rolloff, doppler, cone attenuation)
- [x] AudioListener (active listener tag with volume and velocity)
- [x] AudioMixerGroup (hierarchical audio mixing with parent groups)
- [x] OneShotAudio (temporary audio entities that auto-destroy)
- [x] AmbientSoundZone (spatial audio zones for ambient sounds)
- [x] ReverbZone (environmental reverb effects with presets)
- [x] AudioOcclusion (ray-traced occlusion for audio sources)
- [x] MusicTrack (music playback with crossfading state machine)
- [x] AudioIntegration.h (factory functions, spatial audio calculations, music system)

### Phase 9: Gameplay Systems
- [x] TriggerVolume (box, sphere, capsule triggers with enter/exit/stay events)
- [x] Triggerable, InsideTrigger (entities that can activate triggers)
- [x] NavMeshAgent (AI pathfinding with speed, acceleration, avoidance, status)
- [x] Waypoint, WaypointPath, OnNavMesh (pathfinding waypoints and status)
- [x] Interactable, CanInteract (interaction system with radius, angle, priority)
- [x] Pickup (collectable items with bobbing, respawn, quantity)
- [x] Door (hinged/sliding doors with locked state, auto-close)
- [x] Switch (toggle, hold, one-shot switches with target entities)
- [x] SpawnPoint, IsSpawnPoint (entity spawners with respawn delay)
- [x] Checkpoint (save/respawn points with activation state)
- [x] DamageZone (area damage with type: fire, ice, poison, electric, void)
- [x] DialogueTrigger, IsDialogueNPC (dialogue system hooks)
- [x] QuestMarker (objective markers with map/compass display)
- [x] GameplayIntegration.h (trigger callbacks, interaction system, door/pickup updates)

### Phase 10: Composable Utility Components
- [x] Timer (generic reusable timer with looping, auto-remove, events)
- [x] Cooldown (charges, regeneration, cooldown reduction support)
- [x] StatusEffect, StatusEffects (buff/debuff system with stacking, duration, ticks)
- [x] Team (faction membership with hostile/friendly/neutral masks)
- [x] Target (targeting system with auto-target, lock-on, priority modes)
- [x] FollowTarget, OrbitTarget (movement behaviors for following/orbiting entities)
- [x] Lifetime, DelayedAction (lifecycle management with auto-destroy, fade-out)
- [x] SpawnOnDestroy (spawn entities when destroyed)
- [x] DamageDealer (damage dealing with types, knockback, crits, status effects)
- [x] DamageReceiver (damage receiving with resistances, armor, i-frames)
- [x] HitReaction (knockback, stun, hit-stop for combat feedback)
- [x] Projectile (speed, homing, gravity, piercing, trails)
- [x] StateMachine (generic state machine with transition blending)
- [x] ThreatTable (aggro/threat tracking for AI combat)
- [x] LootTable (probabilistic loot drops)
- [x] Tags: IsProjectile, HasStatusEffects, Invulnerable, Stunned, Dead, IsTeamMember, Targetable, NoGravity, CustomPhysics

### Future: Component Simplification

The current components are verbose. Extract reusable building blocks:

#### Reusable Building Blocks

```cpp
// Spatial volume (used by triggers, zones, fog, grass, etc.)
struct Volume {
    glm::vec3 extents{10.0f};
    bool isGlobal{false};
};

// Grid cell (used by terrain, grass tiles)
struct GridCell {
    int32_t x{0}, z{0};
    uint32_t lod{0};
};

// Mesh instance (used by trees, rocks, detritus)
struct MeshVariant {
    uint32_t variant{0};
    float scale{1.0f};
    glm::vec3 rotation{0.0f};
};

// Shadow/collision flags (shared by many renderables)
struct RenderFlags {
    bool castsShadow{true};
    bool hasCollision{true};
};

// Timed behavior (replaces duration/elapsed in Timer, Lifetime, Cooldown)
struct Timed {
    float duration{1.0f};
    float elapsed{0.0f};
    bool paused{false};
    float progress() const { return duration > 0 ? elapsed / duration : 1.0f; }
};

// Event reference (replaces uint32_t onXxxEvent fields)
struct EventRef {
    uint32_t eventId{~0u};
    bool hasEvent() const { return eventId != ~0u; }
};

// Priority ordering (used by lights, cameras, sounds, interactables)
struct Priority {
    int value{0};                      // Higher = more important
};

// Shape definition (used by triggers, emitters, zones)
enum class ShapeType : uint8_t { Box, Sphere, Capsule, Cone };
struct Shape {
    ShapeType type{ShapeType::Box};
    glm::vec3 extents{1.0f};           // Half-extents for box
    float radius{1.0f};                // For sphere/capsule/cone
    float height{2.0f};                // For capsule/cone
};

// Speed settings (used by agents, followers, movers)
struct SpeedSettings {
    float speed{3.5f};
    float acceleration{8.0f};
    float angularSpeed{120.0f};        // Degrees per second
};
```

#### Pattern Usage Summary

| Building Block | Used By |
|---------------|---------|
| `Volume` | TriggerVolume, GrassVolume, FogVolume, WeatherZone, AmbientSoundZone, ReverbZone, DamageZone |
| `GridCell` | TerrainPatch, GrassTile |
| `MeshVariant` | TreeInstance, RockInstance, DetritusInstance |
| `RenderFlags` | TreeInstance, RockInstance, MeshRenderer |
| `Timed` | Timer, Lifetime, Cooldown, StatusEffect, AnimationState |
| `EventRef` | TriggerVolume, Interactable, Switch, Timer, Lifetime, DelayedAction, DamageReceiver |
| `Priority` | LightBase, CameraComponent, AudioSource, Interactable, ReflectionProbe |
| `Shape` | TriggerVolume, ParticleEmitter |
| `SpeedSettings` | NavMeshAgent, FollowTarget, MovementSettings |

#### Complex Components to Decompose

**WaterSurface (30+ fields) → Composition:**
- `Water` (height, depth, color)
- `WaterWaves` (amplitude, length, steepness, speed)
- `WaterMaterial` (roughness, absorption, scattering, fresnel)
- `WaterFlow` (strength, speed, hasFlowMap)
- `WaterFeatures` (FFT, caustics, foam)
- `WaterTidal` (enabled, range)

**NavMeshAgent (25+ fields) → Composition:**
- `NavAgent` (navMesh, destination, status)
- `MovementSpeed` (speed, acceleration, angularSpeed) - reuse existing
- `Avoidance` (enabled, priority, radius)
- `OffMeshLinks` (canJump, canClimb, maxJump, maxClimb)

**DamageDealer → Composition:**
- `Damage` (amount, type)
- `Knockback` (force) - optional
- `CriticalHit` (chance, multiplier) - optional
- `AppliesEffect` (effectId, chance) - optional

#### Composite Presets (Factory Functions)

For usability, provide presets that combine building blocks:

```cpp
namespace Presets {
    // Creates: Water + WaterWaves + WaterFlow
    entt::entity createRiver(registry, position, controlPoints, width);

    // Creates: Water + WaterWaves + WaterFeatures(FFT, foam)
    entt::entity createOcean(registry, position, size);

    // Creates: Water only (calm)
    entt::entity createLake(registry, position, radius);

    // Creates: MeshVariant + RenderFlags + TreeArchetype + TreeLODState
    entt::entity createTree(registry, position, archetype);

    // Creates: Health + DamageReceiver + Team + Target + ThreatTable
    entt::entity createCombatant(registry, position, team);

    // Creates: Damage + Knockback + CriticalHit
    entt::entity createWeapon(registry, baseDamage, type);

    // Creates: Projectile + Damage + Lifetime + Velocity
    entt::entity createProjectile(registry, owner, position, direction, speed, damage);

    // Creates: TriggerVolume + Volume + IsTrigger
    entt::entity createTrigger(registry, position, extents);

    // Creates: Interactable + Pickup + IsInteractable
    entt::entity createPickup(registry, position, itemId, quantity);

    // Creates: Interactable + Door + IsInteractable
    entt::entity createDoor(registry, position, openAngle, locked);
}
```

#### Migration Path

1. Keep existing monolithic components working
2. Add building block components alongside
3. Refactor existing components to use building blocks internally
4. Update factory functions to use composition
5. Deprecate redundant fields in monolithic components

### System-Level Simplifications

Beyond ECS components, the render systems themselves have significant duplication:

#### InitInfo Consolidation

Almost every system has its own `InitInfo` struct with the same fields:

```cpp
// Current: Each system duplicates ~10 common fields
struct TreeRenderer::InitInfo {
    const vk::raii::Device* raiiDevice;
    vk::Device device;
    VmaAllocator allocator;
    DescriptorManager::Pool* descriptorPool;
    vk::RenderPass renderPass;
    std::string shaderPath;
    uint32_t framesInFlight;
    // ... plus system-specific fields
};
```

**Solution**: Already have `InitContext` - systems should use it plus system-specific extras:

```cpp
// Refactored: Use InitContext + system-specific
struct TreeRenderer::InitInfo {
    const InitContext& ctx;            // All common fields
    vk::RenderPass shadowRenderPass;   // System-specific only
    uint32_t shadowMapSize;
};
```

**Impact**: ~20+ systems with duplicate InitInfo fields could be simplified.

#### Billboard/Sprite Unification

Multiple systems implement billboard rendering independently:

| System | Billboard Use |
|--------|---------------|
| TreeImpostorAtlas | Octahedral impostor billboards |
| LeafSystem | Falling leaf quads |
| SpriteRenderer (ECS) | Generic billboards |
| GrassSystem | Grass blade quads |
| Particle systems | Point/quad particles |

**Solution**: Extract shared `BillboardRenderer`:

```cpp
// Shared billboard infrastructure
class BillboardRenderer {
public:
    struct Config {
        BillboardMode mode;       // FaceCamera, FaceCameraY, Fixed
        bool useAtlas;            // Atlas vs single texture
        bool castsShadow;
    };

    // Used by all billboard systems
    void renderBillboards(cmd, instances, texture, config);
    void renderShadows(cmd, instances, cascadeIndex);
};

// Systems compose with BillboardRenderer
class TreeImpostorSystem {
    BillboardRenderer billboardRenderer;  // Composition
    OctahedralAtlas atlas;               // System-specific
};
```

#### Culling Pattern Extraction

Similar GPU culling code exists in:
- `TreeLeafCulling` - Per-leaf frustum/occlusion culling
- `TreeBranchCulling` - Per-branch shadow culling
- `ImpostorCullSystem` - Per-impostor culling
- `WaterTileCull` - Water tile visibility
- `GrassTileManager` - Grass tile culling

**Solution**: Extract `GPUCullPass`:

```cpp
// Reusable GPU culling infrastructure
class GPUCullPass {
public:
    struct CullConfig {
        bool useFrustumCulling;
        bool useOcclusionCulling;
        bool useDistanceCulling;
        float maxDistance;
    };

    // Generic culling compute pass
    void dispatch(cmd, inputBuffer, outputBuffer, count, frustumPlanes, config);
    uint32_t getVisibleCount() const;
};
```

#### LOD System Generalization

LOD logic duplicated across:
- `TreeLODSystem` - Tree mesh/impostor switching
- `TerrainSystem` - Terrain tile LOD
- `GrassSystem` - Grass density LOD

**Solution**: Generic `LODEvaluator`:

```cpp
struct LODConfig {
    std::vector<float> distances;  // Or screen-space error thresholds
    float hysteresis;
    bool useScreenSpaceError;
};

class LODEvaluator {
public:
    int evaluate(float distance, float screenSize, LODConfig& config);
    float getBlendFactor() const;  // For smooth transitions
};
```

#### Systems Managing Their Own Entity Lists (Anti-Pattern)

These systems maintain `std::vector` of instances instead of using ECS:

| System | Status | Notes |
|--------|--------|-------|
| `RockSystem` | ✅ **Refactored** | Uses `SceneObjectCollection` composition pattern |
| `DetritusSystem` | ✅ **Refactored** | Uses `SceneObjectCollection` composition pattern |
| `TreeSystem::treeInstances_` | ⏳ Pending | Complex: procedural mesh generation per-tree |
| `TreeLODSystem::visibleImpostors_` | ⏳ Pending | Depends on TreeSystem refactoring |

**Note**: RockSystem and DetritusSystem were refactored to use `SceneObjectCollection` (from the scene graph refactoring) rather than direct ECS entities. This provides a clean composition pattern with unified instance management, but instances are stored in the collection rather than as individual ECS entities. This approach was chosen for performance (batch rendering) and simplicity.

**Current anti-pattern:**
```cpp
// TreeSystem owns instance data separately from ECS
class TreeSystem {
    std::vector<TreeInstanceData> treeInstances_;  // NOT in ECS
    void addTree(...) { treeInstances_.push_back(...); }
};
```

**Refactored to use ECS:**
```cpp
// Trees ARE entities - no separate vector needed
entt::entity createTree(entt::registry& reg, glm::vec3 pos, TreeArchetype type) {
    auto e = reg.create();
    reg.emplace<Transform>(e, pos);
    reg.emplace<TreeInstance>(e, type);
    reg.emplace<MeshVariant>(e, getMeshForArchetype(type));
    reg.emplace<RenderFlags>(e);
    return e;
}

// TreeRenderer queries ECS directly
void TreeRenderer::render(entt::registry& reg, vk::CommandBuffer cmd) {
    auto view = reg.view<Transform, TreeInstance, TreeLODState>();
    for (auto [entity, transform, tree, lod] : view.each()) {
        if (lod.level == LODLevel::FullDetail) {
            drawTreeMesh(transform, tree);
        }
    }
}
```

**Benefits:**
- Single source of truth (ECS registry)
- Trees selectable/editable via scene graph UI
- Consistent with other entities (lights, physics objects)
- Systems become stateless renderers

## Resource Management

### Handle System

Resources (meshes, textures, materials) should use handles rather than raw pointers:

```cpp
using MeshHandle = uint32_t;
using TextureHandle = uint32_t;
using MaterialHandle = uint32_t;

class ResourceRegistry {
public:
    MeshHandle createMesh(const MeshData& data);
    void destroyMesh(MeshHandle handle);
    Mesh* getMesh(MeshHandle handle);

    // Same pattern for textures, materials, etc.
};
```

This allows:
- Safe serialization (handles are stable)
- Resource sharing between entities
- Lazy loading and unloading
- Editor reference tracking

## Scene Serialization

With ECS, scene files become entity lists with components:

```json
{
  "entities": [
    {
      "name": "Player",
      "components": {
        "Transform": { "position": [0, 1, 0], "yaw": 0 },
        "MeshRenderer": { "mesh": "player.gltf", "material": "player_mat" },
        "PlayerTag": {},
        "PhysicsBody": { "type": "capsule", "height": 1.8, "radius": 0.3 }
      }
    },
    {
      "name": "Main Light",
      "components": {
        "Transform": { "position": [10, 20, 10] },
        "PointLight": { "color": [1, 0.9, 0.8], "intensity": 5, "radius": 50 },
        "LightEnabled": {}
      }
    }
  ]
}
```

## Existing Systems to Consider

The current codebase has these specialized systems that could integrate with ECS:

| System | Current | ECS Integration |
|--------|---------|-----------------|
| TerrainSystem | Procedural tiles | TerrainPatch entities per tile |
| GrassSystem | GPU instancing | GrassVolume entities for regions |
| TreeSystem | Instanced draws | TreeInstance entities or LODGroup |
| WaterSystem | Single surface | WaterSurface entities per body |
| WeatherSystem | Global state | Could remain global or WeatherZone entities |
| WindSystem | Global simulation | WindZone entities for local effects |
| AnimatedCharacter | Per-character | SkinnedMeshRenderer + Animator components |
| LightManager | Separate list | Already ECS-ready, needs renderer integration |

## Performance Considerations

### EnTT Performance Tips
- Use `view<A, B>()` for iteration, not `get<A>(entity)` in loops
- Use `group<A, B>()` for frequently co-accessed components
- Keep components small and cache-friendly
- Use tags (empty structs) for filtering, not bools in components

### Batching
Group draw calls by:
1. Material (shader, textures)
2. Mesh buffer
3. Instance data

```cpp
// Sort by material, then batch
std::sort(items.begin(), items.end(),
          [](auto& a, auto& b) { return a.material < b.material; });

MaterialHandle currentMat = InvalidMaterial;
for (auto& item : items) {
    if (item.material != currentMat) {
        bindMaterial(item.material);
        currentMat = item.material;
    }
    drawMesh(item.transform, item.mesh);
}
```

## Testing the Scene Graph

Once the build environment is working:

1. Open the application
2. Press `~` or `Tab` to show GUI (if hidden)
3. Go to **Scene > Scene Graph** and **Scene > Inspector**
4. Use **+ Entity** to create test entities
5. Right-click entities for context menu (Create Child, etc.)
6. Drag entities to reparent them
7. Select an entity to edit its components in the Inspector
8. Use **Add Component** to add new components
