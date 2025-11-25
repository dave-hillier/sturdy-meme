# Phase 3: Indirect Lighting

[← Previous: Phase 2 - Shadow Mapping](LIGHTING_PHASE2_SHADOWS.md) | [Back to Overview](LIGHTING_OVERVIEW.md) | [Next: Phase 4 - Atmospheric Scattering →](LIGHTING_PHASE4_ATMOSPHERE.md)

---

## 3.1 Spherical Harmonics Probe System

**Goal:** Capture and apply diffuse indirect lighting using SH probes with dynamic time-of-day support.

**Reference:** Ghost of Tsushima's sky visibility + bounce light approach

### 3.1.1 Core Concept - Sky Visibility Probes

Instead of storing baked irradiance (which is static), Ghost of Tsushima stores **sky visibility** as SH coefficients. At runtime, this is multiplied by the current sky color to get dynamic lighting.

**What's Stored Offline:**

1. **Direct Sky Visibility SH:** How much of the sky hemisphere is visible from each probe
2. **Bounce Sky Visibility SH:** Indirect light from surrounding geometry (pre-lit with white sky)

**Runtime Computation:**

```
Final Irradiance = (Sky Luminance SH × Direct Sky Visibility SH)
                 + (Average Sky Color × Bounce Sky Visibility SH)
                 + (Sun Bounce Contribution)
```

### 3.1.2 Data Structures

```cpp
// L2 Spherical Harmonics = 9 coefficients per color channel
struct SH9 {
    glm::vec3 coefficients[9];
};

struct IrradianceProbe {
    SH9 directSkyVisibility;   // How much sky is visible
    SH9 bounceSkyVisibility;   // Bounce light with white sky
    uint32_t flags;             // Interior/exterior, valid, etc.
};

// Regular grid for world coverage
struct ProbeGrid {
    glm::vec3 origin;
    glm::vec3 spacing;          // 12.5m horizontal, variable vertical
    glm::ivec3 dimensions;

    // Vertical layers at 1.5m, 10m, 30m above ground
    std::vector<float> verticalOffsets;

    std::vector<IrradianceProbe> probes;

    size_t GetProbeIndex(glm::ivec3 coord) const {
        return coord.x + coord.y * dimensions.x +
               coord.z * dimensions.x * dimensions.y;
    }
};

// Tetrahedral mesh for detailed areas (interiors, villages)
struct TetMesh {
    std::vector<glm::vec3> vertices;       // Probe positions
    std::vector<glm::ivec4> tetrahedra;    // Indices into vertices
    std::vector<IrradianceProbe> probes;   // One per vertex

    glm::vec3 boundsMin, boundsMax;
};
```

### 3.1.3 SH Math Fundamentals

```glsl
// SH Basis function constants
const float SH_C0 = 0.282095;    // 1/(2*sqrt(pi))
const float SH_C1 = 0.488603;    // sqrt(3)/(2*sqrt(pi))
const float SH_C2_0 = 1.092548;  // sqrt(15)/(2*sqrt(pi))
const float SH_C2_1 = 0.315392;  // sqrt(5)/(4*sqrt(pi))
const float SH_C2_2 = 0.546274;  // sqrt(15)/(4*sqrt(pi))

// Project direction to SH basis
void ProjectToSH(vec3 dir, out float sh[9]) {
    // L0
    sh[0] = SH_C0;
    // L1
    sh[1] = SH_C1 * dir.y;
    sh[2] = SH_C1 * dir.z;
    sh[3] = SH_C1 * dir.x;
    // L2
    sh[4] = SH_C2_0 * dir.x * dir.y;
    sh[5] = SH_C2_0 * dir.y * dir.z;
    sh[6] = SH_C2_1 * (3.0 * dir.z * dir.z - 1.0);
    sh[7] = SH_C2_0 * dir.x * dir.z;
    sh[8] = SH_C2_2 * (dir.x * dir.x - dir.y * dir.y);
}

// Evaluate irradiance from SH in direction of normal
vec3 EvaluateIrradianceSH(vec3 normal, vec3 sh[9]) {
    // Cosine lobe convolution is pre-applied to coefficients
    vec3 result = sh[0] * SH_C0;
    result += sh[1] * SH_C1 * normal.y;
    result += sh[2] * SH_C1 * normal.z;
    result += sh[3] * SH_C1 * normal.x;
    result += sh[4] * SH_C2_0 * normal.x * normal.y;
    result += sh[5] * SH_C2_0 * normal.y * normal.z;
    result += sh[6] * SH_C2_1 * (3.0 * normal.z * normal.z - 1.0);
    result += sh[7] * SH_C2_0 * normal.x * normal.z;
    result += sh[8] * SH_C2_2 * (normal.x * normal.x - normal.y * normal.y);
    return max(result, vec3(0.0));
}

// Multiply two SH functions (for sky × visibility)
void MultiplySH(vec3 a[9], float b[9], out vec3 result[9]) {
    // Full product gives degree-4 SH, but we truncate to degree-2
    // This is an approximation but works well in practice
    for (int i = 0; i < 9; i++) {
        result[i] = a[i] * b[i];
    }
    // Note: proper SH multiplication requires convolution
    // See "Stupid Spherical Harmonics Tricks" for details
}
```

