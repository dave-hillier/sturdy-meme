# Lighting, Atmospheric Effects & Shadows Implementation Plan

A step-by-step guide to implementing cinematic lighting inspired by Ghost of Tsushima's rendering techniques, adapted for this Vulkan game engine.

## Current State

- Basic forward rendering pipeline with depth buffer
- Single hardcoded directional light in fragment shader
- Simple diffuse lighting (Lambertian with 0.3 ambient floor)
- No shadows, specular, or atmospheric effects
- No post-processing pipeline

---

## Phase Dependencies & Alternatives

Not all phases are equally essential. Here's how they relate:

**Required foundation:**
- **Phase 1** (PBR lighting) and **Phase 2** (shadows) are foundational for any modern look

**Optional enhancements:**
- **Phase 3** (probes) and **Phase 4** (atmosphere) add polish but are more independent of each other

### Recommended Implementation Order

1. **Phase 1** - PBR with simple hemisphere ambient
2. **Phase 2** - Shadows (even basic shadow mapping transforms a scene)
3. **Phase 4** - Atmospheric scattering (big visual impact, independent of probes)
4. **Phase 3** - Add probes later if needed for cinematic quality

### Phase 3 Alternatives (Lighter-Weight Options)

The SH probe system provides high-quality indirect lighting but requires offline baking and assumes static geometry. If this complexity isn't needed, consider these alternatives:

| Instead of... | Use... | Quality |
|---------------|--------|---------|
| SH irradiance probes | Constant ambient + AO | Acceptable for stylized |
| SH irradiance probes | Hemisphere lighting (sky vs ground color) | Better, still cheap |
| Reflection probes | Single skybox cubemap | Flat but functional |
| Reflection probes | Screen-space reflections | Good for planar surfaces |

The atmospheric sky model from Phase 4 can serve as a cheap ambient source - sampling the sky LUT provides some directional fill light without full probe baking.

Many shipped games use "PBR + shadows + atmosphere + simple ambient" and achieve good results. Probes push from "good game lighting" toward "cinematic rendering" but aren't strictly required.

---

## Phase 1: Foundation - Proper Lighting System

### 1.1 Light Data Structures

**Goal:** Replace hardcoded lighting with configurable uniform-based system.

**Tasks:**
1. Create `Light` struct in C++ and corresponding GLSL struct
2. Add light uniform buffer to descriptor set layout
3. Support directional light with: direction, color, intensity
4. Add ambient light color/intensity as separate uniform

**Files to modify:**
- `src/Renderer.h` - Add light data structures
- `src/Renderer.cpp` - Create light uniform buffer, update descriptor sets
- `shaders/shader.frag` - Accept light uniforms instead of hardcoded values

**GLSL Light Struct:**
```glsl
struct DirectionalLight {
    vec3 direction;
    float intensity;
    vec3 color;
    float padding;
};

layout(set = 0, binding = 2) uniform LightData {
    DirectionalLight sun;
    vec3 ambientColor;
    float ambientIntensity;
} lights;
```

### 1.2 Physically-Based Lighting Model

**Goal:** Implement energy-conserving PBR lighting.

**Tasks:**
1. Add material properties: albedo, roughness, metallic
2. Implement GGX specular distribution function (NDF)
3. Implement Smith geometry/visibility function
4. Implement Schlick Fresnel approximation
5. Keep diffuse as Lambertian (energy-conserving)

**Key Equations:**

```glsl
// GGX Normal Distribution Function
float D_GGX(float NoH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NoH2 = NoH * NoH;
    float denom = NoH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Smith Visibility Function
float V_SmithGGX(float NoV, float NoL, float roughness) {
    float a = roughness * roughness;
    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a) + a);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a) + a);
    return 0.5 / (GGXV + GGXL + 0.0001);
}

// Schlick Fresnel
vec3 F_Schlick(float VoH, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
}
```

**Files to create/modify:**
- `shaders/pbr_common.glsl` - Shared PBR functions
- `shaders/shader.frag` - Integrate PBR lighting

### 1.3 Normal Mapping Support

**Goal:** Add per-pixel normal detail to surfaces.

**Tasks:**
1. Add tangent vectors to vertex data
2. Calculate TBN matrix in vertex shader
3. Sample normal map and transform to world space
4. Update Mesh class to compute tangents

**Vertex attribute additions:**
```cpp
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec3 tangent;   // NEW
    glm::vec3 bitangent; // NEW
};
```

---

## Phase 2: Shadow Mapping

### 2.1 Basic Directional Shadow Map

**Goal:** Implement shadow mapping for the directional sun light.

#### 2.1.1 Shadow Map Resources

**Vulkan Resources Required:**

```cpp
struct ShadowMapResources {
    VkImage depthImage;
    VkDeviceMemory depthMemory;
    VkImageView depthView;
    VkSampler shadowSampler;
    VkFramebuffer framebuffer;
    VkRenderPass renderPass;
    VkPipeline pipeline;

    uint32_t resolution = 2048;  // 2048x2048 recommended
};
```

**Image Creation:**

```cpp
VkImageCreateInfo imageInfo{};
imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
imageInfo.imageType = VK_IMAGE_TYPE_2D;
imageInfo.extent = {shadowResolution, shadowResolution, 1};
imageInfo.mipLevels = 1;
imageInfo.arrayLayers = 1;  // Or NUM_CASCADES for CSM
imageInfo.format = VK_FORMAT_D32_SFLOAT;
imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                  VK_IMAGE_USAGE_SAMPLED_BIT;
imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
```

**Shadow Sampler (with hardware PCF):**

```cpp
VkSamplerCreateInfo samplerInfo{};
samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
samplerInfo.magFilter = VK_FILTER_LINEAR;
samplerInfo.minFilter = VK_FILTER_LINEAR;
samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
samplerInfo.compareEnable = VK_TRUE;  // Enable depth comparison
samplerInfo.compareOp = VK_COMPARE_OP_LESS;
```

#### 2.1.2 Light Space Matrix Calculation

```cpp
glm::mat4 CalculateLightSpaceMatrix(
    const glm::vec3& lightDir,
    const Camera& camera,
    float nearPlane,
    float farPlane
) {
    // Get camera frustum corners in world space
    std::array<glm::vec3, 8> frustumCorners = GetFrustumCornersWorldSpace(
        camera.getProjection(nearPlane, farPlane),
        camera.getView()
    );

    // Calculate frustum center
    glm::vec3 center(0.0f);
    for (const auto& corner : frustumCorners) {
        center += corner;
    }
    center /= 8.0f;

    // Light view matrix looking at frustum center
    glm::mat4 lightView = glm::lookAt(
        center - lightDir * 50.0f,  // Pull back along light direction
        center,
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    // Find AABB in light space
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();

    for (const auto& corner : frustumCorners) {
        glm::vec4 lightSpaceCorner = lightView * glm::vec4(corner, 1.0f);
        minX = std::min(minX, lightSpaceCorner.x);
        maxX = std::max(maxX, lightSpaceCorner.x);
        minY = std::min(minY, lightSpaceCorner.y);
        maxY = std::max(maxY, lightSpaceCorner.y);
        minZ = std::min(minZ, lightSpaceCorner.z);
        maxZ = std::max(maxZ, lightSpaceCorner.z);
    }

    // Extend Z range to catch shadow casters behind the camera
    float zMult = 10.0f;
    minZ = minZ < 0 ? minZ * zMult : minZ / zMult;
    maxZ = maxZ < 0 ? maxZ / zMult : maxZ * zMult;

    glm::mat4 lightProjection = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);

    return lightProjection * lightView;
}
```

#### 2.1.3 Shadow Pass Shaders

**Vertex Shader (shadow.vert):**

```glsl
#version 450

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform ShadowUBO {
    mat4 lightSpaceMatrix;
} shadow;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

void main() {
    gl_Position = shadow.lightSpaceMatrix * push.model * vec4(inPosition, 1.0);
}
```

**Fragment Shader (shadow.frag):**

```glsl
#version 450

// Empty - depth is written automatically
// For alpha-tested shadows, add:
// layout(location = 0) in vec2 fragTexCoord;
// layout(set = 1, binding = 0) uniform sampler2D albedoMap;
//
// void main() {
//     if (texture(albedoMap, fragTexCoord).a < 0.5) {
//         discard;
//     }
// }

void main() {}
```

#### 2.1.4 Shadow Sampling in Main Pass

```glsl
layout(set = 1, binding = 0) uniform sampler2DShadow shadowMap;

layout(set = 1, binding = 1) uniform ShadowData {
    mat4 lightSpaceMatrix;
    float shadowBias;
    float normalBias;
    float shadowMapSize;
    float padding;
} shadowData;

// Transform world position to shadow map space
vec4 GetShadowCoord(vec3 worldPos) {
    vec4 shadowCoord = shadowData.lightSpaceMatrix * vec4(worldPos, 1.0);
    shadowCoord.xyz /= shadowCoord.w;
    shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;
    return shadowCoord;
}

// PCF 3x3 with hardware filtering
float SampleShadowPCF(vec3 worldPos, vec3 normal, vec3 lightDir) {
    // Normal offset bias - push sample point along normal
    float cosTheta = saturate(dot(normal, lightDir));
    float normalOffsetScale = shadowData.normalBias * (1.0 - cosTheta);
    vec3 offsetPos = worldPos + normal * normalOffsetScale;

    vec4 shadowCoord = GetShadowCoord(offsetPos);

    // Slope-scale bias
    float bias = shadowData.shadowBias * tan(acos(cosTheta));
    bias = clamp(bias, 0.0, 0.01);
    shadowCoord.z -= bias;

    // PCF 3x3
    float shadow = 0.0;
    float texelSize = 1.0 / shadowData.shadowMapSize;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(x, y) * texelSize;
            // texture() with sampler2DShadow returns comparison result
            shadow += texture(shadowMap, vec3(shadowCoord.xy + offset, shadowCoord.z));
        }
    }

    return shadow / 9.0;
}
```

### 2.2 Cascaded Shadow Maps (CSM)

**Goal:** High-quality shadows at all distances using multiple shadow map cascades.

#### 2.2.1 Cascade Split Calculation

**Practical Split Scheme (PSSM - Parallel Split Shadow Maps):**

```cpp
void CalculateCascadeSplits(
    float nearClip,
    float farClip,
    float lambda,  // 0.5 is good balance between log and uniform
    uint32_t numCascades,
    std::vector<float>& splitDepths
) {
    splitDepths.resize(numCascades + 1);
    splitDepths[0] = nearClip;

    float clipRange = farClip - nearClip;
    float ratio = farClip / nearClip;

    for (uint32_t i = 1; i <= numCascades; i++) {
        float p = static_cast<float>(i) / numCascades;

        // Logarithmic split
        float logSplit = nearClip * std::pow(ratio, p);

        // Uniform split
        float uniformSplit = nearClip + clipRange * p;

        // Blend between log and uniform
        splitDepths[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
    }
}
```

**Recommended Configuration:**

| Cascade | Near | Far | Coverage |
|---------|------|-----|----------|
| 0 | 0.1m | 10m | Close-up detail |
| 1 | 10m | 30m | Near objects |
| 2 | 30m | 100m | Mid-range |
| 3 | 100m | 500m | Far terrain |

#### 2.2.2 Per-Cascade Light Matrix

```cpp
struct CascadeData {
    glm::mat4 viewProjMatrices[MAX_CASCADES];
    glm::vec4 splitDepths;  // View-space depths
    uint32_t numCascades;
};

void UpdateCascadeMatrices(
    const Camera& camera,
    const glm::vec3& lightDir,
    const std::vector<float>& splits,
    CascadeData& cascadeData
) {
    for (uint32_t i = 0; i < splits.size() - 1; i++) {
        cascadeData.viewProjMatrices[i] = CalculateLightSpaceMatrix(
            lightDir,
            camera,
            splits[i],
            splits[i + 1]
        );
        cascadeData.splitDepths[i] = splits[i + 1];
    }
}
```

#### 2.2.3 Texture Array for Cascades

```cpp
// Create 2D texture array for all cascades
VkImageCreateInfo imageInfo{};
imageInfo.imageType = VK_IMAGE_TYPE_2D;
imageInfo.extent = {shadowResolution, shadowResolution, 1};
imageInfo.mipLevels = 1;
imageInfo.arrayLayers = NUM_CASCADES;  // 4 layers
imageInfo.format = VK_FORMAT_D32_SFLOAT;
imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                  VK_IMAGE_USAGE_SAMPLED_BIT;

// Create separate framebuffers per cascade, or use layered rendering
// with gl_Layer in geometry shader
```

#### 2.2.4 Cascade Selection and Sampling

