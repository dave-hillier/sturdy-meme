#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"

layout(binding = BINDING_PP_HDR_INPUT) uniform sampler2D hdrInput;

layout(binding = BINDING_PP_UNIFORMS) uniform PostProcessUniforms {
    float exposure;
    float bloomThreshold;
    float bloomIntensity;
    float autoExposure;  // 0 = manual, 1 = auto
    float previousExposure;
    float deltaTime;
    float adaptationSpeed;
    float bloomRadius;
    // God rays parameters (Phase 4.4)
    vec2 sunScreenPos;     // Sun position in screen space [0,1]
    float godRayIntensity; // God ray strength
    float godRayDecay;     // Falloff per sample
    // Froxel volumetrics (Phase 4.3)
    float froxelEnabled;   // 1.0 = enabled
    float froxelFarPlane;  // Volumetric far plane
    float froxelDepthDist; // Depth distribution factor
    float nearPlane;       // Camera near plane
    float farPlane;        // Camera far plane
    // Purkinje effect (Phase 5.6)
    float sceneIlluminance; // Scene illuminance in lux
    float hdrEnabled;       // 1.0 = HDR tonemapping, 0.0 = bypass
    // Quality settings
    float godRaysEnabled;       // 1.0 = enabled, 0.0 = disabled
    float froxelFilterQuality;  // 0.0 = trilinear (fast), 1.0 = tricubic (quality)
    float bloomEnabled;         // 1.0 = enabled, 0.0 = disabled
    float autoExposureEnabled;  // 1.0 = auto, 0.0 = manual (UI display only)
    // Local tone mapping (bilateral grid)
    float localToneMapEnabled;  // 1.0 = enabled
    float localToneMapContrast; // Contrast reduction (0-1, 0=none, 0.5=typical)
    float localToneMapDetail;   // Detail boost (1.0=neutral, 1.5=punch)
    float minLogLuminance;      // Min log2 luminance for grid
    float maxLogLuminance;      // Max log2 luminance for grid
    float bilateralBlend;       // Blend factor for bilateral vs gaussian (0.4=GOT)
    // Water Volume Renderer - Underwater Effects (Phase 2)
    float underwaterEnabled;    // 1.0 = underwater, 0.0 = above water
    float underwaterDepth;      // Camera depth below water surface (meters)
    vec4 underwaterAbsorption;  // xyz = absorption coefficients, w = turbidity
    vec4 underwaterColor;       // Water tint color
    float underwaterWaterLevel; // Water surface Y position
    // Froxel debug visualization mode (Phase 4.3 testing)
    float froxelDebugMode;      // 0=Normal, 1=Depth slices, 2=Density, 3=Transmittance, 4=Grid cells
    float underwaterPad2, underwaterPad3;  // Alignment padding
} ubo;

// Specialization constant for god ray sample count
// Low=16, Medium=32, High=64
layout(constant_id = 0) const int GOD_RAY_SAMPLES = 64;

layout(binding = BINDING_PP_DEPTH) uniform sampler2D depthInput;
layout(binding = BINDING_PP_FROXEL) uniform sampler3D froxelVolume;
layout(binding = BINDING_PP_BLOOM) uniform sampler2D bloomTexture;
layout(binding = BINDING_PP_BILATERAL_GRID) uniform sampler3D bilateralGrid;
layout(binding = BINDING_PP_GODRAYS) uniform sampler2D godRaysTexture; // Quarter-res god rays

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// Froxel grid constants (must match FroxelSystem.h)
const uint FROXEL_WIDTH = 128;
const uint FROXEL_HEIGHT = 64;
const uint FROXEL_DEPTH = 64;

// Linearize depth from NDC (Vulkan: 0-1 range)
float linearizeDepth(float depth) {
    return ubo.nearPlane * ubo.farPlane / (ubo.farPlane - depth * (ubo.farPlane - ubo.nearPlane));
}