### 3.1.4 Runtime Sky Visibility Lighting

```glsl
// Called once per frame to project current sky to SH
vec3 g_SkyLuminanceSH[9];  // Updated each frame

void UpdateSkyLuminanceSH(vec3 sunDir, vec3 sunColor, vec3 skyColor) {
    // Sample sky in many directions and project to SH
    // In practice, use precomputed sky LUT
    for (int i = 0; i < 9; i++) {
        g_SkyLuminanceSH[i] = vec3(0.0);
    }

    // Monte Carlo integration over hemisphere
    const int NUM_SAMPLES = 1024;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        vec3 dir = HemisphereSample(i, NUM_SAMPLES);
        vec3 skyRadiance = SampleSkyLUT(dir, sunDir);

        float shBasis[9];
        ProjectToSH(dir, shBasis);

        for (int j = 0; j < 9; j++) {
            g_SkyLuminanceSH[j] += skyRadiance * shBasis[j];
        }
    }

    // Normalize
    float weight = 4.0 * PI / float(NUM_SAMPLES);
    for (int i = 0; i < 9; i++) {
        g_SkyLuminanceSH[i] *= weight;
    }
}

// Per-pixel irradiance calculation
vec3 ComputeIndirectDiffuse(
    vec3 worldPos,
    vec3 normal,
    vec3 directSkyVisSH[9],
    vec3 bounceSkyVisSH[9]
) {
    // Direct sky contribution
    vec3 directSH[9];
    MultiplySH(g_SkyLuminanceSH, directSkyVisSH, directSH);
    vec3 directIrradiance = EvaluateIrradianceSH(normal, directSH);

    // Bounce sky contribution (use average sky color)
    vec3 avgSkyColor = g_SkyLuminanceSH[0] * SH_C0;  // DC term
    vec3 bounceIrradiance = EvaluateIrradianceSH(normal, bounceSkyVisSH) * avgSkyColor;

    return directIrradiance + bounceIrradiance;
}
```

### 3.1.5 Sun Bounce Approximation

Ghost of Tsushima adds sun bounce without storing additional data:

```glsl
vec3 ComputeSunBounce(
    vec3 normal,
    vec3 sunDir,
    vec3 sunColor,
    float cloudShadow,
    vec3 bounceSkyVisSH[9]
) {
    // Assume bounce comes from ground and walls
    // Reflect sun direction in imaginary planes

    // Ground bounce (horizontal surfaces)
    vec3 groundReflect = reflect(-sunDir, vec3(0, 1, 0));
    // Wall bounce (vertical surfaces)
    vec3 wallReflect = reflect(-sunDir, normalize(vec3(sunDir.x, 0, sunDir.z)));

    // These point in opposite directions, so use negated
    float groundSH[9], wallSH[9];
    ProjectToSH(-groundReflect, groundSH);
    ProjectToSH(-wallReflect, wallSH);

    // De-ring to ensure positive
    DeRingSH(groundSH);
    DeRingSH(wallSH);

    // Multiply by bounce visibility and sun color
    vec3 groundBounce = EvaluateIrradianceSH(normal, bounceSkyVisSH);
    vec3 wallBounce = EvaluateIrradianceSH(normal, bounceSkyVisSH);

    // Apply sun color with cloud shadowing
    vec3 bouncedSunLight = sunColor * cloudShadow;

    // Blend ground and wall contributions
    return (groundBounce * 0.5 + wallBounce * 0.5) * bouncedSunLight * 0.3;
}
```

### 3.1.6 Probe Interpolation

**Trilinear for Grid:**