```glsl
layout(set = 1, binding = 0) uniform sampler2DArrayShadow shadowMapArray;

layout(set = 1, binding = 1) uniform CascadeUBO {
    mat4 lightSpaceMatrices[4];
    vec4 cascadeSplits;
    uint numCascades;
} cascades;

// Select cascade based on view-space depth
int SelectCascade(float viewDepth) {
    int cascade = 0;
    for (int i = 0; i < int(cascades.numCascades) - 1; i++) {
        if (viewDepth > cascades.cascadeSplits[i]) {
            cascade = i + 1;
        }
    }
    return cascade;
}

float SampleCascadedShadow(vec3 worldPos, vec3 normal, vec3 lightDir, float viewDepth) {
    int cascade = SelectCascade(viewDepth);

    // Transform to light space for selected cascade
    vec4 shadowCoord = cascades.lightSpaceMatrices[cascade] * vec4(worldPos, 1.0);
    shadowCoord.xyz /= shadowCoord.w;
    shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;

    // Apply bias (adjust per cascade - farther cascades need more bias)
    float baseBias = 0.0005;
    float cascadeBias = baseBias * (1.0 + cascade * 0.5);
    float slopeBias = cascadeBias * tan(acos(saturate(dot(normal, lightDir))));
    shadowCoord.z -= clamp(slopeBias, 0.0, 0.01);

    // PCF sampling with array texture
    float shadow = 0.0;
    float texelSize = 1.0 / shadowMapSize;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(x, y) * texelSize;
            shadow += texture(shadowMapArray,
                vec4(shadowCoord.xy + offset, float(cascade), shadowCoord.z));
        }
    }

    return shadow / 9.0;
}
```

#### 2.2.5 Cascade Blending

Prevent visible seams between cascades:

```glsl
float SampleCascadedShadowBlended(vec3 worldPos, vec3 normal, vec3 lightDir, float viewDepth) {
    int cascade = SelectCascade(viewDepth);

    float shadow = SampleShadowForCascade(worldPos, normal, lightDir, cascade);

    // Blend near cascade boundaries
    float blendDistance = 5.0;  // meters
    float nextSplit = cascades.cascadeSplits[cascade];
    float distToSplit = nextSplit - viewDepth;

    if (cascade < int(cascades.numCascades) - 1 && distToSplit < blendDistance) {
        float nextShadow = SampleShadowForCascade(worldPos, normal, lightDir, cascade + 1);
        float blendFactor = smoothstep(0.0, blendDistance, distToSplit);
        shadow = mix(nextShadow, shadow, blendFactor);
    }

    return shadow;
}
```

#### 2.2.6 Debug Visualization

```glsl
// Visualize cascade selection with colors
vec3 GetCascadeDebugColor(int cascade) {
    const vec3 colors[4] = vec3[](
        vec3(1.0, 0.0, 0.0),  // Red - cascade 0
        vec3(0.0, 1.0, 0.0),  // Green - cascade 1
        vec3(0.0, 0.0, 1.0),  // Blue - cascade 2
        vec3(1.0, 1.0, 0.0)   // Yellow - cascade 3
    );
    return colors[cascade];
}
```

### 2.3 Shadow Quality Improvements

#### 2.3.1 Percentage-Closer Soft Shadows (PCSS)

PCSS provides contact-hardening shadows that are sharp near occluders and soft far away.

```glsl
// PCSS Constants
const float LIGHT_SIZE = 0.02;  // Adjust based on your scene scale
const int BLOCKER_SEARCH_SAMPLES = 16;
const int PCF_SAMPLES = 32;

// Poisson disk samples (pre-generated)
const vec2 poissonDisk[32] = vec2[]( /* ... */ );

float SearchBlockerDistance(vec3 shadowCoord, float searchWidth) {
    float blockerSum = 0.0;
    int blockerCount = 0;

    for (int i = 0; i < BLOCKER_SEARCH_SAMPLES; i++) {
        vec2 offset = poissonDisk[i] * searchWidth;
        float depth = texture(shadowMap, shadowCoord.xy + offset).r;

        if (depth < shadowCoord.z) {
            blockerSum += depth;
            blockerCount++;
        }
    }

    if (blockerCount == 0) return -1.0;  // No blockers found
    return blockerSum / float(blockerCount);
}

float PCSS(vec3 shadowCoord, vec3 normal, vec3 lightDir) {
    // 1. Blocker search
    float searchWidth = LIGHT_SIZE * shadowCoord.z / shadowCoord.z;
    float avgBlockerDepth = SearchBlockerDistance(shadowCoord, searchWidth);

    if (avgBlockerDepth < 0.0) return 1.0;  // No shadow

    // 2. Penumbra estimation
    float penumbraWidth = (shadowCoord.z - avgBlockerDepth) * LIGHT_SIZE / avgBlockerDepth;

    // 3. PCF with variable filter size
    float shadow = 0.0;
    for (int i = 0; i < PCF_SAMPLES; i++) {
        vec2 offset = poissonDisk[i] * penumbraWidth;
        float depth = texture(shadowMap, shadowCoord.xy + offset).r;
        shadow += shadowCoord.z - bias > depth ? 0.0 : 1.0;
    }

    return shadow / float(PCF_SAMPLES);
}
```

#### 2.3.2 Shadow Map Stabilization

Prevent shadow swimming when camera moves:

```cpp
// Snap light frustum to texel boundaries
void StabilizeCascade(glm::mat4& lightViewProj, float shadowMapSize) {
    // Get the shadow origin in light space
    glm::vec4 shadowOrigin = lightViewProj * glm::vec4(0, 0, 0, 1);
    shadowOrigin *= shadowMapSize / 2.0f;

    // Calculate offset to snap to texel
    glm::vec4 roundedOrigin = glm::round(shadowOrigin);
    glm::vec4 roundOffset = roundedOrigin - shadowOrigin;
    roundOffset *= 2.0f / shadowMapSize;
    roundOffset.z = 0.0f;
    roundOffset.w = 0.0f;

    // Apply offset to projection matrix
    lightViewProj[3] += roundOffset;
}
```

#### 2.3.3 Far Shadow Maps (Ghost of Tsushima approach)

For large open worlds, use a separate low-resolution far shadow atlas:

```cpp
struct FarShadowAtlas {
    // 128x128 shadow map per 200m world tile
    static constexpr uint32_t TILE_SIZE = 200;  // meters
    static constexpr uint32_t TILE_RESOLUTION = 128;
    static constexpr uint32_t ATLAS_TILES = 16;  // 16x16 grid

    VkImage atlasImage;  // 2048x2048 total
    std::unordered_map<glm::ivec2, uint32_t> tileMapping;
};
```

#### 2.3.4 Screen-Space Contact Shadows

Add fine detail shadows that shadow maps miss:

```glsl
float ScreenSpaceContactShadow(vec3 worldPos, vec3 lightDir, sampler2D depthBuffer) {
    vec3 rayStart = worldPos;
    vec3 rayDir = lightDir;
    float rayLength = 0.5;  // meters
    int numSteps = 16;

    vec3 rayStep = rayDir * rayLength / float(numSteps);
    vec3 currentPos = rayStart;

    for (int i = 0; i < numSteps; i++) {
        currentPos += rayStep;

        // Project to screen space
        vec4 clipPos = viewProj * vec4(currentPos, 1.0);
        vec2 screenUV = clipPos.xy / clipPos.w * 0.5 + 0.5;

        if (screenUV.x < 0.0 || screenUV.x > 1.0 ||
            screenUV.y < 0.0 || screenUV.y > 1.0) {
            break;
        }

        float sceneDepth = texture(depthBuffer, screenUV).r;
        float rayDepth = clipPos.z / clipPos.w;

        if (rayDepth > sceneDepth + 0.001) {
            // Ray went behind geometry - shadowed
            return 0.0;
        }
    }

    return 1.0;  // No occlusion found
}
```

---

## Phase 3: Indirect Lighting

### 3.1 Spherical Harmonics Probe System

**Goal:** Capture and apply diffuse indirect lighting using SH probes with dynamic time-of-day support.

**Reference:** Ghost of Tsushima's sky visibility + bounce light approach

#### 3.1.1 Core Concept - Sky Visibility Probes

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

#### 3.1.2 Data Structures

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

#### 3.1.3 SH Math Fundamentals

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

#### 3.1.4 Runtime Sky Visibility Lighting

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

#### 3.1.5 Sun Bounce Approximation

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

#### 3.1.6 Probe Interpolation

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

#### 3.1.7 Light Leak Prevention

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

#### 3.1.8 SH De-Ringing

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

#### 3.1.9 Directionality Boost

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

### 3.2 Reflection Probes

**Goal:** Capture specular indirect lighting with dynamic relighting.

#### 3.2.1 Probe Capture Data

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

#### 3.2.2 Runtime Relighting

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

#### 3.2.3 Cube Map Shadow Tracing

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

#### 3.2.4 Pre-filtered Environment Maps

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

#### 3.2.5 Sampling with Parallax Correction

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

### 3.3 Horizon Occlusion

**Goal:** Prevent unrealistic specular glow on backfacing normal map bumps.

#### 3.3.1 The Problem

When a normal map tilts the surface normal away from the geometric normal, the reflection vector can point into the surface. Standard IBL sampling will return bright sky reflections for these "impossible" reflections.

#### 3.3.2 Cone-Based Approximation

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

#### 3.3.3 Parallax-Compensated Roughness

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

## Phase 4: Atmospheric Scattering

### 4.1 Physically-Based Sky Model

**Goal:** Implement multi-scattering atmospheric model with accurate Rayleigh scattering.

**Reference:** Bruneton & Neyret (2008), Hillaire (2020), Ghost of Tsushima LMS color space

#### 4.1.1 Atmospheric Parameters

```cpp
struct AtmosphereParams {
    // Planet geometry
    float planetRadius = 6371000.0f;       // Earth radius in meters
    float atmosphereRadius = 6471000.0f;   // Top of atmosphere

    // Rayleigh scattering (air molecules)
    glm::vec3 rayleighScatteringBase = glm::vec3(5.802e-6f, 13.558e-6f, 33.1e-6f);
    float rayleighScaleHeight = 8000.0f;   // Density falloff

    // Mie scattering (aerosols/haze)
    float mieScatteringBase = 3.996e-6f;
    float mieAbsorptionBase = 4.4e-6f;
    float mieScaleHeight = 1200.0f;
    float mieAnisotropy = 0.8f;            // Phase function asymmetry

    // Ozone absorption (affects blue channel at horizon)
    glm::vec3 ozoneAbsorption = glm::vec3(0.65e-6f, 1.881e-6f, 0.085e-6f);
    float ozoneLayerCenter = 25000.0f;     // meters
    float ozoneLayerWidth = 15000.0f;

    // Sun
    float sunAngularRadius = 0.00935f / 2.0f;  // radians
    glm::vec3 solarIrradiance = glm::vec3(1.474f, 1.8504f, 1.91198f);  // W/m²
};
```

#### 4.1.2 LUT Precomputation Overview

| LUT | Dimensions | Contents | When Computed |
|-----|------------|----------|---------------|
| Transmittance | 256×64 | Optical depth integral | Once at startup |
| Multi-scatter | 32×32 | Multiple scattering contribution | Once at startup |
| Sky-View | 192×108 | In-scattered radiance | Every frame |
| Aerial Perspective | 32×32×32 | Volumetric in-scattering | Every frame |

#### 4.1.3 Transmittance LUT

Stores how much light reaches a point from infinity (or the ground).

**Parameterization:**

```cpp
// X: cosine of view zenith angle (μ)
// Y: normalized altitude (r - planetRadius) / (atmosphereRadius - planetRadius)
glm::vec2 TransmittanceLUTParams(float altitude, float cosZenith) {
    float H = sqrt(atmosphereRadius * atmosphereRadius -
                   planetRadius * planetRadius);
    float rho = sqrt(max((planetRadius + altitude) * (planetRadius + altitude) -
                        planetRadius * planetRadius, 0.0f));

    float d = DistanceToAtmosphereBoundary(altitude, cosZenith);
    float dMin = atmosphereRadius - planetRadius - altitude;
    float dMax = rho + H;

    float xMu = (d - dMin) / (dMax - dMin);
    float xR = rho / H;

    return glm::vec2(xMu, xR);
}
```

**Compute Shader:**

```glsl
layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba16f, binding = 0) writeonly uniform image2D transmittanceLUT;

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    vec2 uv = (vec2(texel) + 0.5) / vec2(TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT);

    // Decode altitude and angle from UV
    float r, mu;
    UVToTransmittanceParams(uv, r, mu);

    // Integrate optical depth along ray
    vec3 transmittance = ComputeTransmittance(r, mu);

    imageStore(transmittanceLUT, texel, vec4(transmittance, 1.0));
}

vec3 ComputeTransmittance(float r, float mu) {
    float dx = DistanceToAtmosphereBoundary(r, mu) / float(TRANSMITTANCE_STEPS);

    vec3 opticalDepth = vec3(0.0);

    for (int i = 0; i <= TRANSMITTANCE_STEPS; i++) {
        float d = float(i) * dx;
        float altitude = sqrt(r * r + d * d + 2.0 * r * mu * d) - planetRadius;

        vec3 localDensity = GetAtmosphereDensity(altitude);
        float weight = (i == 0 || i == TRANSMITTANCE_STEPS) ? 0.5 : 1.0;
        opticalDepth += localDensity * weight * dx;
    }

    return exp(-opticalDepth);
}
```

