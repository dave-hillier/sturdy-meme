#version 450

// Weather Particle Fragment Shader (Phase 4.3.9)
// With volumetric fog lighting integration

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "ubo_common.glsl"

// Froxel volume for fog lighting (Phase 4.3.9)
layout(binding = 3) uniform sampler3D froxelVolume;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in float fragAlpha;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) flat in uint fragType;

layout(location = 0) out vec4 outColor;

// Froxel parameters (must match FroxelSystem)
const uint FROXEL_DEPTH = 64;
const float FROXEL_FAR_PLANE = 200.0;
const float FROXEL_DEPTH_DIST = 1.2;

// Convert linear depth to froxel slice index
float depthToSlice(float linearDepth) {
    float normalized = clamp(linearDepth / FROXEL_FAR_PLANE, 0.0, 1.0);
    return log(1.0 + normalized * (pow(FROXEL_DEPTH_DIST, float(FROXEL_DEPTH)) - 1.0)) /
           log(FROXEL_DEPTH_DIST);
}

// Sample froxel fog lighting at world position
// Returns the in-scattered light from fog at this location
vec3 sampleFroxelFogLighting(vec3 worldPos) {
    // Transform to view space to get linear depth
    vec4 viewPos = ubo.view * vec4(worldPos, 1.0);
    float linearDepth = -viewPos.z;  // View space Z is negative

    // Skip if outside froxel range
    if (linearDepth > FROXEL_FAR_PLANE || linearDepth < 0.0) {
        return vec3(0.0);
    }

    // Transform to clip space for screen UV
    vec4 clipPos = ubo.proj * viewPos;
    vec2 ndc = clipPos.xy / clipPos.w;
    vec2 uv = ndc * 0.5 + 0.5;

    // Compute froxel W coordinate
    float sliceIndex = depthToSlice(linearDepth);
    float w = sliceIndex / float(FROXEL_DEPTH);

    // Sample froxel volume
    vec4 fogData = texture(froxelVolume, vec3(uv, w));

    // fogData format: RGB = L/alpha, A = alpha
    // Return the in-scattered light (normalized scatter * alpha)
    return fogData.rgb * fogData.a;
}

void main() {
    // Early discard for zero alpha
    if (fragAlpha <= 0.0) {
        discard;
    }

    vec3 color;
    float alpha;

    // Sample fog lighting at particle position
    vec3 fogLight = sampleFroxelFogLighting(fragWorldPos);

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

        // Add fog lighting contribution (rain scatters fog light)
        color += fogLight * 0.5;

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

        // Add fog lighting contribution (snow scatters fog light more diffusely)
        color += fogLight * 0.3;

        // Subtle sparkle effect (based on viewing angle and time)
        vec3 viewDir = normalize(ubo.cameraPosition.xyz - fragWorldPos);
        float sparkle = pow(max(0.0, dot(viewDir, vec3(0.0, 1.0, 0.0))), 8.0);
        sparkle *= sin(fragWorldPos.x * 100.0 + fragWorldPos.z * 100.0) * 0.5 + 0.5;
        color += vec3(1.0) * sparkle * 0.2;
    }

    // Final output with alpha
    outColor = vec4(color, alpha);
}