```glsl
vec3 SampleProbeGrid(vec3 worldPos, vec3 normal) {
    // Convert world position to grid coordinates
    vec3 gridPos = (worldPos - gridOrigin) / gridSpacing;
    ivec3 baseCoord = ivec3(floor(gridPos));
    vec3 frac = fract(gridPos);

    // Sample 8 corners and trilinearly interpolate
    vec3 irradiance = vec3(0.0);
    for (int z = 0; z <= 1; z++) {
        for (int y = 0; y <= 1; y++) {
            for (int x = 0; x <= 1; x++) {
                ivec3 coord = baseCoord + ivec3(x, y, z);
                coord = clamp(coord, ivec3(0), gridDimensions - 1);

                float weight = (x == 0 ? 1.0 - frac.x : frac.x) *
                              (y == 0 ? 1.0 - frac.y : frac.y) *
                              (z == 0 ? 1.0 - frac.z : frac.z);

                IrradianceProbe probe = FetchProbe(coord);
                irradiance += weight * ComputeIndirectDiffuse(
                    worldPos, normal,
                    probe.directSkyVisSH,
                    probe.bounceSkyVisSH
                );
            }
        }
    }

    return irradiance;
}
```

**Tetrahedral for Detailed Areas:**

```glsl
vec3 SampleTetMesh(vec3 worldPos, vec3 normal, TetMesh mesh) {
    // Find containing tetrahedron
    int tetIndex = FindContainingTetrahedron(worldPos, mesh);
    if (tetIndex < 0) return vec3(0.0);

    // Compute barycentric coordinates
    ivec4 tet = mesh.tetrahedra[tetIndex];
    vec4 bary = ComputeBarycentric(
        worldPos,
        mesh.vertices[tet.x],
        mesh.vertices[tet.y],
        mesh.vertices[tet.z],
        mesh.vertices[tet.w]
    );

    // Blend probes at tetrahedron vertices
    vec3 irradiance = vec3(0.0);
    irradiance += bary.x * ComputeProbeIrradiance(mesh.probes[tet.x], normal);
    irradiance += bary.y * ComputeProbeIrradiance(mesh.probes[tet.y], normal);
    irradiance += bary.z * ComputeProbeIrradiance(mesh.probes[tet.z], normal);
    irradiance += bary.w * ComputeProbeIrradiance(mesh.probes[tet.w], normal);

    return irradiance;
}
```

### 3.1.7 Light Leak Prevention

Ghost of Tsushima's interior mask technique:

```glsl
// Probe classification
const uint PROBE_EXTERIOR = 0;
const uint PROBE_INTERIOR = 1;

// Surface interior weight (0.5 default, 0=exterior, 1=interior)
uniform float surfaceInteriorMask;

vec3 SampleProbeGridWithLeakPrevention(vec3 worldPos, vec3 normal) {
    vec3 gridPos = (worldPos - gridOrigin) / gridSpacing;
    ivec3 baseCoord = ivec3(floor(gridPos));
    vec3 frac = fract(gridPos);

    float totalWeight = 0.0;
    vec3 irradiance = vec3(0.0);

    for (int z = 0; z <= 1; z++) {
        for (int y = 0; y <= 1; y++) {
            for (int x = 0; x <= 1; x++) {
                ivec3 coord = baseCoord + ivec3(x, y, z);
                IrradianceProbe probe = FetchProbe(coord);

                float trilinearWeight = (x == 0 ? 1.0 - frac.x : frac.x) *
                                       (y == 0 ? 1.0 - frac.y : frac.y) *
                                       (z == 0 ? 1.0 - frac.z : frac.z);

                // Interior mask weighting
                float maskWeight;
                if (probe.flags == PROBE_INTERIOR) {
                    maskWeight = surfaceInteriorMask;
                } else {
                    maskWeight = 1.0 - surfaceInteriorMask;
                }

                float weight = trilinearWeight * maskWeight;
                totalWeight += weight;

                irradiance += weight * ComputeProbeIrradiance(probe, normal);
            }
        }
    }

    // Normalize (fallback to unweighted if all weights zero)
    if (totalWeight > 0.001) {
        return irradiance / totalWeight;
    }
    return SampleProbeGridUnweighted(worldPos, normal);
}
```

### 3.1.8 SH De-Ringing

Prevent negative lobes in SH:

```glsl
// Peter-Pike Sloan's de-ringing method
void DeRingSH(inout vec3 sh[9]) {
    // Find minimum using Newton's method (1 iteration sufficient)
    vec3 dir = normalize(vec3(sh[3].x, sh[1].x, sh[2].x));  // L1 direction

    // Binary search for windowing factor
    float windowScale = 1.0;
    for (int i = 0; i < 3; i++) {
        float minVal = EvaluateSHAtDirection(sh, dir);
        if (minVal < 0.0) {
            windowScale *= 0.5;
            ApplyWindowingToSH(sh, windowScale);
        }
    }
}

void ApplyWindowingToSH(inout vec3 sh[9], float scale) {
    // Apply band-specific windowing
    // L1 band
    sh[1] *= scale;
    sh[2] *= scale;
    sh[3] *= scale;
    // L2 band (more aggressive)
    float scale2 = scale * scale;
    sh[4] *= scale2;
    sh[5] *= scale2;
    sh[6] *= scale2;
    sh[7] *= scale2;
    sh[8] *= scale2;
}
```