#### 4.1.4 Multiple Scattering LUT

Bruneton's multi-scattering approximation:

```glsl
// Precompute the ratio of multiple-scattered radiance to single-scattered
// This is approximately constant for isotropic scattering

layout(local_size_x = 8, local_size_y = 8) in;
layout(rg16f, binding = 0) writeonly uniform image2D multiScatterLUT;

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    vec2 uv = (vec2(texel) + 0.5) / vec2(32, 32);

    float cosLightZenith = uv.x * 2.0 - 1.0;
    float altitude = uv.y * (atmosphereRadius - planetRadius);

    // Integrate over hemisphere
    vec3 fms = vec3(0.0);  // Second order scattering
    vec3 luminance = vec3(0.0);

    const int SAMPLE_COUNT = 64;
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        vec2 xi = Hammersley(i, SAMPLE_COUNT);
        vec3 dir = UniformHemisphere(xi);

        // Single scattering in this direction
        vec3 singleScatter = ComputeSingleScattering(altitude, dir, cosLightZenith);

        // Accumulate for hemisphere average
        float weight = dot(dir, vec3(0, 1, 0)) * 2.0;
        luminance += singleScatter * weight;
    }

    luminance /= float(SAMPLE_COUNT);

    // Store Rayleigh and Mie separately
    imageStore(multiScatterLUT, texel, vec4(luminance.rg, 0.0, 1.0));
}
```

#### 4.1.5 Sky-View LUT (Runtime)

Resample 3D LUTs to 2D for current sun angle:

```glsl
// Called each frame when sun angle changes

layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba16f, binding = 0) writeonly uniform image2D skyViewLUT;
layout(binding = 1) uniform sampler2D transmittanceLUT;
layout(binding = 2) uniform sampler2D multiScatterLUT;

uniform vec3 sunDirection;
uniform float cameraAltitude;

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    vec2 uv = (vec2(texel) + 0.5) / vec2(SKY_VIEW_WIDTH, SKY_VIEW_HEIGHT);

    // Decode view direction from UV (non-linear mapping for horizon detail)
    vec3 viewDir = SkyViewUVToDirection(uv);

    // Ray march through atmosphere
    vec3 inscatter = vec3(0.0);
    vec3 transmittance = vec3(1.0);

    float rayLength = RayIntersectAtmosphere(cameraAltitude, viewDir);
    float stepSize = rayLength / float(SKY_VIEW_STEPS);

    for (int i = 0; i < SKY_VIEW_STEPS; i++) {
        float t = (float(i) + 0.5) * stepSize;
        vec3 pos = vec3(0, cameraAltitude + planetRadius, 0) + viewDir * t;
        float altitude = length(pos) - planetRadius;

        // Sample transmittance to sun
        vec3 sunTransmittance = SampleTransmittanceLUT(altitude, dot(normalize(pos), sunDirection));

        // Local scattering
        vec3 density = GetAtmosphereDensity(altitude);
        vec3 rayleighScatter = density.x * rayleighScatteringBase;
        vec3 mieScatter = density.y * vec3(mieScatteringBase);

        // Phase functions
        float cosTheta = dot(viewDir, sunDirection);
        float rayleighPhase = RayleighPhase(cosTheta);
        float miePhase = HenyeyGreensteinPhase(cosTheta, mieAnisotropy);

        // Single scattering
        vec3 singleScatter = sunTransmittance *
            (rayleighScatter * rayleighPhase + mieScatter * miePhase);

        // Add multi-scattering
        vec2 ms = SampleMultiScatterLUT(altitude, dot(normalize(pos), sunDirection));
        vec3 multiScatter = ms.x * rayleighScatter + ms.y * mieScatter;

        // Integrate
        vec3 extinction = rayleighScatter + mieScatter + GetOzoneAbsorption(altitude);
        vec3 segmentTransmittance = exp(-extinction * stepSize);

        inscatter += transmittance * (singleScatter + multiScatter) *
                    (1.0 - segmentTransmittance) / max(extinction, vec3(0.0001));
        transmittance *= segmentTransmittance;
    }

    // Apply sun irradiance
    inscatter *= solarIrradiance;

    imageStore(skyViewLUT, texel, vec4(inscatter, 1.0));
}
```

#### 4.1.6 Phase Functions

```glsl
float RayleighPhase(float cosTheta) {
    return 3.0 / (16.0 * PI) * (1.0 + cosTheta * cosTheta);
}

float HenyeyGreensteinPhase(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(denom, 1.5));
}

// Cornette-Shanks (more physically accurate than HG for Mie)
float CornetteShanksMiePhase(float cosTheta, float g) {
    float g2 = g * g;
    float num = 3.0 * (1.0 - g2) * (1.0 + cosTheta * cosTheta);
    float denom = 8.0 * PI * (2.0 + g2) * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return num / denom;
}
```

#### 4.1.7 LMS Color Space for Accurate Rayleigh

Ghost of Tsushima's optimization for spectral accuracy:

```cpp
// Standard Rec709 Rayleigh coefficients produce greenish sunsets
// LMS spectral primaries give more accurate results

// Custom LMS primaries optimized for Rayleigh accuracy
const glm::mat3 RGB_TO_LMS = glm::mat3(
    0.4122214708f, 0.5363325363f, 0.0514459929f,
    0.2119034982f, 0.6806995451f, 0.1073969566f,
    0.0883024619f, 0.2817188376f, 0.6299787005f
);

const glm::mat3 LMS_TO_RGB = glm::inverse(RGB_TO_LMS);

// Optimized Rayleigh coefficients for LMS space
const glm::vec3 RAYLEIGH_LMS = glm::vec3(6.95e-6f, 12.28e-6f, 28.44e-6f);
```

**GLSL Usage:**

```glsl
vec3 ComputeRayleighScatteringLMS(vec3 rgbColor) {
    vec3 lms = RGB_TO_LMS * rgbColor;
    vec3 scattered = lms * RAYLEIGH_LMS;
    return LMS_TO_RGB * scattered;
}
```

#### 4.1.8 Earth Shadow

Apply analytical shadow at sunrise/sunset:

```glsl
float ComputeEarthShadow(vec3 worldPos, vec3 sunDir) {
    // Vector from planet center to position
    vec3 toPos = worldPos + vec3(0, planetRadius, 0);
    float altitude = length(toPos) - planetRadius;

    // Project onto sun direction
    float sunDist = dot(toPos, sunDir);

    // Check if in shadow cone
    if (sunDist < 0.0) {
        // Behind planet relative to sun
        float perpDist = length(toPos - sunDir * sunDist);
        float shadowRadius = planetRadius * (1.0 - altitude / atmosphereRadius);

        if (perpDist < shadowRadius) {
            // In umbra/penumbra
            float penumbraWidth = atmosphereRadius * 0.1;  // Soft edge
            return smoothstep(shadowRadius - penumbraWidth, shadowRadius, perpDist);
        }
    }

    return 1.0;
}
```

#### 4.1.9 Irradiance LUTs for Lighting

Ghost of Tsushima stores separate Rayleigh and Mie irradiance for lighting clouds/haze:

```glsl
// These LUTs store scattered light *before* multiplying by phase function
// Indexed by: altitude, sun angle, view angle

// For clouds and haze, sample these LUTs and multiply by appropriate phase
vec3 SampleAtmosphericIrradiance(vec3 worldPos, vec3 sunDir) {
    float altitude = GetAltitude(worldPos);
    float cosSunZenith = dot(normalize(worldPos + vec3(0, planetRadius, 0)), sunDir);

    vec2 uv = EncodeIrradianceLUTUV(altitude, cosSunZenith);

    vec3 rayleighIrr = texture(rayleighIrradianceLUT, uv).rgb;
    vec3 mieIrr = texture(mieIrradianceLUT, uv).rgb;

    return rayleighIrr + mieIrr;
}
```

#### 4.1.10 Rendering the Sky

```glsl
// Final sky rendering in fragment shader
vec3 RenderSky(vec3 viewDir) {
    // Sample sky-view LUT
    vec2 uv = DirectionToSkyViewUV(viewDir);
    vec3 skyColor = texture(skyViewLUT, uv).rgb;

    // Add sun disk
    float sunCosAngle = dot(viewDir, sunDirection);
    if (sunCosAngle > cos(sunAngularRadius)) {
        // Inside sun disk
        vec3 sunTransmittance = SampleTransmittanceLUT(cameraAltitude, sunCosAngle);
        float limbDarkening = sqrt(1.0 - pow(1.0 - sunCosAngle, 2.0) /
                                        pow(sin(sunAngularRadius), 2.0));
        skyColor += solarIrradiance * sunTransmittance * limbDarkening * 1000.0;
    }

    return skyColor;
}
```

### 4.2 Volumetric Clouds

**Goal:** Render procedural volumetric clouds with realistic lighting.

**Reference:** Andrew Schneider (Horizon Zero Dawn), Sebastian Hillaire, Ghost of Tsushima

#### 4.2.1 Cloud Map Architecture

Ghost of Tsushima renders clouds to a **paraboloid map** covering the entire hemisphere:

**Triple-Buffer System:**
- 2 textures for temporal blending (scroll in wind direction)
- 1 texture for current frame rendering
- Time-sliced updates over 60 frames for performance

**Paraboloid Map Format (RGB11F):**
- **Red channel:** Mie scattering (multiplied by Mie irradiance from sky LUT)
- **Green channel:** Rayleigh scattering (multiplied by Rayleigh irradiance)
- **Blue channel:** Transmittance

**Resolution:** 768x768 (low due to performance constraints)

```cpp
struct CloudRenderTarget {
    VkImage paraboloidMaps[3];      // Triple buffer
    VkImageView paraboloidViews[3];
    uint32_t currentWriteIndex;
    uint32_t blendIndex0, blendIndex1;
    float blendFactor;
};
```

#### 4.2.2 Density Anti-Aliasing

Low resolution causes aliasing where cloud position changes rapidly in UV space. Solution: reduce density in high-derivative areas.

**Calculate derivatives in polar coordinates:**

```glsl
// Compute rate of change of cloud position with respect to texture coordinates
float dRadial_dU = computeRadialDerivative(uv);   // Radial position vs radial texcoord
float dAngular_dV = computeAngularDerivative(uv); // Angular position vs angular texcoord

float maxDerivative = max(dRadial_dU, dAngular_dV);

// Reduce density when derivatives are high
float densityScale = saturate(1.0 / (1.0 + maxDerivative * antiAliasStrength));
float finalDensity = baseDensity * densityScale;
```

**Before/After:** Eliminates noisy sampling artifacts and pixelated edges at horizon.

#### 4.2.3 Cloud Density Modeling

**Noise Textures Required:**

1. **Base shape noise** (3D, 128³): Low-frequency Perlin-Worley
2. **Detail noise** (3D, 32³): High-frequency Worley
3. **Weather map** (2D, 1024²): Coverage, type, precipitation
4. **Curl noise** (3D, 32³): For wispy detail distortion

**Density Function:**

```glsl
float SampleCloudDensity(vec3 worldPos, float lod) {
    // Sample weather map for coverage
    vec2 weatherUV = worldPos.xz * weatherMapScale;
    vec4 weather = texture(weatherMap, weatherUV);
    float coverage = weather.r;
    float cloudType = weather.g;  // 0=stratus, 1=cumulus

    // Height gradient based on cloud type
    float heightFraction = GetHeightFraction(worldPos);
    float heightGradient = GetHeightGradient(heightFraction, cloudType);

    // Sample base shape noise
    vec3 baseUV = worldPos * baseNoiseScale + windOffset;
    float baseNoise = texture(baseShapeNoise, baseUV).r;

    // Apply coverage and height gradient
    float baseDensity = RemapClamped(baseNoise, 1.0 - coverage, 1.0, 0.0, 1.0);
    baseDensity *= heightGradient;

    // Add detail erosion (skip at distance for performance)
    if (lod < detailLodThreshold) {
        vec3 detailUV = worldPos * detailNoiseScale + windOffset * 0.5;
        float detailNoise = texture(detailNoise, detailUV).r;
        baseDensity = RemapClamped(baseDensity, detailNoise * 0.3, 1.0, 0.0, 1.0);
    }

    return max(baseDensity, 0.0);
}
```

#### 4.2.4 Phase Function with Depth-Dependent Scattering

**Key Insight from Ghost of Tsushima:** The asymmetry parameter `g` varies based on optical depth. Forward scattering dominates in wispy areas; multiple scattering (approximated by back-scatter) dominates in dense areas.

