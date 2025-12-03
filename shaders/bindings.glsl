// Central binding definitions for the main rendering descriptor set
// Include this file in shaders to use consistent binding numbers
// Keep in sync with src/Bindings.h for C++ code

#ifndef BINDINGS_GLSL
#define BINDINGS_GLSL

// =============================================================================
// Main Rendering Descriptor Set (Set 0)
// =============================================================================

// Uniform Buffers
#define BINDING_UBO                     0   // UniformBufferObject - core rendering data
#define BINDING_SNOW_UBO               10   // SnowUBO - snow rendering parameters
#define BINDING_CLOUD_SHADOW_UBO       11   // CloudShadowUBO - cloud shadow parameters

// Textures
#define BINDING_DIFFUSE_TEX             1   // Diffuse/albedo texture
#define BINDING_SHADOW_MAP              2   // Cascaded shadow map
#define BINDING_NORMAL_MAP              3   // Normal map texture
#define BINDING_EMISSIVE_MAP            5   // Emissive texture
#define BINDING_POINT_SHADOW_MAP        6   // Point light shadow cubemap
#define BINDING_SPOT_SHADOW_MAP         7   // Spot light shadow map
#define BINDING_SNOW_MASK               8   // Snow coverage mask
#define BINDING_CLOUD_SHADOW_MAP        9   // Cloud shadow projection

// Storage Buffers
#define BINDING_LIGHT_BUFFER            4   // Point/spot light array

// =============================================================================
// Skinned Mesh Descriptor Set (extends main set)
// =============================================================================
#define BINDING_BONE_MATRICES          10   // Note: Shares binding 10 with SNOW_UBO
                                            // but in a different descriptor set layout

// =============================================================================
// Terrain Descriptor Set
// Note: Terrain uses a separate descriptor set with different layout
// =============================================================================
#define BINDING_TERRAIN_UBO             5   // Terrain uses binding 5 for main UBO

#endif // BINDINGS_GLSL