### 3.1.9 Directionality Boost

Compensate for SH's inability to represent sharp features:

```glsl
vec3 ApplyDirectionalityBoost(vec3 sh[9], float boostFactor) {
    // Find dominant direction from L1 band
    vec3 dominantDir = normalize(vec3(
        sh[3].x + sh[3].y + sh[3].z,
        sh[1].x + sh[1].y + sh[1].z,
        sh[2].x + sh[2].y + sh[2].z
    ));

    // Project delta function in dominant direction
    float deltaSH[9];
    ProjectToSH(dominantDir, deltaSH);

    // Lerp towards delta
    for (int i = 0; i < 9; i++) {
        sh[i] = mix(sh[i], sh[i] * deltaSH[i] * 3.0, boostFactor);
    }
}
```

---

## 3.2 Reflection Probes

**Goal:** Capture specular indirect lighting with dynamic relighting.

### 3.2.1 Probe Capture Data

```cpp
struct ReflectionProbe {
    VkImage albedoCubemap;    // BC1, 256x256x6
    VkImage normalDepthCubemap; // BC6H, 256x256x6
                               // RG: Octahedral normal, B: Hyperbolic depth

    glm::vec3 position;
    glm::vec3 boxMin, boxMax;  // Parallax correction volume
    float blendDistance;

    // For nested probes (interior within exterior)
    int parentProbeIndex;
};
```

### 3.2.2 Runtime Relighting

Ghost of Tsushima relights reflection probes every frame:

```glsl
// Compute shader - relight one probe per frame
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0) uniform samplerCube albedoCube;
layout(binding = 1) uniform samplerCube normalDepthCube;
layout(binding = 2, rgba16f) writeonly uniform imageCube outputCube;
layout(binding = 3) uniform sampler2D farShadowAtlas;

void main() {
    ivec3 texel = ivec3(gl_GlobalInvocationID);
    vec3 dir = TexelToDirection(texel);

    vec4 albedo = texture(albedoCube, dir);
    vec4 normalDepth = texture(normalDepthCube, dir);

    // Decode octahedral normal
    vec3 normal = DecodeOctahedral(normalDepth.rg);

    // Compute world position from depth
    float depth = DecodeHyperbolicDepth(normalDepth.b);
    vec3 worldPos = probePosition + dir * depth;

    // Sample shadow (use far shadow atlas for interiors)
    float shadow = SampleFarShadowAtlas(worldPos, sunDir);

    // Indirect lighting from SH (single sample for performance)
    vec3 indirectSH = SampleProbeGridSingleSH(probePosition);
    vec3 indirect = EvaluateIrradianceSH(normal, indirectSH);

    // Direct lighting
    float NoL = max(dot(normal, sunDir), 0.0);
    vec3 direct = sunColor * NoL * shadow;

    vec3 finalColor = albedo.rgb * (direct + indirect);

    imageStore(outputCube, texel, vec4(finalColor, 1.0));
}
```

### 3.2.3 Cube Map Shadow Tracing

For interior probes where far shadows are insufficient:

```glsl
float TraceCubemapShadow(vec3 worldPos, vec3 lightDir, samplerCube depthCube) {
    // Trace from surface point towards light
    vec3 rayDir = lightDir;

    // Find intersection with probe volume
    vec3 toEdge = (probeBoxMax - worldPos) / rayDir;
    vec3 toEdgeNeg = (probeBoxMin - worldPos) / rayDir;
    vec3 tFar = max(toEdge, toEdgeNeg);
    float t = min(min(tFar.x, tFar.y), tFar.z);

    // Sample depth at intersection
    vec3 sampleDir = normalize(worldPos + rayDir * t - probePosition);
    float storedDepth = texture(depthCube, sampleDir).b;
    storedDepth = DecodeHyperbolicDepth(storedDepth);

    // Compare depths (PCF 2x2)
    float shadow = 0.0;
    for (int i = 0; i < 4; i++) {
        vec2 offset = poissonDisk[i] * 0.01;
        vec3 offsetDir = sampleDir + vec3(offset, 0.0);
        float d = DecodeHyperbolicDepth(texture(depthCube, offsetDir).b);
        shadow += (t < d + 0.1) ? 1.0 : 0.0;
    }

    return shadow * 0.25;
}
```