```glsl
float HenyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(denom, 1.5));
}

float CloudPhaseFunction(float cosTheta, float transmittanceToLight, float segmentTransmittance) {
    // Product of transmittance to light and current segment transmittance
    float opticalDepthFactor = transmittanceToLight * segmentTransmittance;

    // Lerp between back-scatter (dense) and forward-scatter (wispy)
    float gForward = 0.8;   // Strong forward scattering for single-scatter
    float gBack = -0.15;    // Slight back-scatter for multi-scatter approximation

    float g = mix(gBack, gForward, opticalDepthFactor);

    float phase = HenyeyGreenstein(cosTheta, g);

    // Scale back-scatter contribution by multi-scattering factor
    // 2.16 found by simulating dense Mie layer with albedo 0.9
    float multiScatterScale = 2.16;
    if (g < 0.0) {
        phase *= multiScatterScale;
    }

    return phase;
}
```

**Effects achieved:**
- **Silver lining:** Bright edges on backlit clouds (forward scatter at low density)
- **Dark edges:** Natural darkening without "powdered sugar" heuristic
- **Soft interior:** Multi-scatter brightens dense cloud interiors

#### 4.2.5 Ray Marching Implementation

```glsl
struct CloudMarchResult {
    vec3 scattering;      // Accumulated in-scattered light
    float transmittance;  // Remaining light transmission
};

CloudMarchResult MarchClouds(vec3 rayOrigin, vec3 rayDir, float maxDist) {
    CloudMarchResult result;
    result.scattering = vec3(0.0);
    result.transmittance = 1.0;

    // Find intersection with cloud layer
    vec2 cloudIntersection = IntersectCloudLayer(rayOrigin, rayDir);
    if (cloudIntersection.x > cloudIntersection.y) {
        return result; // No intersection
    }

    float t = cloudIntersection.x;
    float tEnd = min(cloudIntersection.y, maxDist);

    // Adaptive step size
    float stepSize = (tEnd - t) / float(numSteps);

    // Jitter start position for temporal AA
    t += stepSize * BlueNoise(gl_FragCoord.xy, frameIndex);

    vec3 sunDir = normalize(sunPosition);
    float cosTheta = dot(rayDir, sunDir);

    for (int i = 0; i < numSteps && result.transmittance > 0.01; i++) {
        vec3 pos = rayOrigin + rayDir * t;

        float density = SampleCloudDensity(pos, t);

        if (density > 0.001) {
            // Sample transmittance to sun (cheaper: 6 samples)
            float transmittanceToSun = SampleTransmittanceToSun(pos, sunDir);

            // Phase function with depth-dependent scattering
            float phase = CloudPhaseFunction(cosTheta, transmittanceToSun, result.transmittance);

            // In-scattering from sun
            vec3 sunLight = sunColor * sunIntensity * transmittanceToSun * phase;

            // In-scattering from sky (ambient, use sky LUT)
            vec3 skyLight = SampleSkyIrradiance(pos) * ambientScatterStrength;

            // Accumulate scattering
            float extinction = density * extinctionCoeff * stepSize;
            float segmentTransmittance = exp(-extinction);

            // Energy-conserving integration
            vec3 integScatter = (sunLight + skyLight) * (1.0 - segmentTransmittance) / max(extinction, 0.0001);
            result.scattering += result.transmittance * integScatter;
            result.transmittance *= segmentTransmittance;
        }

        t += stepSize;
    }

    return result;
}
```

#### 4.2.6 Light Sampling Optimization

Sampling transmittance to sun is expensive. Use cone sampling with 6 samples:

```glsl
float SampleTransmittanceToSun(vec3 pos, vec3 sunDir) {
    float transmittance = 1.0;

    // 6 samples along cone towards sun
    const float coneSpread = 0.3;
    const int numLightSamples = 6;
    float stepSize = lightMarchDistance / float(numLightSamples);

    for (int i = 0; i < numLightSamples; i++) {
        float t = stepSize * (float(i) + 0.5);

        // Add cone spread for softer shadows
        vec3 offset = RandomInCone(sunDir, coneSpread) * t * 0.1;
        vec3 samplePos = pos + sunDir * t + offset;

        float density = SampleCloudDensity(samplePos, t);
        transmittance *= exp(-density * extinctionCoeff * stepSize);

        if (transmittance < 0.01) break;
    }

    return transmittance;
}
```

#### 4.2.7 Temporal Reprojection

Clouds update over 60 frames. Use reprojection to hide artifacts:

```glsl
vec3 ReprojectClouds(vec2 currentUV, vec3 currentColor, float currentTransmittance) {
    // Get previous frame UV based on camera/wind motion
    vec2 prevUV = currentUV - motionVector;

    // Sample previous frame
    vec4 prevCloud = texture(prevCloudMap, prevUV);

    // Reject if UV is off-screen or transmittance changed significantly
    bool valid = all(greaterThan(prevUV, vec2(0.0))) &&
                 all(lessThan(prevUV, vec2(1.0))) &&
                 abs(prevCloud.a - currentTransmittance) < 0.2;

    if (valid) {
        // Blend with history (0.95 = very stable, 0.8 = more responsive)
        float blendFactor = 0.9;
        return mix(currentColor, prevCloud.rgb, blendFactor);
    }

    return currentColor;
}
```

#### 4.2.8 Compositing with Scene

```glsl
// In final composite shader
vec4 CompositeCloudLayer(vec2 uv, vec3 sceneColor, float sceneDepth) {
    // Sample cloud paraboloid map
    vec3 viewDir = GetViewDirection(uv);
    vec2 paraboloidUV = ViewDirToParaboloid(viewDir);

    vec4 cloud = texture(cloudMap, paraboloidUV);
    vec3 cloudScatter = cloud.r * mieIrradiance + cloud.g * rayleighIrradiance;
    float cloudTransmittance = cloud.b;

    // Only apply clouds where sky is visible (infinite depth)
    if (sceneDepth >= farPlane * 0.99) {
        return vec4(cloudScatter + sceneColor * cloudTransmittance, 1.0);
    }

    return vec4(sceneColor, 1.0);
}
```

#### 4.2.9 Cloud Rendering Pipeline Summary

```
Frame N:
1. Update 1/60th of paraboloid cloud map (time-sliced)
2. Ray march clouds for updated region
3. Store Mie scatter, Rayleigh scatter, transmittance
4. Apply temporal reprojection

Composite Pass:
1. Sample cloud map using view direction → paraboloid UV
2. Multiply channels by current sky LUT values
3. Blend with scene based on depth
```

**Performance Budget (from Ghost of Tsushima):**
- Cloud map update: ~0.5-1.0ms per frame (time-sliced)
- Compositing: ~0.1ms
- Total cloud overhead: <1.5ms on PS4

### 4.3 Volumetric Haze/Fog

**Goal:** Add atmospheric haze using froxel-based volumetrics with god rays.

