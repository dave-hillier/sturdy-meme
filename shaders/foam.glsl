/*
 * foam.glsl - Enhanced Flow-Aware Foam System
 *
 * Phase 2 of the Far Cry 5-inspired water rendering system.
 * Implements physically-based foam following flow and shore contours.
 *
 * Foam sources:
 *   1. Shore/obstacle proximity (SDF-based)
 *   2. Flow velocity (turbulent areas)
 *   3. Flow acceleration/deceleration (where flow changes)
 *   4. Wave peaks (existing)
 *
 * Reference: GDC 2018 "Water Rendering in Far Cry 5"
 */

// ============================================================================
// FOAM NOISE FUNCTIONS
// ============================================================================

// Voronoi-based cellular noise for foam bubbles
// Returns distance to nearest cell center (good for bubble patterns)
float voronoiNoise(vec2 uv, float scale) {
    vec2 p = uv * scale;
    vec2 i = floor(p);
    vec2 f = fract(p);

    float minDist = 1.0;

    // Check 3x3 neighborhood
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(float(x), float(y));
            // Random point in cell
            vec2 cellOffset = i + neighbor;
            vec2 point = fract(sin(vec2(
                dot(cellOffset, vec2(127.1, 311.7)),
                dot(cellOffset, vec2(269.5, 183.3))
            )) * 43758.5453);

            vec2 diff = neighbor + point - f;
            float dist = length(diff);
            minDist = min(minDist, dist);
        }
    }

    return minDist;
}

