// Common push constants definition - include this in all shaders using material push constants
// This ensures the struct stays in sync between vertex and fragment shaders

#ifndef PUSH_CONSTANTS_COMMON_GLSL
#define PUSH_CONSTANTS_COMMON_GLSL

// PBR texture flags - indicates which optional PBR textures are bound
#define PBR_HAS_ROUGHNESS_MAP  (1u << 0)
#define PBR_HAS_METALLIC_MAP   (1u << 1)
#define PBR_HAS_AO_MAP         (1u << 2)
#define PBR_HAS_HEIGHT_MAP     (1u << 3)

layout(push_constant) uniform PushConstants {
    mat4 model;
    float roughness;
    float metallic;
    float emissiveIntensity;
    float opacity;  // For camera occlusion fading (1.0 = fully visible)
    vec4 emissiveColor;
    uint pbrFlags;  // Bitmask indicating which PBR textures are bound
    float _padding1;
    float _padding2;
    float _padding3;
} material;

#endif // PUSH_CONSTANTS_COMMON_GLSL