**Reference:** Bart Wronski (Assassin's Creed), Drobot (Call of Duty), Ghost of Tsushima

#### 4.3.1 Froxel Grid Architecture

A froxel (frustum voxel) grid is a 3D texture aligned to the view frustum for efficient volumetric rendering.

**Configuration:**

```cpp
struct FroxelGrid {
    // Grid dimensions
    static constexpr uint32_t WIDTH = 128;
    static constexpr uint32_t HEIGHT = 64;
    static constexpr uint32_t DEPTH = 64;

    // Depth distribution factor (each slice 20% thicker than previous)
    static constexpr float DEPTH_DISTRIBUTION = 1.2f;

    // Far plane for volumetrics (usually less than scene far plane)
    float volumetricFarPlane = 500.0f;

    // Textures
    VkImage scatteringTexture;   // RGB11F - in-scattered light / opacity
    VkImage shadowAOTexture;     // RG16F - shadow, ambient occlusion
    VkImage lightTexture;        // RGB11F - local light contribution

    // Convert linear depth to slice index
    float DepthToSlice(float linearDepth) {
        float normalized = linearDepth / volumetricFarPlane;
        // Exponential distribution gives more resolution near camera
        return log(1.0f + normalized * (pow(DEPTH_DISTRIBUTION, DEPTH) - 1.0f)) /
               log(DEPTH_DISTRIBUTION);
    }

    // Convert slice to linear depth
    float SliceToDepth(float slice) {
        return volumetricFarPlane *
               (pow(DEPTH_DISTRIBUTION, slice) - 1.0f) /
               (pow(DEPTH_DISTRIBUTION, DEPTH) - 1.0f);
    }
};
```

#### 4.3.2 Vulkan Resources

```cpp
void CreateFroxelResources(VkDevice device, FroxelGrid& grid) {
    // Scattering texture (stores L / α for anti-aliasing)
    VkImageCreateInfo scatterInfo{};
    scatterInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    scatterInfo.imageType = VK_IMAGE_TYPE_3D;
    scatterInfo.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    scatterInfo.extent = {FroxelGrid::WIDTH, FroxelGrid::HEIGHT, FroxelGrid::DEPTH};
    scatterInfo.mipLevels = 1;
    scatterInfo.arrayLayers = 1;
    scatterInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    scatterInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    vkCreateImage(device, &scatterInfo, nullptr, &grid.scatteringTexture);

    // Shadow/AO texture for temporal filtering
    VkImageCreateInfo shadowAOInfo = scatterInfo;
    shadowAOInfo.format = VK_FORMAT_R16G16_SFLOAT;
    vkCreateImage(device, &shadowAOInfo, nullptr, &grid.shadowAOTexture);
}
```

#### 4.3.3 Density Functions

Ghost of Tsushima uses analytic density functions that can be integrated analytically:

```glsl
// Haze parameters
uniform float hazeBaseHeight;
uniform float hazeScaleHeight;      // Exponential falloff
uniform float hazeDensity;
uniform float hazeAbsorption;

uniform float fogBaseHeight;
uniform float fogLayerThickness;
uniform float fogDensity;

// Exponential height falloff (good for general haze)
float ExponentialHeightDensity(float height) {
    return hazeDensity * exp(-(height - hazeBaseHeight) / hazeScaleHeight);
}

// Sigmoidal layer (good for ground fog)
float SigmoidalLayerDensity(float height) {
    float d0 = 0.0;  // Usually 0
    float d1 = fogDensity;
    float t = (height - fogBaseHeight) / fogLayerThickness;
    return mix(d1, d0, 1.0 / (1.0 + exp(-t)));
}

// Combined density
float GetHazeDensity(vec3 worldPos) {
    float height = worldPos.y;
    return ExponentialHeightDensity(height) + SigmoidalLayerDensity(height);
}

// Analytic integration of exponential density along ray
vec2 IntegrateExponentialDensity(float h0, float h1, float scaleHeight, float density) {
    // Returns (optical depth, average height)
    float deltaH = h1 - h0;
    if (abs(deltaH) < 0.001) {
        float d = density * exp(-h0 / scaleHeight);
        return vec2(d * length(deltaH), h0);
    }

    float invScaleHeight = 1.0 / scaleHeight;
    float opticalDepth = density * scaleHeight *
        (exp(-h0 * invScaleHeight) - exp(-h1 * invScaleHeight)) / deltaH;

    return vec2(opticalDepth, (h0 + h1) * 0.5);
}
```

#### 4.3.4 Froxel Update Compute Shader

```glsl
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(binding = 0, r11f_g11f_b10f) writeonly uniform image3D scatteringVolume;
layout(binding = 1, rg16f) uniform image3D shadowAOVolume;  // Read/write for temporal
layout(binding = 2) uniform sampler2DArrayShadow cascadeShadowMap;
layout(binding = 3) uniform sampler3D prevScatteringVolume;

// Per-frame uniforms
uniform mat4 invViewProj;
uniform mat4 prevViewProj;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform float frameIndex;

void main() {
    ivec3 texel = ivec3(gl_GlobalInvocationID);

    // Froxel center in NDC
    vec3 uvw = (vec3(texel) + 0.5) / vec3(FROXEL_WIDTH, FROXEL_HEIGHT, FROXEL_DEPTH);

    // Depth slice to linear depth
    float linearDepth = SliceToDepth(uvw.z * FROXEL_DEPTH);

    // World position
    vec4 clipPos = vec4(uvw.xy * 2.0 - 1.0, LinearToNDCDepth(linearDepth), 1.0);
    vec4 worldPos4 = invViewProj * clipPos;
    vec3 worldPos = worldPos4.xyz / worldPos4.w;

    // Sample density
    float density = GetHazeDensity(worldPos);

    if (density < 0.0001) {
        imageStore(scatteringVolume, texel, vec4(0.0));
        return;
    }

    // Jitter shadow sample for temporal stability
    vec2 jitter = BlueNoise2D(texel.xy, frameIndex) * 0.5;

    // Sample cascaded shadow map
    float shadow = SampleCascadedShadow(worldPos + sunDirection * jitter.x, sunDirection);

    // Sample ambient occlusion from SH probes
    float ao = SampleAmbientOcclusion(worldPos);

    // Temporal filtering for shadow/AO
    vec4 prevShadowAO = imageLoad(shadowAOVolume, texel);
    vec2 currentShadowAO = vec2(shadow, ao);

    // Reprojection for temporal filtering
    vec4 prevClip = prevViewProj * vec4(worldPos, 1.0);
    vec3 prevUVW = prevClip.xyz / prevClip.w * 0.5 + 0.5;

    if (all(greaterThan(prevUVW, vec3(0.0))) && all(lessThan(prevUVW, vec3(1.0)))) {
        // Blend with history
        currentShadowAO = mix(currentShadowAO, prevShadowAO.xy, 0.9);
    }

    imageStore(shadowAOVolume, texel, vec4(currentShadowAO, 0.0, 1.0));

    shadow = currentShadowAO.x;
    ao = currentShadowAO.y;

    // In-scattering from sun
    vec3 sunIrradiance = SampleAtmosphericIrradiance(worldPos, sunDirection);
    float phase = HenyeyGreensteinPhase(dot(normalize(worldPos - cameraPos), sunDirection), 0.8);
    vec3 sunInScatter = sunIrradiance * phase * shadow * density;

    // In-scattering from sky (ambient)
    vec3 skyIrradiance = SampleSkyIrradianceSH(worldPos);
    vec3 skyInScatter = skyIrradiance * ao * density * 0.1;  // Isotropic

    vec3 totalInScatter = sunInScatter + skyInScatter;

    // Store L / α (not L) for anti-aliasing
    // This will be converted back when compositing
    imageStore(scatteringVolume, texel, vec4(totalInScatter, 1.0));
}
```

#### 4.3.5 Integration with Quad Swizzling

Ghost of Tsushima integrates scattering front-to-back using quad swizzling for efficiency:

```glsl
// Second pass: integrate scattering front to back
layout(local_size_x = 4, local_size_y = 4, local_size_z = 1) in;

layout(binding = 0, r11f_g11f_b10f) uniform image3D scatteringVolume;

shared vec4 sharedScatter[4][4][4];  // Per-thread group cache

void main() {
    ivec2 xyTexel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 localXY = ivec2(gl_LocalInvocationID.xy);

    // Load all depth slices for this XY into shared memory
    for (int z = 0; z < FROXEL_DEPTH; z++) {
        sharedScatter[localXY.x][localXY.y][z % 4] =
            imageLoad(scatteringVolume, ivec3(xyTexel, z));
    }

    barrier();

    // Integrate front to back
    vec3 accumulatedScatter = vec3(0.0);
    float accumulatedTransmittance = 1.0;

    for (int z = 0; z < FROXEL_DEPTH; z++) {
        float sliceDepth = SliceToDepth(float(z));
        float nextSliceDepth = SliceToDepth(float(z + 1));
        float sliceThickness = nextSliceDepth - sliceDepth;

        // Get in-scattering for this froxel
        vec4 sliceData = sharedScatter[localXY.x][localXY.y][z % 4];
        vec3 inScatter = sliceData.rgb;

        // Get extinction from density (could store separately)
        float extinction = length(inScatter) / max(0.0001, sliceData.a);

        // Beer-Lambert
        float segmentTransmittance = exp(-extinction * sliceThickness);

        // Energy-conserving accumulation
        vec3 segmentScatter = inScatter * (1.0 - segmentTransmittance);
        accumulatedScatter += accumulatedTransmittance * segmentScatter;
        accumulatedTransmittance *= segmentTransmittance;

        // Store integrated result (L / α for anti-aliasing)
        float alpha = 1.0 - accumulatedTransmittance;
        vec3 normalizedScatter = accumulatedScatter / max(alpha, 0.0001);

        imageStore(scatteringVolume, ivec3(xyTexel, z),
            vec4(normalizedScatter, alpha));
    }
}
```

#### 4.3.6 Anti-Aliasing via L/α Storage

Ghost of Tsushima's key insight: storing `L / α` instead of `L` provides smoother results at density boundaries.

**Why it works:**

```glsl
// Volume rendering equation:
// L_out = L_in * T + L_scatter
// where T = transmittance, L_scatter = in-scattered light

// If L_scatter = σ_s × L_i (scattering × irradiance)
// and α = 1 - T = 1 - exp(-σ_e × d)

// Then L_scatter ≈ (1 - T) × (σ_s / σ_e) × L_i
//                = α × (albedo) × L_i

// So L / α = albedo × L_i, which is smooth even when α varies rapidly

vec3 SampleFroxelAntiAliased(vec3 worldPos) {
    vec3 uvw = WorldToFroxelUVW(worldPos);

    // Tricubic sampling for smoothness
    vec4 data = TricubicSample(scatteringVolume, uvw);

    vec3 normalizedScatter = data.rgb;

    // Recompute actual alpha at this exact position
    float density = GetHazeDensity(worldPos);
    float alpha = 1.0 - exp(-density * GetSliceThickness(uvw.z));

    // Recover actual scattering
    return normalizedScatter * alpha;
}
```

#### 4.3.7 Tricubic Filtering

Trilinear filtering causes visible banding. Ghost of Tsushima uses tricubic B-spline filtering:

```glsl
// Tricubic B-spline filtering (8 trilinear taps)
vec4 TricubicSample(sampler3D vol, vec3 uvw) {
    vec3 texSize = vec3(textureSize(vol, 0));
    vec3 coord = uvw * texSize - 0.5;
    vec3 f = fract(coord);
    coord = floor(coord);

    // B-spline weights
    vec3 w0 = (1.0 - f) * (1.0 - f) * (1.0 - f) / 6.0;
    vec3 w1 = (4.0 - 6.0 * f * f + 3.0 * f * f * f) / 6.0;
    vec3 w2 = (1.0 + 3.0 * f + 3.0 * f * f - 3.0 * f * f * f) / 6.0;
    vec3 w3 = f * f * f / 6.0;

    // Optimized: combine adjacent weights and use bilinear filtering
    vec3 s0 = w0 + w1;
    vec3 s1 = w2 + w3;
    vec3 f0 = w1 / s0;
    vec3 f1 = w3 / s1;

    vec3 t0 = (coord - 0.5 + f0) / texSize;
    vec3 t1 = (coord + 1.5 + f1) / texSize;

    // 8 trilinear samples (cheaper than 64 point samples)
    vec4 result = vec4(0.0);
    result += s0.x * s0.y * s0.z * texture(vol, vec3(t0.x, t0.y, t0.z));
    result += s1.x * s0.y * s0.z * texture(vol, vec3(t1.x, t0.y, t0.z));
    result += s0.x * s1.y * s0.z * texture(vol, vec3(t0.x, t1.y, t0.z));
    result += s1.x * s1.y * s0.z * texture(vol, vec3(t1.x, t1.y, t0.z));
    result += s0.x * s0.y * s1.z * texture(vol, vec3(t0.x, t0.y, t1.z));
    result += s1.x * s0.y * s1.z * texture(vol, vec3(t1.x, t0.y, t1.z));
    result += s0.x * s1.y * s1.z * texture(vol, vec3(t0.x, t1.y, t1.z));
    result += s1.x * s1.y * s1.z * texture(vol, vec3(t1.x, t1.y, t1.z));

    return result;
}
```

#### 4.3.8 Local Light Contribution

```glsl
// Additional pass for local lights (point, spot)
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

// Flat bit array for light culling (from Drobot's technique)
layout(std430, binding = 0) readonly buffer LightBits {
    uint lightBitArray[];
};

void main() {
    ivec3 texel = ivec3(gl_GlobalInvocationID);
    vec3 worldPos = FroxelToWorld(texel);
    float density = GetHazeDensity(worldPos);

    if (density < 0.0001) return;

    // Get tile index for light culling
    uint tileIndex = GetTileIndex(texel.xy);

    vec3 localLightScatter = vec3(0.0);

    // Iterate through light bit array
    uint wordIndex = tileIndex * LIGHTS_PER_TILE_WORDS;
    for (uint w = 0; w < LIGHTS_PER_TILE_WORDS; w++) {
        uint bits = lightBitArray[wordIndex + w];
        while (bits != 0u) {
            uint bitIndex = findLSB(bits);
            bits &= ~(1u << bitIndex);

            uint lightIndex = w * 32u + bitIndex;
            Light light = lights[lightIndex];

            // Evaluate light at froxel position
            vec3 toLight = light.position - worldPos;
            float dist = length(toLight);
            float attenuation = 1.0 / (dist * dist + 0.01);

            // Spot light cone
            if (light.type == LIGHT_SPOT) {
                float cosAngle = dot(-normalize(toLight), light.direction);
                attenuation *= smoothstep(light.outerCone, light.innerCone, cosAngle);
            }

            // Phase function (simplified for local lights)
            vec3 viewDir = normalize(worldPos - cameraPosition);
            float phase = HenyeyGreensteinPhase(dot(viewDir, normalize(toLight)), 0.5);

            localLightScatter += light.color * attenuation * phase * density;
        }
    }

    // Add to main scattering volume
    vec4 existing = imageLoad(scatteringVolume, texel);
    imageStore(scatteringVolume, texel,
        vec4(existing.rgb + localLightScatter, existing.a));
}
```

#### 4.3.9 Fog Particles with Haze Lighting

Ghost of Tsushima lights fog particles using the froxel volume:

```glsl
// In particle fragment shader
uniform sampler3D froxelLightVolume;

vec3 SampleHazeLighting(vec3 worldPos, float particleOpacity) {
    vec3 uvw = WorldToFroxelUVW(worldPos);

    // Sample pre-integrated light from froxel volume
    vec3 froxelLight = texture(froxelLightVolume, uvw).rgb;

    // For high opacity particles, use tricubic for quality
    if (particleOpacity > 0.3) {
        froxelLight = TricubicSample(froxelLightVolume, uvw).rgb;
    }

    return froxelLight * particleOpacity;
}

// Multi-scattering approximation for particle haze
vec3 ApplyParticleMultiScatter(vec3 singleScatter, float cosTheta, float opacity) {
    // Ratio of back-scatter to forward-scatter phase functions
    float rBack = 0.8 / HenyeyGreensteinPhase(cosTheta, 0.8);
    float rForward = HenyeyGreensteinPhase(cosTheta, -0.15) /
                     HenyeyGreensteinPhase(cosTheta, 0.8);

    // Interpolate based on view direction
    float rms = mix(rBack, rForward, cosTheta * 0.5 + 0.5);

    // Reduce effect for low opacity particles
    float multiScatterFactor = mix(1.0, rms, opacity * 0.5);

    return singleScatter * multiScatterFactor;
}
```

#### 4.3.10 Compositing with Scene

```glsl
// Final composite fragment shader
layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D sceneDepth;
layout(binding = 2) uniform sampler3D scatteringVolume;

out vec4 fragColor;

void main() {
    vec2 uv = gl_FragCoord.xy / screenSize;
    vec3 color = texture(sceneColor, uv).rgb;
    float depth = texture(sceneDepth, uv).r;

    float linearDepth = LinearizeDepth(depth);

    // Sample froxel volume at scene depth
    vec3 uvw = vec3(uv, DepthToSlice(linearDepth) / FROXEL_DEPTH);

    // Tricubic sample for quality
    vec4 fogData = TricubicSample(scatteringVolume, uvw);
    vec3 inScatter = fogData.rgb * fogData.a;  // Un-normalize
    float transmittance = 1.0 - fogData.a;

    // Apply fog
    color = color * transmittance + inScatter;

    fragColor = vec4(color, 1.0);
}
```

#### 4.3.11 Performance Summary

| Stage | Resolution | Time (PS4) |
|-------|------------|------------|
| Froxel update | 128×64×64 | 0.3ms |
| Integration | 128×64×64 | 0.1ms |
| Local lights | 128×64×64 | 0.1ms |
| Composite | Screen | 0.1ms |
| **Total** | | **~0.6ms** |

### 4.4 Light Shafts (God Rays)

**Goal:** Add volumetric light shafts through fog/haze.

The froxel-based volumetrics naturally produce god rays when:
1. Sun shadow maps have proper resolution
2. Froxels near the camera are well-sampled
3. Temporal filtering is stable

For additional god ray quality, add epipolar sampling near the sun position:

```glsl
// Screen-space god ray enhancement (optional)
vec3 EnhanceGodRays(vec2 uv, vec3 baseColor, vec2 sunScreenPos) {
    vec2 delta = (sunScreenPos - uv) / float(GOD_RAY_SAMPLES);

    float illumination = 0.0;
    vec2 sampleUV = uv;

    for (int i = 0; i < GOD_RAY_SAMPLES; i++) {
        float depth = texture(sceneDepth, sampleUV).r;
        if (depth > 0.999) {
            // Sky pixel - accumulate light
            illumination += 1.0;
        }
        sampleUV += delta;
    }

    illumination /= float(GOD_RAY_SAMPLES);
    vec3 godRayColor = sunColor * illumination * godRayIntensity;

    return baseColor + godRayColor * (1.0 - dot(uv - sunScreenPos, uv - sunScreenPos) * 2.0);
}
```

---

## Phase 5: Post-Processing Pipeline

### 5.1 HDR Rendering Setup

**Goal:** Switch to HDR render targets and implement a proper post-processing pipeline.

#### 5.1.1 HDR Render Target

```cpp
struct HDRRenderTarget {
    VkImage colorImage;
    VkImageView colorView;
    VkDeviceMemory colorMemory;

    VkImage depthImage;
    VkImageView depthView;
    VkDeviceMemory depthMemory;

    uint32_t width, height;
};

void CreateHDRRenderTarget(VkDevice device, HDRRenderTarget& target,
                           uint32_t width, uint32_t height) {
    // HDR color buffer
    VkImageCreateInfo colorInfo{};
    colorInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    colorInfo.imageType = VK_IMAGE_TYPE_2D;
    colorInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;  // Full HDR precision
    // Alternative: VK_FORMAT_B10G11R11_UFLOAT_PACK32 for bandwidth
    colorInfo.extent = {width, height, 1};
    colorInfo.mipLevels = 1;
    colorInfo.arrayLayers = 1;
    colorInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    colorInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    colorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_STORAGE_BIT;  // For compute passes

    vkCreateImage(device, &colorInfo, nullptr, &target.colorImage);

    // Depth buffer (shared with main pass)
    VkImageCreateInfo depthInfo = colorInfo;
    depthInfo.format = VK_FORMAT_D32_SFLOAT;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT;

    vkCreateImage(device, &depthInfo, nullptr, &target.depthImage);
}
```

#### 5.1.2 Post-Process Pipeline Structure

```cpp
struct PostProcessPipeline {
    // Intermediate buffers
    VkImage luminanceBuffer;      // R32F - for histogram
    VkImage bilateralGrid;        // RGBA16F - 64x32x64 3D texture
    VkImage bloomBuffer[2];       // R11G11B10F - ping-pong for blur
    VkImage exposureBuffer;       // R32F - single pixel, previous exposure

    // Compute pipelines
    VkPipeline histogramPipeline;
    VkPipeline histogramReducePipeline;
    VkPipeline bilateralBuildPipeline;
    VkPipeline bilateralBlurPipeline;
    VkPipeline bloomDownsamplePipeline;
    VkPipeline bloomUpsamplePipeline;

    // Final composite pipeline
    VkPipeline tonemapPipeline;
};
```

#### 5.1.3 Render Pass Order

```
1. Main Scene Pass (HDR)
   - Forward rendering to HDR target
   - Output: HDR color + depth

2. Volumetric Composite (Compute)
   - Apply fog/haze to HDR buffer
   - Output: HDR color with volumetrics

3. Exposure Calculation (Async Compute)
   - Build luminance histogram
   - Calculate target exposure
   - Output: Exposure value

4. Bilateral Grid Build (Async Compute)
   - Populate bilateral grid from HDR
   - Blur bilateral grid
   - Output: Blurred luminance grid

5. Bloom (Compute)
   - Threshold bright pixels
   - Progressive downsample + blur
   - Additive upsample
   - Output: Bloom texture

6. Final Composite (Fragment)
   - Sample bilateral grid for local tonemapping
   - Apply exposure
   - Apply tone mapping curve
   - Apply color grading
   - Add bloom
   - Apply Purkinje shift (night)
   - Output: LDR to swapchain
```

### 5.2 Exposure Control

**Goal:** Implement robust auto-exposure that handles high dynamic range scenes.

#### 5.2.1 The Problem with Luminance-Based Exposure

Ghost of Tsushima discovered that luminance-based exposure creates a feedback loop with artists:
- Dark albedos → low scene luminance → exposure increases → looks too bright
- Artists darken albedos further → cycle continues
- Results in unrealistic material properties

**Solution:** Use illuminance (light arriving at surfaces) instead of luminance (light leaving surfaces).

#### 5.2.2 Histogram-Based Exposure

```cpp
struct ExposureSettings {
    float minEV = -4.0f;           // Minimum exposure (bright scenes)
    float maxEV = 16.0f;           // Maximum exposure (dark scenes)
    float adaptationSpeed = 1.5f;  // EV per second
    float lowPercentile = 0.4f;    // Ignore darkest 40%
    float highPercentile = 0.95f;  // Ignore brightest 5%
    float highlightThreshold = 10.0f;  // Luminance threshold for highlight mode
};
```

#### 5.2.3 Histogram Build Compute Shader

```glsl
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D hdrInput;
layout(binding = 1, r32ui) uniform uimage1D histogram;

layout(push_constant) uniform PushConstants {
    float minLogLum;
    float maxLogLum;
    float invLogLumRange;
    uint pixelCount;
} pc;

shared uint localHistogram[256];

void main() {
    // Initialize shared memory
    if (gl_LocalInvocationIndex < 256) {
        localHistogram[gl_LocalInvocationIndex] = 0;
    }
    barrier();

    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 texSize = textureSize(hdrInput, 0);

    if (texel.x < texSize.x && texel.y < texSize.y) {
        vec3 color = texelFetch(hdrInput, texel, 0).rgb;

        // Compute luminance
        float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));

        // Convert to log scale and bin
        if (luminance > 0.0001) {
            float logLum = clamp(log2(luminance), pc.minLogLum, pc.maxLogLum);
            float normalized = (logLum - pc.minLogLum) * pc.invLogLumRange;
            uint bin = uint(normalized * 255.0);
            atomicAdd(localHistogram[bin], 1);
        }
    }

    barrier();

    // Merge to global histogram
    if (gl_LocalInvocationIndex < 256) {
        atomicAdd(imageLoad(histogram, int(gl_LocalInvocationIndex)).r,
                  localHistogram[gl_LocalInvocationIndex]);
    }
}
```

#### 5.2.4 Histogram Reduction and Exposure Calculation

```glsl
#version 450

layout(local_size_x = 256) in;

layout(binding = 0, r32ui) uniform uimage1D histogram;
layout(binding = 1, r32f) uniform image1D exposureOutput;
layout(binding = 2) uniform sampler1D previousExposure;

layout(push_constant) uniform PushConstants {
    float minLogLum;
    float logLumRange;
    float lowPercentile;
    float highPercentile;
    float adaptationSpeed;
    float deltaTime;
    uint totalPixels;
} pc;

shared uint prefixSum[256];
shared float weightedSum;
shared uint validPixelCount;

void main() {
    uint tid = gl_LocalInvocationIndex;

    // Load histogram bin
    uint count = imageLoad(histogram, int(tid)).r;
    prefixSum[tid] = count;

    // Parallel prefix sum
    barrier();
    for (uint stride = 1; stride < 256; stride *= 2) {
        uint val = 0;
        if (tid >= stride) {
            val = prefixSum[tid - stride];
        }
        barrier();
        prefixSum[tid] += val;
        barrier();
    }

    // Find percentile bounds
    uint lowThreshold = uint(pc.lowPercentile * float(pc.totalPixels));
    uint highThreshold = uint(pc.highPercentile * float(pc.totalPixels));

    // Compute weighted average of valid bins
    if (tid == 0) {
        weightedSum = 0.0;
        validPixelCount = 0;
    }
    barrier();

    uint cumulative = prefixSum[tid];
    uint prevCumulative = (tid > 0) ? prefixSum[tid - 1] : 0;

    // Check if this bin contributes to the target percentile range
    if (cumulative > lowThreshold && prevCumulative < highThreshold) {
        // Bin center in log luminance
        float logLum = pc.minLogLum + (float(tid) + 0.5) / 255.0 * pc.logLumRange;

        // Weight by pixel count in valid range
        uint validInBin = min(cumulative, highThreshold) -
                         max(prevCumulative, lowThreshold);

        atomicAdd(weightedSum, logLum * float(validInBin));
        atomicAdd(validPixelCount, validInBin);
    }

    barrier();

    // Calculate and store exposure
    if (tid == 0) {
        float avgLogLum = weightedSum / max(float(validPixelCount), 1.0);
        float targetExposure = -avgLogLum;  // EV = -log2(L)

        // Temporal adaptation
        float prevExposure = texelFetch(previousExposure, 0, 0).r;
        float adaptedExposure = prevExposure + (targetExposure - prevExposure) *
                               (1.0 - exp(-pc.adaptationSpeed * pc.deltaTime));

        imageStore(exposureOutput, 0, vec4(adaptedExposure));

        // Clear histogram for next frame
    }
}
```

#### 5.2.5 Illuminance-Based Exposure

For more physically accurate exposure, compute average illuminance instead:

```glsl
// In the histogram build shader, instead of luminance:
float illuminance = EstimateIlluminance(texel, color, normal, depth);

float EstimateIlluminance(ivec2 texel, vec3 color, vec3 normal, float depth) {
    // Get surface albedo (requires G-buffer or estimate)
    vec3 albedo = EstimateAlbedo(color);

    // Illuminance = Luminance / Albedo (approximately)
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float albedoLum = dot(albedo, vec3(0.2126, 0.7152, 0.0722));

    return luminance / max(albedoLum, 0.04);  // Clamp to avoid division by dark albedo
}
```

### 5.3 Local Tone Mapping

**Goal:** Maintain visibility in shadows without blowing out highlights, while preserving local contrast.

**Reference:** Ghost of Tsushima's bilateral grid approach (Bart Wronski's similar approach)

