#version 450

// Tree impostor capture fragment shader
// Outputs G-buffer style data: albedo+alpha and normal+depth+AO

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragViewPos;

// Output: Two render targets for G-buffer impostor atlas
layout(location = 0) out vec4 outAlbedoAlpha;     // RGB = albedo, A = alpha
layout(location = 1) out vec4 outNormalDepthAO;   // RG = encoded normal, B = depth, A = AO

layout(binding = 0) uniform sampler2D albedoTex;
layout(binding = 1) uniform sampler2D normalTex;  // For AO extraction (optional)

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    mat4 model;
    vec4 captureParams; // x = cell index, y = is leaf pass (1.0), z = bounding radius, w = alpha test
} push;

// Octahedral normal encoding (compact 2-channel representation)
vec2 encodeOctahedral(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0) {
        vec2 signNotZero = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * signNotZero;
    }
    return n.xy * 0.5 + 0.5;  // Map to [0, 1]
}

void main() {
    vec4 albedo = texture(albedoTex, fragTexCoord);

    // Alpha test for leaves
    float alphaTest = push.captureParams.w;
    if (albedo.a < alphaTest) {
        discard;
    }

    // Encode normal (transform to view space for consistent lighting)
    vec3 normal = normalize(fragNormal);
    vec2 encodedNormal = encodeOctahedral(normal);

    // Compute normalized depth (0 = near, 1 = far within bounding sphere)
    // fragViewPos.z is in NDC space [-1, 1], remap to [0, 1]
    float depth = fragViewPos.z * 0.5 + 0.5;

    // Simple AO approximation (could sample from texture if available)
    float ao = 1.0;

    // Output G-buffer data
    outAlbedoAlpha = vec4(albedo.rgb, albedo.a);
    outNormalDepthAO = vec4(encodedNormal, depth, ao);
}
