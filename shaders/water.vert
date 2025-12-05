#version 450

/*
 * water.vert - Water surface vertex shader
 * Implements Gerstner wave animation for realistic ocean/lake surfaces
 * Phase 4: Samples displacement map for interactive splashes
 */

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[4];
    vec4 cascadeSplits;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 moonColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;
    vec4 pointLightColor;
    vec4 windDirectionAndSpeed;  // xy = direction, z = speed, w = time
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float julianDay;
} ubo;

// Water-specific uniforms
layout(std140, binding = 1) uniform WaterUniforms {
    vec4 waterColor;           // rgb = base water color, a = transparency
    vec4 waveParams;           // x = amplitude, y = wavelength, z = steepness, w = speed
    vec4 waveParams2;          // Second wave layer parameters
    vec4 waterExtent;          // xy = position offset, zw = size
    vec4 scatteringCoeffs;     // rgb = absorption coefficients, a = turbidity
    float waterLevel;          // Y height of water plane
    float foamThreshold;       // Wave height threshold for foam
    float fresnelPower;        // Fresnel reflection power
    float terrainSize;         // Terrain size for UV calculation
    float terrainHeightScale;  // Terrain height scale
    float shoreBlendDistance;  // Distance over which shore fades
    float shoreFoamWidth;      // Width of shore foam band
    float flowStrength;        // How much flow affects UV offset
    float flowSpeed;           // Flow animation speed multiplier
    float flowFoamStrength;    // How much flow speed affects foam
    float fbmNearDistance;     // Distance for max FBM detail
    float fbmFarDistance;      // Distance for min FBM detail
    float specularRoughness;   // Base roughness for specular
    float absorptionScale;     // How quickly light is absorbed
    float scatteringScale;     // How much light scatters
    float displacementScale;   // Scale for interactive displacement (Phase 4)
};

// Displacement map (Phase 4: Interactive splashes)
layout(binding = 5) uniform sampler2D displacementMap;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out float fragWaveHeight;

// Gerstner wave function
// Returns displacement and calculates tangent/bitangent for normal
vec3 gerstnerWave(vec2 pos, float time, vec2 direction, float wavelength, float steepness, float amplitude,
                  out vec3 tangent, out vec3 bitangent) {
    float k = 2.0 * 3.14159265359 / wavelength;
    float c = sqrt(9.8 / k);  // Phase speed from dispersion relation
    vec2 d = normalize(direction);
    float f = k * (dot(d, pos) - c * time);
    float a = steepness / k;

    // Gerstner wave displacement
    vec3 displacement;
    displacement.x = d.x * (a * cos(f));
    displacement.z = d.y * (a * cos(f));
    displacement.y = amplitude * sin(f);

    // Tangent (along wave direction)
    tangent = vec3(
        1.0 - d.x * d.x * steepness * sin(f),
        d.x * steepness * cos(f),
        -d.x * d.y * steepness * sin(f)
    );

    // Bitangent (perpendicular to wave direction)
    bitangent = vec3(
        -d.x * d.y * steepness * sin(f),
        d.y * steepness * cos(f),
        1.0 - d.y * d.y * steepness * sin(f)
    );

    return displacement;
}

void main() {
    float time = ubo.windDirectionAndSpeed.w;
    vec2 windDir = normalize(ubo.windDirectionAndSpeed.xy + vec2(0.001));

    // Base world position
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    vec2 pos = worldPos.xz;

    // Accumulate multiple Gerstner waves for more realistic surface
    vec3 totalDisplacement = vec3(0.0);
    vec3 totalTangent = vec3(1.0, 0.0, 0.0);
    vec3 totalBitangent = vec3(0.0, 0.0, 1.0);

    // Primary wave (wind-driven)
    vec3 tangent1, bitangent1;
    vec3 wave1 = gerstnerWave(pos, time * waveParams.w, windDir,
                               waveParams.y, waveParams.z, waveParams.x,
                               tangent1, bitangent1);
    totalDisplacement += wave1;

    // Secondary wave (cross-wind, smaller)
    vec3 tangent2, bitangent2;
    vec2 crossDir = vec2(-windDir.y, windDir.x) * 0.7 + windDir * 0.3;
    vec3 wave2 = gerstnerWave(pos, time * waveParams2.w * 1.3, crossDir,
                               waveParams2.y * 0.6, waveParams2.z * 0.7, waveParams2.x * 0.5,
                               tangent2, bitangent2);
    totalDisplacement += wave2;

    // Tertiary wave (counter direction, even smaller for detail)
    vec3 tangent3, bitangent3;
    vec2 counterDir = -windDir * 0.5 + vec2(windDir.y, -windDir.x) * 0.5;
    vec3 wave3 = gerstnerWave(pos, time * waveParams.w * 0.7, counterDir,
                               waveParams.y * 0.3, waveParams.z * 0.5, waveParams.x * 0.25,
                               tangent3, bitangent3);
    totalDisplacement += wave3;

    // Blend tangents/bitangents
    totalTangent = normalize(tangent1 + tangent2 * 0.5 + tangent3 * 0.25);
    totalBitangent = normalize(bitangent1 + bitangent2 * 0.5 + bitangent3 * 0.25);

    // Calculate normal from tangent and bitangent
    vec3 normal = normalize(cross(totalBitangent, totalTangent));

    // Apply Gerstner wave displacement
    worldPos.xyz += totalDisplacement;

    // Phase 4: Sample displacement map for interactive splashes
    // Calculate UV for displacement map (world position to UV)
    vec2 displacementUV = (worldPos.xz - waterExtent.xy) / waterExtent.zw + 0.5;
    displacementUV = clamp(displacementUV, 0.0, 1.0);

    // Sample displacement and apply with scale
    float interactiveDisplacement = texture(displacementMap, displacementUV).r;
    worldPos.y += interactiveDisplacement * displacementScale;

    // Output
    gl_Position = ubo.proj * ubo.view * worldPos;
    fragWorldPos = worldPos.xyz;
    fragNormal = normal;
    fragTexCoord = inTexCoord;
    fragWaveHeight = totalDisplacement.y + interactiveDisplacement * displacementScale;
}