// ============================================================================
// Tricubic B-Spline Filtering (Phase 4.3.7)
// Provides smoother fog gradients than trilinear filtering
// ============================================================================

// Cubic B-spline weight function
// Returns (w0, w1, w2, w3) weights and optimized texture offsets
vec4 bsplineWeights(float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    float invT = 1.0 - t;
    float invT2 = invT * invT;
    float invT3 = invT2 * invT;

    // Cubic B-spline basis functions
    float w0 = invT3 / 6.0;
    float w1 = (4.0 - 6.0 * t2 + 3.0 * t3) / 6.0;
    float w2 = (1.0 + 3.0 * t + 3.0 * t2 - 3.0 * t3) / 6.0;
    float w3 = t3 / 6.0;

    return vec4(w0, w1, w2, w3);
}

// Optimized tricubic using 8 bilinear samples instead of 64 point samples
// Based on GPU Gems 2, Chapter 20: Fast Third-Order Texture Filtering
vec4 sampleFroxelTricubic(vec3 uvw) {
    vec3 texSize = vec3(float(FROXEL_WIDTH), float(FROXEL_HEIGHT), float(FROXEL_DEPTH));
    vec3 invTexSize = 1.0 / texSize;

    // Convert to texel coordinates
    vec3 texCoord = uvw * texSize - 0.5;
    vec3 texCoordFloor = floor(texCoord);
    vec3 frac = texCoord - texCoordFloor;

    // Calculate B-spline weights for each axis
    vec4 xWeights = bsplineWeights(frac.x);
    vec4 yWeights = bsplineWeights(frac.y);
    vec4 zWeights = bsplineWeights(frac.z);

    // Combine adjacent weights for bilinear optimization
    // g0 = w0 + w1, g1 = w2 + w3
    vec2 gX = vec2(xWeights.x + xWeights.y, xWeights.z + xWeights.w);
    vec2 gY = vec2(yWeights.x + yWeights.y, yWeights.z + yWeights.w);
    vec2 gZ = vec2(zWeights.x + zWeights.y, zWeights.z + zWeights.w);

    // Calculate bilinear sample offsets
    // h0 = w1 / g0 - 1, h1 = w3 / g1 + 1
    vec2 hX = vec2(xWeights.y / gX.x - 1.0, xWeights.w / gX.y + 1.0);
    vec2 hY = vec2(yWeights.y / gY.x - 1.0, yWeights.w / gY.y + 1.0);
    vec2 hZ = vec2(zWeights.y / gZ.x - 1.0, zWeights.w / gZ.y + 1.0);

    // Base texel position
    vec3 baseUV = (texCoordFloor + 0.5) * invTexSize;

    // Sample 8 bilinear taps (2x2x2 grid)
    vec4 c000 = texture(froxelVolume, baseUV + vec3(hX.x, hY.x, hZ.x) * invTexSize);
    vec4 c100 = texture(froxelVolume, baseUV + vec3(hX.y, hY.x, hZ.x) * invTexSize);
    vec4 c010 = texture(froxelVolume, baseUV + vec3(hX.x, hY.y, hZ.x) * invTexSize);
    vec4 c110 = texture(froxelVolume, baseUV + vec3(hX.y, hY.y, hZ.x) * invTexSize);
    vec4 c001 = texture(froxelVolume, baseUV + vec3(hX.x, hY.x, hZ.y) * invTexSize);
    vec4 c101 = texture(froxelVolume, baseUV + vec3(hX.y, hY.x, hZ.y) * invTexSize);
    vec4 c011 = texture(froxelVolume, baseUV + vec3(hX.x, hY.y, hZ.y) * invTexSize);
    vec4 c111 = texture(froxelVolume, baseUV + vec3(hX.y, hY.y, hZ.y) * invTexSize);

    // Blend in X
    vec4 c00 = mix(c000 * gX.x, c100 * gX.y, 0.5) * 2.0;  // Normalized blend
    vec4 c10 = mix(c010 * gX.x, c110 * gX.y, 0.5) * 2.0;
    vec4 c01 = mix(c001 * gX.x, c101 * gX.y, 0.5) * 2.0;
    vec4 c11 = mix(c011 * gX.x, c111 * gX.y, 0.5) * 2.0;

    // Proper weighted blend
    c00 = c000 * gX.x + c100 * gX.y;
    c10 = c010 * gX.x + c110 * gX.y;
    c01 = c001 * gX.x + c101 * gX.y;
    c11 = c011 * gX.x + c111 * gX.y;

    // Blend in Y
    vec4 c0 = c00 * gY.x + c10 * gY.y;
    vec4 c1 = c01 * gY.x + c11 * gY.y;

    // Blend in Z
    return c0 * gZ.x + c1 * gZ.y;
}

