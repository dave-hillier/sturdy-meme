/*
 * wind_animation_common.glsl - Common wind animation functions for vegetation
 *
 * Provides shared wind animation code for trees, leaves, and grass.
 * GPU Gems 3 style multi-frequency oscillation for natural motion.
 *
 * Usage in shaders: #include "wind_animation_common.glsl"
 * Requires: noise_common.glsl (for simplex3)
 */

#ifndef WIND_ANIMATION_COMMON_GLSL
#define WIND_ANIMATION_COMMON_GLSL

// Wind parameters extracted from uniform buffer
struct WindParams {
    vec2 direction;     // Normalized wind direction in XZ plane
    float strength;     // Wind strength
    float speed;        // Wind speed
    float gustFreq;     // Gust frequency
    float gustAmp;      // Gust amplitude
    float noiseScale;   // Noise sampling scale
    float time;         // Animation time
};

// Extract wind parameters from standard wind uniform layout
// windDirectionAndStrength: xy = normalized direction, z = strength, w = speed
// windParams: x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
WindParams windExtractParams(vec4 windDirectionAndStrength, vec4 windParamsVec) {
    WindParams p;
    p.direction = windDirectionAndStrength.xy;
    p.strength = windDirectionAndStrength.z;
    p.speed = windDirectionAndStrength.w;
    p.gustFreq = windParamsVec.x;
    p.gustAmp = windParamsVec.y;
    p.noiseScale = windParamsVec.z;
    p.time = windParamsVec.w;
    return p;
}

// Result of tree trunk/branch oscillation calculation
struct TreeWindOscillation {
    float mainBend;     // Primary bend in wind direction
    float perpBend;     // Secondary perpendicular sway (figure-8 motion)
    vec3 windDir3D;     // Wind direction in 3D (XZ plane)
    vec3 windPerp3D;    // Perpendicular to wind direction
    float treePhase;    // Per-tree phase offset
};

// Calculate tree oscillation using GPU Gems 3 style multi-frequency approach
// treeBaseWorld: World position of tree base (for phase calculation)
// wind: Wind parameters
// Returns oscillation values for trunk/branch sway
TreeWindOscillation windCalculateTreeOscillation(vec3 treeBaseWorld, WindParams wind) {
    TreeWindOscillation result;

    // Per-tree phase offset from noise (so trees don't sway in sync)
    result.treePhase = simplex3(treeBaseWorld * 0.1) * 6.28318;

    // Wind direction in 3D (XZ plane)
    result.windDir3D = vec3(wind.direction.x, 0.0, wind.direction.y);

    // Perpendicular to wind direction (for secondary sway)
    result.windPerp3D = vec3(-wind.direction.y, 0.0, wind.direction.x);

    // Multi-frequency oscillation for natural motion
    float mainBendTime = wind.time * wind.gustFreq;
    result.mainBend =
        0.5 * sin(mainBendTime + result.treePhase) +
        0.3 * sin(mainBendTime * 2.1 + result.treePhase * 1.3) +
        0.2 * sin(mainBendTime * 3.7 + result.treePhase * 0.7);

    // Secondary perpendicular sway (figure-8 motion)
    result.perpBend =
        0.3 * sin(mainBendTime * 1.3 + result.treePhase + 1.57) +
        0.2 * sin(mainBendTime * 2.7 + result.treePhase * 0.9);

    return result;
}

// Calculate branch flexibility based on branch level
// branchLevel: 0 = trunk (stiff), 3 = tips (flexible)
// Returns flexibility factor for bend calculations
float windCalculateBranchFlexibility(float branchLevel) {
    return 0.04 + branchLevel * 0.06;  // 0.04 to 0.22 (doubled for more visible sway)
}

// Calculate wind direction-relative motion scale
// Branches facing into wind move less, back-facing move more
// branchDirWorld: Normalized branch direction in world space
// windDir3D: Wind direction in 3D (XZ plane)
// Returns scale factor for motion (0.5 to 1.5)
float windCalculateDirectionScale(vec3 branchDirWorld, vec3 windDir3D) {
    float windAlignment = dot(branchDirWorld, windDir3D);
    // windAlignment: 1 = facing wind, -1 = back to wind
    // Scale: back-facing (1.5x), perpendicular (1.0x), wind-facing (0.5x)
    return mix(1.5, 0.5, (windAlignment + 1.0) * 0.5);
}

