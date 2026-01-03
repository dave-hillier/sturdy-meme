# Ambient Occlusion & Global Illumination Implementation Plan

## Overview

Three-layer approach mirroring UE5 Lumen's software path, tailored for a 16km² world with cities:

| Layer | Technique | Detail Level | Cost |
|-------|-----------|--------------|------|
| 1 | GTAO | Pixel-level | ~0.4ms |
| 2 | Mesh SDF AO | Sub-meter (buildings) | ~0.3ms |
| 3 | Cascaded Sky Probes | 4m-128m (global ambient) | ~0.1ms lookup |

## Phase 1: GTAO (Ground-Truth Ambient Occlusion)

Screen-space AO that leverages the existing Hi-Z pyramid.

### New Files
- `src/postprocess/GTAOSystem.h/.cpp` - Main system (follows SSRSystem pattern)
- `shaders/gtao.comp.glsl` - Main GTAO compute shader
- `shaders/gtao_spatial_filter.comp.glsl` - Bilateral blur
- `shaders/gtao_temporal_filter.comp.glsl` - Temporal accumulation
- `shaders/gtao_common.glsl` - Shared functions

### GTAOSystem Design

```cpp
class GTAOSystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
        const vk::raii::Device* raiiDevice;
    };

    static std::unique_ptr<GTAOSystem> create(const InitInfo& info);
    static std::unique_ptr<GTAOSystem> create(const InitContext& ctx);

    void resize(VkExtent2D newExtent);

    // Record GTAO passes - call after depth pass, before lighting
    void recordCompute(VkCommandBuffer cmd, uint32_t frameIndex,
                       VkImageView depthView,
                       VkImageView normalView,  // GBuffer normals
                       VkImageView hiZView,     // From HiZSystem
                       const glm::mat4& view, const glm::mat4& proj);

    VkImageView getAOResultView() const;
    VkSampler getSampler() const;

    // Configuration
    void setRadius(float r);        // World-space radius (default 0.5m)
    void setFalloff(float f);       // Distance falloff
    void setNumSlices(int n);       // Angular slices (default 4)
    void setNumSteps(int n);        // Steps per slice (default 3)

private:
    // R8_UNORM AO texture (double-buffered for temporal)
    VkImage aoResult[2];
    VkImageView aoResultView[2];

    // Compute pipelines
    vk::raii::Pipeline gtaoPipeline;
    vk::raii::Pipeline spatialFilterPipeline;
    vk::raii::Pipeline temporalFilterPipeline;
};
```

### GTAO Algorithm (shaders/gtao.comp.glsl)

```glsl
// Ground-Truth Ambient Occlusion
// Based on: "Practical Real-Time Strategies for Accurate Indirect Occlusion" (SIGGRAPH 2016)

// For each pixel:
// 1. Reconstruct view-space position from depth
// 2. Sample Hi-Z pyramid at multiple scales
// 3. For each angular slice, find horizon angle
// 4. Integrate visibility over hemisphere
// 5. Apply cosine-weighted AO formula

#define NUM_SLICES 4
#define NUM_STEPS 3

float computeGTAO(vec2 uv, vec3 viewPos, vec3 viewNormal) {
    float ao = 0.0;
    float radius = ubo.gtaoRadius;

    for (int slice = 0; slice < NUM_SLICES; slice++) {
        float phi = (PI / NUM_SLICES) * (float(slice) + noise(uv));
        vec2 dir = vec2(cos(phi), sin(phi));

        // Find horizon angles in both directions
        float horizonCos1 = findHorizon(uv, viewPos, dir, radius);
        float horizonCos2 = findHorizon(uv, viewPos, -dir, radius);

        // Integrate visibility
        ao += integrateArc(viewNormal, dir, horizonCos1, horizonCos2);
    }

    return ao / float(NUM_SLICES);
}
```

### Integration Points
- Add to `RendererSystems` alongside SSRSystem
- Execute in `PostStage` after Hi-Z generation
- Output bound to main shader as `aoTexture`
- Multiply final ambient term: `ambient *= texture(aoTexture, uv).r`

---

## Phase 2: Mesh SDF AO (Signed Distance Field)

For sub-meter occlusion between buildings, under awnings, in doorways.

