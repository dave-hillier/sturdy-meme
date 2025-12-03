#version 450

const int NUM_CASCADES = 4;

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
    vec4 windDirectionAndSpeed;           // xy = direction, z = speed, w = time
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;       // 1.0 = show cascade colors
    float julianDay;           // Julian day for sidereal rotation
    float cloudStyle;
    float snowAmount;            // Global snow intensity (0-1)
    float snowRoughness;         // Snow surface roughness
    float snowTexScale;          // World-space snow texture scale
    vec4 snowColor;              // rgb = snow color, a = unused
    vec4 snowMaskParams;         // xy = mask origin, z = mask size, w = unused
    // Volumetric snow cascade parameters
    vec4 snowCascade0Params;     // xy = origin, z = size, w = texel size
    vec4 snowCascade1Params;     // xy = origin, z = size, w = texel size
    vec4 snowCascade2Params;     // xy = origin, z = size, w = texel size
    float useVolumetricSnow;     // 1.0 = use cascades, 0.0 = use legacy mask
    float snowMaxHeight;         // Maximum snow height in meters
    float debugSnowDepth;        // 1.0 = show depth visualization
    float snowPadding2;
    // Cloud shadow parameters
    mat4 cloudShadowMatrix;      // World XZ to cloud shadow UV transform
    float cloudShadowIntensity;  // How dark cloud shadows are (0-1)
    float cloudShadowEnabled;    // 1.0 = enabled, 0.0 = disabled
    float cloudShadowPadding1;
    float cloudShadowPadding2;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float roughness;
    float metallic;
    float emissiveIntensity;
    float opacity;  // For camera occlusion fading (1.0 = fully visible)
    vec4 emissiveColor;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
// Locations 4, 5 are bone indices/weights (unused in non-skinned path)
layout(location = 6) in vec4 inColor;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec4 fragTangent;
layout(location = 4) out vec4 fragColor;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;
    fragNormal = mat3(push.model) * inNormal;
    fragTexCoord = inTexCoord;
    fragWorldPos = worldPos.xyz;
    // Transform tangent direction by model matrix, preserve handedness in w
    fragTangent = vec4(mat3(push.model) * inTangent.xyz, inTangent.w);
    fragColor = inColor;
}
