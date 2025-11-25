# Phase 5: Post-Processing Pipeline

[← Previous: Phase 4 - Atmospheric Scattering](LIGHTING_PHASE4_ATMOSPHERE.md) | [Back to Overview](LIGHTING_OVERVIEW.md) | [Next: Phase 6 - Integration →](LIGHTING_PHASE6_INTEGRATION.md)

---

## 5.1 HDR Rendering Setup

**Goal:** Switch to HDR render targets and implement a proper post-processing pipeline.

### 5.1.1 HDR Render Target

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

### 5.1.2 Post-Process Pipeline Structure

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

### 5.1.3 Render Pass Order

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

---

## 5.2 Exposure Control

**Goal:** Implement robust auto-exposure that handles high dynamic range scenes.

### 5.2.1 The Problem with Luminance-Based Exposure

Ghost of Tsushima discovered that luminance-based exposure creates a feedback loop with artists:
- Dark albedos → low scene luminance → exposure increases → looks too bright
- Artists darken albedos further → cycle continues
- Results in unrealistic material properties

**Solution:** Use illuminance (light arriving at surfaces) instead of luminance (light leaving surfaces).

### 5.2.2 Histogram-Based Exposure

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

### 5.2.3 Histogram Build Compute Shader

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

### 5.2.4 Histogram Reduction and Exposure Calculation

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
    }
}
```

### 5.2.5 Illuminance-Based Exposure

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

---

## 5.3 Local Tone Mapping

**Goal:** Maintain visibility in shadows without blowing out highlights, while preserving local contrast.

**Reference:** Ghost of Tsushima's bilateral grid approach (Bart Wronski's similar approach)

### 5.3.1 Why Local Tone Mapping?

Traditional global tone mapping applies the same curve everywhere:
- Interior scenes with windows: either interior is black or exterior is white
- Can differ by 10+ EV between indoor and outdoor in Ghost of Tsushima

**Solution:** Reduce contrast based on local average luminance, then add back detail.

### 5.3.2 Bilateral Grid Algorithm

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

### 5.3.3 Grid Population Compute Shader

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

### 5.3.4 Grid Blur (Separable)

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

### 5.3.5 Hybrid Bilateral + Gaussian Approach

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

### 5.3.6 Local Tone Mapping Application

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

---

## 5.4 Color Grading

**Goal:** Artistic color control with physically-based white balance.

### 5.4.1 White Balance with von Kries / Bradford Transform

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

### 5.4.2 Lift-Gamma-Gain Color Correction

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

### 5.4.3 3D LUT Application

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

---

## 5.5 Tone Mapping Operator

**Goal:** Map HDR to displayable range with pleasing highlight rolloff.

### 5.5.1 The Problem with Per-Channel Tone Mapping

Standard per-channel tone mapping clips saturated colors to the display gamut edges (cyan, magenta, yellow), causing ugly color shifts.

### 5.5.2 Ghost of Tsushima's Custom Color Space

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

### 5.5.3 Complete Tone Mapping Pipeline

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

### 5.5.4 Tone Curve Options

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

---

## 5.6 Night Vision Enhancement (Purkinje Effect)

**Goal:** Simulate human mesopic/scotopic vision for convincing night scenes.

### 5.6.1 Background: Rod and Cone Vision

Human vision uses two receptor types:
- **Cones (L, M, S):** Color vision, work in bright light (photopic)
- **Rods:** Monochromatic, more sensitive, work in low light (scotopic)

At night, rods take over, causing:
- Loss of color perception
- Blue shift (rods are more sensitive to blue)
- Loss of central vision detail

### 5.6.2 Complete Purkinje Implementation

```glsl
uniform float sceneIlluminance;  // Average scene illuminance in lux

// Constants
const vec3 k = vec3(0.2, 0.2, 0.3);   // Rod input strength
const vec3 m_const = vec3(0.63, 0.34, 0.03); // Cone sensitivity

// RGB to LMSR conversion
const mat4 RGB_TO_LMSR = mat4(
    0.4122, 0.2119, 0.0883, 0.0,   // L
    0.5363, 0.6807, 0.2817, 0.0,   // M
    0.0514, 0.1074, 0.6300, 0.0,   // S
    0.0913, 0.3198, 0.4900, 1.0    // Rod (approximate)
);

// LMS to RGB
const mat3 LMS_TO_RGB = mat3(
     2.8588, -1.6294, -0.0253,
    -1.0215,  1.9779, -0.0988,
     0.0522, -0.1970,  1.5801
);

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
    vec3 g = (m_const + k * rod) / (m_const + lms + k * rod);

    // Convert to opponent color space
    vec3 opponent = A * (lms * g);

    // Delta opponent from rod contribution
    vec3 deltaOpponent = vec3(0.0);

    // Luminance channel: rods add brightness to dark areas
    float lumBoost = smoothstep(10.0, 0.01, sceneIlluminance);
    deltaOpponent.x = rod * 0.5 * lumBoost;

    // Red-green channel: rods shift towards green/blue
    deltaOpponent.y = -0.3 * rod * lumBoost;

    // Blue-yellow channel: slight blue shift
    deltaOpponent.z = rod * 0.2 * lumBoost;

    // Add rod contribution
    opponent += deltaOpponent;

    // Convert back to LMS then RGB
    vec3 outputLMS = A_inv * opponent;
    outputLMS = max(outputLMS, vec3(0.0));
    vec3 outputRGB = LMS_TO_RGB * outputLMS;

    // Blend based on illuminance
    float blend = smoothstep(10.0, 0.01, sceneIlluminance);
    return mix(color, outputRGB, blend);
}
```

### 5.6.3 Simplified Purkinje (Performance)

For lower-end hardware:

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

### 5.6.4 Integration with Tone Mapping

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

---

## 5.7 Additional Post-Processing Effects

### 5.7.1 Bloom

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
```

### 5.7.2 Vignette

```glsl
vec3 ApplyVignette(vec3 color, vec2 uv, float intensity, float smoothness) {
    vec2 centered = uv - 0.5;
    float dist = length(centered);
    float vignette = smoothstep(0.5, 0.5 - smoothness, dist * (1.0 + intensity));
    return color * vignette;
}
```

---

## 5.8 Post-Processing Performance Summary

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

[← Previous: Phase 4 - Atmospheric Scattering](LIGHTING_PHASE4_ATMOSPHERE.md) | [Back to Overview](LIGHTING_OVERVIEW.md) | [Next: Phase 6 - Integration →](LIGHTING_PHASE6_INTEGRATION.md)
