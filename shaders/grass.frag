#version 450

const float PI = 3.14159265359;

// Atmospheric parameters (matched with main shader)
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
    mat4 lightSpaceMatrix;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;  // xyz = position, w = intensity
    vec4 pointLightColor;     // rgb = color, a = radius
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

// Material parameters for grass
const float GRASS_ROUGHNESS = 0.7;  // Grass is fairly rough/matte
const float GRASS_SSS_STRENGTH = 0.35;  // Subsurface scattering intensity
const float GRASS_SPECULAR_STRENGTH = 0.15;  // Subtle specular highlights

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

// Atmospheric scattering helpers
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
    // Include camera X/Z to avoid radial artifacts centered at world origin
    vec3 origin = vec3(ubo.cameraPosition.x, PLANET_RADIUS + max(ubo.cameraPosition.y, 0.0), ubo.cameraPosition.z);
    ScatteringResult result = integrateAtmosphere(origin, normalize(viewDir), viewDistance, 8);

    vec3 sunLight = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 scatterLight = result.inscatter * (sunLight + vec3(0.02));

    float night = 1.0 - smoothstep(-0.05, 0.08, ubo.sunDirection.y);
    scatterLight += night * vec3(0.01, 0.015, 0.03) * (1.0 - result.transmittance);

    vec3 foggedColor = color * result.transmittance + scatterLight;
    return foggedColor;
}

// GGX Distribution for specular highlights
float D_GGX(float NoH, float roughness) {
    float r = max(roughness, 0.04);
    float a = r * r;
    float a2 = a * a;
    float NoH2 = NoH * NoH;
    float denom = NoH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Fresnel-Schlick approximation
vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Subsurface scattering approximation for thin translucent materials like grass
vec3 calculateSSS(vec3 lightDir, vec3 viewDir, vec3 normal, vec3 lightColor, vec3 albedo) {
    // Light passing through the blade from behind
    vec3 scatterDir = normalize(lightDir + normal * 0.5);
    float sssAmount = pow(max(dot(viewDir, -scatterDir), 0.0), 3.0);

    // Color shift - light through grass gets more yellow-green
    vec3 sssColor = albedo * vec3(1.1, 1.2, 0.8);

    return sssColor * lightColor * sssAmount * GRASS_SSS_STRENGTH;
}

// Calculate point light contribution
vec3 calculatePointLight(vec3 N, vec3 V, vec3 worldPos, vec3 albedo) {
    vec3 lightPos = ubo.pointLightPosition.xyz;
    float lightIntensity = ubo.pointLightPosition.w;
    vec3 lightColor = ubo.pointLightColor.rgb;
    float lightRadius = ubo.pointLightColor.a;

    if (lightIntensity <= 0.0) return vec3(0.0);

    vec3 lightVec = lightPos - worldPos;
    float distance = length(lightVec);
    vec3 L = normalize(lightVec);

    // Windowed inverse-square falloff
    float attenuation = 1.0;
    if (lightRadius > 0.0) {
        float distRatio = distance / lightRadius;
        float windowedFalloff = max(1.0 - distRatio * distRatio, 0.0);
        windowedFalloff *= windowedFalloff;
        attenuation = windowedFalloff / (distance * distance + 0.01);
    } else {
        attenuation = 1.0 / (distance * distance + 0.01);
    }

    // Two-sided diffuse for grass
    float NdotL = dot(N, L);
    float diffuse = max(NdotL, 0.0) + max(-NdotL, 0.0) * 0.6;

    // Add SSS for point light
    vec3 sss = calculateSSS(L, V, N, lightColor, albedo);

    return (albedo * diffuse + sss) * lightColor * lightIntensity * attenuation;
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Apply subtle clump-based color variation
    // Different clumps have slightly different hue/saturation
    vec3 albedo = fragColor;

    // Subtle hue shift based on clumpId (shift toward yellow or blue-green)
    vec3 warmShift = vec3(0.05, 0.03, -0.02);    // Slightly warmer/yellower
    vec3 coolShift = vec3(-0.02, 0.02, 0.03);    // Slightly cooler/bluer
    vec3 colorShift = mix(coolShift, warmShift, fragClumpId);
    albedo += colorShift * CLUMP_COLOR_INFLUENCE;

    // Subtle brightness variation per clump
    float brightnessVar = 0.9 + fragClumpId * 0.2;  // 0.9 to 1.1
    albedo *= mix(1.0, brightnessVar, CLUMP_COLOR_INFLUENCE);

    // === SUN LIGHTING ===
    vec3 sunL = normalize(ubo.sunDirection.xyz);
    float shadow = calculateShadow(fragWorldPos, N, sunL);

    // Two-sided diffuse (grass blades are thin)
    float sunNdotL = dot(N, sunL);
    float sunDiffuse = max(sunNdotL, 0.0) + max(-sunNdotL, 0.0) * 0.6;

    // Specular highlight (subtle for grass)
    vec3 H = normalize(V + sunL);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    float D = D_GGX(NoH, GRASS_ROUGHNESS);
    vec3 F0 = vec3(0.04);  // Dielectric grass
    vec3 F = F_Schlick(VoH, F0);
    vec3 specular = D * F * GRASS_SPECULAR_STRENGTH;

    // Subsurface scattering - light through grass blades when backlit
    vec3 sss = calculateSSS(sunL, V, N, ubo.sunColor.rgb, albedo);

    vec3 sunLight = (albedo * sunDiffuse + specular + sss) * ubo.sunColor.rgb * ubo.sunDirection.w * shadow;

    // === MOON LIGHTING ===
    vec3 moonL = normalize(ubo.moonDirection.xyz);
    float moonNdotL = dot(N, moonL);
    float moonDiffuse = max(moonNdotL, 0.0) + max(-moonNdotL, 0.0) * 0.6;
    vec3 moonColor = vec3(0.3, 0.35, 0.5);
    vec3 moonLight = albedo * moonColor * moonDiffuse * ubo.moonDirection.w;

    // === POINT LIGHT ===
    vec3 pointLight = calculatePointLight(N, V, fragWorldPos, albedo);

    // === FRESNEL RIM LIGHTING ===
    // Grass blades catch light at grazing angles
    float NoV = max(dot(N, V), 0.0);
    float rimFresnel = pow(1.0 - NoV, 4.0);
    // Rim color based on sky/ambient
    vec3 rimColor = ubo.ambientColor.rgb * 0.5 + ubo.sunColor.rgb * ubo.sunDirection.w * 0.2;
    vec3 rimLight = rimColor * rimFresnel * 0.15;

    // === AMBIENT LIGHTING ===
    // Ambient occlusion - darker at base, brighter at tip
    float ao = 0.4 + fragHeight * 0.6;

    // Height-based ambient color shift (base is cooler/darker, tips catch more sky)
    vec3 ambientBase = ubo.ambientColor.rgb * vec3(0.8, 0.85, 0.9);  // Cooler at base
    vec3 ambientTip = ubo.ambientColor.rgb * vec3(1.0, 1.0, 0.95);   // Warmer at tip
    vec3 ambient = albedo * mix(ambientBase, ambientTip, fragHeight);

    // === COMBINE LIGHTING ===
    vec3 finalColor = (ambient + sunLight + moonLight + pointLight + rimLight) * ao;

    // === AERIAL PERSPECTIVE ===
    vec3 cameraToFrag = fragWorldPos - ubo.cameraPosition.xyz;
    float viewDistance = length(cameraToFrag);
    vec3 atmosphericColor = applyAerialPerspective(finalColor, cameraToFrag, viewDistance);

    outColor = vec4(atmosphericColor, 1.0);
}