#### 5.3.1 Why Local Tone Mapping?

Traditional global tone mapping applies the same curve everywhere:
- Interior scenes with windows: either interior is black or exterior is white
- Can differ by 10+ EV between indoor and outdoor in Ghost of Tsushima

**Solution:** Reduce contrast based on local average luminance, then add back detail.

#### 5.3.2 Bilateral Grid Algorithm

The bilateral filter is expensive to compute directly. The **bilateral grid** algorithm makes it efficient:

1. Build a 3D grid where Z = luminance
2. Store weighted values at grid cells
3. Blur the grid (separable Gaussian)
4. Sample at (x, y, luminance) to get filtered result

```cpp
struct BilateralGrid {
    // Resolution: 64 x 32 x 64 (x, y, luminance)
    static constexpr uint32_t WIDTH = 64;
    static constexpr uint32_t HEIGHT = 32;
    static constexpr uint32_t DEPTH = 64;  // Luminance bins

    VkImage gridTexture;       // RGBA16F: RGB = weighted color sum, A = weight
    VkImage blurredGrid;       // Same format, after blur

    float minLogLum = -8.0f;
    float maxLogLum = 8.0f;
};
```

#### 5.3.3 Grid Population Compute Shader

```glsl
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D hdrInput;
layout(binding = 1, rgba16f) uniform image3D bilateralGrid;

layout(push_constant) uniform PushConstants {
    float minLogLum;
    float invLogLumRange;
    vec2 inputSize;
    vec2 gridSizeXY;
} pc;

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (texel.x >= int(pc.inputSize.x) || texel.y >= int(pc.inputSize.y)) return;

    vec3 color = texelFetch(hdrInput, texel, 0).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float logLum = log2(max(luminance, 0.0001));

    // Grid coordinates
    vec2 gridXY = vec2(texel) / pc.inputSize * pc.gridSizeXY;
    float gridZ = (logLum - pc.minLogLum) * pc.invLogLumRange * float(GRID_DEPTH);

    // Trilinear splatting to 8 neighboring cells
    ivec3 baseCoord = ivec3(floor(gridXY), floor(gridZ));
    vec3 frac = vec3(fract(gridXY), fract(gridZ));

    for (int z = 0; z <= 1; z++) {
        for (int y = 0; y <= 1; y++) {
            for (int x = 0; x <= 1; x++) {
                ivec3 coord = baseCoord + ivec3(x, y, z);
                coord = clamp(coord, ivec3(0), ivec3(GRID_WIDTH-1, GRID_HEIGHT-1, GRID_DEPTH-1));

                float weight = (x == 0 ? 1.0 - frac.x : frac.x) *
                              (y == 0 ? 1.0 - frac.y : frac.y) *
                              (z == 0 ? 1.0 - frac.z : frac.z);

                // Atomic add (need image atomics or multiple passes)
                vec4 existing = imageLoad(bilateralGrid, coord);
                imageStore(bilateralGrid, coord,
                    existing + vec4(logLum * weight, weight, 0.0, 0.0));
            }
        }
    }
}
```

#### 5.3.4 Grid Blur (Separable)

