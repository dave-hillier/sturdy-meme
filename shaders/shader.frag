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

// Height fog parameters (Phase 4.3 - Volumetric Haze)
const float FOG_BASE_HEIGHT = 0.0;        // Ground level
const float FOG_SCALE_HEIGHT = 50.0;      // Exponential falloff height in scene units
const float FOG_DENSITY = 0.015;          // Base fog density
const float FOG_LAYER_THICKNESS = 10.0;   // Low-lying fog layer thickness
const float FOG_LAYER_DENSITY = 0.03;     // Low-lying fog density

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[NUM_CASCADES];  // Per-cascade light matrices
    vec4 cascadeSplits;                   // View-space split depths
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 moonColor;                       // rgb = moon color
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
layout(binding = 5) uniform sampler2D emissiveMap;
layout(binding = 6) uniform samplerCubeArrayShadow pointShadowMaps;  // Point light cube shadow maps
layout(binding = 7) uniform sampler2DArrayShadow spotShadowMaps;     // Spot light shadow maps

// Light types
const uint LIGHT_TYPE_POINT = 0;
const uint LIGHT_TYPE_SPOT = 1;

// Maximum lights (must match CPU side)
const uint MAX_LIGHTS = 16;

// GPU light structure (must match CPU GPULight struct)
struct GPULight {
    vec4 positionAndType;    // xyz = position, w = type (0=point, 1=spot)
    vec4 directionAndCone;   // xyz = direction (for spot), w = outer cone angle (cos)
    vec4 colorAndIntensity;  // rgb = color, a = intensity
    vec4 radiusAndInnerCone; // x = radius, y = inner cone angle (cos), z = shadow map index (-1 = no shadow), w = padding
};

// Light buffer SSBO
layout(std430, binding = 4) readonly buffer LightBuffer {
    uvec4 lightCount;        // x = active light count
    GPULight lights[MAX_LIGHTS];
} lightBuffer;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float roughness;
    float metallic;
    float emissiveIntensity;
    float padding;
    vec4 emissiveColor;
} material;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec4 fragTangent;

layout(location = 0) out vec4 outColor;

struct ScatteringResult {
    vec3 inscatter;
    vec3 transmittance;
};

// Height fog density functions (from Phase 4.3.3)
// Exponential height falloff - good for general atmospheric haze
float exponentialHeightDensity(float height) {
    float relativeHeight = height - FOG_BASE_HEIGHT;
    return FOG_DENSITY * exp(-max(relativeHeight, 0.0) / FOG_SCALE_HEIGHT);
}

// Sigmoidal layer density - good for low-lying ground fog
float sigmoidalLayerDensity(float height) {
    float t = (height - FOG_BASE_HEIGHT) / FOG_LAYER_THICKNESS;
    // Smooth transition from full density below to zero above
    return FOG_LAYER_DENSITY / (1.0 + exp(t * 2.0));
}

// Combined fog density at a given height
float getHeightFogDensity(float height) {
    return exponentialHeightDensity(height) + sigmoidalLayerDensity(height);
}

// Analytically integrate exponential fog density along a ray
// Returns optical depth (for transmittance calculation)
float integrateExponentialFog(vec3 startPos, vec3 endPos) {
    float h0 = startPos.y;
    float h1 = endPos.y;
    float distance = length(endPos - startPos);

    if (distance < 0.001) return 0.0;

    float deltaH = h1 - h0;

    // For nearly horizontal rays, use simple density * distance
    if (abs(deltaH) < 0.01) {
        float avgHeight = (h0 + h1) * 0.5;
        return getHeightFogDensity(avgHeight) * distance;
    }

    // Analytical integration of exponential density along ray
    // ∫ density(h(t)) dt from 0 to distance
    float invScaleHeight = 1.0 / FOG_SCALE_HEIGHT;

    // Exponential fog component
    float expIntegral = FOG_DENSITY * FOG_SCALE_HEIGHT *
        abs(exp(-(max(h0 - FOG_BASE_HEIGHT, 0.0)) * invScaleHeight) -
            exp(-(max(h1 - FOG_BASE_HEIGHT, 0.0)) * invScaleHeight)) /
        max(abs(deltaH / distance), 0.001);

    // Sigmoidal component (approximate with average)
    float avgSigmoidal = (sigmoidalLayerDensity(h0) + sigmoidalLayerDensity(h1)) * 0.5;
    float sigIntegral = avgSigmoidal * distance;

    return expIntegral + sigIntegral;
}

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