// Calculate bend offset for tree trunk/branch animation
// osc: Tree oscillation values from windCalculateTreeOscillation
// offsetFromPivot: Vertex position relative to pivot point
// flexibility: From windCalculateBranchFlexibility
// windStrength: Wind strength
// directionScale: From windCalculateDirectionScale
// Returns 3D bend offset to apply to vertex position
vec3 windCalculateBendOffset(
    TreeWindOscillation osc,
    vec3 offsetFromPivot,
    float flexibility,
    float windStrength,
    float directionScale
) {
    float heightAbovePivot = max(0.0, offsetFromPivot.y);
    float bendAmount = heightAbovePivot * flexibility * windStrength * directionScale;

    return osc.windDir3D * osc.mainBend * bendAmount +
           osc.windPerp3D * osc.perpBend * bendAmount * 0.5;
}

// Calculate high-frequency detail motion for branch tips
// localPos: Local vertex position
// branchLevel: Branch level (0-3)
// windStrength: Wind strength
// gustFreq: Gust frequency
// windTime: Animation time
// Returns 3D detail offset to apply to vertex position
vec3 windCalculateDetailOffset(
    vec3 localPos,
    float branchLevel,
    float windStrength,
    float gustFreq,
    float windTime
) {
    float detailFreq = windTime * gustFreq * 5.0;
    float detailNoise = simplex3(vec3(localPos.x * 2.0, localPos.y * 2.0, detailFreq * 0.3));
    float detailAmount = branchLevel * 0.03 * windStrength;  // Increased from 0.01 for more visible tip flutter
    return vec3(detailNoise, 0.0, detailNoise * 0.7) * detailAmount;
}

// Calculate leaf oscillation values for wind animation
// worldPosition: World position of the leaf
// wind: Wind parameters
// windPhaseOffset: Per-leaf phase offset for variation
// Returns: osc1, osc2, osc3 oscillation values and combined oscillation
struct LeafWindOscillation {
    float osc1;         // Primary oscillation
    float osc2;         // Secondary oscillation (2x frequency)
    float osc3;         // Tertiary oscillation (5x frequency)
    float combined;     // Weighted combination (0.5*osc1 + 0.3*osc2 + 0.2*osc3)
};

LeafWindOscillation windCalculateLeafOscillation(
    vec3 worldPosition,
    WindParams wind,
    float windPhaseOffset
) {
    LeafWindOscillation result;

    // Sample wind noise using world position for spatial coherence
    float windOffset = 2.0 * 3.14159265 * simplex3(worldPosition / wind.noiseScale) + windPhaseOffset;

    // Multi-frequency oscillation values
    result.osc1 = sin(wind.time * wind.gustFreq + windOffset);
    result.osc2 = sin(2.0 * wind.time * wind.gustFreq + 1.3 * windOffset);
    result.osc3 = sin(5.0 * wind.time * wind.gustFreq + 1.5 * windOffset);

    result.combined = 0.5 * result.osc1 + 0.3 * result.osc2 + 0.2 * result.osc3;

    return result;
}

// Calculate branch sway influence on leaves
// Uses same oscillation as parent tree for hierarchical motion
// osc: Tree oscillation from windCalculateTreeOscillation
// leafHeight: Leaf height relative to tree base
// windStrength: Wind strength
// Returns 3D offset for branch sway influence
vec3 windCalculateBranchSwayInfluence(
    TreeWindOscillation osc,
    float leafHeight,
    float windStrength
) {
    // Match branch tip motion: branches use heightAbovePivot * flexibility
    // where heightAbovePivot is ~20% of leafHeight for tip branches
    // and tip flexibility is 0.22, so factor ≈ 0.2 * 0.22 = 0.044
    float branchInfluence = leafHeight * 0.04 * windStrength;
    return osc.windDir3D * osc.mainBend * branchInfluence +
           osc.windPerp3D * osc.perpBend * branchInfluence * 0.5;
}

// Calculate grass wind offset
// Convenience function that wraps grass wind calculation parameters
// worldPosXZ: World position (XZ components)
// windDir: Normalized wind direction
// bladeHash: Per-blade random hash (0-1)
// facing: Blade facing angle
// wind: Wind parameters
// Returns wind offset value to apply to blade deformation
float windCalculateGrassOffset(
    vec2 worldPosXZ,
    vec2 windDir,
    float bladeHash,
    float facing,
    WindParams wind
) {
    // Note: This function requires grassSampleWind from grass_blade_common.glsl
    // The actual wind sampling is done there, this just handles the phase offset

    // Per-blade phase offset for variation (prevents lockstep motion)
    float windPhase = bladeHash * 6.28318;
    float phaseOffset = sin(wind.time * 2.5 + windPhase) * 0.3;

    // Wind angle relative to blade facing
    float windAngle = atan(windDir.y, windDir.x);
    float relativeWindAngle = windAngle - facing;

    return phaseOffset * cos(relativeWindAngle);
}

#endif // WIND_ANIMATION_COMMON_GLSL