```glsl
// Blur in X direction
layout(local_size_x = 64) in;

layout(binding = 0, rgba16f) uniform image3D gridInput;
layout(binding = 1, rgba16f) uniform image3D gridOutput;

// Wide Gaussian kernel (radius 5)
const float kernel[11] = float[](
    0.035, 0.058, 0.086, 0.113, 0.132,
    0.152,  // center
    0.132, 0.113, 0.086, 0.058, 0.035
);

shared vec4 cache[64 + 10];  // Grid width + kernel padding

void main() {
    ivec3 coord = ivec3(gl_GlobalInvocationID);

    // Load into shared memory with padding
    int cacheIndex = int(gl_LocalInvocationID.x) + 5;
    cache[cacheIndex] = imageLoad(gridInput, coord);

    // Load padding
    if (gl_LocalInvocationID.x < 5) {
        cache[cacheIndex - 5] = imageLoad(gridInput, coord - ivec3(5, 0, 0));
        cache[cacheIndex + 64] = imageLoad(gridInput, coord + ivec3(64, 0, 0));
    }

    barrier();

    // Apply kernel
    vec4 result = vec4(0.0);
    for (int i = -5; i <= 5; i++) {
        result += cache[cacheIndex + i] * kernel[i + 5];
    }

    imageStore(gridOutput, coord, result);
}
```

#### 5.3.5 Hybrid Bilateral + Gaussian Approach

Ghost of Tsushima found pure bilateral filtering caused ringing on smooth gradients (clouds, soft shadows). Solution: blend with a wide 2D Gaussian.

```glsl
vec3 SampleLocalToneMapping(vec2 uv, float inputLogLum) {
    // Sample bilateral grid
    vec3 gridUVW = vec3(uv, (inputLogLum - minLogLum) * invLogLumRange);
    vec4 bilateral = texture(bilateralGrid, gridUVW);
    float bilateralLum = bilateral.x / max(bilateral.y, 0.0001);

    // Sample wide Gaussian blur (2D, no luminance dimension)
    // This is a separate 256x128 texture with very wide blur
    float gaussianLum = texture(gaussianBlur, uv).r;

    // Blend: 40% bilateral, 60% Gaussian
    float blendedLum = mix(gaussianLum, bilateralLum, 0.4);

    return blendedLum;
}
```

#### 5.3.6 Local Tone Mapping Application

```glsl
vec3 ApplyLocalToneMapping(vec3 hdrColor, vec2 uv) {
    float inputLum = dot(hdrColor, vec3(0.2126, 0.7152, 0.0722));
    float inputLogLum = log2(max(inputLum, 0.0001));

    // Sample filtered local luminance
    float filteredLogLum = SampleLocalToneMapping(uv, inputLogLum);

    // Local tone mapping parameters (artist controlled per weather/time)
    float midpointLogLum = 0.0;   // M - neutral gray point
    float contrastScale = 0.5;    // C - 0.5 = 50% contrast reduction
    float detailStrength = 1.0;   // D - preserve original detail

    // Apply local tone mapping formula
    // I_o = M + C * (B - M) + D * (I_i - B)
    float outputLogLum = midpointLogLum +
                        contrastScale * (filteredLogLum - midpointLogLum) +
                        detailStrength * (inputLogLum - filteredLogLum);

    // Convert back from log space
    float outputLum = exp2(outputLogLum);

    // Scale color by luminance ratio
    float scale = outputLum / max(inputLum, 0.0001);
    return hdrColor * scale;
}
```

#### 5.3.7 Ringing Artifact Prevention

```glsl
// Detect and reduce ringing in smooth gradients
float DetectRinging(float inputLogLum, float filteredLogLum, vec2 uv) {
    // Sample neighbors to detect gradient
    float lumDx = dFdx(inputLogLum);
    float lumDy = dFdy(inputLogLum);
    float gradient = sqrt(lumDx * lumDx + lumDy * lumDy);

    // In smooth gradients, deviation from bilateral is likely ringing
    float deviation = abs(inputLogLum - filteredLogLum);

    // If smooth area with high deviation, likely ringing
    float ringingLikelihood = (1.0 - smoothstep(0.0, 0.5, gradient)) *
                              smoothstep(0.1, 0.5, deviation);

    return ringingLikelihood;
}

vec3 ApplyLocalToneMappingWithAntiRinging(vec3 hdrColor, vec2 uv) {
    float inputLogLum = log2(max(dot(hdrColor, vec3(0.2126, 0.7152, 0.0722)), 0.0001));

    float bilateralLum = SampleBilateralGrid(uv, inputLogLum);
    float gaussianLum = SampleGaussianBlur(uv);

    // Detect ringing
    float ringing = DetectRinging(inputLogLum, bilateralLum, uv);

    // Use more Gaussian where ringing is detected
    float blendFactor = mix(0.4, 0.1, ringing);  // Less bilateral when ringing
    float filteredLogLum = mix(gaussianLum, bilateralLum, blendFactor);

    // Continue with standard local tone mapping...
}
```

### 5.4 Color Grading

**Goal:** Artistic color control with physically-based white balance.

#### 5.4.1 White Balance with von Kries / Bradford Transform

Ghost of Tsushima provides an intuitive interface: pick the color you want white to become.

```glsl
// Color space matrices
const mat3 RGB_TO_XYZ = mat3(
    0.4124564, 0.3575761, 0.1804375,
    0.2126729, 0.7151522, 0.0721750,
    0.0193339, 0.1191920, 0.9503041
);

const mat3 XYZ_TO_RGB = mat3(
     3.2404542, -1.5371385, -0.4985314,
    -0.9692660,  1.8760108,  0.0415560,
     0.0556434, -0.2040259,  1.0572252
);

// Bradford chromatic adaptation matrix
const mat3 BRADFORD = mat3(
     0.8951000,  0.2664000, -0.1614000,
    -0.7502000,  1.7135000,  0.0367000,
     0.0389000, -0.0685000,  1.0296000
);

const mat3 BRADFORD_INV = mat3(
     0.9869929, -0.1470543,  0.1599627,
     0.4323053,  0.5183603,  0.0492912,
    -0.0085287,  0.0400428,  0.9684867
);

vec3 ApplyWhiteBalance(vec3 color, vec3 targetWhite) {
    // Source white point (D65)
    vec3 srcWhiteXYZ = vec3(0.95047, 1.0, 1.08883);

    // Target white point (user selected color)
    vec3 dstWhiteXYZ = RGB_TO_XYZ * targetWhite;

    // Convert to Bradford LMS
    vec3 srcLMS = BRADFORD * srcWhiteXYZ;
    vec3 dstLMS = BRADFORD * dstWhiteXYZ;

    // Compute diagonal scaling matrix
    vec3 scale = dstLMS / srcLMS;
    mat3 scaleMatrix = mat3(
        scale.x, 0.0, 0.0,
        0.0, scale.y, 0.0,
        0.0, 0.0, scale.z
    );

    // Combined transform: RGB -> XYZ -> LMS -> scale -> LMS -> XYZ -> RGB
    mat3 transform = XYZ_TO_RGB * BRADFORD_INV * scaleMatrix * BRADFORD * RGB_TO_XYZ;

    return transform * color;
}
```

#### 5.4.2 Precomputed White Balance Matrix

For performance, precompute on CPU and pass as uniform:

```cpp
glm::mat3 ComputeWhiteBalanceMatrix(glm::vec3 targetWhite) {
    // ... same computation as GLSL
    // Returns combined transform matrix
}

// In uniform buffer
struct ColorGradingUBO {
    glm::mat3 whiteBalanceMatrix;
    float saturation;
    float contrast;
    glm::vec3 shadows;   // Color adjustment in shadows
    glm::vec3 midtones;  // Color adjustment in midtones
    glm::vec3 highlights; // Color adjustment in highlights
};
```

#### 5.4.3 Lift-Gamma-Gain Color Correction

```glsl
vec3 ApplyLiftGammaGain(vec3 color, vec3 lift, vec3 gamma, vec3 gain) {
    // Lift: adds color to shadows
    // Gamma: adjusts midtones
    // Gain: multiplies highlights

    vec3 result = color;

    // Gain (multiply)
    result *= gain;

    // Lift (add, scaled by inverse luminance)
    float lum = dot(result, vec3(0.2126, 0.7152, 0.0722));
    result += lift * (1.0 - lum);

    // Gamma (power)
    result = pow(max(result, vec3(0.0)), 1.0 / gamma);

    return result;
}
```

#### 5.4.4 3D LUT Application

```glsl
layout(binding = 3) uniform sampler3D colorLUT;

vec3 ApplyColorLUT(vec3 color) {
    // LUT is typically 32x32x32 or 64x64x64
    const float LUT_SIZE = 32.0;

    // Scale and offset for proper sampling
    vec3 scale = (LUT_SIZE - 1.0) / LUT_SIZE;
    vec3 offset = 0.5 / LUT_SIZE;

    return texture(colorLUT, color * scale + offset).rgb;
}
```

### 5.5 Tone Mapping Operator

**Goal:** Map HDR to displayable range with pleasing highlight rolloff.

#### 5.5.1 The Problem with Per-Channel Tone Mapping

Standard per-channel tone mapping clips saturated colors to the display gamut edges (cyan, magenta, yellow), causing ugly color shifts.

#### 5.5.2 Ghost of Tsushima's Custom Color Space

They modified ACES CG primaries to reduce the yellow shift in reds/oranges:

```glsl
// Standard ACES CG primaries
// Red:   (0.713, 0.293)
// Green: (0.165, 0.830)
// Blue:  (0.128, 0.044)

// Modified primaries (Ghost of Tsushima)
// Red:   (0.750, 0.270)  <- X increased from 0.713 to 0.75
// Green: (0.165, 0.830)  <- unchanged
// Blue:  (0.128, 0.044)  <- unchanged

// Transformation matrices
const mat3 CUSTOM_TO_XYZ = mat3(
    0.6870, 0.1478, 0.1149,
    0.2580, 0.6879, 0.0541,
    0.0000, 0.0247, 1.0257
);

const mat3 XYZ_TO_CUSTOM = mat3(
     1.5953, -0.3348, -0.1779,
    -0.5967,  1.6054,  0.0027,
     0.0114, -0.0387,  0.9751
);

// Convert Rec709 to custom space
const mat3 REC709_TO_CUSTOM = XYZ_TO_CUSTOM * RGB_TO_XYZ;
const mat3 CUSTOM_TO_REC709 = XYZ_TO_RGB * CUSTOM_TO_XYZ;
```

#### 5.5.3 Complete Tone Mapping Pipeline

```glsl
vec3 ToneMap(vec3 hdrColor) {
    // 1. Apply exposure
    hdrColor *= exp2(exposure);

    // 2. Convert to custom tone mapping space
    vec3 customColor = REC709_TO_CUSTOM * hdrColor;

    // 3. Apply tone curve per channel
    customColor = ApplyToneCurve(customColor);

    // 4. Convert back to Rec709
    vec3 ldrColor = CUSTOM_TO_REC709 * customColor;

    // 5. Gamut mapping (soft clamp negatives)
    ldrColor = max(ldrColor, vec3(0.0));

    return ldrColor;
}
```

#### 5.5.4 Tone Curve Options

**ACES Filmic:**

```glsl
vec3 ACESFilmic(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
```

**Gran Turismo (Hajime Uchimura):**

```glsl
vec3 GTTonemap(vec3 x) {
    const float P = 1.0;   // Max brightness
    const float a = 1.0;   // Contrast
    const float m = 0.22;  // Linear section start
    const float l = 0.4;   // Linear section length
    const float c = 1.33;  // Black tightness
    const float b = 0.0;   // Pedestal

    vec3 s = vec3(m + l);
    vec3 w = vec3((1.0 - s) * (1.0 - s));

    return mix(
        (P - (P - s) * exp(-c * (x - m) / s)) * (x / (x + 0.001)),
        l * x / m + b,
        step(x, vec3(m))
    );
}
```

**AgX (modern alternative):**

```glsl
// AgX - designed for better hue preservation
vec3 AgXDefaultContrastApprox(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return +15.5 * x4 * x2
           -40.14 * x4 * x
           +31.96 * x4
           -6.868 * x2 * x
           +0.4298 * x2
           +0.1191 * x
           -0.00232;
}

vec3 AgX(vec3 color) {
    const mat3 AgXInsetMatrix = mat3(
        0.842, 0.0423, 0.0424,
        0.0784, 0.878, 0.0784,
        0.0792, 0.0792, 0.879
    );
    const mat3 AgXOutsetMatrix = mat3(
        1.196, -0.0528, -0.0529,
        -0.0981, 1.151, -0.0981,
        -0.099, -0.099, 1.152
    );

    color = AgXInsetMatrix * color;
    color = max(color, vec3(0.0));
    color = log2(color + 1e-5);
    color = (color - vec3(-10.0)) / (vec3(6.5) - vec3(-10.0));
    color = clamp(color, 0.0, 1.0);
    color = AgXDefaultContrastApprox(color);
    color = AgXOutsetMatrix * color;
    return color;
}
```

### 5.6 Night Vision Enhancement (Purkinje Effect)

**Goal:** Simulate human mesopic/scotopic vision for convincing night scenes.

#### 5.6.1 Background: Rod and Cone Vision

Human vision uses two receptor types:
- **Cones (L, M, S):** Color vision, work in bright light (photopic)
- **Rods:** Monochromatic, more sensitive, work in low light (scotopic)

At night, rods take over, causing:
- Loss of color perception
- Blue shift (rods are more sensitive to blue)
- Loss of central vision detail

