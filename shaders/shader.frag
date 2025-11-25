#version 450

const float PI = 3.14159265359;
const int NUM_CASCADES = 4;

// Atmospheric parameters (Phase 4 - scaled to kilometers for scene scale)
const float PLANET_RADIUS = 6371.0;
const float ATMOSPHERE_RADIUS = 6471.0;

const vec3 RAYLEIGH_SCATTERING_BASE = vec3(5.802e-3, 13.558e-3, 33.1e-3);
const float RAYLEIGH_SCALE_HEIGHT = 8.0;

const float MIE_SCATTERING_BASE = 3.996e-3;
const float MIE_ABSORPTION_BASE = 4.4e-3;
const float MIE_SCALE_HEIGHT = 1.2;
const float MIE_ANISOTROPY = 0.8;

const vec3 OZONE_ABSORPTION = vec3(0.65e-3, 1.881e-3, 0.085e-3);
const float OZONE_LAYER_CENTER = 25.0;
const float OZONE_LAYER_WIDTH = 15.0;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[NUM_CASCADES];  // Per-cascade light matrices
    vec4 cascadeSplits;                   // View-space split depths
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;  // xyz = position, w = intensity
    vec4 pointLightColor;     // rgb = color, a = radius
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;       // 1.0 = show cascade colors
    float padding;
} ubo;

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2DArrayShadow shadowMapArray;  // Changed to array for CSM
layout(binding = 3) uniform sampler2D normalMap;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float roughness;
    float metallic;
    float emissiveIntensity;
} material;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

struct ScatteringResult {
    vec3 inscatter;
    vec3 transmittance;
};

float rayleighPhase(float cosTheta) {
    return 3.0 / (16.0 * PI) * (1.0 + cosTheta * cosTheta);
}

float cornetteShanksPhase(float cosTheta, float g) {
    float g2 = g * g;
    float num = 3.0 * (1.0 - g2) * (1.0 + cosTheta * cosTheta);
    float denom = 8.0 * PI * (2.0 + g2) * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return num / denom;
}

vec2 raySphereIntersect(vec3 origin, vec3 dir, float radius) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) return vec2(1e9, -1e9);
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

float ozoneDensity(float altitude) {
    float z = (altitude - OZONE_LAYER_CENTER) / OZONE_LAYER_WIDTH;
    return exp(-0.5 * z * z);
}

ScatteringResult integrateAtmosphere(vec3 origin, vec3 dir, float maxDistance, int sampleCount) {
    vec2 atmo = raySphereIntersect(origin, dir, ATMOSPHERE_RADIUS);
    float start = max(atmo.x, 0.0);
    float end = min(atmo.y, maxDistance);

    if (end <= 0.0) {
        return ScatteringResult(vec3(0.0), vec3(1.0));
    }

    vec2 planet = raySphereIntersect(origin, dir, PLANET_RADIUS);
    if (planet.x > 0.0) {
        end = min(end, planet.x);
    }

    if (end <= start) {
        return ScatteringResult(vec3(0.0), vec3(1.0));
    }

    float stepSize = (end - start) / float(sampleCount);
    vec3 transmittance = vec3(1.0);
    vec3 inscatter = vec3(0.0);

    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    float cosViewSun = dot(dir, sunDir);
    float rayleighP = rayleighPhase(cosViewSun);
    float mieP = cornetteShanksPhase(cosViewSun, MIE_ANISOTROPY);

    for (int i = 0; i < sampleCount; i++) {
        float t = start + (float(i) + 0.5) * stepSize;
        vec3 pos = origin + dir * t;
        float altitude = max(length(pos) - PLANET_RADIUS, 0.0);

        float rayleighDensity = exp(-altitude / RAYLEIGH_SCALE_HEIGHT);
        float mieDensity = exp(-altitude / MIE_SCALE_HEIGHT);
        float ozone = ozoneDensity(altitude);

        vec3 rayleighScatter = rayleighDensity * RAYLEIGH_SCATTERING_BASE;
        vec3 mieScatter = mieDensity * vec3(MIE_SCATTERING_BASE);

        vec3 extinction = rayleighScatter + mieScatter +
                          mieDensity * vec3(MIE_ABSORPTION_BASE) +
                          ozone * OZONE_ABSORPTION;

        vec3 segmentScatter = rayleighScatter * rayleighP + mieScatter * mieP;

        vec3 attenuation = exp(-extinction * stepSize);
        inscatter += transmittance * segmentScatter * stepSize;
        transmittance *= attenuation;
    }

    return ScatteringResult(inscatter, transmittance);
}

