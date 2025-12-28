#version 450

// Loading screen fragment shader - simple colored quad with gradient

layout(push_constant) uniform PushConstants {
    float time;
    float aspect;
    float progress;
    float _pad;
} pc;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    // Animated gradient based on time
    float pulse = 0.5 + 0.5 * sin(pc.time * 2.0);

    // Create a nice gradient from corner to corner
    float gradient = (fragTexCoord.x + fragTexCoord.y) * 0.5;

    // Base color: blue-ish theme
    vec3 color1 = vec3(0.1, 0.3, 0.6);  // Deep blue
    vec3 color2 = vec3(0.3, 0.6, 0.9);  // Light blue

    vec3 baseColor = mix(color1, color2, gradient);

    // Add pulsing brightness
    baseColor *= 0.8 + 0.2 * pulse;

    // Add edge glow effect
    float edgeDist = min(min(fragTexCoord.x, 1.0 - fragTexCoord.x),
                         min(fragTexCoord.y, 1.0 - fragTexCoord.y));
    float edge = 1.0 - smoothstep(0.0, 0.15, edgeDist);
    baseColor += vec3(0.2, 0.4, 0.7) * edge * pulse;

    outColor = vec4(baseColor, 1.0);
}
