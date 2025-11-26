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

layout(location = 0) in vec2 fragUV;
layout(location = 1) in float fragAlpha;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) flat in uint fragType;

layout(location = 0) out vec4 outColor;

void main() {
    // Early discard for zero alpha
    if (fragAlpha <= 0.0) {
        discard;
    }

    vec3 color;
    float alpha;

    if (fragType == 0u) {
        // RAIN: Blue-white streak
        // Create elongated rain drop shape
        vec2 uv = fragUV;

        // Distance from center line (vertical streak)
        float distFromCenter = abs(uv.x - 0.5) * 2.0;

        // Soft edges on the sides
        float sideAlpha = 1.0 - smoothstep(0.3, 1.0, distFromCenter);

        // Tapered ends
        float endAlpha = smoothstep(0.0, 0.1, uv.y) * smoothstep(1.0, 0.9, uv.y);

        // Combined shape
        alpha = sideAlpha * endAlpha * fragAlpha;

        // Rain color: slightly blue-tinted white
        vec3 baseColor = vec3(0.7, 0.8, 1.0);

        // Add highlight from sun/moon
        vec3 sunDir = normalize(ubo.sunDirection.xyz);
        float sunIntensity = ubo.sunDirection.w;

        // Backlit effect when rain is between camera and light
        vec3 viewDir = normalize(ubo.cameraPosition.xyz - fragWorldPos);
        float backlit = max(0.0, dot(viewDir, sunDir));
        backlit = pow(backlit, 4.0);

        // Mix in sun color for backlit drops
        color = mix(baseColor, ubo.sunColor.rgb * 1.5, backlit * 0.3 * sunIntensity);

        // Add ambient contribution
        color += ubo.ambientColor.rgb * 0.2;

        // Boost brightness slightly for visibility
        color *= 1.2;

    } else {
        // SNOW: Soft white flake
        vec2 uv = fragUV;
        vec2 centered = uv - 0.5;

        // Circular soft snowflake shape
        float dist = length(centered) * 2.0;
        float circleAlpha = 1.0 - smoothstep(0.5, 1.0, dist);

        // Add subtle 6-fold symmetry pattern
        float angle = atan(centered.y, centered.x);
        float symmetry = 0.8 + 0.2 * cos(angle * 6.0);

        alpha = circleAlpha * symmetry * fragAlpha;

        // Snow color: bright white with slight blue tint
        vec3 baseColor = vec3(0.95, 0.97, 1.0);

        // Ambient lighting
        color = baseColor * (ubo.ambientColor.rgb * 0.8 + vec3(0.2));

        // Sun contribution
        float sunIntensity = ubo.sunDirection.w;
        color += baseColor * ubo.sunColor.rgb * sunIntensity * 0.3;

        // Moon contribution at night
        float moonIntensity = ubo.moonDirection.w;
        color += baseColor * ubo.moonColor.rgb * moonIntensity * 0.15;

        // Subtle sparkle effect (based on viewing angle and time)
        vec3 viewDir = normalize(ubo.cameraPosition.xyz - fragWorldPos);
        float sparkle = pow(max(0.0, dot(viewDir, vec3(0.0, 1.0, 0.0))), 8.0);
        sparkle *= sin(fragWorldPos.x * 100.0 + fragWorldPos.z * 100.0) * 0.5 + 0.5;
        color += vec3(1.0) * sparkle * 0.2;
    }

    // Final output with alpha
    outColor = vec4(color, alpha);
}