vec3 applyAerialPerspective(vec3 color, vec3 viewDir, float viewDistance) {
    vec3 origin = vec3(0.0, PLANET_RADIUS + max(ubo.cameraPosition.y, 0.0), 0.0);
    ScatteringResult result = integrateAtmosphere(origin, normalize(viewDir), viewDistance, 12);

    vec3 sunLight = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 scatterLight = result.inscatter * (sunLight + vec3(0.02));

    float night = 1.0 - smoothstep(-0.05, 0.08, ubo.sunDirection.y);
    scatterLight += night * vec3(0.01, 0.015, 0.03) * (1.0 - result.transmittance);

    vec3 foggedColor = color * result.transmittance + scatterLight;
    return foggedColor;
}

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

// Compute TBN matrix from derivatives (cotangent frame)
mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invmax, B * invmax, N);
}

// Apply normal map to get perturbed normal
vec3 perturbNormal(vec3 N, vec3 V, vec2 texcoord, vec3 worldPos) {
    vec3 normalSample = texture(normalMap, texcoord).rgb;
    normalSample = normalSample * 2.0 - 1.0;
    mat3 TBN = cotangentFrame(N, worldPos, texcoord);
    return normalize(TBN * normalSample);
}

// Cascade debug colors
vec3 getCascadeDebugColor(int cascade) {
    const vec3 colors[4] = vec3[](
        vec3(1.0, 0.0, 0.0),  // Red - cascade 0 (closest)
        vec3(0.0, 1.0, 0.0),  // Green - cascade 1
        vec3(0.0, 0.0, 1.0),  // Blue - cascade 2
        vec3(1.0, 1.0, 0.0)   // Yellow - cascade 3 (farthest)
    );
    return colors[cascade];
}

// Select cascade based on view-space depth
int selectCascade(float viewDepth) {
    int cascade = 0;
    for (int i = 0; i < NUM_CASCADES - 1; i++) {
        if (viewDepth > ubo.cascadeSplits[i]) {
            cascade = i + 1;
        }
    }
    return cascade;
}

// Sample shadow for a specific cascade
float sampleShadowForCascade(vec3 worldPos, vec3 normal, vec3 lightDir, int cascade) {
    // Transform to light space for this cascade
    vec4 lightSpacePos = ubo.cascadeViewProj[cascade] * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Transform XY to [0,1] range for texture UV (Z is already [0,1] from Vulkan matrix)
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Check if outside shadow map
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;  // No shadow outside frustum
    }

    // Bias to reduce shadow acne (adjust per cascade - farther cascades need more bias)
    float cosTheta = max(dot(normal, lightDir), 0.0);
    float baseBias = 0.0005;
    float cascadeBias = baseBias * (1.0 + float(cascade) * 0.5);
    float slopeBias = cascadeBias * tan(acos(cosTheta));
    float bias = clamp(slopeBias, 0.0, 0.01);

    // PCF 3x3 sampling from array texture
    float shadow = 0.0;
    float texelSize = 1.0 / ubo.shadowMapSize;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(x, y) * texelSize;
            // Sample from texture array: vec4(u, v, layer, depth_compare)
            shadow += texture(shadowMapArray, vec4(projCoords.xy + offset, float(cascade), projCoords.z - bias));
        }
    }
    shadow /= 9.0;

    return shadow;
}

