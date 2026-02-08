// Scene instance data for batched rendering
// Include this in instanced shaders to access per-instance data via gl_InstanceIndex
//
// Usage:
//   #include "scene_instance_common.glsl"
//   ...
//   SceneInstance inst = sceneInstances[gl_InstanceIndex];
//   mat4 model = inst.model;

#ifndef SCENE_INSTANCE_COMMON_GLSL
#define SCENE_INSTANCE_COMMON_GLSL

#include "bindings.glsl"

// Per-instance data for scene objects
// Must match GPUSceneInstanceData in GPUSceneBuffer.h (std430 layout)
struct SceneInstance {
    mat4 model;              // Model transform matrix (64 bytes)
    vec4 materialParams;     // x=roughness, y=metallic, z=emissiveIntensity, w=opacity
    vec4 emissiveColor;      // rgb=emissive color, a=unused
    uint pbrFlags;           // PBR texture flags bitmask
    float alphaTestThreshold;
    float hueShift;          // Hue shift for NPC tinting
    uint materialIndex;      // Index into material SSBO for bindless rendering
};

// Instance buffer (readonly for vertex/fragment shaders)
layout(std430, binding = BINDING_SCENE_INSTANCE_BUFFER) readonly buffer SceneInstanceBuffer {
    SceneInstance sceneInstances[];
};

// Helper to get material properties from instance
#define INSTANCE_ROUGHNESS(inst) (inst.materialParams.x)
#define INSTANCE_METALLIC(inst) (inst.materialParams.y)
#define INSTANCE_EMISSIVE_INTENSITY(inst) (inst.materialParams.z)
#define INSTANCE_OPACITY(inst) (inst.materialParams.w)

#endif // SCENE_INSTANCE_COMMON_GLSL
