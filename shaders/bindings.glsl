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
#define BINDING_BONE_MATRICES          12   // Bone matrices for skinned meshes

// =============================================================================
// Grass/Leaf System Descriptor Set
// Note: Grass/Leaf systems share bindings with main set for UBOs/textures
// but have their own instance and wind buffers
// =============================================================================
#define BINDING_GRASS_INSTANCE_BUFFER   1   // Grass instance SSBO
#define BINDING_GRASS_WIND_UBO          3   // Wind uniforms for grass (main pass)
#define BINDING_GRASS_SHADOW_WIND_UBO   2   // Wind uniforms for grass (shadow pass)
#define BINDING_GRASS_SNOW_MASK         5   // Snow mask (grass-specific binding)
#define BINDING_GRASS_CLOUD_SHADOW      6   // Cloud shadow map (grass-specific binding)

#define BINDING_LEAF_PARTICLE_BUFFER    1   // Leaf particle SSBO
#define BINDING_LEAF_WIND_UBO           2   // Wind uniforms for leaves

// =============================================================================
// Sky Shader Descriptor Set
// Note: Sky uses main UBO but has its own atmosphere LUT bindings
// =============================================================================
#define BINDING_SKY_TRANSMITTANCE_LUT   1   // Transmittance LUT 256x64
#define BINDING_SKY_MULTISCATTER_LUT    2   // Multi-scatter LUT 32x32
#define BINDING_SKY_SKYVIEW_LUT         3   // Sky-view LUT 192x108
#define BINDING_SKY_RAYLEIGH_IRR_LUT    4   // Rayleigh irradiance LUT
#define BINDING_SKY_MIE_IRR_LUT         5   // Mie irradiance LUT
#define BINDING_SKY_CLOUDMAP_LUT        6   // Cloud map paraboloid LUT

// =============================================================================
// Terrain Descriptor Set
// Note: Terrain uses a separate descriptor set with different layout
// =============================================================================
#define BINDING_TERRAIN_UBO             5   // Terrain uses binding 5 for main UBO

#endif // BINDINGS_GLSL