// Apply height fog with in-scattering (Phase 4.3 volumetric haze)
vec3 applyHeightFog(vec3 color, vec3 cameraPos, vec3 fragPos, vec3 sunDir, vec3 sunColor) {
    vec3 viewDir = fragPos - cameraPos;
    float viewDistance = length(viewDir);
    viewDir = normalize(viewDir);

    // Calculate fog optical depth along the view ray
    float opticalDepth = integrateExponentialFog(cameraPos, fragPos);

    // Beer-Lambert transmittance
    float transmittance = exp(-opticalDepth);

    // In-scattering from sun (with Mie-like phase function for forward scattering)
    float cosTheta = dot(viewDir, sunDir);
    float phase = cornetteShanksPhase(cosTheta, 0.6);  // Slightly lower g for fog

    // Sun contribution modulated by height at midpoint
    vec3 midPoint = (cameraPos + fragPos) * 0.5;
    float midHeight = midPoint.y;

    // Sun visibility (above horizon and not blocked by terrain)
    float sunVisibility = smoothstep(-0.1, 0.1, sunDir.y);

    // Fog color: blend between sun-lit and ambient based on sun angle
    vec3 fogSunColor = sunColor * phase * sunVisibility;

    // Ambient sky light contribution (approximate hemisphere irradiance)
    float night = 1.0 - smoothstep(-0.05, 0.08, sunDir.y);
    vec3 ambientFog = mix(vec3(0.4, 0.5, 0.6), vec3(0.02, 0.03, 0.05), night);

    // Combined in-scatter (energy conserving)
    vec3 inScatter = (fogSunColor + ambientFog * 0.3) * (1.0 - transmittance);

    return color * transmittance + inScatter;
}