### New Files
- `src/sdf/SDFGenerator.h/.cpp` - Offline SDF generation tool
- `src/sdf/SDFAtlas.h/.cpp` - Runtime SDF texture atlas
- `src/sdf/SDFAOSystem.h/.cpp` - Runtime cone tracing
- `shaders/sdf_ao.comp.glsl` - SDF cone trace shader
- `shaders/sdf_common.glsl` - SDF sampling utilities
- `tools/sdf_generator.cpp` - Build-time SDF baking

### SDF Generation (Build Time)

Per mesh asset, generate a 3D distance field:

```cpp
// tools/sdf_generator.cpp
struct SDFConfig {
    uint32_t resolution = 64;     // 64³ default
    float padding = 0.1f;         // Padding around mesh bounds
    bool signedDistance = true;   // Interior = negative
};

// Output: meshname.sdf (binary 3D texture)
// Format: R16F (signed distance in local units)
```

### SDFAtlas Design

Runtime atlas managing all loaded SDFs:

```cpp
class SDFAtlas {
public:
    struct SDFEntry {
        uint32_t atlasIndex;      // Index in 3D texture array
        glm::vec3 boundsMin;      // World-space bounds
        glm::vec3 boundsMax;
        glm::vec3 invScale;       // For UV calculation
    };

    // Load SDF for mesh, returns atlas index
    uint32_t loadSDF(const std::string& meshName);

    // Get atlas texture for shader binding
    VkImageView getAtlasView() const;

    // Get entry buffer for GPU lookup
    VkBuffer getEntryBuffer() const;

private:
    // 3D texture array: 64x64x64 x N layers
    VkImage atlasImage;
    std::vector<SDFEntry> entries;
};
```

### SDFAOSystem Design

```cpp
class SDFAOSystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
        SDFAtlas* sdfAtlas;
    };

    static std::unique_ptr<SDFAOSystem> create(const InitInfo& info);

    // Record SDF AO cone trace
    void recordCompute(VkCommandBuffer cmd, uint32_t frameIndex,
                       VkImageView depthView,
                       VkImageView normalView,
                       const glm::mat4& view, const glm::mat4& proj,
                       const std::vector<SDFInstance>& instances);

    VkImageView getSDFAOView() const;

private:
    // R8_UNORM AO result
    VkImage sdfAOResult;

    // Cone trace pipeline
    vk::raii::Pipeline coneTracePipeline;
};
```

### SDF Cone Tracing (shaders/sdf_ao.comp.glsl)

```glsl
// Cone trace against SDF atlas for ambient occlusion
// Based on UE4 Distance Field Ambient Occlusion

#define NUM_CONES 4
#define CONE_ANGLE 0.5  // ~30 degrees

float traceSDFCone(vec3 origin, vec3 dir, float maxDist) {
    float occlusion = 0.0;
    float t = 0.01;  // Start offset

    for (int i = 0; i < 16; i++) {
        vec3 p = origin + dir * t;
        float d = sampleGlobalSDF(p);  // Sample all nearby SDFs

        if (d < 0.001) return 1.0;  // Hit

        // Cone occlusion based on distance and cone width
        float coneWidth = t * CONE_ANGLE;
        occlusion = max(occlusion, 1.0 - d / coneWidth);

        t += max(d * 0.5, 0.1);  // Step by safe distance
        if (t > maxDist) break;
    }

    return occlusion;
}

float computeSDFAO(vec3 worldPos, vec3 worldNormal) {
    float ao = 0.0;

    for (int i = 0; i < NUM_CONES; i++) {
        vec3 coneDir = getConeDirection(worldNormal, i, NUM_CONES);
        ao += traceSDFCone(worldPos, coneDir, 10.0);
    }

    return 1.0 - (ao / float(NUM_CONES));
}
```

### Integration Points
- SDFGenerator runs at build time (CMake custom command)
- SDFAtlas loaded at scene init
- SDFAOSystem executes in PostStage after GTAO
- Combine: `finalAO = min(gtao, sdfAO)` (take minimum = most occluded)

---

## Phase 3: Cascaded Sky Visibility Probes

Global ambient from sky, modulated by local visibility.

### New Files
- `src/lighting/SkyProbeSystem.h/.cpp` - Probe management and lookup
- `src/lighting/SkyProbeBaker.h/.cpp` - Probe baking (offline/runtime)
- `shaders/sky_probe_bake.comp.glsl` - Probe visibility baking
- `shaders/sky_probe_sample.glsl` - Probe interpolation (include)
- `tools/sky_probe_baker.cpp` - Build-time baking tool

### Cascade Configuration