// Convert linear depth to froxel slice index
float depthToSlice(float linearDepth) {
    float normalized = linearDepth / ubo.froxelFarPlane;
    normalized = clamp(normalized, 0.0, 1.0);
    return log(1.0 + normalized * (pow(ubo.froxelDepthDist, float(FROXEL_DEPTH)) - 1.0)) /
           log(ubo.froxelDepthDist);
}

// Sample froxel volume for volumetric fog (Phase 4.3)
// Quality setting controls filtering: 0 = trilinear (fast), 1 = tricubic (quality)
vec4 sampleFroxelFog(vec2 uv, float linearDepth) {
    // Clamp to volumetric range
    float clampedDepth = min(linearDepth, ubo.froxelFarPlane);

    // Convert to froxel UVW coordinates
    float sliceIndex = depthToSlice(clampedDepth);
    float w = sliceIndex / float(FROXEL_DEPTH);

    // Sample with quality-dependent filtering
    vec4 fogData;
    if (ubo.froxelFilterQuality > 0.5) {
        // High quality: tricubic B-spline filtering (8 bilinear samples)
        fogData = sampleFroxelTricubic(vec3(uv, w));
    } else {
        // Low quality: simple trilinear filtering (1 sample)
        fogData = texture(froxelVolume, vec3(uv, w));
    }

    // fogData format: RGB = L/alpha (normalized scatter), A = alpha
    // Recover actual scattering: L = (L/alpha) * alpha
    vec3 inScatter = fogData.rgb * fogData.a;
    float transmittance = 1.0 - fogData.a;

    return vec4(inScatter, transmittance);
}

// ============================================================================
// Froxel Debug Visualization (Phase 4.3 Testing)
// Provides visual feedback for testing froxel grid behavior
// ============================================================================