vec3 applyAerialPerspective(vec3 color, vec3 viewDir, float viewDistance) {
    vec3 cameraPos = ubo.cameraPosition.xyz;
    vec3 fragPos = cameraPos + viewDir;

    // Apply local height fog first (scene scale)
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    vec3 sunColor = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 fogged = applyHeightFog(color, cameraPos, fragPos, sunDir, sunColor);

    // Then apply large-scale atmospheric scattering (km scale)
    vec3 origin = vec3(0.0, PLANET_RADIUS + max(cameraPos.y, 0.0), 0.0);
    ScatteringResult result = integrateAtmosphere(origin, normalize(viewDir), viewDistance, 12);

    vec3 sunLight = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 scatterLight = result.inscatter * (sunLight + vec3(0.02));

    float night = 1.0 - smoothstep(-0.05, 0.08, ubo.sunDirection.y);
    scatterLight += night * vec3(0.01, 0.015, 0.03) * (1.0 - result.transmittance);

    // Combine: atmospheric scattering adds to fogged scene
    // Use reduced atmospheric effect since we're at scene scale
    float atmoBlend = clamp(viewDistance * 0.001, 0.0, 0.3);
    vec3 finalColor = mix(fogged, fogged * result.transmittance + scatterLight, atmoBlend);

    return finalColor;
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

// Apply normal map using vertex tangent to build TBN matrix
vec3 perturbNormal(vec3 N, vec4 tangent, vec2 texcoord) {
    vec3 T = normalize(tangent.xyz);
    // Re-orthogonalize T with respect to N
    T = normalize(T - dot(T, N) * N);
    // Compute bitangent using handedness stored in tangent.w
    vec3 B = cross(N, T) * tangent.w;

    mat3 TBN = mat3(T, B, N);

    vec3 normalSample = texture(normalMap, texcoord).rgb;
    normalSample = normalSample * 2.0 - 1.0;

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

// Calculate attenuation for a light with windowed falloff
float calculateAttenuation(float distance, float radius) {
    if (radius > 0.0) {
        // Windowed inverse-square falloff
        float distRatio = distance / radius;
        float windowedFalloff = max(1.0 - distRatio * distRatio, 0.0);
        windowedFalloff *= windowedFalloff;
        return windowedFalloff / (distance * distance + 0.01);
    } else {
        return 1.0 / (distance * distance + 0.01);
    }
}

// Calculate spot light cone falloff
float calculateSpotFalloff(vec3 L, vec3 spotDir, float innerCone, float outerCone) {
    float cosAngle = dot(-L, spotDir);
    // Smooth falloff between inner and outer cone
    return smoothstep(outerCone, innerCone, cosAngle);
}

// Calculate contribution from a single dynamic light (point or spot)
// Helper function to create a look-at matrix
mat4 lookAt(vec3 eye, vec3 center, vec3 up) {
    vec3 f = normalize(center - eye);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);

    mat4 result = mat4(1.0);
    result[0][0] = s.x;
    result[1][0] = s.y;
    result[2][0] = s.z;
    result[0][1] = u.x;
    result[1][1] = u.y;
    result[2][1] = u.z;
    result[0][2] = -f.x;
    result[1][2] = -f.y;
    result[2][2] = -f.z;
    result[3][0] = -dot(s, eye);
    result[3][1] = -dot(u, eye);
    result[3][2] = dot(f, eye);
    return result;
}

// Helper function to create a perspective matrix
mat4 perspective(float fovy, float aspect, float near, float far) {
    float tanHalfFovy = tan(fovy / 2.0);

    mat4 result = mat4(0.0);
    result[0][0] = 1.0 / (aspect * tanHalfFovy);
    result[1][1] = 1.0 / tanHalfFovy;
    result[2][2] = -(far + near) / (far - near);
    result[2][3] = -1.0;
    result[3][2] = -(2.0 * far * near) / (far - near);
    return result;
}

// Sample dynamic light shadow map
float sampleDynamicShadow(GPULight light, vec3 worldPos) {
    int shadowIndex = int(light.radiusAndInnerCone.z);

    // No shadow if index is -1
    if (shadowIndex < 0) return 1.0;

    uint lightType = uint(light.positionAndType.w);
    vec3 lightPos = light.positionAndType.xyz;

    if (lightType == LIGHT_TYPE_POINT) {
        // Point light - sample cube map
        vec3 fragToLight = worldPos - lightPos;
        float currentDepth = length(fragToLight);
        float radius = light.radiusAndInnerCone.x;

        // Normalize depth to [0,1] range based on light radius
        float normalizedDepth = currentDepth / radius;

        // Sample cube shadow map
        vec4 shadowCoord = vec4(normalize(fragToLight), float(shadowIndex));
        float shadow = texture(pointShadowMaps, shadowCoord, normalizedDepth);

        return shadow;
    }
    else {  // LIGHT_TYPE_SPOT
        // Spot light - sample 2D shadow map
        vec3 spotDir = normalize(light.directionAndCone.xyz);

        // Create light view-projection matrix
        mat4 lightView = lookAt(lightPos, lightPos + spotDir, vec3(0.0, 1.0, 0.0));
        float outerCone = light.directionAndCone.w;
        float fov = acos(outerCone) * 2.0;
        mat4 lightProj = perspective(fov, 1.0, 0.1, light.radiusAndInnerCone.x);

        // Transform world position to light clip space
        vec4 lightSpacePos = lightProj * lightView * vec4(worldPos, 1.0);
        vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

        // Transform to [0,1] range
        projCoords = projCoords * 0.5 + 0.5;

        // Sample shadow map
        vec4 shadowCoord = vec4(projCoords.xy, float(shadowIndex), projCoords.z);
        float shadow = texture(spotShadowMaps, shadowCoord);

        return shadow;
    }
}

vec3 calculateDynamicLight(GPULight light, vec3 N, vec3 V, vec3 worldPos, vec3 albedo) {
    vec3 lightPos = light.positionAndType.xyz;
    uint lightType = uint(light.positionAndType.w);
    vec3 lightColor = light.colorAndIntensity.rgb;
    float lightIntensity = light.colorAndIntensity.a;
    float lightRadius = light.radiusAndInnerCone.x;

    // Skip if light is disabled
    if (lightIntensity <= 0.0) return vec3(0.0);

    // Calculate light direction and distance
    vec3 lightVec = lightPos - worldPos;
    float distance = length(lightVec);
    vec3 L = normalize(lightVec);

    // Early out if beyond radius
    if (lightRadius > 0.0 && distance > lightRadius) return vec3(0.0);

    // Calculate attenuation
    float attenuation = calculateAttenuation(distance, lightRadius);

    // For spot lights, apply cone falloff
    if (lightType == LIGHT_TYPE_SPOT) {
        vec3 spotDir = normalize(light.directionAndCone.xyz);
        float outerCone = light.directionAndCone.w;
        float innerCone = light.radiusAndInnerCone.y;
        float spotFalloff = calculateSpotFalloff(L, spotDir, innerCone, outerCone);
        attenuation *= spotFalloff;
    }

    // Sample shadow map
    float shadow = sampleDynamicShadow(light, worldPos);
    attenuation *= shadow;

    // Calculate PBR lighting contribution with shadow
    return calculatePBR(N, V, L, lightColor, lightIntensity * attenuation, albedo, 1.0);
}

// Calculate contribution from all dynamic lights
vec3 calculateAllDynamicLights(vec3 N, vec3 V, vec3 worldPos, vec3 albedo) {
    vec3 totalLight = vec3(0.0);
    uint numLights = min(lightBuffer.lightCount.x, MAX_LIGHTS);

    for (uint i = 0; i < numLights; i++) {
        totalLight += calculateDynamicLight(lightBuffer.lights[i], N, V, worldPos, albedo);
    }

    return totalLight;
}

void main() {
    vec3 geometricN = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Enable normal mapping for debugging
    vec3 N = perturbNormal(geometricN, fragTangent, fragTexCoord);

    vec4 texColor = texture(texSampler, fragTexCoord);
    vec3 albedo = texColor.rgb;

    // Calculate shadow for sun
    vec3 sunL = normalize(ubo.sunDirection.xyz);
    float shadow = calculateShadow(fragWorldPos, N, sunL);

    // Sun lighting with shadow
    float sunIntensity = ubo.sunDirection.w;
    vec3 sunLight = calculatePBR(N, V, sunL, ubo.sunColor.rgb, sunIntensity, albedo, shadow);

    // Moon lighting (no shadow - moon is soft fill light but becomes primary light at night)
    vec3 moonL = normalize(ubo.moonDirection.xyz);
    float moonIntensity = ubo.moonDirection.w;
    vec3 moonLight = calculatePBR(N, V, moonL, ubo.moonColor.rgb, moonIntensity, albedo, 1.0);

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

    // Dynamic lights contribution (multiple point and spot lights)
    vec3 dynamicLights = calculateAllDynamicLights(N, V, fragWorldPos, albedo);

    vec3 finalColor = ambient + sunLight + moonLight + dynamicLights;

    // Add emissive glow from emissive map + material emissive color
    vec3 emissiveMapSample = texture(emissiveMap, fragTexCoord).rgb;
    float emissiveMapLum = dot(emissiveMapSample, vec3(0.299, 0.587, 0.114));
    // Use emissive map color if present, otherwise use material emissive color
    vec3 emissiveColor = emissiveMapLum > 0.01
        ? emissiveMapSample * material.emissiveColor.rgb
        : mix(albedo, material.emissiveColor.rgb, material.emissiveColor.a);
    vec3 emissive = emissiveColor * material.emissiveIntensity;
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
