#version 450

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

layout(binding = 2) uniform sampler2DArray shadowMap;

// Light buffer (SSBO)
struct Light {
    vec4 positionAndType;   // xyz = position, w = type (0=point, 1=spot, 2=directional)
    vec4 directionAndRange; // xyz = direction (spot/dir), w = range
    vec4 colorAndIntensity; // rgb = color, a = intensity
    vec4 spotParams;        // x = inner cone, y = outer cone, z = shadow index, w = flags
};

layout(std430, binding = 4) readonly buffer LightBuffer {
    uint lightCount;
    uint padding[3];
    Light lights[];
};

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in float fragDepth;

layout(location = 0) out vec4 outColor;

// Procedural bark texture
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

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < 4; i++) {
        value += amplitude * noise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value;
}

vec3 getBarkColor(vec2 uv) {
    // Base bark colors - brown tones
    vec3 darkBark = vec3(0.15, 0.10, 0.05);
    vec3 lightBark = vec3(0.35, 0.25, 0.15);

    // Scale UVs for bark detail
    vec2 barkUV = uv * vec2(8.0, 40.0);  // More vertical stretch

    // Layered noise for bark texture
    float n1 = fbm(barkUV);
    float n2 = fbm(barkUV * 2.0 + 100.0) * 0.5;
    float n3 = noise(barkUV * 8.0) * 0.25;

    float barkPattern = n1 + n2 + n3;

    // Vertical ridges
    float ridges = abs(sin(barkUV.x * 3.14159 * 4.0)) * 0.3;
    barkPattern += ridges;

    return mix(darkBark, lightBark, clamp(barkPattern, 0.0, 1.0));
}

// Shadow sampling
float sampleShadowPCF(vec3 worldPos, int cascade) {
    vec4 shadowCoord = ubo.cascadeViewProj[cascade] * vec4(worldPos, 1.0);
    shadowCoord.xyz /= shadowCoord.w;
    shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;

    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 ||
        shadowCoord.y < 0.0 || shadowCoord.y > 1.0 ||
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0) {
        return 1.0;
    }

    float shadow = 0.0;
    float texelSize = 1.0 / ubo.shadowMapSize;
    float bias = 0.002;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(x, y) * texelSize;
            float depth = texture(shadowMap, vec3(shadowCoord.xy + offset, float(cascade))).r;
            shadow += shadowCoord.z - bias > depth ? 0.0 : 1.0;
        }
    }

    return shadow / 9.0;
}

float getShadow(vec3 worldPos, float viewDepth) {
    int cascade = 0;
    if (viewDepth > ubo.cascadeSplits.x) cascade = 1;
    if (viewDepth > ubo.cascadeSplits.y) cascade = 2;
    if (viewDepth > ubo.cascadeSplits.z) cascade = 3;

    return sampleShadowPCF(worldPos, cascade);
}

void main() {
    vec3 normal = normalize(fragNormal);

    // Get bark color
    vec3 barkColor = getBarkColor(fragUV);

    // Lighting
    vec3 lightDir = normalize(ubo.sunDirection.xyz);
    vec3 viewDir = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Diffuse
    float NdotL = max(dot(normal, lightDir), 0.0);

    // Simple specular
    vec3 halfDir = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfDir), 0.0);
    float specular = pow(NdotH, 32.0) * 0.2;

    // Shadow
    float shadow = getShadow(fragWorldPos, fragDepth);

    // Sun contribution
    vec3 sunContrib = ubo.sunColor.rgb * (NdotL + specular) * shadow;

    // Ambient
    vec3 ambient = ubo.ambientColor.rgb * 0.3;

    // Point light contributions
    vec3 pointLightContrib = vec3(0.0);
    for (uint i = 0; i < min(lightCount, 32u); i++) {
        Light light = lights[i];
        vec3 lightPos = light.positionAndType.xyz;
        float lightType = light.positionAndType.w;
        float range = light.directionAndRange.w;
        vec3 lightColor = light.colorAndIntensity.rgb;
        float intensity = light.colorAndIntensity.a;

        if (lightType == 0.0) {  // Point light
            vec3 toLight = lightPos - fragWorldPos;
            float dist = length(toLight);
            if (dist < range) {
                float attenuation = 1.0 - smoothstep(0.0, range, dist);
                attenuation *= attenuation;
                vec3 L = normalize(toLight);
                float NdotLPoint = max(dot(normal, L), 0.0);
                pointLightContrib += lightColor * intensity * NdotLPoint * attenuation;
            }
        }
    }

    // Combine lighting
    vec3 finalColor = barkColor * (sunContrib + ambient + pointLightContrib);

    // Simple tone mapping
    finalColor = finalColor / (finalColor + vec3(1.0));

    outColor = vec4(finalColor, 1.0);
}