// HSV to RGB conversion for rainbow coloring
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// Visualize froxel data for debugging
// Returns: RGB color for the debug visualization, A = 1 if debug mode active
vec4 visualizeFroxelDebug(vec2 uv, float linearDepth, int debugMode) {
    // Mode 0 = Normal (no debug), handled in caller
    if (debugMode <= 0) {
        return vec4(0.0);
    }

    // Clamp to volumetric range
    float clampedDepth = min(linearDepth, ubo.froxelFarPlane);
    float sliceIndex = depthToSlice(clampedDepth);
    float w = sliceIndex / float(FROXEL_DEPTH);

    // Sample the raw froxel data
    vec4 fogData = texture(froxelVolume, vec3(uv, w));

    if (debugMode == 1) {
        // Mode 1: Depth slices - rainbow gradient based on Z slice
        // Useful for verifying exponential depth distribution
        float hue = w;  // 0-1 maps to full rainbow
        vec3 sliceColor = hsv2rgb(vec3(hue, 0.9, 0.9));
        // Add grid lines every 8 slices
        float sliceLines = step(0.9, fract(sliceIndex / 8.0));
        sliceColor = mix(sliceColor, vec3(1.0), sliceLines * 0.5);
        return vec4(sliceColor, 1.0);
    }
    else if (debugMode == 2) {
        // Mode 2: Density visualization - grayscale density map
        // Useful for seeing where fog accumulates
        float density = fogData.a;  // Alpha stores density/opacity
        // Apply contrast curve to make low densities more visible
        float visualDensity = pow(density, 0.5);  // Gamma correction
        vec3 densityColor = vec3(visualDensity);
        // Tint very high density values red as warning
        if (density > 0.9) {
            densityColor = mix(densityColor, vec3(1.0, 0.0, 0.0), (density - 0.9) * 10.0);
        }
        return vec4(densityColor, 1.0);
    }
    else if (debugMode == 3) {
        // Mode 3: Transmittance visualization - shows light penetration
        // Dark areas = fog blocks light, bright = light passes through
        float transmittance = 1.0 - fogData.a;
        // Use blue-white gradient (like x-ray vision)
        vec3 transColor = mix(vec3(0.0, 0.1, 0.3), vec3(1.0), transmittance);
        return vec4(transColor, 1.0);
    }
    else if (debugMode == 4) {
        // Mode 4: Grid cells - show froxel boundaries
        // Useful for understanding the grid resolution
        vec3 gridCoord = vec3(uv, w) * vec3(float(FROXEL_WIDTH), float(FROXEL_HEIGHT), float(FROXEL_DEPTH));
        vec3 cellFrac = fract(gridCoord);

        // Draw cell edges
        float edgeThickness = 0.08;
        float edgeX = step(cellFrac.x, edgeThickness) + step(1.0 - edgeThickness, cellFrac.x);
        float edgeY = step(cellFrac.y, edgeThickness) + step(1.0 - edgeThickness, cellFrac.y);
        float edgeZ = step(cellFrac.z, edgeThickness) + step(1.0 - edgeThickness, cellFrac.z);
        float isEdge = max(max(edgeX, edgeY), edgeZ);

        // Base color from in-scattered light
        vec3 inScatter = fogData.rgb * fogData.a;
        vec3 baseColor = inScatter * 2.0;  // Boost for visibility

        // Overlay grid in cyan
        vec3 gridColor = mix(baseColor, vec3(0.0, 1.0, 1.0), isEdge * 0.7);
        return vec4(gridColor, 1.0);
    }
    else if (debugMode == 5) {
        // Mode 5: Volume Raymarch - shows true 3D volumetric structure
        // Marches through the entire froxel volume to accumulate and visualize 3D density
        vec3 accumColor = vec3(0.0);
        float accumAlpha = 0.0;

        // March through all depth slices from front to back
        const int NUM_SAMPLES = 32;
        for (int i = 0; i < NUM_SAMPLES; i++) {
            float sampleW = float(i) / float(NUM_SAMPLES - 1);
            vec4 sampleData = texture(froxelVolume, vec3(uv, sampleW));

            // Get slice density
            float density = sampleData.a;
            vec3 scatter = sampleData.rgb * density;

            // Color by depth (rainbow gradient for 3D effect)
            float hue = sampleW * 0.8;  // Rainbow from red to violet
            vec3 sliceColor = hsv2rgb(vec3(hue, 0.8, 0.9));

            // Modulate by density to show where fog actually is
            sliceColor *= density * 5.0;

            // Front-to-back compositing
            float sampleAlpha = density * 0.3;  // Scale for visibility
            accumColor += sliceColor * (1.0 - accumAlpha);
            accumAlpha += sampleAlpha * (1.0 - accumAlpha);

            if (accumAlpha > 0.95) break;  // Early out when saturated
        }

        return vec4(accumColor, 1.0);
    }
    else if (debugMode == 6) {
        // Mode 6: Cross-section - show horizontal slice through volume at pixel depth
        // Reveals the XY distribution of fog at the current depth
        float sliceNum = floor(sliceIndex);
        float sliceFrac = fract(sliceIndex);

        // Sample a grid of points at this depth slice
        vec3 sliceColor = vec3(0.0);

        // Get data at this exact slice
        vec4 thisSlice = fogData;

        // Show density as brightness
        float density = thisSlice.a;

        // Add a checkerboard pattern based on XY froxel cells to show resolution
        vec2 cellXY = vec2(uv) * vec2(float(FROXEL_WIDTH), float(FROXEL_HEIGHT));
        float checker = mod(floor(cellXY.x) + floor(cellXY.y), 2.0);

        // Color: warm = high density, cool = low density
        vec3 densityColor = mix(
            vec3(0.1, 0.2, 0.5),  // Cool blue for low
            vec3(1.0, 0.4, 0.1),  // Warm orange for high
            density * 2.0
        );

        // Subtle checkerboard overlay
        sliceColor = densityColor * (0.8 + 0.2 * checker);

        // Draw depth slice indicator lines
        float sliceLine = step(0.95, fract(sliceIndex));
        sliceColor = mix(sliceColor, vec3(1.0, 1.0, 0.0), sliceLine * 0.5);

        return vec4(sliceColor, 1.0);
    }

    return vec4(0.0);  // Unknown mode, no debug
}