// Multi-octave foam noise with flow distortion
float foamFBM(vec2 uv, vec2 flowDir, float time, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    float totalAmplitude = 0.0;

    // Flow offset that increases with octave (higher freq follows flow more)
    vec2 flowOffset = flowDir * time * 0.1;

    for (int i = 0; i < octaves; i++) {
        vec2 sampleUV = uv * frequency + flowOffset * float(i + 1) * 0.3;

        // Mix of different noise types for more interesting foam
        float cellNoise = voronoiNoise(sampleUV, 1.0);
        float perlin = fract(sin(dot(floor(sampleUV * 8.0), vec2(127.1, 311.7))) * 43758.5453);

        value += mix(cellNoise, perlin, 0.3) * amplitude;
        totalAmplitude += amplitude;

        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value / totalAmplitude;
}

// ============================================================================
// SDF-BASED SHORE FOAM
// ============================================================================

// Enhanced shore foam structure
struct ShoreFoam {
    float intensity;      // Total shore foam amount
    float primaryBand;    // Main foam line
    float secondaryBand;  // Secondary receding foam
    float sprayBand;      // Fine spray at waterline
};

// Calculate shore foam from SDF (signed distance field)
// shoreDist: normalized distance to shore (0 = at shore, 1 = far from shore)
// flowSpeed: local flow velocity magnitude
// time: animation time
ShoreFoam calculateShoreFoam(float shoreDist, float flowSpeed, vec2 worldUV, float time) {
    ShoreFoam foam;

    // Convert normalized shore distance to approximate world units
    // Assuming shoreDistance config is ~50m and normalized 0-1
    float dist = shoreDist;

    // Primary foam band - right at the shoreline
    // Narrower band, high intensity
    float primaryWidth = 0.08;  // ~4m at 50m shore distance
    foam.primaryBand = 1.0 - smoothstep(0.0, primaryWidth, dist);

    // Add noise to primary band edge for organic look
    float edgeNoise = voronoiNoise(worldUV * 0.5 + time * 0.05, 8.0);
    foam.primaryBand *= smoothstep(0.2, 0.5, edgeNoise);

    // Secondary foam band - receding waves
    float secondaryStart = primaryWidth * 0.5;
    float secondaryEnd = 0.25;  // ~12.5m
    foam.secondaryBand = smoothstep(secondaryStart, secondaryStart + 0.02, dist)
                       * (1.0 - smoothstep(secondaryEnd * 0.7, secondaryEnd, dist));

    // Animate secondary band with waves
    float wavePhase = sin(dist * 30.0 - time * 2.0) * 0.5 + 0.5;
    foam.secondaryBand *= wavePhase * 0.6;

    // Add noise modulation
    float secondaryNoise = foamFBM(worldUV * 0.3, vec2(0.0), time, 3);
    foam.secondaryBand *= smoothstep(0.3, 0.6, secondaryNoise);

    // Fine spray at very edge
    foam.sprayBand = 1.0 - smoothstep(0.0, 0.02, dist);
    float sprayNoise = voronoiNoise(worldUV * 2.0 + time * 0.3, 16.0);
    foam.sprayBand *= smoothstep(0.1, 0.4, sprayNoise);

    // Boost all foam in faster-flowing areas (rapids hitting shore)
    float flowBoost = 1.0 + flowSpeed * 0.8;
    foam.primaryBand *= flowBoost;
    foam.secondaryBand *= flowBoost;

    // Combined intensity
    foam.intensity = max(foam.primaryBand, max(foam.secondaryBand, foam.sprayBand));
    foam.intensity = clamp(foam.intensity, 0.0, 1.0);

    return foam;
}

// ============================================================================
// FLOW TURBULENCE FOAM
// ============================================================================

// Foam generated by flow velocity and turbulence
struct FlowFoam {
    float intensity;       // Total flow foam amount
    float velocityFoam;    // From flow speed
    float turbulenceFoam;  // From flow direction changes
    float obstacleFoam;    // Near obstacles in flow path
};

// Calculate foam from flow characteristics
// flowDir: normalized flow direction
// flowSpeed: flow velocity magnitude (0-1)
// shoreDist: distance to nearest obstacle/shore
// worldUV: world-space UV for noise sampling
// time: animation time
FlowFoam calculateFlowFoam(vec2 flowDir, float flowSpeed, float shoreDist,
                           vec2 worldUV, float time) {
    FlowFoam foam;

    // Velocity-based foam - fast water generates foam
    float speedThreshold = 0.25;
    foam.velocityFoam = smoothstep(speedThreshold, 0.7, flowSpeed);

    // Add flowing noise pattern
    vec2 flowUV = worldUV - flowDir * time * 0.2;
    float velocityNoise = foamFBM(flowUV, flowDir, time, 4);
    foam.velocityFoam *= smoothstep(0.25, 0.6, velocityNoise);

    // Turbulence foam - sample flow gradient to detect changes
    // This creates foam where flow direction/speed changes (eddies, rapids)
    // We approximate this with noise that follows the flow
    float turbulenceNoise = foamFBM(worldUV * 1.5, flowDir, time * 1.5, 3);
    foam.turbulenceFoam = flowSpeed * smoothstep(0.4, 0.7, turbulenceNoise) * 0.5;

    // Add some randomized streaks in flow direction
    vec2 streakUV = worldUV * 3.0 - flowDir * time * 0.5;
    float streakNoise = fract(sin(dot(floor(streakUV), vec2(127.1, 311.7))) * 43758.5453);
    foam.turbulenceFoam += flowSpeed * step(0.85, streakNoise) * 0.3;

    // Obstacle foam - where fast water meets obstacles
    // High flow + close to obstacle = lots of foam
    float obstacleProximity = 1.0 - smoothstep(0.0, 0.15, shoreDist);
    foam.obstacleFoam = obstacleProximity * flowSpeed * 1.5;

    // Add splashing effect near obstacles
    float splashNoise = voronoiNoise(worldUV * 2.0 + flowDir * time * 0.5, 12.0);
    foam.obstacleFoam *= (0.6 + 0.4 * smoothstep(0.2, 0.5, splashNoise));

    // Combined intensity with priority to obstacle foam
    foam.intensity = max(foam.obstacleFoam, max(foam.velocityFoam, foam.turbulenceFoam));
    foam.intensity = clamp(foam.intensity, 0.0, 1.0);

    return foam;
}

// ============================================================================
// WAVE PEAK FOAM
// ============================================================================

// Enhanced wave foam with better noise patterns
float calculateWaveFoam(float waveHeight, float foamThreshold, vec2 worldUV,
                        vec2 flowDir, float time) {
    // Base foam from wave height
    float heightFoam = smoothstep(foamThreshold * 0.6, foamThreshold, waveHeight);

    // Add breaking wave pattern
    float breakingNoise = foamFBM(worldUV * 0.8 + flowDir * time * 0.1, flowDir, time, 3);
    heightFoam *= smoothstep(0.2, 0.6, breakingNoise);

    // Add foam "caps" using voronoi
    float capNoise = voronoiNoise(worldUV * 1.5 - flowDir * time * 0.2, 6.0);
    float caps = smoothstep(0.3, 0.1, capNoise) * heightFoam;

    return max(heightFoam * 0.7, caps);
}

// ============================================================================
// FOAM MATERIAL PROPERTIES
// ============================================================================

// Calculate foam color with depth-based variation
vec3 calculateFoamColor(float intensity, float waterDepth, vec3 baseWaterColor) {
    // Base foam is bright white/blue
    vec3 brightFoam = vec3(0.95, 0.97, 1.0);

    // Thicker foam is slightly more opaque/less blue
    vec3 thickFoam = vec3(0.9, 0.92, 0.94);

    // Blend based on intensity (more foam = thicker appearance)
    vec3 foamColor = mix(brightFoam, thickFoam, intensity * 0.5);

    // In shallow water, foam picks up a tiny bit of water color
    float shallowBlend = smoothstep(5.0, 0.0, waterDepth) * 0.15;
    foamColor = mix(foamColor, baseWaterColor * 1.2, shallowBlend);

    return foamColor;
}

// Calculate foam opacity (how much foam obscures water beneath)
float calculateFoamOpacity(float intensity) {
    // Foam builds up opacity non-linearly
    // Light foam is translucent, heavy foam is opaque
    return smoothstep(0.0, 0.3, intensity) * 0.85 + intensity * 0.15;
}

// ============================================================================
// COMBINED FOAM SYSTEM
// ============================================================================

// Full foam calculation result
struct FoamResult {
    float totalIntensity;  // Combined foam amount (0-1)
    vec3 color;            // Foam color
    float opacity;         // How much foam obscures water

    // Individual components for debugging/tweaking
    float shoreFoam;
    float flowFoam;
    float waveFoam;
};

// Calculate all foam for a water fragment
// flowSample: from flow_common.glsl (contains flowDir, speed, shoreDist)
// waveHeight: current wave displacement
// foamThreshold: wave height threshold for foam
// waterDepth: depth of water at this point
// worldPos: world-space position
// baseWaterColor: water color for tinting
// time: animation time
FoamResult calculateAllFoam(
    vec2 flowDir,
    float flowSpeed,
    float shoreDist,
    float waveHeight,
    float foamThreshold,
    float waterDepth,
    vec3 worldPos,
    vec3 baseWaterColor,
    float time
) {
    FoamResult result;

    vec2 worldUV = worldPos.xz * 0.05;  // Scale for noise sampling (higher = smaller foam cells)

    // Shore/obstacle foam
    ShoreFoam shore = calculateShoreFoam(shoreDist, flowSpeed, worldUV, time);
    result.shoreFoam = shore.intensity;

    // Flow turbulence foam
    FlowFoam flow = calculateFlowFoam(flowDir, flowSpeed, shoreDist, worldUV, time);
    result.flowFoam = flow.intensity;

    // Wave peak foam
    result.waveFoam = calculateWaveFoam(waveHeight, foamThreshold, worldUV, flowDir, time);

    // Combine foam sources
    // Shore foam takes priority, then flow, then waves
    result.totalIntensity = max(result.shoreFoam, max(result.flowFoam, result.waveFoam));

    // Smooth blend for overlapping areas
    float overlap = result.shoreFoam * result.flowFoam;
    result.totalIntensity = mix(result.totalIntensity,
                                result.totalIntensity + overlap * 0.3,
                                0.5);
    result.totalIntensity = clamp(result.totalIntensity, 0.0, 1.0);

    // Calculate final foam appearance
    result.color = calculateFoamColor(result.totalIntensity, waterDepth, baseWaterColor);
    result.opacity = calculateFoamOpacity(result.totalIntensity);

    return result;
}

// Simplified foam for distant water (LOD)
float calculateDistantFoam(float flowSpeed, float shoreDist, float waveHeight,
                           float foamThreshold) {
    // Just basic thresholds, no expensive noise
    float shore = 1.0 - smoothstep(0.0, 0.15, shoreDist);
    float flow = smoothstep(0.4, 0.8, flowSpeed) * 0.6;
    float wave = smoothstep(foamThreshold * 0.7, foamThreshold, waveHeight);

    return max(shore, max(flow, wave));
}
