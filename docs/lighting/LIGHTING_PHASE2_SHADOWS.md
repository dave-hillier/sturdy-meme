# Phase 2: Shadow Mapping

[← Previous: Phase 1 - PBR Lighting](LIGHTING_PHASE1_PBR.md) | [Back to Overview](LIGHTING_OVERVIEW.md) | [Next: Phase 3 - Indirect Lighting →](LIGHTING_PHASE3_INDIRECT.md)

---

## 2.1 Basic Directional Shadow Map

**Goal:** Implement shadow mapping for the directional sun light.

### 2.1.1 Shadow Map Resources

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

### 2.1.2 Light Space Matrix Calculation

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

### 2.1.3 Shadow Pass Shaders

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

### 2.1.4 Shadow Sampling in Main Pass

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

---

## 2.2 Cascaded Shadow Maps (CSM)

**Goal:** High-quality shadows at all distances using multiple shadow map cascades.

### 2.2.1 Cascade Split Calculation

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

### 2.2.2 Per-Cascade Light Matrix

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

### 2.2.3 Texture Array for Cascades

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

### 2.2.4 Cascade Selection and Sampling

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

### 2.2.5 Cascade Blending

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

### 2.2.6 Debug Visualization

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

---

## 2.3 Shadow Quality Improvements

### 2.3.1 Percentage-Closer Soft Shadows (PCSS)

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

### 2.3.2 Shadow Map Stabilization

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

### 2.3.3 Far Shadow Maps (Ghost of Tsushima approach)

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

### 2.3.4 Screen-Space Contact Shadows

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

[← Previous: Phase 1 - PBR Lighting](LIGHTING_PHASE1_PBR.md) | [Back to Overview](LIGHTING_OVERVIEW.md) | [Next: Phase 3 - Indirect Lighting →](LIGHTING_PHASE3_INDIRECT.md)
