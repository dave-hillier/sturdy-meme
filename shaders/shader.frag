#version 450

const float PI = 3.14159265359;

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
    float timeOfDay;
    float shadowMapSize;
} ubo;

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2DShadow shadowMap;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float roughness;
    float metallic;
} material;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

// GGX Normal Distribution Function
float D_GGX(float NoH, float roughness) {
    // Clamp minimum roughness to prevent infinitely tight highlights
    float r = max(roughness, 0.04);
    float a = r * r;
    float a2 = a * a;
    float NoH2 = NoH * NoH;
    float denom = NoH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Smith Visibility Function (height-correlated)
float V_SmithGGX(float NoV, float NoL, float roughness) {
    float r = max(roughness, 0.04);
    float a = r * r;
    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a) + a);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a) + a);
    return 0.5 / (GGXV + GGXL + 0.0001);
}

// Schlick Fresnel approximation
vec3 F_Schlick(float VoH, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
}

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

    // Bias to reduce shadow acne
    float cosTheta = max(dot(normal, lightDir), 0.0);
    float bias = 0.005 * tan(acos(cosTheta));
    bias = clamp(bias, 0.0, 0.01);

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

// Calculate PBR lighting for a single light
vec3 calculatePBR(vec3 N, vec3 V, vec3 L, vec3 lightColor, float lightIntensity, vec3 albedo, float shadow) {
    vec3 H = normalize(V + L);

    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 0.0001);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    // Dielectric F0 (0.04 is typical for non-metals)
    vec3 F0 = mix(vec3(0.04), albedo, material.metallic);

    // Specular BRDF
    float D = D_GGX(NoH, material.roughness);
    float Vis = V_SmithGGX(NoV, NoL, material.roughness);
    vec3 F = F_Schlick(VoH, F0);

    vec3 specular = D * Vis * F;

    // Energy-conserving diffuse
    vec3 kD = (1.0 - F) * (1.0 - material.metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * lightColor * lightIntensity * NoL * shadow;
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    vec4 texColor = texture(texSampler, fragTexCoord);
    vec3 albedo = texColor.rgb;

    // Calculate shadow for sun
    vec3 sunL = normalize(ubo.sunDirection.xyz);
    float shadow = calculateShadow(fragWorldPos, N, sunL);

    // Sun lighting with shadow
    float sunIntensity = ubo.sunDirection.w;
    vec3 sunLight = calculatePBR(N, V, sunL, ubo.sunColor.rgb, sunIntensity, albedo, shadow);

    // Moon lighting (no shadow - moon is soft fill light)
    vec3 moonL = normalize(ubo.moonDirection.xyz);
    float moonIntensity = ubo.moonDirection.w;
    vec3 moonLight = calculatePBR(N, V, moonL, vec3(0.3, 0.35, 0.5), moonIntensity, albedo, 1.0);

    // Ambient lighting
    // For dielectrics: diffuse ambient from all directions
    // For metals: specular ambient (environment reflection approximation)
    vec3 F0 = mix(vec3(0.04), albedo, material.metallic);
    vec3 ambientDiffuse = ubo.ambientColor.rgb * albedo * (1.0 - material.metallic);
    // Metals need higher ambient to simulate environment reflections
    // Rougher metals get more ambient, smoother metals rely more on direct specular
    float envReflection = mix(0.3, 1.0, material.roughness);
    vec3 ambientSpecular = ubo.ambientColor.rgb * F0 * material.metallic * envReflection;
    vec3 ambient = ambientDiffuse + ambientSpecular;

    vec3 finalColor = ambient + sunLight + moonLight;

    outColor = vec4(finalColor, texColor.a);
}