// Cascaded shadow calculation with blending between cascades
float calculateShadow(vec3 worldPos, vec3 normal, vec3 lightDir) {
    // Calculate view-space depth
    vec4 viewPos = ubo.view * vec4(worldPos, 1.0);
    float viewDepth = -viewPos.z;  // Negate because view space looks down -Z

    // Select cascade based on depth
    int cascade = selectCascade(viewDepth);

    // Sample shadow from the selected cascade
    float shadow = sampleShadowForCascade(worldPos, normal, lightDir, cascade);

    // Blend near cascade boundaries to prevent visible seams
    float blendDistance = 5.0;  // Blend over 5 units
    if (cascade < NUM_CASCADES - 1) {
        float splitDepth = ubo.cascadeSplits[cascade];
        float distToSplit = splitDepth - viewDepth;

        if (distToSplit < blendDistance && distToSplit > 0.0) {
            float nextShadow = sampleShadowForCascade(worldPos, normal, lightDir, cascade + 1);
            float blendFactor = smoothstep(0.0, blendDistance, distToSplit);
            shadow = mix(nextShadow, shadow, blendFactor);
        }
    }

    return shadow;
}

// Get cascade index for debug visualization (used in main)
int getCascadeForDebug(vec3 worldPos) {
    vec4 viewPos = ubo.view * vec4(worldPos, 1.0);
    float viewDepth = -viewPos.z;
    return selectCascade(viewDepth);
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

// Calculate point light contribution with distance attenuation
vec3 calculatePointLight(vec3 N, vec3 V, vec3 worldPos, vec3 albedo) {
    vec3 lightPos = ubo.pointLightPosition.xyz;
    float lightIntensity = ubo.pointLightPosition.w;
    vec3 lightColor = ubo.pointLightColor.rgb;
    float lightRadius = ubo.pointLightColor.a;

    // Skip if light is disabled (zero intensity)
    if (lightIntensity <= 0.0) return vec3(0.0);

    // Calculate light direction and distance
    vec3 lightVec = lightPos - worldPos;
    float distance = length(lightVec);
    vec3 L = normalize(lightVec);

    // Smooth attenuation that reaches zero at lightRadius
    float attenuation = 1.0;
    if (lightRadius > 0.0) {
        // Smooth windowed falloff using saturate(1 - (d/r)^4)^2
        // This provides a smooth transition to zero at the radius boundary
        float distRatio = distance / lightRadius;
        float distRatio4 = distRatio * distRatio * distRatio * distRatio;
        float windowedFalloff = max(1.0 - distRatio4, 0.0);
        windowedFalloff *= windowedFalloff;
        // Inverse square with smooth window
        attenuation = windowedFalloff / (distance * distance + 0.01);
    } else {
        // No radius specified, use simple inverse square
        attenuation = 1.0 / (distance * distance + 0.01);
    }

    // Calculate PBR lighting contribution (no shadow for point light)
    return calculatePBR(N, V, L, lightColor, lightIntensity * attenuation, albedo, 1.0);
}

void main() {
    vec3 geometricN = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Apply normal mapping
    vec3 N = perturbNormal(geometricN, V, fragTexCoord, fragWorldPos);

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

    // Point light contribution
    vec3 pointLight = calculatePointLight(N, V, fragWorldPos, albedo);

    vec3 finalColor = ambient + sunLight + moonLight + pointLight;

    // Add emissive glow
    vec3 emissive = albedo * material.emissiveIntensity;
    finalColor += emissive;

    vec3 cameraToFrag = fragWorldPos - ubo.cameraPosition.xyz;
    vec3 atmosphericColor = applyAerialPerspective(finalColor, cameraToFrag, length(cameraToFrag));

    // Debug cascade visualization overlay
    if (ubo.debugCascades > 0.5) {
        int cascade = getCascadeForDebug(fragWorldPos);
        vec3 cascadeColor = getCascadeDebugColor(cascade);
        atmosphericColor = mix(atmosphericColor, cascadeColor, 0.3);  // 30% tint
    }

    outColor = vec4(atmosphericColor, texColor.a);
}
