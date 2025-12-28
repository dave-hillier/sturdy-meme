# Procedural Cities - Phase 6-7: Runtime & Integration

[← Back to Index](README.md)

## 8. Phase 6: LOD & Streaming System

### 8.1 LOD Strategy

#### 8.1.1 Building LOD Levels

| LOD Level | Distance | Description |
|-----------|----------|-------------|
| LOD0 | 0-50m | Full detail with all geometry |
| LOD1 | 50-150m | Simplified geometry, full textures |
| LOD2 | 150-400m | Billboard impostor |
| LOD3 | 400m+ | Merged settlement silhouette |

#### 8.1.2 Building LOD Generation

```cpp
class BuildingLODGenerator {
public:
    // Generate simplified mesh
    Mesh generateLOD1(const Mesh& fullDetail, float targetTriangleReduction = 0.5f);

    // Generate impostor billboard
    Impostor generateLOD2(const Mesh& fullDetail, uint32_t atlasSize = 256);

    // Generate settlement silhouette mesh
    Mesh generateSettlementSilhouette(
        const std::vector<BuildingMesh>& buildings,
        float simplification = 0.1f
    );
};

struct Impostor {
    glm::vec4 atlasRect;            // UV rect in impostor atlas
    glm::vec3 boundingBoxMin;
    glm::vec3 boundingBoxMax;
    float groundOffset;
};
```

### 8.2 Impostor Atlas

#### 8.2.1 Atlas Generation

```cpp
class BuildingImpostorAtlas {
public:
    struct Config {
        uint32_t atlasSize = 4096;
        uint32_t tileSize = 256;
        int viewsPerBuilding = 8;       // Octagonal views
    };

    void generate(
        const std::vector<BuildingMesh>& buildings,
        const Config& config
    );

    void save(const std::string& atlasPath, const std::string& metadataPath);

private:
    // Render building from multiple angles
    std::vector<Image> renderViews(const BuildingMesh& building, int viewCount);

    // Pack into atlas
    void packAtlas(const std::vector<std::vector<Image>>& allViews);
};
```

### 8.3 Streaming System

#### 8.3.1 Settlement Streaming

```cpp
class SettlementStreamingSystem {
public:
    struct Config {
        float loadRadius = 500.0f;
        float unloadRadius = 700.0f;
        float lodTransitionDistance = 50.0f;
        uint32_t maxConcurrentLoads = 2;
    };

    void update(const Camera& camera);

private:
    // Currently loaded settlements
    std::map<uint32_t, LoadedSettlement> loadedSettlements;

    // Async loading queue
    std::queue<uint32_t> loadQueue;

    void startLoad(uint32_t settlementId);
    void finishLoad(uint32_t settlementId, SettlementData&& data);
    void unload(uint32_t settlementId);
};

struct LoadedSettlement {
    SettlementDefinition definition;

    // Per-LOD data
    std::vector<RenderableHandle> lod0Renderables;
    std::vector<RenderableHandle> lod1Renderables;
    RenderableHandle lod2Impostor;
    RenderableHandle lod3Silhouette;

    // Current LOD state
    int currentLOD;
    float lodBlendFactor;
};
```

### 8.4 Culling Integration

#### 8.4.1 Settlement Culling

```cpp
class SettlementCullingSystem {
public:
    void update(
        const Camera& camera,
        const std::vector<LoadedSettlement>& settlements
    );

    // Frustum culling for individual buildings
    std::vector<uint32_t> getVisibleBuildings(uint32_t settlementId) const;

    // Occlusion culling using Hi-Z
    void cullOccluded(const HiZPyramid& hiZ);

private:
    // Per-settlement visibility data
    std::map<uint32_t, std::vector<bool>> buildingVisibility;
};
```

### 8.5 Deliverables - Phase 6

- [ ] BuildingLODGenerator (mesh simplification)
- [ ] BuildingImpostorAtlas generation
- [ ] SettlementStreamingSystem
- [ ] SettlementCullingSystem with Hi-Z integration
- [ ] LOD transition shader (dithered cross-fade)
- [ ] Streaming configuration and tuning

**Testing**: Performance validation:
- 60 FPS with 5+ settlements in view
- Smooth LOD transitions (no popping)
- Memory budget adherence (<200MB per settlement at LOD0)

---

## 9. Phase 7: Integration & Polish

### 9.1 Runtime Integration

#### 9.1.1 SettlementSystem

```cpp
// src/settlement/SettlementSystem.h

class SettlementSystem : public RenderSystem {
public:
    static std::unique_ptr<SettlementSystem> create(const InitContext& ctx);

    void init(const InitContext& ctx) override;
    void update(const UpdateContext& ctx) override;
    void render(const RenderContext& ctx) override;
    void cleanup() override;

    // Settlement queries
    const Settlement* getNearestSettlement(glm::vec3 position) const;
    bool isInsideSettlement(glm::vec3 position) const;
    float getSettlementDensity(glm::vec3 position) const;

private:
    std::unique_ptr<SettlementStreamingSystem> streaming;
    std::unique_ptr<SettlementCullingSystem> culling;
    std::unique_ptr<BuildingRenderer> buildingRenderer;

    // All settlement definitions (loaded at startup)
    std::vector<SettlementDefinition> allSettlements;
};
```

