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
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float padding;
} ubo;

// Leaf states
const uint STATE_FALLING = 1;
const uint STATE_GROUNDED = 2;
const uint STATE_DISTURBED = 3;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) flat in uint fragLeafType;
layout(location = 4) flat in uint fragState;
layout(location = 5) in float fragDistToCamera;

layout(location = 0) out vec4 outColor;

// Leaf colors by type (autumn palette)
vec3 getLeafBaseColor(uint leafType) {
    if (leafType == 0u) {
        // Oak-like: Orange-brown
        return vec3(0.85, 0.45, 0.15);
    } else if (leafType == 1u) {
        // Maple-like: Red-orange
        return vec3(0.9, 0.25, 0.1);
    } else if (leafType == 2u) {
        // Aspen-like: Yellow-gold
        return vec3(0.95, 0.8, 0.2);
    } else {
        // Willow-like: Brown-green
        return vec3(0.55, 0.5, 0.25);
    }
}

// Create leaf shape mask from UV
float getLeafShape(vec2 uv, uint leafType) {
    vec2 centered = uv - 0.5;

    if (leafType == 0u) {
        // Oval shape (oak-like)
        float d = length(centered * vec2(1.0, 1.5));
        return 1.0 - smoothstep(0.35, 0.5, d);

    } else if (leafType == 1u) {
        // Pointed shape (maple-like) - 5 lobes
        float angle = atan(centered.y, centered.x);
        float r = length(centered);
        float lobes = 0.3 + 0.15 * cos(angle * 5.0);
        return 1.0 - smoothstep(lobes - 0.05, lobes + 0.05, r);

    } else if (leafType == 2u) {
        // Round shape (aspen-like)
        float d = length(centered);
        return 1.0 - smoothstep(0.35, 0.45, d);

    } else {
        // Long narrow shape (willow-like)
        float d = length(centered * vec2(2.5, 0.8));
        return 1.0 - smoothstep(0.35, 0.5, d);
    }
}

// Add vein pattern
float getVeinPattern(vec2 uv, uint leafType) {
    vec2 centered = uv - 0.5;

    // Central vein
    float centralVein = 1.0 - smoothstep(0.0, 0.02, abs(centered.x));

    // Side veins
    float sideVeins = 0.0;
    for (int i = 1; i <= 4; i++) {
        float y = float(i) * 0.1;
        float xOffset = centered.y * 0.3;  // Angle toward tip
        float vein = 1.0 - smoothstep(0.0, 0.015, abs(centered.y - y) + abs(centered.x - xOffset * float(i)) * 0.5);
        vein += 1.0 - smoothstep(0.0, 0.015, abs(centered.y + y) + abs(centered.x + xOffset * float(i)) * 0.5);
        sideVeins = max(sideVeins, vein * 0.5);
    }

    return centralVein * 0.3 + sideVeins * 0.2;
}

void main() {
    // Get leaf shape alpha
    float shape = getLeafShape(fragUV, fragLeafType);

    // Discard if outside leaf shape
    if (shape < 0.1) {
        discard;
    }

    // Get base color for this leaf type
    vec3 baseColor = getLeafBaseColor(fragLeafType);

    // Add some color variation
    float colorVar = sin(fragWorldPos.x * 10.0 + fragWorldPos.z * 7.0) * 0.1;
    baseColor = clamp(baseColor + colorVar, 0.0, 1.0);

    // Darken veins slightly
    float veins = getVeinPattern(fragUV, fragLeafType);
    baseColor = mix(baseColor, baseColor * 0.7, veins);

    // === LIGHTING ===
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Sun lighting
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    float sunIntensity = ubo.sunDirection.w;

    // Diffuse (Lambert)
    float NdotL = dot(normal, sunDir);
    float diffuse = max(NdotL, 0.0);

    // Translucency (light passing through leaf)
    float translucency = max(-NdotL, 0.0) * 0.4;  // Backlit effect
    vec3 transColor = baseColor * vec3(1.2, 1.1, 0.9);  // Warmer when backlit

    // Ambient
    vec3 ambient = ubo.ambientColor.rgb;

    // Combine lighting
    vec3 litColor = baseColor * (diffuse * sunIntensity * ubo.sunColor.rgb + ambient);
    litColor += transColor * translucency * sunIntensity * ubo.sunColor.rgb;

    // Moon lighting (subtle at night)
    float moonIntensity = ubo.moonDirection.w;
    if (moonIntensity > 0.01) {
        vec3 moonDir = normalize(ubo.moonDirection.xyz);
        float moonDiffuse = max(dot(normal, moonDir), 0.0);
        litColor += baseColor * moonDiffuse * moonIntensity * ubo.moonColor.rgb * 0.3;
    }

    // === DISTANCE FADE ===
    float fadeStart = 40.0;
    float fadeEnd = 60.0;
    float distanceFade = 1.0 - smoothstep(fadeStart, fadeEnd, fragDistToCamera);

    // Grounded leaves fade closer
    if (fragState == STATE_GROUNDED) {
        fadeStart = 25.0;
        fadeEnd = 35.0;
        distanceFade = 1.0 - smoothstep(fadeStart, fadeEnd, fragDistToCamera);
    }

    // Apply shape softening at edges
    float edgeSoftness = smoothstep(0.1, 0.3, shape);

    // Final alpha
    float alpha = edgeSoftness * distanceFade;

    // Slight desaturation for grounded leaves (they're drier/older)
    if (fragState == STATE_GROUNDED) {
        float lum = dot(litColor, vec3(0.299, 0.587, 0.114));
        litColor = mix(litColor, vec3(lum), 0.2);
        litColor *= 0.9;  // Slightly darker
    }

    outColor = vec4(litColor, alpha);
}
