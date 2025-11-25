#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 rayleighScattering;
    vec4 mieScattering;
    vec4 absorptionExtinction;
    vec4 atmosphereParams;
    float timeOfDay;
    float shadowMapSize;
} ubo;

layout(binding = 2) uniform sampler2DShadow shadowMap;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in float fragHeight;
layout(location = 3) in float fragClumpId;
layout(location = 4) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

// Clump color variation parameters
const float CLUMP_COLOR_INFLUENCE = 0.15;  // Subtle color variation (0-1)

// Shadow calculation with PCF
float calculateShadow(vec3 worldPos, vec3 normal, vec3 lightDir) {
    // Transform to light space
    vec4 lightSpacePos = ubo.lightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Transform to [0,1] range for UV coordinates
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Check if outside shadow map
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0) {
        return 1.0;  // No shadow outside frustum
    }

    // Bias to reduce shadow acne - grass needs less bias due to thin geometry
    float cosTheta = max(dot(normal, lightDir), 0.0);
    float bias = 0.003 * tan(acos(cosTheta));
    bias = clamp(bias, 0.0, 0.008);

    // PCF 3x3
    float shadow = 0.0;
    float texelSize = 1.0 / ubo.shadowMapSize;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(x, y) * texelSize;
            shadow += texture(shadowMap, vec3(projCoords.xy + offset, projCoords.z - bias));
        }
    }
    shadow /= 9.0;

    return shadow;
}

void main() {
    vec3 normal = normalize(fragNormal);

    // Apply subtle clump-based color variation
    // Different clumps have slightly different hue/saturation
    vec3 color = fragColor;

    // Subtle hue shift based on clumpId (shift toward yellow or blue-green)
    float hueShift = (fragClumpId - 0.5) * 2.0;  // -1 to 1
    vec3 warmShift = vec3(0.05, 0.03, -0.02);    // Slightly warmer/yellower
    vec3 coolShift = vec3(-0.02, 0.02, 0.03);    // Slightly cooler/bluer
    vec3 colorShift = mix(coolShift, warmShift, fragClumpId);
    color += colorShift * CLUMP_COLOR_INFLUENCE;

    // Subtle brightness variation per clump
    float brightnessVar = 0.9 + fragClumpId * 0.2;  // 0.9 to 1.1
    color *= mix(1.0, brightnessVar, CLUMP_COLOR_INFLUENCE);

    // Calculate shadow for sun light
    vec3 sunL = normalize(ubo.sunDirection.xyz);
    float shadow = calculateShadow(fragWorldPos, normal, sunL);

    // Two-sided lighting (grass blades are thin)
    float sunDot = dot(normal, ubo.sunDirection.xyz);
    float sunDiffuse = max(sunDot, 0.0) + max(-sunDot, 0.0) * 0.6;  // Backface gets some light
    vec3 sunLight = ubo.sunColor.rgb * sunDiffuse * ubo.sunDirection.w * shadow;

    float moonDot = dot(normal, ubo.moonDirection.xyz);
    float moonDiffuse = max(moonDot, 0.0) + max(-moonDot, 0.0) * 0.6;
    vec3 moonLight = vec3(0.08, 0.08, 0.12) * moonDiffuse * ubo.moonDirection.w;

    // Ambient occlusion - darker at base
    float ao = 0.5 + fragHeight * 0.5;

    // Final lighting
    vec3 lighting = (ubo.ambientColor.rgb + sunLight + moonLight) * ao;

    outColor = vec4(color * lighting, 1.0);
}
