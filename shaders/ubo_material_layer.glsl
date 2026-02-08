// Material Layer UBO - shader-side definition
// Part of the composable material system
// Matches MaterialLayerUBO in src/core/material/MaterialLayer.h

#ifndef UBO_MATERIAL_LAYER_GLSL
#define UBO_MATERIAL_LAYER_GLSL

#include "bindings.glsl"

#ifndef MATERIAL_LAYER_UBO_BINDING
#define MATERIAL_LAYER_UBO_BINDING BINDING_TERRAIN_MATERIAL_LAYER_UBO
#endif

// Maximum layers supported (must match MaterialLayerUBO::MAX_GPU_LAYERS)
#define MAX_MATERIAL_LAYERS 4

// Layer data structure (matches MaterialLayerUBO::LayerData)
struct LayerData {
    vec4 params0;   // mode, heightMin, heightMax, heightSoftness
    vec4 params1;   // slopeMin, slopeMax, slopeSoftness, opacity
    vec4 params2;   // noiseScale, noiseThreshold, noiseSoftness, invertBlend
    vec4 center;    // distanceCenter.xyz, distanceMin
    vec4 direction; // direction.xyz, distanceMax/directionalScale
};

layout(std140, binding = MATERIAL_LAYER_UBO_BINDING) uniform MaterialLayerUBO {
    LayerData materialLayers[MAX_MATERIAL_LAYERS];
    int numMaterialLayers;
    // Note: Use individual ints instead of int[3] array because std140
    // layout gives arrays a 16-byte stride per element, creating a size
    // mismatch with the C++ struct where ints are packed contiguously.
    int layerPadding0;
    int layerPadding1;
    int layerPadding2;
} materialLayerUbo;

#endif // UBO_MATERIAL_LAYER_GLSL