### 3.2.4 Pre-filtered Environment Maps

```cpp
void PrefilterReflectionProbe(
    VkCommandBuffer cmd,
    ReflectionProbe& probe,
    uint32_t mipLevels
) {
    // Mip 0 = mirror, increasing mips = rougher surfaces
    for (uint32_t mip = 0; mip < mipLevels; mip++) {
        float roughness = float(mip) / float(mipLevels - 1);

        // GGX importance sampling
        for (uint32_t face = 0; face < 6; face++) {
            // Dispatch compute shader with roughness parameter
            PrefilterFace(cmd, probe, face, mip, roughness);
        }
    }
}
```

```glsl
// Pre-filter compute shader
vec3 PrefilterEnvMap(vec3 R, float roughness) {
    vec3 N = R;
    vec3 V = R;

    vec3 prefilteredColor = vec3(0.0);
    float totalWeight = 0.0;

    const uint SAMPLE_COUNT = 1024u;
    for (uint i = 0u; i < SAMPLE_COUNT; i++) {
        vec2 Xi = Hammersley(i, SAMPLE_COUNT);
        vec3 H = ImportanceSampleGGX(Xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NoL = max(dot(N, L), 0.0);
        if (NoL > 0.0) {
            prefilteredColor += texture(environmentMap, L).rgb * NoL;
            totalWeight += NoL;
        }
    }

    return prefilteredColor / totalWeight;
}
```

### 3.2.5 Sampling with Parallax Correction

```glsl
vec3 SampleReflectionProbe(
    vec3 worldPos,
    vec3 R,
    float roughness,
    ReflectionProbe probe
) {
    // Parallax correction
    vec3 correctedR = ParallaxCorrectCubemap(R, worldPos,
        probe.boxMin, probe.boxMax, probe.position);

    // Roughness to mip level
    float mip = roughness * float(numMipLevels - 1);

    // Luminance normalization
    // Ensures probe at capture location has expected brightness
    vec3 probeSH = SampleProbeGridSingleSH(probe.position);
    float probeLuminance = dot(probeSH[0], vec3(0.2126, 0.7152, 0.0722));
    float currentSkyLuminance = dot(g_SkyLuminanceSH[0], vec3(0.2126, 0.7152, 0.0722));
    float normalization = currentSkyLuminance / max(probeLuminance, 0.001);

    vec3 color = textureLod(probe.cubemap, correctedR, mip).rgb;
    return color * normalization;
}
```

---

## 3.3 Horizon Occlusion

**Goal:** Prevent unrealistic specular glow on backfacing normal map bumps.

### 3.3.1 The Problem

When a normal map tilts the surface normal away from the geometric normal, the reflection vector can point into the surface. Standard IBL sampling will return bright sky reflections for these "impossible" reflections.

### 3.3.2 Cone-Based Approximation

```glsl
float ComputeHorizonOcclusion(vec3 R, vec3 N, vec3 V, float roughness) {
    // Angle between reflection and vertex normal
    float NoR = dot(N, R);

    // Cone half-angle containing 95% of GGX energy
    float alpha = roughness * roughness;
    float coneAngle = atan(alpha / (1.0 + alpha)) * 0.95;

    // How far the cone dips below the horizon
    float thetaR = acos(saturate(NoR));  // Reflection tilt from vertex normal
    float thetaN = acos(saturate(dot(N, V)));  // Normal map tilt

    // Extra occlusion beyond what BRDF handles
    float thetaO = max(thetaR - 2.0 * thetaN - coneAngle, 0.0);

    // Occlusion amount (0.95 = max occlusion = ue fraction)
    float occlusion = saturate(1.0 - thetaO / (PI * 0.5));

    return occlusion;
}

// Simpler approximation
float HorizonOcclusionSimple(vec3 R, vec3 N, float roughness) {
    float horizon = saturate(1.0 + dot(R, N));
    return saturate(pow(horizon, 8.0 * roughness));
}
```

### 3.3.3 Parallax-Compensated Roughness

When sampling far from probe capture point, adjust roughness:

```glsl
float AdjustRoughnessForDistance(
    float roughness,
    float distanceToProbe,
    float distanceAtCapture
) {
    // Cone angle scales with distance
    float ratio = distanceToProbe / max(distanceAtCapture, 0.01);
    return roughness * ratio;
}
```

---

[← Previous: Phase 2 - Shadow Mapping](LIGHTING_PHASE2_SHADOWS.md) | [Back to Overview](LIGHTING_OVERVIEW.md) | [Next: Phase 4 - Atmospheric Scattering →](LIGHTING_PHASE4_ATMOSPHERE.md)
