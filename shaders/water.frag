#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * water.frag - Water surface fragment shader
 * Implements Fresnel reflections, specular highlights, and foam for realistic water
 */

#include "constants_common.glsl"
#include "lighting_common.glsl"
#include "shadow_common.glsl"
#include "atmosphere_common.glsl"

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[NUM_CASCADES];
    vec4 cascadeSplits;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 moonColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;
    vec4 pointLightColor;
    vec4 windDirectionAndSpeed;
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
    float waterLevel;          // Y height of water plane
    float foamThreshold;       // Wave height threshold for foam
    float fresnelPower;        // Fresnel reflection power
    float padding;
};

layout(binding = 2) uniform sampler2DArrayShadow shadowMapArray;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in float fragWaveHeight;

layout(location = 0) out vec4 outColor;

// Procedural noise for water detail
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value;
}

// Schlick's Fresnel approximation
float fresnelSchlick(float cosTheta, float F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), fresnelPower);
}

// Sample environment reflection (simplified sky reflection)
vec3 sampleReflection(vec3 reflectDir, vec3 sunDir, vec3 sunColor) {
    // Simplified sky color based on reflection direction
    float skyGradient = smoothstep(-0.1, 0.5, reflectDir.y);

    // Day/night sky colors
    float dayAmount = smoothstep(-0.05, 0.2, sunDir.y);
    vec3 daySkyLow = vec3(0.6, 0.7, 0.9);
    vec3 daySkyHigh = vec3(0.3, 0.5, 0.85);
    vec3 nightSky = vec3(0.02, 0.03, 0.08);

    vec3 skyLow = mix(nightSky, daySkyLow, dayAmount);
    vec3 skyHigh = mix(nightSky * 0.5, daySkyHigh, dayAmount);
    vec3 skyColor = mix(skyLow, skyHigh, skyGradient);

    // Add sun reflection (specular highlight from sky)
    float sunDot = max(dot(reflectDir, sunDir), 0.0);
    vec3 sunReflect = sunColor * pow(sunDot, 256.0) * 2.0;  // Tight specular

    return skyColor + sunReflect;
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    vec3 moonDir = normalize(ubo.moonDirection.xyz);

    // Add procedural detail to normal
    float time = ubo.windDirectionAndSpeed.w;
    vec2 detailUV = fragWorldPos.xz * 0.5;
    float detail1 = fbm(detailUV + time * 0.3, 3) * 2.0 - 1.0;
    float detail2 = fbm(detailUV * 1.7 - time * 0.2, 3) * 2.0 - 1.0;

    // Perturb normal with detail noise
    vec3 detailNormal = normalize(N + vec3(detail1, 0.0, detail2) * 0.15);
    N = normalize(mix(N, detailNormal, 0.5));

    // Fresnel effect - more reflection at grazing angles
    float NdotV = max(dot(N, V), 0.0);
    float fresnel = fresnelSchlick(NdotV, 0.02);  // Water F0 ~0.02

    // Calculate reflection vector
    vec3 R = reflect(-V, N);

    // Shadow calculation
    float shadow = calculateCascadedShadow(
        fragWorldPos, N, sunDir,
        ubo.view, ubo.cascadeSplits, ubo.cascadeViewProj,
        ubo.shadowMapSize, shadowMapArray
    );

    // Base water color with depth-based absorption
    vec3 baseColor = waterColor.rgb;

    // Reflection color from environment
    vec3 sunColor = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 reflectionColor = sampleReflection(R, sunDir, sunColor);

    // Refraction color (simplified - just darkened water color)
    vec3 refractionColor = baseColor * 0.7;

    // Blend reflection and refraction based on Fresnel
    vec3 waterSurfaceColor = mix(refractionColor, reflectionColor, fresnel);

    // Sun specular highlight (Blinn-Phong for water)
    vec3 H = normalize(V + sunDir);
    float spec = pow(max(dot(N, H), 0.0), 256.0);  // Very tight specular for water
    vec3 sunSpecular = sunColor * spec * shadow;

    // Moon specular (softer, dimmer)
    vec3 moonH = normalize(V + moonDir);
    float moonSpec = pow(max(dot(N, moonH), 0.0), 128.0);
    vec3 moonColor = ubo.moonColor.rgb * ubo.moonDirection.w;
    vec3 moonSpecular = moonColor * moonSpec * 0.5;

    // Diffuse sun lighting on water (very subtle)
    float NdotL = max(dot(N, sunDir), 0.0);
    vec3 diffuse = baseColor * sunColor * NdotL * shadow * 0.3;

    // Ambient lighting
    vec3 ambient = baseColor * ubo.ambientColor.rgb * 0.4;

    // Foam on wave peaks
    float foamAmount = smoothstep(foamThreshold * 0.7, foamThreshold, fragWaveHeight);
    // Add some noise to foam distribution
    float foamNoise = fbm(fragWorldPos.xz * 2.0 + time * 0.5, 4);
    foamAmount *= smoothstep(0.3, 0.7, foamNoise);
    vec3 foamColor = vec3(0.9, 0.95, 1.0);

    // Combine all lighting
    vec3 finalColor = waterSurfaceColor + sunSpecular + moonSpecular + diffuse + ambient;

    // Mix in foam
    finalColor = mix(finalColor, foamColor, foamAmount * 0.8);

    // Apply aerial perspective (atmospheric scattering)
    vec3 cameraToFrag = fragWorldPos - ubo.cameraPosition.xyz;
    float viewDistance = length(cameraToFrag);
    finalColor = applyAerialPerspective(finalColor, ubo.cameraPosition.xyz,
                                         normalize(cameraToFrag), viewDistance,
                                         sunDir, sunColor);

    // Output with transparency
    // Water is more opaque where foam is, and has base transparency
    float alpha = mix(waterColor.a, 1.0, foamAmount);
    outColor = vec4(finalColor, alpha);
}