### 9.2 Shader Integration

#### 9.2.1 Building Shader

```glsl
// shaders/building.vert
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    // ...
} scene;

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint materialFlags;
    float lodBlend;
} pc;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out mat3 fragTBN;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    fragNormal = mat3(pc.model) * inNormal;
    fragTexCoord = inTexCoord;

    vec3 T = normalize(mat3(pc.model) * inTangent.xyz);
    vec3 N = normalize(fragNormal);
    vec3 B = cross(N, T) * inTangent.w;
    fragTBN = mat3(T, B, N);

    gl_Position = scene.proj * scene.view * worldPos;
}
```

```glsl
// shaders/building.frag
#version 450

#include "lighting_common.glsl"
#include "pbr_common.glsl"
#include "dither_common.glsl"

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in mat3 fragTBN;

layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D roughnessMap;
layout(set = 1, binding = 3) uniform sampler2D aoMap;

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint materialFlags;
    float lodBlend;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // LOD dithering for smooth transitions
    if (pc.lodBlend < 1.0 && shouldDiscardForLOD(pc.lodBlend)) {
        discard;
    }

    // Sample textures
    vec4 albedo = texture(albedoMap, fragTexCoord);
    vec3 normal = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
    float roughness = texture(roughnessMap, fragTexCoord).r;
    float ao = texture(aoMap, fragTexCoord).r;

    // Transform normal to world space
    vec3 N = normalize(fragTBN * normal);

    // PBR lighting
    vec3 V = normalize(cameraPos - fragWorldPos);
    vec3 color = calculatePBRLighting(albedo.rgb, N, V, roughness, 0.0, ao);

    outColor = vec4(color, albedo.a);
}
```

#### 9.2.2 Impostor Shader

```glsl
// shaders/building_impostor.vert
#version 450

layout(location = 0) in vec3 inPosition;     // Billboard corner
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inAtlasRect;    // Per-instance atlas UV rect
layout(location = 3) in mat4 inModel;        // Per-instance transform

layout(set = 0, binding = 0) uniform SceneUBO { /* ... */ } scene;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Camera-facing billboard
    vec3 right = vec3(scene.view[0][0], scene.view[1][0], scene.view[2][0]);
    vec3 up = vec3(0, 1, 0);

    vec3 worldPos = inModel[3].xyz;
    vec2 size = vec2(inModel[0][0], inModel[1][1]);

    worldPos += right * inPosition.x * size.x;
    worldPos += up * inPosition.y * size.y;

    // Map to atlas rect
    fragTexCoord = inAtlasRect.xy + inTexCoord * inAtlasRect.zw;

    gl_Position = scene.proj * scene.view * vec4(worldPos, 1.0);
}
```

### 9.3 Time of Day Integration

```cpp
class SettlementLighting {
public:
    void update(float timeOfDay, const std::vector<LoadedSettlement>& settlements);

private:
    // Enable/disable window glow based on time
    void updateWindowEmission(float timeOfDay);

    // Update street lamp states
    void updateStreetLamps(float timeOfDay);

    // Chimney smoke intensity based on time and weather
    void updateChimneySmokeParams(float timeOfDay, float temperature);
};
```

### 9.4 Weather Integration

```cpp
class SettlementWeatherEffects {
public:
    void update(const WeatherState& weather);

private:
    // Wet surfaces during rain
    void updateWetness(float rainIntensity);

    // Snow accumulation on roofs
    void updateSnowAccumulation(float snowAmount);

    // Puddle placement in settlement streets
    void updatePuddles(float groundWetness);
};
```

### 9.5 Audio Integration

```cpp
struct SettlementAmbience {
    // Distance-based ambient sounds
    std::string ambienceId;             // "village_day", "village_night", etc.
    float radius;
    float volume;

    // Point sound emitters
    struct SoundEmitter {
        glm::vec3 position;
        std::string soundId;            // "blacksmith_hammer", "church_bell", etc.
        float radius;
        float probability;              // Chance to play per interval
    };
    std::vector<SoundEmitter> emitters;
};
```

### 9.6 Deliverables - Phase 7

- [ ] SettlementSystem runtime class
- [ ] Building and impostor shaders
- [ ] Time of day integration (window glow, lamps)
- [ ] Weather effect integration
- [ ] Audio hooks for settlement ambience
- [ ] Full integration with existing render pipeline

**Testing**: Complete end-to-end validation:
- Walk through settlement in real-time
- Day/night cycle with lighting changes
- Weather effects (rain on roofs, snow accumulation)
- Performance profiling and optimization

---