```cpp
struct SkyProbeCascade {
    float spacing;        // Probe spacing in meters
    uint32_t gridSize;    // 64³ per cascade
    float range;          // Total range covered
};

// 4 cascades, camera-relative
static constexpr SkyProbeCascade CASCADES[4] = {
    { 4.0f,  64, 256.0f   },  // Cascade 0: 4m spacing, 256m range
    { 16.0f, 64, 1024.0f  },  // Cascade 1: 16m spacing, 1km range
    { 64.0f, 64, 4096.0f  },  // Cascade 2: 64m spacing, 4km range
    { 256.0f,64, 16384.0f },  // Cascade 3: 256m spacing, 16km range
};

// Total probes: 4 × 64³ = 1,048,576 probes
// Memory per probe (SH1 RGB): 12 floats = 48 bytes
// Total memory: ~50MB
```

### Probe Data Format

```cpp
// Per-probe data (SH1 = 4 coefficients per channel)
struct SkyProbeData {
    // L0 (constant term) - average visibility
    glm::vec3 sh0;          // 12 bytes

    // L1 (linear terms) - directional visibility
    glm::vec3 sh1_x;        // 12 bytes
    glm::vec3 sh1_y;        // 12 bytes
    glm::vec3 sh1_z;        // 12 bytes
};  // 48 bytes total

// Alternative compact format (bent normal + occlusion)
struct SkyProbeCompact {
    glm::vec3 bentNormal;   // Dominant unoccluded direction
    float visibility;       // 0-1 sky visibility
};  // 16 bytes
```

### SkyProbeSystem Design

```cpp
class SkyProbeSystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        std::string shaderPath;
        uint32_t framesInFlight;
    };

    static std::unique_ptr<SkyProbeSystem> create(const InitInfo& info);

    // Update cascade positions based on camera
    void updateCascades(const glm::vec3& cameraPos);

    // Record probe update (if using runtime baking)
    void recordUpdate(VkCommandBuffer cmd, uint32_t frameIndex,
                      VkImageView depthView,
                      const std::vector<SDFInstance>& sdfInstances);

    // Get probe textures for shader binding
    VkImageView getCascadeView(uint32_t cascade) const;
    VkBuffer getCascadeInfoBuffer() const;  // Cascade transforms

    // Load baked probes from file
    bool loadBakedProbes(const std::string& path);

private:
    // 3D textures per cascade (RGBA16F for SH1)
    struct Cascade {
        VkImage probeTexture;       // 64³ × 4 channels (SH0 + visibility)
        VkImage probeTextureSH1;    // 64³ × 12 channels (SH1 xyz)
        glm::vec3 worldOrigin;      // Bottom-left corner in world
        float spacing;
    };

    std::array<Cascade, 4> cascades;

    // Per-frame cascade info for GPU
    BufferUtils::PerFrameBufferSet cascadeInfoBuffers;
};
```

### Probe Sampling (shaders/sky_probe_sample.glsl)

```glsl
// Sample sky visibility from cascaded probes
// Include in any shader needing ambient lighting

struct CascadeInfo {
    vec3 origin;
    float spacing;
    vec3 invSize;       // 1.0 / (gridSize * spacing)
    float blendStart;   // Start blending to next cascade
};

layout(set = X, binding = Y) uniform sampler3D skyProbeCascades[4];
layout(set = X, binding = Z) uniform CascadeInfoUBO {
    CascadeInfo cascades[4];
};

vec3 sampleSkyProbe(vec3 worldPos, vec3 normal) {
    vec3 result = vec3(0.0);
    float totalWeight = 0.0;

    for (int c = 0; c < 4; c++) {
        vec3 localPos = (worldPos - cascades[c].origin) * cascades[c].invSize;

        // Check if in range
        if (any(lessThan(localPos, vec3(0.0))) ||
            any(greaterThan(localPos, vec3(1.0)))) {
            continue;
        }

        // Sample SH coefficients
        vec4 sh0_vis = texture(skyProbeCascades[c], localPos);

        // Evaluate SH in normal direction (simplified for SH0)
        float visibility = sh0_vis.a;
        vec3 skyColor = sh0_vis.rgb;

        // Cascade blend weight (prefer finer cascades)
        float weight = 1.0 / float(c + 1);

        result += skyColor * visibility * weight;
        totalWeight += weight;
    }

    return result / max(totalWeight, 0.001);
}
```

### Integration Points
- Cascades update positions each frame (cheap - just buffer update)
- Probe baking done offline or incrementally at runtime
- Sample in fragment shaders: `ambient = sampleSkyProbe(worldPos, normal) * irradianceLUT`
- Combines with existing AtmosphereLUTSystem irradiance