// ACES Filmic Tone Mapping
vec3 ACESFilmic(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Compute luminance using standard weights
float getLuminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// ============================================================================
// Purkinje Effect - Night Vision Enhancement (Phase 5.6)
// Simulates human mesopic/scotopic vision for convincing night scenes
// At night, rods take over from cones causing:
// - Loss of color perception (desaturation)
// - Blue shift (rods more sensitive to blue)
// - Brightening of dark areas (rod sensitivity)
// ============================================================================
vec3 SimplePurkinje(vec3 color, float illuminance) {
    // Skip effect in bright conditions (above 10 lux)
    if (illuminance > 10.0) {
        return color;
    }

    // Desaturate in low light (rods are monochromatic)
    float desat = smoothstep(10.0, 0.01, illuminance) * 0.7;
    float lum = getLuminance(color);
    vec3 desaturated = mix(color, vec3(lum), desat);

    // Blue shift in low light (rods are more sensitive to shorter wavelengths)
    float blueShift = smoothstep(5.0, 0.01, illuminance) * 0.3;
    desaturated.b = mix(desaturated.b, lum * 1.2, blueShift);
    desaturated.r = mix(desaturated.r, lum * 0.9, blueShift);

    // Brighten dark areas (rod sensitivity boost)
    float boost = smoothstep(1.0, 0.001, illuminance) * 0.5;
    desaturated += vec3(lum * boost);

    return desaturated;
}

// NOTE: Auto-exposure is now computed via histogram compute shaders
// (histogram_build.comp and histogram_reduce.comp)
// The computed exposure is passed through ubo.exposure from the CPU

// ============================================================================
// Local Tone Mapping (Bilateral Grid) - Ghost of Tsushima technique
// Reduces global contrast while preserving local detail
// ============================================================================

// Bilateral grid dimensions (must match BilateralGridSystem)
const ivec3 BILATERAL_GRID_SIZE = ivec3(64, 32, 64);

// Sample bilateral grid with trilinear interpolation
float sampleBilateralGrid(vec2 uv, float logLum) {
    // Normalize log luminance to [0,1]
    float normalizedLum = (logLum - ubo.minLogLuminance) /
                          (ubo.maxLogLuminance - ubo.minLogLuminance);
    normalizedLum = clamp(normalizedLum, 0.0, 1.0);

    // Sample the blurred bilateral grid
    vec3 gridUVW = vec3(uv, normalizedLum);
    vec4 gridSample = texture(bilateralGrid, gridUVW);

    // Grid stores (weighted_log_lum, weight, 0, 0)
    // Normalize to get average log luminance in this region
    if (gridSample.g > 0.001) {
        return gridSample.r / gridSample.g;
    }
    return logLum;  // Fallback to input
}

// Apply local tone mapping using bilateral grid
// Based on: Petri, "Samurai Cinema", SIGGRAPH 2021
//
// Formula: output = midpoint + (blurred - midpoint) * contrast + (input - blurred) * detail
//
// - blurred: bilaterally filtered log luminance (smooth version)
// - midpoint: target middle gray (in log space)
// - contrast: 0 = flat, 1 = original contrast
// - detail: multiplier for local contrast (1 = neutral, >1 = punch)
vec3 applyLocalToneMapping(vec3 hdrColor, vec2 uv) {
    // Compute log luminance of input
    float lum = getLuminance(hdrColor);
    float logLum = log2(max(lum, 0.0001));

    // Sample bilaterally filtered log luminance
    float blurredLogLum = sampleBilateralGrid(uv, logLum);

    // Midpoint is typically log2(0.18) ≈ -2.47 for middle gray
    float midpoint = log2(0.18);

    // Apply contrast reduction: move blurred value toward midpoint
    float contrast = ubo.localToneMapContrast;
    float adjustedBlurred = mix(blurredLogLum, midpoint, 1.0 - contrast);

    // Compute detail (difference between input and blurred)
    float detail = logLum - blurredLogLum;

    // Final log luminance: adjusted base + boosted detail
    float outputLogLum = adjustedBlurred + detail * ubo.localToneMapDetail;

    // Convert back to linear luminance
    float outputLum = exp2(outputLogLum);

    // Apply luminance ratio to color (preserve hue/saturation)
    float lumRatio = outputLum / max(lum, 0.0001);
    return hdrColor * lumRatio;
}

// God rays / Light shafts (Phase 4.4)
// Screen-space radial blur from sun position
// GOD_RAY_SAMPLES is now a specialization constant defined at top of file

vec3 computeGodRays(vec2 uv, vec2 sunPos) {
    // Only process if sun is roughly on screen
    if (sunPos.x < -0.5 || sunPos.x > 1.5 || sunPos.y < -0.5 || sunPos.y > 1.5) {
        return vec3(0.0);
    }

    // Direction from pixel to sun
    vec2 delta = (sunPos - uv) / float(GOD_RAY_SAMPLES);

    // Initial sample weight
    float illumination = 0.0;
    float weight = 1.0;
    vec2 sampleUV = uv;

    // Sky depth threshold - pixels at or near far plane are sky
    const float SKY_DEPTH_THRESHOLD = 0.9999;

    // Accumulate samples along ray toward sun
    for (int i = 0; i < GOD_RAY_SAMPLES; i++) {
        sampleUV += delta;

        // Check bounds
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            break;
        }

        // Only accumulate from sky pixels (not geometry like point lights)
        float sampleDepth = texture(depthInput, sampleUV).r;
        if (sampleDepth < SKY_DEPTH_THRESHOLD) {
            // This is geometry, not sky - skip it
            weight *= ubo.godRayDecay;
            continue;
        }

        // Sample brightness at this point
        vec3 sampleColor = texture(hdrInput, sampleUV).rgb;
        float brightness = getLuminance(sampleColor);

        // Only accumulate from bright sky pixels (threshold at bloom level)
        if (brightness > ubo.bloomThreshold * 0.5) {
            illumination += brightness * weight;
        }

        // Exponential decay
        weight *= ubo.godRayDecay;
    }

    // Normalize and scale
    illumination /= float(GOD_RAY_SAMPLES);

    // Fade based on distance from sun
    float distFromSun = length(uv - sunPos);
    float radialFalloff = 1.0 - clamp(distFromSun * 1.5, 0.0, 1.0);
    radialFalloff *= radialFalloff;  // Squared falloff

    // Return warm-tinted god rays
    vec3 godRayColor = vec3(1.0, 0.95, 0.8);  // Slight warm tint
    return godRayColor * illumination * radialFalloff * ubo.godRayIntensity;
}

