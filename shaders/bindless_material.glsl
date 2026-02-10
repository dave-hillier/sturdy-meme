// Bindless material sampling utilities
// Provides access to the global texture array and material data SSBO.
//
// Requires: GL_EXT_nonuniform_qualifier extension
// Requires: bindings.glsl included first

#ifndef BINDLESS_MATERIAL_GLSL
#define BINDLESS_MATERIAL_GLSL

#extension GL_EXT_nonuniform_qualifier : require

// Set 1: Global bindless texture array
layout(set = BINDLESS_TEXTURE_SET, binding = BINDLESS_TEXTURE_BINDING)
    uniform sampler2D globalTextures[];

// Set 2: Material data SSBO
struct MaterialData {
    uint albedoIndex;
    uint normalIndex;
    uint roughnessIndex;
    uint metallicIndex;
    uint aoIndex;
    uint heightIndex;
    uint emissiveIndex;
    uint _pad0;
    float roughness;
    float metallic;
    float emissiveStrength;
    float alphaCutoff;
};

layout(set = BINDLESS_MATERIAL_SET, binding = BINDLESS_MATERIAL_BINDING, std430)
    readonly buffer MaterialBuffer {
    MaterialData materialDataArray[];
};

// Sample a texture from the bindless array using nonuniform indexing
vec4 sampleBindlessTexture(uint textureIndex, vec2 uv) {
    return texture(globalTextures[nonuniformEXT(textureIndex)], uv);
}

// Get material data by index
MaterialData getBindlessMaterial(uint matIndex) {
    return materialDataArray[matIndex];
}

#endif // BINDLESS_MATERIAL_GLSL