---

## Phase 4: Integration & Combination

### Combined AO in Fragment Shader

```glsl
// In lighting_common.glsl or shader.frag

vec3 computeAmbientLighting(vec3 worldPos, vec3 normal, vec3 albedo) {
    // Sample all AO sources
    float gtao = texture(gtaoTexture, screenUV).r;
    float sdfAO = texture(sdfAOTexture, screenUV).r;

    // Combine AO (multiplicative)
    float combinedAO = gtao * sdfAO;

    // Sky visibility from probes
    vec3 skyVisibility = sampleSkyProbe(worldPos, normal);

    // Sample sky irradiance from existing LUT
    vec3 skyIrradiance = sampleIrradianceLUT(normal);

    // Final ambient
    vec3 ambient = albedo * skyIrradiance * skyVisibility * combinedAO;

    return ambient;
}
```

### Render Pipeline Integration

```
Current Pipeline:
1. ComputeStage - Terrain LOD, grass sim, weather
2. ShadowStage  - Shadow map rendering
3. FroxelStage  - Volumetric fog update
4. HDRStage     - Main scene rendering
5. PostStage    - HiZ, bloom, SSR, composite

New Pipeline:
1. ComputeStage - Terrain LOD, grass sim, weather, [SkyProbe cascade update]
2. ShadowStage  - Shadow map rendering
3. FroxelStage  - Volumetric fog update
4. HDRStage     - Main scene + GBuffer normals
5. AOStage (NEW)- HiZ gen, GTAO, SDF-AO
6. PostStage    - Bloom, SSR, composite (AO already applied in HDR)
```

---

## Implementation Order

### Step 1: GTAO Foundation
1. Create GTAOSystem following SSRSystem pattern
2. Implement basic GTAO compute shader
3. Add spatial bilateral filter
4. Integrate with existing depth/normal buffers
5. Test: Should see dark corners, under objects

### Step 2: GTAO Polish
1. Add temporal filtering (double-buffer)
2. Add Hi-Z acceleration for long-range samples
3. Tune parameters via GUI
4. Test: Stable AO without flickering

### Step 3: SDF Infrastructure
1. Create SDFGenerator tool
2. Generate SDFs for test building meshes
3. Create SDFAtlas for runtime management
4. Test: Verify SDFs load correctly

### Step 4: SDF-AO Runtime
1. Create SDFAOSystem
2. Implement cone tracing shader
3. Combine with GTAO output
4. Test: Buildings should cast ambient shadows on streets

### Step 5: Sky Probe Infrastructure
1. Create SkyProbeSystem with cascade structure
2. Implement probe baking (offline first)
3. Create probe sampling shader include
4. Test: Basic probe lookup working

### Step 6: Sky Probe Integration
1. Connect to AtmosphereLUTSystem irradiance
2. Add runtime cascade position updates
3. Integrate sampling into all lit shaders
4. Test: Streets darker than rooftops

### Step 7: Full Integration
1. Combine all three systems in pipeline
2. Add debug visualization (AO only view)
3. Performance profiling and optimization
4. GUI controls for all parameters

---

## Resource Summary

| Resource | Memory | Notes |
|----------|--------|-------|
| GTAO buffer | ~4MB | R8 at 1080p, double-buffered |
| SDF Atlas | ~300MB | 100 buildings × 64³ × R16F |
| Sky Probes | ~50MB | 4 cascades × 64³ × 48 bytes |
| **Total** | **~354MB** | |

## Performance Targets

| System | Target | Notes |
|--------|--------|-------|
| GTAO | <0.5ms | 4 slices × 3 steps |
| SDF-AO | <0.4ms | 4 cones × 16 steps |
| Sky Probe lookup | <0.1ms | Texture fetch only |
| **Total** | **<1.0ms** | |

---

## Testing

### GTAO
- Render AO buffer to screen (debug view)
- Compare corners vs open areas
- Check temporal stability with camera motion

### SDF-AO
- Place test building, verify shadow on ground plane
- Check alley between two buildings
- Verify no artifacts at SDF boundaries

### Sky Probes
- Compare lit/shadow sides of buildings
- Check sky visibility in open vs covered areas
- Verify cascade transitions are smooth

### Combined
- A/B comparison with AO on/off
- Profile GPU time per system
- Check for visual artifacts at system boundaries
