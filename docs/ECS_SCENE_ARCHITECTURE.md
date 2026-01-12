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
- [ ] Decal component (projected textures on surfaces)
- [ ] SpriteRenderer (billboard sprites with atlas support)
- [ ] RenderTarget (render-to-texture for cameras, mirrors, portals)
- [ ] ReflectionProbe (local cubemap reflections)
- [ ] LightProbe (spherical harmonics for indirect lighting)

### Phase 8: Audio
- [ ] AudioSource (spatial 3D audio emitter)
- [ ] AudioListener (active listener tag, typically on camera/player)
- [ ] AudioIntegration.h (bridge to audio system)

### Phase 9: Gameplay Systems
- [ ] TriggerVolume (generic trigger zones for gameplay events)
- [ ] NavMeshAgent (AI pathfinding with navigation mesh)
- [ ] Interactable (player interaction targets)
- [ ] GameplayIntegration.h (trigger callbacks, interaction system)

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