#### 5.6.2 Mathematical Model

Based on Kirk (2006) and Kowalik papers:

```cpp
// Constants from the papers
struct PurkinjeConstants {
    // Rod input strength for L, M, S pathways
    glm::vec3 k = glm::vec3(0.2f, 0.2f, 0.3f);

    // Cone sensitivity at reference luminance
    glm::vec3 m = glm::vec3(0.63f, 0.34f, 0.03f);

    // Opponent color space conversion matrix
    glm::mat3 A = glm::mat3(
        0.6150,  0.3525, -0.0375,
       -0.6129,  0.9832,  0.0099,
        0.0016,  0.0031,  0.5693
    );
};
```

#### 5.6.3 RGB to LMSR Conversion

First, compute spectral response of cones and rods:

```glsl
// Derived using Smits' spectral reconstruction method
// These coefficients convert Rec709 RGB to cone (LMS) and rod (R) excitation
const mat4 RGB_TO_LMSR = mat4(
    0.4122, 0.2119, 0.0883, 0.0,   // L
    0.5363, 0.6807, 0.2817, 0.0,   // M
    0.0514, 0.1074, 0.6300, 0.0,   // S
    0.0913, 0.3198, 0.4900, 1.0    // Rod (approximate)
);

// Inverse for reconstruction
const mat3 LMS_TO_RGB = mat3(
     2.8588, -1.6294, -0.0253,
    -1.0215,  1.9779, -0.0988,
     0.0522, -0.1970,  1.5801
);
```

#### 5.6.4 Complete Purkinje Implementation

```glsl
uniform float sceneIlluminance;  // Average scene illuminance in lux

// Constants
const vec3 k = vec3(0.2, 0.2, 0.3);   // Rod input strength
const vec3 m_const = vec3(0.63, 0.34, 0.03); // Cone sensitivity

// Opponent color space matrix
const mat3 A = mat3(
    0.6150,  0.3525, -0.0375,
   -0.6129,  0.9832,  0.0099,
    0.0016,  0.0031,  0.5693
);

const mat3 A_inv = inverse(A);

vec3 ApplyPurkinjeShift(vec3 color) {
    // Skip if scene is bright enough
    if (sceneIlluminance > 10.0) {
        return color;
    }

    // Convert to LMSR
    vec4 lmsr = RGB_TO_LMSR * vec4(color, 1.0);
    vec3 lms = lmsr.xyz;
    float rod = lmsr.w;

    // Multiplicative gain control
    // Models how rods influence cone pathways
    vec3 g = (m_const + k * rod) / (m_const + lms + k * rod);

    // Convert to opponent color space
    vec3 opponent = A * (lms * g);

    // Compute rod contribution to opponent channels
    // Based on measurements of rod intrusion into color pathways
    float rodLuminance = rod;
    float rodBlueYellow = 0.0;  // Rods don't contribute to blue-yellow
    float rodRedGreen = -0.3 * rod;  // Slight green shift from rods

    // Delta opponent from rod contribution
    vec3 deltaOpponent = vec3(0.0);

    // Luminance channel: rods add brightness to dark areas
    float lumBoost = smoothstep(10.0, 0.01, sceneIlluminance);
    deltaOpponent.x = rodLuminance * 0.5 * lumBoost;

    // Red-green channel: rods shift towards green/blue
    deltaOpponent.y = rodRedGreen * lumBoost;

    // Blue-yellow channel: slight blue shift
    deltaOpponent.z = rod * 0.2 * lumBoost;

    // Add rod contribution
    opponent += deltaOpponent;

    // Convert back to LMS then RGB
    vec3 outputLMS = A_inv * opponent;
    outputLMS = max(outputLMS, vec3(0.0));
    vec3 outputRGB = LMS_TO_RGB * outputLMS;

    // Blend based on illuminance (smooth transition)
    float blend = smoothstep(10.0, 0.01, sceneIlluminance);
    return mix(color, outputRGB, blend);
}
```

#### 5.6.5 Simplified Purkinje (Performance)

For lower-end hardware, a simplified version:

```glsl
vec3 SimplePurkinje(vec3 color, float illuminance) {
    // Desaturate in low light
    float desat = smoothstep(10.0, 0.01, illuminance) * 0.7;
    float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));
    vec3 desaturated = mix(color, vec3(lum), desat);

    // Blue shift in low light
    float blueShift = smoothstep(5.0, 0.01, illuminance) * 0.3;
    desaturated.b = mix(desaturated.b, lum * 1.2, blueShift);
    desaturated.r = mix(desaturated.r, lum * 0.9, blueShift);

    // Brighten dark areas (rod sensitivity)
    float boost = smoothstep(1.0, 0.001, illuminance) * 0.5;
    desaturated += vec3(lum * boost);

    return desaturated;
}
```

#### 5.6.6 Integration with Tone Mapping

Apply Purkinje after tone mapping but before gamma:

```glsl
vec3 FinalPostProcess(vec3 hdrColor, vec2 uv) {
    // 1. Local tone mapping
    vec3 localMapped = ApplyLocalToneMapping(hdrColor, uv);

    // 2. Global tone mapping
    vec3 toneMapped = ToneMap(localMapped);

    // 3. Color grading
    vec3 graded = ApplyWhiteBalance(toneMapped, whiteBalanceTarget);
    graded = ApplyLiftGammaGain(graded, lift, gamma, gain);

    // 4. Purkinje effect (before gamma)
    vec3 purkinje = ApplyPurkinjeShift(graded);

    // 5. Gamma correction (sRGB)
    vec3 gammaCorrected = pow(purkinje, vec3(1.0 / 2.2));

    return gammaCorrected;
}
```

### 5.7 Additional Post-Processing Effects

#### 5.7.1 Bloom

```glsl
// Threshold and downsample
vec3 BloomThreshold(vec3 color, float threshold, float knee) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float soft = luminance - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.00001);
    float contribution = max(soft, luminance - threshold);
    contribution /= max(luminance, 0.00001);
    return color * contribution;
}

// Progressive downsample with 13-tap tent filter
vec3 BloomDownsample(sampler2D src, vec2 uv, vec2 texelSize) {
    vec3 a = texture(src, uv + texelSize * vec2(-1, -1)).rgb;
    vec3 b = texture(src, uv + texelSize * vec2( 0, -1)).rgb;
    vec3 c = texture(src, uv + texelSize * vec2( 1, -1)).rgb;
    vec3 d = texture(src, uv + texelSize * vec2(-1,  0)).rgb;
    vec3 e = texture(src, uv).rgb;
    vec3 f = texture(src, uv + texelSize * vec2( 1,  0)).rgb;
    vec3 g = texture(src, uv + texelSize * vec2(-1,  1)).rgb;
    vec3 h = texture(src, uv + texelSize * vec2( 0,  1)).rgb;
    vec3 i = texture(src, uv + texelSize * vec2( 1,  1)).rgb;

    return e * 0.25 + (b + d + f + h) * 0.125 + (a + c + g + i) * 0.0625;
}

// Upsample with tent filter and accumulate
vec3 BloomUpsample(sampler2D src, sampler2D bloom, vec2 uv, vec2 texelSize, float radius) {
    vec3 current = texture(src, uv).rgb;

    vec3 a = texture(bloom, uv + texelSize * vec2(-radius, -radius)).rgb;
    vec3 b = texture(bloom, uv + texelSize * vec2( 0, -radius)).rgb;
    vec3 c = texture(bloom, uv + texelSize * vec2( radius, -radius)).rgb;
    vec3 d = texture(bloom, uv + texelSize * vec2(-radius,  0)).rgb;
    vec3 e = texture(bloom, uv).rgb;
    vec3 f = texture(bloom, uv + texelSize * vec2( radius,  0)).rgb;
    vec3 g = texture(bloom, uv + texelSize * vec2(-radius,  radius)).rgb;
    vec3 h = texture(bloom, uv + texelSize * vec2( 0,  radius)).rgb;
    vec3 i = texture(bloom, uv + texelSize * vec2( radius,  radius)).rgb;

    vec3 upsampled = (a + c + g + i) * 0.0625 +
                     (b + d + f + h) * 0.125 +
                     e * 0.25;

    return current + upsampled;
}
```

#### 5.7.2 Vignette

```glsl
vec3 ApplyVignette(vec3 color, vec2 uv, float intensity, float smoothness) {
    vec2 centered = uv - 0.5;
    float dist = length(centered);
    float vignette = smoothstep(0.5, 0.5 - smoothness, dist * (1.0 + intensity));
    return color * vignette;
}
```

### 5.8 Post-Processing Performance Summary

| Pass | Resolution | Format | Time (PS4 Pro) |
|------|------------|--------|----------------|
| Histogram build | 1920×1080 → 256 bins | R32UI | 0.15ms |
| Histogram reduce | 256 → 1 | R32F | 0.02ms |
| Bilateral grid build | Full → 64×32×64 | RGBA16F | 0.10ms |
| Bilateral grid blur | 64×32×64 × 3 | RGBA16F | 0.08ms |
| Wide Gaussian | 256×128 | R16F | 0.05ms |
| Bloom (6 mips) | Various | R11G11B10F | 0.30ms |
| Final composite | 1920×1080 | RGBA8 | 0.15ms |
| **Total** | | | **~0.85ms** |

---

## Phase 6: Integration & Polish

### 6.1 Time of Day System

**Tasks:**
1. Create sun/moon position calculator (astronomical or simplified)
2. Update sky LUTs based on sun angle
3. Transition lighting parameters smoothly
4. Update probe lighting in real-time

### 6.2 Weather System

**Tasks:**
1. Create weather state data (cloud density, haze, wind)
2. Blend between weather states
3. Adjust atmospheric parameters per-weather
4. Add rain/particle effects (future work)

### 6.3 Performance Optimization

**Techniques:**
1. Temporal reprojection for volumetrics
2. Async compute for histogram, bilateral grid
3. Checkerboard rendering for volumetrics
4. LOD for reflection probes
5. Streaming for probe data

---

## Implementation Order (Recommended)

| Priority | Phase | Feature | Complexity | Impact |
|----------|-------|---------|------------|--------|
| 1 | 1.1 | Light uniforms | Low | Foundation |
| 2 | 1.2 | PBR lighting | Medium | Visual quality |
| 3 | 2.1 | Basic shadow map | Medium | Grounding |
| 4 | 5.1 | HDR pipeline | Medium | Enables post-FX |
| 5 | 5.5 | Tone mapping | Low | Visual quality |
| 6 | 2.2 | Cascaded shadows | High | Shadow quality |
| 7 | 4.1 | Sky model | High | Atmosphere |
| 8 | 4.3 | Volumetric haze | High | Atmosphere |
| 9 | 3.1 | SH probes | High | Indirect light |
| 10 | 4.2 | Volumetric clouds | Very High | Dramatic skies |
| 11 | 5.3 | Local tone mapping | Medium | HDR handling |
| 12 | 3.2 | Reflection probes | High | Specular IBL |
| 13 | 5.6 | Purkinje effect | Medium | Night scenes |

---

## References

### Papers & Presentations
- Bruneton & Neyret, "Precomputed Atmospheric Scattering" (2008)
- Hillaire, "A Scalable and Production Ready Sky and Atmosphere" (2020)
- Schneider, "The Real-time Volumetric Cloudscapes of Horizon Zero Dawn" (2015)
- Wronski, "Volumetric Fog and Lighting" (2014)
- Petri, "Samurai Cinema: Creating a Real-Time Cinematic Experience" (GDC 2021)

### Code Resources
- learnopengl.com - PBR, shadows, HDR tutorials
- github.com/sebh/UnrealEngineSkyAtmosphere - Atmosphere implementation
- Filament renderer source code - PBR reference

---

## Appendix: Vulkan-Specific Considerations

### Render Pass Structure

```
Pass 1: Shadow Depth Pass
  - Render scene depth from light view
  - Output: Shadow map texture array

Pass 2: Main Scene Pass
  - Forward render with lighting
  - Sample shadow maps
  - Output: HDR color + depth

Pass 3: Volumetric Compute
  - Build froxel grid
  - Accumulate scattering
  - Output: Volumetric texture

Pass 4: Post-Process Pass
  - Composite volumetrics
  - Tone mapping
  - Color grading
  - Output: Final LDR to swapchain
```

### Descriptor Set Layout

```cpp
Set 0: Per-frame data
  - Binding 0: Camera UBO
  - Binding 1: Light UBO
  - Binding 2: Time/exposure UBO

Set 1: Shadow data
  - Binding 0: Shadow cascade matrices
  - Binding 1: Shadow map sampler (array)

Set 2: Material data
  - Binding 0: Albedo texture
  - Binding 1: Normal map
  - Binding 2: Roughness/metallic

Set 3: Environment data
  - Binding 0: Sky LUT
  - Binding 1: Irradiance SH buffer
  - Binding 2: Reflection cubemap
```