// ============================================================================
// Water Volume Renderer - Underwater Effects (Phase 2)
// Volumetric fog, light absorption, and Snell's window when camera is underwater
// ============================================================================

// Apply underwater fog using Beer-Lambert law
// Returns fog color to blend with scene based on view distance
vec3 applyUnderwaterFog(vec3 sceneColor, float linearDepth) {
    // Get absorption coefficients from water material
    vec3 absorption = ubo.underwaterAbsorption.xyz;
    float turbidity = ubo.underwaterAbsorption.w;

    // Scale absorption based on water type (turbidity increases absorption)
    vec3 totalAbsorption = absorption * (1.0 + turbidity * 0.5);

    // Beer-Lambert absorption: I = I0 * e^(-absorption * distance)
    vec3 transmittance = exp(-totalAbsorption * linearDepth);

    // Fog color is water color tinted by depth (deeper = darker, more blue)
    vec3 fogColor = ubo.underwaterColor.rgb;

    // Add depth-based color shift - blue dominates at depth
    float depthFactor = min(linearDepth / 50.0, 1.0);  // Normalize to 50m
    fogColor = mix(fogColor, vec3(0.05, 0.1, 0.15), depthFactor * 0.5);

    // Add slight scattering contribution (turbidity-based)
    // In turbid water, scattered light adds to fog color
    vec3 scatterColor = fogColor * turbidity * 0.3;

    // Final color: transmitted scene + scattered fog
    return sceneColor * transmittance + fogColor * (1.0 - transmittance) + scatterColor;
}

