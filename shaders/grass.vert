#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "noise_common.glsl"
#include "wind_animation_common.glsl"
#include "grass_blade_common.glsl"

struct GrassInstance {
    vec4 positionAndFacing;  // xyz = position, w = facing angle
    vec4 heightHashTilt;     // x = height, y = hash, z = tilt, w = clumpId
    vec4 terrainNormal;      // xyz = terrain normal (for tangent alignment), w = unused
};

layout(std430, binding = BINDING_GRASS_INSTANCE_BUFFER) readonly buffer InstanceBuffer {
    GrassInstance instances[];
};

// Wind uniform buffer
layout(binding = BINDING_GRASS_WIND_UBO) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                 // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
} wind;

layout(push_constant) uniform PushConstants {
    float time;
    float tileOriginX;   // For tiled mode (not used in vertex shader, but must match C++ struct)
    float tileOriginZ;
    float tileSize;      // Tile size in world units (varies by LOD)
    float spacingMult;   // Spacing multiplier for this LOD
    uint lodLevel;       // LOD level (0 = high detail, 1 = medium, 2 = low)
    float tileLoadTime;  // Time when tile was loaded (not used in vertex shader)
    float padding;
} push;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out float fragHeight;
layout(location = 3) out float fragClumpId;
layout(location = 4) out vec3 fragWorldPos;

void main() {
    // With indirect draw:
    // - gl_InstanceIndex = which blade (0 to instanceCount-1)
    // - gl_VertexIndex = which vertex within this blade (0 to vertexCount-1)

    // Get instance data using gl_InstanceIndex
    GrassInstance inst = instances[gl_InstanceIndex];
    vec3 basePos = inst.positionAndFacing.xyz;
    float facing = inst.positionAndFacing.w;
    float height = inst.heightHashTilt.x;
    float bladeHash = inst.heightHashTilt.y;
    float tilt = inst.heightHashTilt.z;
    float clumpId = inst.heightHashTilt.w;
    vec3 terrainNormal = normalize(inst.terrainNormal.xyz);

    // Build coordinate frame aligned to terrain surface
    // T = tangent (facing direction on terrain surface)
    // B = bitangent (perpendicular to T on terrain surface)
    // N = terrain normal (grass "up" direction)
    vec3 T, B;
    grassBuildTerrainBasis(terrainNormal, facing, T, B);

    // Extract wind parameters using common function
    WindParams windParams = windExtractParams(wind.windDirectionAndStrength, wind.windParams);

    // Sample wind strength at this blade's position using grass-specific wind function
    float windSample = grassSampleWind(
        vec2(basePos.x, basePos.z),
        windParams.direction,
        windParams.strength,
        windParams.speed,
        windParams.time,
        windParams.gustFreq,
        windParams.gustAmp
    );

    // Per-blade phase offset and wind angle calculation using common function
    float grassPhaseOffset = windCalculateGrassOffset(
        vec2(basePos.x, basePos.z),
        windParams.direction,
        bladeHash,
        facing,
        windParams
    );

    // Wind offset combines sampled wind with phase offset using unified constants
    float windAngle = atan(windParams.direction.y, windParams.direction.x);
    float relativeWindAngle = windAngle - facing;
    float windEffect = windSample * GRASS_WIND_EFFECT_MULTIPLIER;
    float windOffset = (windEffect + grassPhaseOffset * GRASS_WIND_PHASE_MULTIPLIER) * cos(relativeWindAngle);

    // Calculate blade deformation (fold/droop) - shared with shadow pass
    GrassBladeControlPoints cp = grassCalculateBladeDeformation(height, bladeHash, tilt, windOffset);

    // Calculate blade vertex position
    vec3 localPos;
    float t;
    float widthAtT;
    grassCalculateBladeVertex(gl_VertexIndex, cp, localPos, t, widthAtT);

    // Blade's right vector (B - bitangent, perpendicular to facing in terrain plane)
    vec3 bladeRight = B;

    // View-facing thickening: widen blade when viewed edge-on
    // Calculate view direction to blade base
    vec3 viewDir = normalize(ubo.cameraPosition.xyz - basePos);

    // How much we're viewing edge-on (0 = face-on, 1 = edge-on)
    float edgeFactor = abs(dot(viewDir, bladeRight));

    // Thicken when viewed edge-on using unified constants
    float thickenAmount = 1.0 + edgeFactor * GRASS_EDGE_THICKEN_FACTOR;
    vec3 thickenedPos = vec3(localPos.x * thickenAmount, localPos.y, localPos.z);

    // Transform from local blade space to world space using terrain basis
    // Local X = blade width direction (bitangent B)
    // Local Y = blade up direction (terrain normal N)
    // Local Z = blade facing direction (tangent T)
    vec3 worldOffset = B * thickenedPos.x +
                       terrainNormal * thickenedPos.y +
                       T * thickenedPos.z;

    // Final world position
    vec3 worldPos = basePos + worldOffset;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);

    // Calculate normal (perpendicular to blade surface)
    // Bezier tangent is in local space (X=width, Y=up, Z=forward)
    vec3 localTangent = normalize(grassBezierDerivative(cp.p0, cp.p1, cp.p2, t));

    // Transform tangent to world space using terrain basis
    vec3 worldTangent = B * localTangent.x +
                        terrainNormal * localTangent.y +
                        T * localTangent.z;

    // Base normal is perpendicular to both tangent and blade width direction
    vec3 surfaceNormal = normalize(cross(worldTangent, bladeRight));

    // Add curvature to make blades appear rounded rather than flat
    // Determine which side of blade this vertex is on (-1 = left, +1 = right, 0 = tip)
    float sideFactor = 0.0;
    if (widthAtT > 0.001) {
        sideFactor = localPos.x / widthAtT;  // -1 for left edge, +1 for right edge
    }

    // Tilt normal outward at edges (along bladeRight direction)
    // This creates a curved appearance even though geometry is flat
    vec3 outwardTilt = bladeRight * sideFactor;
    vec3 normal = normalize(mix(surfaceNormal, outwardTilt, GRASS_BLADE_CURVATURE * 0.5));

    // Color gradient: darker at base, lighter at tip using unified constants
    fragColor = mix(GRASS_COLOR_BASE, GRASS_COLOR_TIP, t);

    fragNormal = normal;
    fragHeight = t;
    fragClumpId = clumpId;
    fragWorldPos = worldPos;
}
