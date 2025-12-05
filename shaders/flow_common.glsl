/*
 * flow_common.glsl - Flow map sampling utilities
 *
 * Implements two-phase flow sampling to eliminate pulsing artifacts.
 * Based on Valve's "Water Flow in Portal 2" (Alex Vlachos, SIGGRAPH 2010)
 * and Far Cry 5's flow map system.
 *
 * Flow map format:
 *   R: Flow direction X (-1 to 1, encoded as 0-1)
 *   G: Flow direction Y (-1 to 1, encoded as 0-1)
 *   B: Flow speed multiplier (0 to 1)
 *   A: Signed distance to shore (normalized, for foam)
 */

// Decode flow direction from texture (0-1 range to -1 to 1 range)
vec2 decodeFlowDirection(vec2 encodedFlow) {
    return encodedFlow * 2.0 - 1.0;
}

// Calculate flow phase for two-layer sampling
// Returns vec2(phase0, phase1) where each phase cycles 0-1
vec2 calculateFlowPhases(float time, float flowSpeed) {
    // Phase offset between the two layers (0.5 = half cycle)
    const float phaseOffset = 0.5;

    // Calculate base phase from time
    float phase = fract(time * flowSpeed);

    // Two phases offset by half a cycle
    float phase0 = fract(phase);
    float phase1 = fract(phase + phaseOffset);

    return vec2(phase0, phase1);
}

// Calculate blend weight between two flow phases
// Creates smooth transition to hide texture reset
float calculateFlowBlend(vec2 phases) {
    // Blend weight based on how close each phase is to 0 or 1
    // When phase approaches 1, we fade to the other layer
    float blend = abs(phases.x - 0.5) * 2.0;
    return blend;
}

// Sample a texture with flow-based UV offset
// flowDir: decoded flow direction (-1 to 1)
// phase: current flow phase (0 to 1)
// flowSpeed: speed multiplier from flow map
// baseUV: original texture coordinates
// flowStrength: how much the flow affects UV (world-space distance)
vec2 calculateFlowUV(vec2 baseUV, vec2 flowDir, float phase, float flowStrength) {
    // Offset UV by flow direction scaled by phase
    // Phase goes 0->1, so UV slides along flow direction
    return baseUV - flowDir * phase * flowStrength;
}

// Complete two-phase flow sampling structure
struct FlowSample {
    vec2 uv0;       // UV for first phase
    vec2 uv1;       // UV for second phase
    float blend;    // Blend weight (0 = use uv0, 1 = use uv1)
    vec2 flowDir;   // Decoded flow direction
    float speed;    // Flow speed
    float shoreDist; // Distance to shore (for foam)
};

// Calculate flow sample data from flow map
// flowMapSample: raw sample from flow map texture (RGBA)
// baseUV: original texture coordinates
// time: current time in seconds
// flowStrength: UV offset magnitude (larger = more visible flow)
FlowSample calculateFlowSample(vec4 flowMapSample, vec2 baseUV, float time, float flowStrength) {
    FlowSample result;

    // Decode flow direction from RG channels
    result.flowDir = decodeFlowDirection(flowMapSample.rg);
    result.speed = flowMapSample.b;
    result.shoreDist = flowMapSample.a;

    // Scale flow by speed
    vec2 scaledFlow = result.flowDir * result.speed;

    // Calculate phases
    vec2 phases = calculateFlowPhases(time, 0.25); // Base cycle speed

    // Calculate UVs for both phases
    result.uv0 = calculateFlowUV(baseUV, scaledFlow, phases.x, flowStrength);
    result.uv1 = calculateFlowUV(baseUV, scaledFlow, phases.y, flowStrength);

    // Calculate blend weight
    result.blend = calculateFlowBlend(phases);

    return result;
}

// Blend two texture samples using flow weights
vec4 blendFlowSamples(vec4 sample0, vec4 sample1, float blend) {
    return mix(sample0, sample1, blend);
}

vec3 blendFlowSamples(vec3 sample0, vec3 sample1, float blend) {
    return mix(sample0, sample1, blend);
}

float blendFlowSamples(float sample0, float sample1, float blend) {
    return mix(sample0, sample1, blend);
}

// Generate procedural flow-based noise
// Useful for foam and surface detail that follows flow
float flowNoise(vec2 uv, vec2 flowDir, float time, float scale) {
    // Offset UV by flow over time
    vec2 flowUV = uv - flowDir * time * 0.1;

    // Simple noise (can be replaced with better noise function)
    vec2 i = floor(flowUV * scale);
    vec2 f = fract(flowUV * scale);
    f = f * f * (3.0 - 2.0 * f);

    float a = fract(sin(dot(i, vec2(127.1, 311.7))) * 43758.5453);
    float b = fract(sin(dot(i + vec2(1.0, 0.0), vec2(127.1, 311.7))) * 43758.5453);
    float c = fract(sin(dot(i + vec2(0.0, 1.0), vec2(127.1, 311.7))) * 43758.5453);
    float d = fract(sin(dot(i + vec2(1.0, 1.0), vec2(127.1, 311.7))) * 43758.5453);

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Calculate foam intensity based on flow speed and shore distance
// Higher flow speed = more turbulent foam
// Closer to shore = more foam accumulation
float calculateFlowFoam(float flowSpeed, float shoreDist, float time) {
    // Fast-flowing water generates foam
    float speedFoam = smoothstep(0.3, 0.8, flowSpeed);

    // Shore proximity generates foam (shoreDist is 0 at shore, 1 far away)
    float shoreFoam = smoothstep(0.3, 0.0, shoreDist);

    // Combine with some noise for variation
    float foamNoise = fract(sin(time * 2.0 + shoreDist * 10.0) * 43758.5453);

    return max(speedFoam, shoreFoam) * (0.7 + 0.3 * foamNoise);
}
