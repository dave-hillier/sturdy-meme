// Shared struct definitions for GPU-driven tree impostor rendering
// Used by both tree_impostor_cull.comp and tree_impostor.vert

#ifndef TREE_IMPOSTOR_INSTANCE_GLSL
#define TREE_IMPOSTOR_INSTANCE_GLSL

// Visible impostor instance (output from compute shader, input to vertex shader)
// 48 bytes total for std430 alignment
struct ImpostorInstance {
    vec4 positionAndScale;     // xyz = world position, w = scale
    vec4 rotationAndArchetype; // x = rotation, y = archetype, z = blend factor, w = reserved
    vec4 sizeAndOffset;        // x = hSize, y = vSize, z = baseOffset, w = reserved
};

#endif // TREE_IMPOSTOR_INSTANCE_GLSL
