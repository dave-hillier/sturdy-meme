#version 450

/*
 * terrain_wireframe.frag - Terrain wireframe visualization
 * Shows LOD levels with different colors for debugging
 */

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in float fragDepth;

layout(location = 0) out vec4 outColor;

// Color palette for LOD levels
vec3 lodColors[8] = vec3[](
    vec3(1.0, 0.0, 0.0),    // Level 0 - Red
    vec3(1.0, 0.5, 0.0),    // Level 1 - Orange
    vec3(1.0, 1.0, 0.0),    // Level 2 - Yellow
    vec3(0.0, 1.0, 0.0),    // Level 3 - Green
    vec3(0.0, 1.0, 1.0),    // Level 4 - Cyan
    vec3(0.0, 0.0, 1.0),    // Level 5 - Blue
    vec3(0.5, 0.0, 1.0),    // Level 6 - Purple
    vec3(1.0, 0.0, 1.0)     // Level 7+ - Magenta
);

void main() {
    // Simple diffuse lighting
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diff = max(dot(normal, lightDir), 0.0) * 0.7 + 0.3;

    // Get LOD color based on depth
    int lodIndex = int(fragDepth) % 8;
    vec3 lodColor = lodColors[lodIndex];

    // Output color with lighting
    outColor = vec4(lodColor * diff, 1.0);
}
