# Phase 4: Atmospheric Scattering

[← Previous: Phase 3 - Indirect Lighting](LIGHTING_PHASE3_INDIRECT.md) | [Back to Overview](LIGHTING_OVERVIEW.md) | [Next: Phase 5 - Post-Processing →](LIGHTING_PHASE5_POSTPROCESS.md)

---

## 4.1 Physically-Based Sky Model

**Goal:** Implement multi-scattering atmospheric model with accurate Rayleigh scattering.

**Reference:** Bruneton & Neyret (2008), Hillaire (2020), Ghost of Tsushima LMS color space

### 4.1.1 Atmospheric Parameters

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

### 4.1.2 LUT Precomputation Overview

| LUT | Dimensions | Contents | When Computed |
|-----|------------|----------|---------------|
| Transmittance | 256×64 | Optical depth integral | Once at startup |
| Multi-scatter | 32×32 | Multiple scattering contribution | Once at startup |
| Sky-View | 192×108 | In-scattered radiance | Every frame |
| Aerial Perspective | 32×32×32 | Volumetric in-scattering | Every frame |

### 4.1.3 Transmittance LUT

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

### 4.1.4 Multiple Scattering LUT

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

### 4.1.5 Sky-View LUT (Runtime)

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

### 4.1.6 Phase Functions

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

### 4.1.7 LMS Color Space for Accurate Rayleigh

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

### 4.1.8 Earth Shadow

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

### 4.1.9 Irradiance LUTs for Lighting

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

### 4.1.10 Rendering the Sky

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

---

## 4.2 Volumetric Clouds

**Goal:** Render procedural volumetric clouds with realistic lighting.

**Reference:** Andrew Schneider (Horizon Zero Dawn), Sebastian Hillaire, Ghost of Tsushima

### 4.2.1 Cloud Map Architecture

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

### 4.2.2 Density Anti-Aliasing

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

### 4.2.3 Cloud Density Modeling

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

### 4.2.4 Phase Function with Depth-Dependent Scattering

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

### 4.2.5 Ray Marching Implementation

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

### 4.2.6 Light Sampling Optimization

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

### 4.2.7 Temporal Reprojection

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

### 4.2.8 Compositing with Scene

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

### 4.2.9 Cloud Rendering Pipeline Summary

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

---

## 4.3 Volumetric Haze/Fog

**Goal:** Add atmospheric haze using froxel-based volumetrics with god rays.

**Reference:** Bart Wronski (Assassin's Creed), Drobot (Call of Duty), Ghost of Tsushima

### 4.3.1 Froxel Grid Architecture

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

### 4.3.2 Vulkan Resources

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

### 4.3.3 Density Functions

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

### 4.3.4 Froxel Update Compute Shader

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

### 4.3.5 Integration with Quad Swizzling

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

### 4.3.6 Anti-Aliasing via L/α Storage

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

### 4.3.7 Tricubic Filtering

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

### 4.3.8 Local Light Contribution

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

### 4.3.9 Fog Particles with Haze Lighting

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

### 4.3.10 Compositing with Scene

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

### 4.3.11 Performance Summary

| Stage | Resolution | Time (PS4) |
|-------|------------|------------|
| Froxel update | 128×64×64 | 0.3ms |
| Integration | 128×64×64 | 0.1ms |
| Local lights | 128×64×64 | 0.1ms |
| Composite | Screen | 0.1ms |
| **Total** | | **~0.6ms** |

---

## 4.4 Light Shafts (God Rays)

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

[← Previous: Phase 3 - Indirect Lighting](LIGHTING_PHASE3_INDIRECT.md) | [Back to Overview](LIGHTING_OVERVIEW.md) | [Next: Phase 5 - Post-Processing →](LIGHTING_PHASE5_POSTPROCESS.md)