// Apply Snell's window effect when looking up from underwater
// Light can only enter water within a 97.2° cone (critical angle)
// Outside this cone, we see total internal reflection of underwater scene
vec3 applySnellsWindow(vec3 sceneColor, vec2 uv) {
    // Screen center represents looking straight up
    vec2 screenCenter = vec2(0.5, 0.5);
    float distFromCenter = length(uv - screenCenter) * 2.0;  // Normalize to [0,1] at edges

    // Critical angle for water-air interface: arcsin(1/1.33) ≈ 48.6°
    // This maps to roughly 0.65 in our normalized screen space when looking up
    const float CRITICAL_ANGLE = 0.65;

    // Inside the window, we see refracted above-water scene (sky, etc.)
    // Outside the window, we see total internal reflection
    if (distFromCenter > CRITICAL_ANGLE) {
        // Total internal reflection zone
        // In a full implementation, this would reflect the underwater scene
        // For now, we darken and tint the edge to simulate the effect
        float reflectionAmount = smoothstep(CRITICAL_ANGLE, 1.0, distFromCenter);

        // Darken and apply water color tint for reflected region
        vec3 reflectedColor = sceneColor * ubo.underwaterColor.rgb * 0.3;

        return mix(sceneColor, reflectedColor, reflectionAmount);
    }

    // Inside Snell's window - apply subtle edge distortion/chromatic aberration
    float edgeFactor = smoothstep(0.0, CRITICAL_ANGLE, distFromCenter);

    // Slight barrel distortion at window edge (Snell's law refraction)
    // This is a simplified version - real implementation would ray-trace
    float distortion = edgeFactor * 0.02;
    vec2 distortedUV = uv + (uv - screenCenter) * distortion;

    // Clamp to valid UV range
    distortedUV = clamp(distortedUV, 0.0, 1.0);

    // Sample with slight chromatic aberration at edge
    vec3 distortedColor;
    if (distortion > 0.001) {
        distortedColor.r = texture(hdrInput, distortedUV + vec2(0.002, 0.0) * edgeFactor).r;
        distortedColor.g = texture(hdrInput, distortedUV).g;
        distortedColor.b = texture(hdrInput, distortedUV - vec2(0.002, 0.0) * edgeFactor).b;
    } else {
        distortedColor = sceneColor;
    }

    // Darken edges of Snell's window (light loses intensity at steep angles)
    float edgeDarkening = 1.0 - edgeFactor * 0.3;
    return distortedColor * edgeDarkening;
}

