#version 450

// CBT/LEB Terrain Fragment Shader
// PBR shading with height map-derived normals, shadows, and wireframe debug

const float PI = 3.14159265359;
const int NUM_CASCADES = 4;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[NUM_CASCADES];
    vec4 cascadeSplits;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;
    vec4 pointLightColor;
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float padding;
} ubo;

layout(binding = 1) uniform sampler2D albedoMap;
layout(binding = 2) uniform sampler2DArrayShadow shadowMapArray;
layout(binding = 3) uniform sampler2D normalMap;

layout(push_constant) uniform PushConstants {
    float terrainSize;
    float heightScale;
    float maxDepth;
    float debugWireframe;
} pc;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec4 fragTangent;
layout(location = 4) in vec3 fragBarycentric;

layout(location = 0) out vec4 outColor;

// ============== Shadow Functions ==============

int selectCascade(float viewDepth) {
    int cascade = 0;
    for (int i = 0; i < NUM_CASCADES - 1; i++) {
        if (viewDepth > ubo.cascadeSplits[i]) {
            cascade = i + 1;
        }
    }
    return cascade;
}

float sampleShadowForCascade(vec3 worldPos, vec3 normal, vec3 lightDir, int cascade) {
    vec4 lightSpacePos = ubo.cascadeViewProj[cascade] * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;
    }

    float cosTheta = max(dot(normal, lightDir), 0.0);
    float baseBias = 0.001;
    float cascadeBias = baseBias * (1.0 + float(cascade) * 0.5);
    float slopeBias = cascadeBias * tan(acos(cosTheta));
    float bias = clamp(slopeBias, 0.0, 0.01);

    float shadow = 0.0;
    float texelSize = 1.0 / ubo.shadowMapSize;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(x, y) * texelSize;
            shadow += texture(shadowMapArray, vec4(projCoords.xy + offset, float(cascade), projCoords.z - bias));
        }
    }
    shadow /= 9.0;

    return shadow;
}

float calculateShadow(vec3 worldPos, vec3 normal, vec3 lightDir) {
    vec4 viewPos = ubo.view * vec4(worldPos, 1.0);
    float viewDepth = -viewPos.z;
    int cascade = selectCascade(viewDepth);
    return sampleShadowForCascade(worldPos, normal, lightDir, cascade);
}

// ============== PBR Functions ==============

float D_GGX(float NoH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NoH2 = NoH * NoH;
    float denom = NoH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float V_SmithGGX(float NoV, float NoL, float roughness) {
    float a = roughness * roughness;
    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a) + a);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a) + a);
    return 0.5 / (GGXV + GGXL + 0.0001);
}

vec3 F_Schlick(float VoH, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
}

// ============== Wireframe ==============

float wireframeEdge(vec3 bary) {
    vec3 d = fwidth(bary);
    vec3 a3 = smoothstep(vec3(0.0), d * 1.5, bary);
    return min(min(a3.x, a3.y), a3.z);
}

// ============== Main ==============

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Sample terrain albedo - tile the texture
    float textureScale = 10.0;  // Tile the texture across terrain
    vec3 albedo = texture(albedoMap, fragTexCoord * textureScale).rgb;

    // Material properties for terrain
    float roughness = 0.8;
    float metallic = 0.0;
    vec3 F0 = vec3(0.04);

    // Sun lighting
    vec3 sunL = normalize(ubo.sunDirection.xyz);
    float sunIntensity = ubo.sunDirection.w;
    float shadow = calculateShadow(fragWorldPos, N, sunL);

    vec3 H = normalize(V + sunL);
    float NoL = max(dot(N, sunL), 0.0);
    float NoV = max(dot(N, V), 0.0001);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    float D = D_GGX(NoH, roughness);
    float Vis = V_SmithGGX(NoV, NoL, roughness);
    vec3 F = F_Schlick(VoH, F0);

    vec3 specular = D * Vis * F;
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    vec3 sunLight = (diffuse + specular) * ubo.sunColor.rgb * sunIntensity * NoL * shadow;

    // Ambient
    vec3 ambient = ubo.ambientColor.rgb * albedo * 0.3;

    vec3 finalColor = ambient + sunLight;

    // Apply wireframe overlay if debug enabled
    if (pc.debugWireframe > 0.5) {
        float edge = wireframeEdge(fragBarycentric);
        vec3 wireColor = vec3(0.0, 1.0, 0.5);  // Green wireframe
        finalColor = mix(wireColor, finalColor, edge);
    }

    // LOD debug visualization based on UV density
    if (pc.debugWireframe > 1.5) {
        // Color by subdivision depth (approximated by UV derivative)
        vec2 dUV = fwidth(fragTexCoord);
        float lodLevel = -log2(max(dUV.x, dUV.y) * 10.0);
        vec3 lodColors[5] = vec3[](
            vec3(1.0, 0.0, 0.0),  // Red - coarse
            vec3(1.0, 0.5, 0.0),  // Orange
            vec3(1.0, 1.0, 0.0),  // Yellow
            vec3(0.0, 1.0, 0.0),  // Green
            vec3(0.0, 0.0, 1.0)   // Blue - fine
        );
        int idx = clamp(int(lodLevel), 0, 4);
        finalColor = mix(finalColor, lodColors[idx], 0.4);
    }

    outColor = vec4(finalColor, 1.0);
}