// Main underwater effect composition
vec3 applyUnderwaterEffects(vec3 sceneColor, vec2 uv, float linearDepth) {
    // When froxel fog is enabled, it handles underwater scattering/absorption
    // We only apply Beer-Lambert absorption here as a supplement for distant objects
    // The Snell's window effect is disabled as froxel volumetrics provide better
    // underwater visuals without the harsh radial halo artifact

    // Apply subtle Beer-Lambert absorption for depth cues on distant geometry
    vec3 absorption = ubo.underwaterAbsorption.xyz;
    float turbidity = ubo.underwaterAbsorption.w;
    vec3 transmittance = exp(-absorption * (1.0 + turbidity * 0.3) * linearDepth * 0.1);

    // Blend toward water color at distance
    vec3 fogColor = ubo.underwaterColor.rgb * 0.3;
    return sceneColor * transmittance + fogColor * (1.0 - transmittance);
}

void main() {
    vec3 hdr = texture(hdrInput, fragTexCoord).rgb;

    // Get depth for volumetric effects
    float depth = texture(depthInput, fragTexCoord).r;
    float linearDepth = linearizeDepth(depth);

    // Apply froxel volumetric fog (Phase 4.3) - works both above and underwater
    // Underwater fog density is added in froxel_update.comp when below water level
    if (ubo.froxelEnabled > 0.5) {
        int debugMode = int(ubo.froxelDebugMode + 0.5);  // Round to nearest int

        if (debugMode > 0) {
            // Debug visualization mode - replace scene with debug view
            vec4 debugVis = visualizeFroxelDebug(fragTexCoord, linearDepth, debugMode);
            if (debugVis.a > 0.5) {
                hdr = debugVis.rgb;
            }
        } else {
            // Normal fog rendering
            vec4 fog = sampleFroxelFog(fragTexCoord, linearDepth);
            vec3 inScatter = fog.rgb;
            float transmittance = fog.a;

            // Apply fog: scene * transmittance + in-scatter
            hdr = hdr * transmittance + inScatter;
        }
    }

    // Apply underwater effects (Water Volume Renderer Phase 2)
    if (ubo.underwaterEnabled > 0.5) {
        hdr = applyUnderwaterEffects(hdr, fragTexCoord, linearDepth);
    }

    // Exposure is computed by histogram compute shaders and passed via uniform
    float finalExposure = ubo.exposure;

    // Sample bloom from multi-pass bloom texture (only if enabled)
    vec3 bloom = vec3(0.0);
    if (ubo.bloomEnabled > 0.5) {
        bloom = texture(bloomTexture, fragTexCoord).rgb;
    }

    // Sample god rays from quarter-resolution compute texture (Phase 4.4 optimization)
    // The god rays are pre-computed at 1/4 resolution and bilinearly upsampled here
    vec3 godRays = vec3(0.0);
    if (ubo.godRaysEnabled > 0.5 && ubo.godRayIntensity > 0.0) {
        // Sample from quarter-res texture (hardware bilinear upscale)
        godRays = texture(godRaysTexture, fragTexCoord).rgb;
    }

    // Combine HDR with bloom and god rays
    vec3 combined = hdr + bloom * ubo.bloomIntensity + godRays;

    vec3 finalColor;
    if (ubo.hdrEnabled > 0.5) {
        // Apply exposure
        vec3 exposed = combined * exp2(finalExposure);

        // Apply local tone mapping (bilateral grid) - Ghost of Tsushima technique
        // Reduces global contrast while preserving local detail
        // Applied before ACES to maintain HDR range for the tonemapper
        if (ubo.localToneMapEnabled > 0.5) {
            exposed = applyLocalToneMapping(exposed, fragTexCoord);
        }

        // Apply ACES tone mapping
        vec3 mapped = ACESFilmic(exposed);

        // Apply Purkinje effect for night vision (Phase 5.6)
        // Applied after tone mapping but before final output
        finalColor = SimplePurkinje(mapped, ubo.sceneIlluminance);
    } else {
        // HDR bypass - output raw HDR clamped to [0,1]
        finalColor = clamp(combined, 0.0, 1.0);
    }

    outColor = vec4(finalColor, 1.0);
}
