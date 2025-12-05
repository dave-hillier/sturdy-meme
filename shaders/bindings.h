// Single source of truth for shader bindings
// This file is designed to be included by both C++ and GLSL code
// C++: #include "shaders/bindings.h"
// GLSL: #include "bindings.h"

#ifndef BINDINGS_H
#define BINDINGS_H

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

// PBR Material Textures (optional, for Substance/PBR materials)
#define BINDING_ROUGHNESS_MAP          13   // Per-pixel roughness (linear, R channel)
#define BINDING_METALLIC_MAP           14   // Per-pixel metallic (linear, R channel)
#define BINDING_AO_MAP                 15   // Ambient occlusion (linear, R channel)
#define BINDING_HEIGHT_MAP             16   // Height/displacement map (linear, R channel)

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
#define BINDING_TERRAIN_CBT_BUFFER      0   // CBT buffer (SSBO)
#define BINDING_TERRAIN_DISPATCH        1   // Dispatch indirect buffer
#define BINDING_TERRAIN_DRAW            2   // Draw indirect buffer
#define BINDING_TERRAIN_HEIGHT_MAP      3   // Height map texture
#define BINDING_TERRAIN_UBO             4   // Terrain uniforms
#define BINDING_TERRAIN_VISIBLE_INDICES 5   // Visible indices buffer
#define BINDING_TERRAIN_CULL_DISPATCH   6   // Cull dispatch indirect buffer
// Fragment shader textures
#define BINDING_TERRAIN_ALBEDO          6   // Terrain albedo texture
#define BINDING_TERRAIN_SHADOW_MAP      7   // Shadow map array
#define BINDING_TERRAIN_FAR_LOD_GRASS   8   // Far LOD grass texture
#define BINDING_TERRAIN_SNOW_MASK       9   // Legacy snow mask
#define BINDING_TERRAIN_SNOW_CASCADE_0 10   // Snow cascade 0
#define BINDING_TERRAIN_SNOW_CASCADE_1 11   // Snow cascade 1
#define BINDING_TERRAIN_SNOW_CASCADE_2 12   // Snow cascade 2
#define BINDING_TERRAIN_CLOUD_SHADOW   13   // Cloud shadow map

// Shadow culling (compute pass - uses terrain compute descriptor set)
#define BINDING_TERRAIN_SHADOW_VISIBLE 14   // Shadow visible indices buffer
#define BINDING_TERRAIN_SHADOW_DRAW    15   // Shadow indirect draw buffer
// Hole mask for caves/wells (R8_UNORM: 0=solid, 1=hole)
#define BINDING_TERRAIN_HOLE_MASK      16   // Hole mask texture
// Terrain-specific UBO bindings (separate from textures)
#define BINDING_TERRAIN_SNOW_UBO       17   // Snow UBO for terrain
#define BINDING_TERRAIN_CLOUD_SHADOW_UBO 18 // Cloud shadow UBO for terrain

// =============================================================================
// Grass Compute Shader Descriptor Set
// =============================================================================
#define BINDING_GRASS_COMPUTE_INSTANCES    0   // Grass instance buffer (output)
#define BINDING_GRASS_COMPUTE_INDIRECT     1   // Indirect draw buffer
#define BINDING_GRASS_COMPUTE_UNIFORMS     2   // Grass uniforms
#define BINDING_GRASS_COMPUTE_HEIGHT_MAP   3   // Terrain height map
#define BINDING_GRASS_COMPUTE_DISPLACEMENT 4   // Displacement map

// =============================================================================
// Grass Displacement Compute Shader Descriptor Set
// =============================================================================
#define BINDING_GRASS_DISP_OUTPUT          0   // Displacement output image
#define BINDING_GRASS_DISP_SOURCE          1   // Source buffer
#define BINDING_GRASS_DISP_UNIFORMS        2   // Displacement uniforms

// =============================================================================
// Leaf Compute Shader Descriptor Set
// =============================================================================
#define BINDING_LEAF_COMPUTE_INPUT         0   // Input particle buffer
#define BINDING_LEAF_COMPUTE_OUTPUT        1   // Output particle buffer
#define BINDING_LEAF_COMPUTE_INDIRECT      2   // Indirect draw buffer
#define BINDING_LEAF_COMPUTE_UNIFORMS      3   // Leaf uniforms
#define BINDING_LEAF_COMPUTE_WIND          4   // Wind uniforms
#define BINDING_LEAF_COMPUTE_HEIGHT_MAP    5   // Terrain height map
#define BINDING_LEAF_COMPUTE_DISPLACEMENT  6   // Displacement map
#define BINDING_LEAF_COMPUTE_DISP_REGION   7   // Displacement region

// =============================================================================
// Weather System Descriptor Set
// =============================================================================
#define BINDING_WEATHER_PARTICLES          0   // Particle buffer (SSBO)
#define BINDING_WEATHER_INDIRECT           1   // Indirect draw buffer
#define BINDING_WEATHER_UBO                2   // Weather UBO (main uniforms)
#define BINDING_WEATHER_UNIFORMS           3   // Weather-specific uniforms
#define BINDING_WEATHER_WIND               4   // Wind uniforms
#define BINDING_WEATHER_FROXEL             3   // Froxel volume (fragment shader)

// =============================================================================
// Post-Process Descriptor Set
// =============================================================================
#define BINDING_PP_HDR_INPUT               0   // HDR input texture
#define BINDING_PP_UNIFORMS                1   // Post-process uniforms
#define BINDING_PP_DEPTH                   2   // Depth buffer
#define BINDING_PP_FROXEL                  3   // Froxel volume
#define BINDING_PP_BLOOM                   4   // Bloom texture

// =============================================================================
// Bloom Pass Descriptor Set
// =============================================================================
#define BINDING_BLOOM_INPUT                0   // Input texture
#define BINDING_BLOOM_SECONDARY            1   // Secondary texture (composite)

// =============================================================================
// Histogram Descriptor Set
// =============================================================================
#define BINDING_HISTOGRAM_IMAGE            0   // HDR input image (histogram build)
#define BINDING_HISTOGRAM_BUFFER           1   // Histogram buffer (build) / input (reduce)
#define BINDING_HISTOGRAM_PARAMS           2   // Histogram params
#define BINDING_HISTOGRAM_EXPOSURE         3   // Exposure buffer (reduce output)

// =============================================================================
// Hi-Z Descriptor Set
// =============================================================================
#define BINDING_HIZ_SRC_DEPTH              0   // Source depth buffer
#define BINDING_HIZ_SRC_MIP                1   // Previous mip level
#define BINDING_HIZ_DST_MIP                2   // Destination mip (image)
#define BINDING_HIZ_CULL_UNIFORMS          0   // Culling uniforms
#define BINDING_HIZ_CULL_OBJECTS           1   // Object data buffer
#define BINDING_HIZ_CULL_INDIRECT          2   // Indirect draw buffer
#define BINDING_HIZ_CULL_COUNT             3   // Draw count buffer
#define BINDING_HIZ_CULL_PYRAMID           4   // Hi-Z pyramid sampler

// =============================================================================
// Atmosphere LUT Compute Descriptor Set
// =============================================================================
#define BINDING_ATMO_OUTPUT_LUT            0   // Output LUT image
#define BINDING_ATMO_TRANSMITTANCE         1   // Transmittance LUT (input)
#define BINDING_ATMO_MULTISCATTER          2   // Multi-scatter LUT (input) / uniforms for multiscatter
#define BINDING_ATMO_UNIFORMS              3   // Atmosphere uniforms

// Irradiance LUT specific (uses separate output images)
#define BINDING_IRRADIANCE_RAYLEIGH_OUT    0   // Rayleigh irradiance output
#define BINDING_IRRADIANCE_MIE_OUT         1   // Mie irradiance output
#define BINDING_IRRADIANCE_TRANSMITTANCE   2   // Transmittance LUT input
#define BINDING_IRRADIANCE_UNIFORMS        3   // Atmosphere uniforms

// =============================================================================
// Cloud Map LUT Descriptor Set
// =============================================================================
#define BINDING_CLOUDMAP_OUTPUT            0   // Output cloud map image
#define BINDING_CLOUDMAP_UNIFORMS          1   // Cloud map uniforms

// =============================================================================
// Cloud Shadow Descriptor Set
// =============================================================================
#define BINDING_CLOUD_SHADOW_OUTPUT        0   // Output shadow map image
#define BINDING_CLOUD_SHADOW_CLOUDMAP      1   // Cloud map input
#define BINDING_CLOUD_SHADOW_UNIFORMS      2   // Cloud shadow uniforms

// =============================================================================
// Snow Accumulation Descriptor Set
// =============================================================================
#define BINDING_SNOW_MASK_OUTPUT           0   // Output snow mask image
#define BINDING_SNOW_MASK_UNIFORMS         1   // Snow mask uniforms
#define BINDING_SNOW_MASK_INTERACTIONS     2   // Snow interaction buffer

// =============================================================================
// Volumetric Snow Descriptor Set
// =============================================================================
#define BINDING_VOL_SNOW_CASCADE_0         0   // Cascade 0 output image
#define BINDING_VOL_SNOW_CASCADE_1         1   // Cascade 1 output image
#define BINDING_VOL_SNOW_CASCADE_2         2   // Cascade 2 output image
#define BINDING_VOL_SNOW_UNIFORMS          3   // Volumetric snow uniforms
#define BINDING_VOL_SNOW_INTERACTIONS      4   // Interaction buffer

// =============================================================================
// Froxel Update Descriptor Set
// =============================================================================
#define BINDING_FROXEL_SCATTERING          0   // Current frame scattering volume (write)
#define BINDING_FROXEL_INTEGRATED          1   // Integrated volume (used by integration pass)
#define BINDING_FROXEL_UNIFORMS            2   // Froxel uniforms UBO
#define BINDING_FROXEL_SHADOW              3   // Shadow map array
#define BINDING_FROXEL_LIGHTS              4   // Light buffer SSBO
#define BINDING_FROXEL_HISTORY             5   // Previous frame history volume (read)

// =============================================================================
// Catmull-Clark Subdivision Descriptor Set
// =============================================================================
#define BINDING_CC_SCENE_UBO               0   // Scene uniforms
#define BINDING_CC_CBT_BUFFER              1   // CBT buffer
#define BINDING_CC_VERTEX_BUFFER           2   // Vertex buffer
#define BINDING_CC_HALFEDGE_BUFFER         3   // Halfedge buffer
#define BINDING_CC_FACE_BUFFER             4   // Face buffer

// =============================================================================
// Water System Descriptor Set
// =============================================================================
#define BINDING_WATER_UBO                  1   // Water uniforms
#define BINDING_WATER_SHADOW_MAP           2   // Shadow map array
#define BINDING_WATER_TERRAIN_HEIGHT       3   // Terrain height map
#define BINDING_WATER_FLOW_MAP             4   // Flow map
#define BINDING_WATER_DISPLACEMENT         5   // Displacement map
#define BINDING_WATER_FOAM_NOISE           6   // Foam noise texture
#define BINDING_WATER_TEMPORAL_FOAM        7   // Temporal foam map
#define BINDING_WATER_CAUSTICS             8   // Caustics texture
#define BINDING_WATER_SSR                  9   // SSR texture
#define BINDING_WATER_SCENE_DEPTH         10   // Scene depth texture

// =============================================================================
// Water Displacement Compute Descriptor Set
// =============================================================================
#define BINDING_WATER_DISP_OUTPUT          0   // Displacement output image
#define BINDING_WATER_DISP_PREV            1   // Previous displacement map
#define BINDING_WATER_DISP_PARTICLES       2   // Splash particle buffer

// =============================================================================
// Water Tile Cull Compute Descriptor Set
// =============================================================================
#define BINDING_WATER_CULL_DEPTH           0   // Depth texture
#define BINDING_WATER_CULL_TILES           1   // Tile buffer output
#define BINDING_WATER_CULL_COUNTER         2   // Visible count buffer
#define BINDING_WATER_CULL_INDIRECT        3   // Indirect draw buffer

// =============================================================================
// Foam Blur Compute Descriptor Set
// =============================================================================
#define BINDING_FOAM_OUTPUT                0   // Foam buffer output
#define BINDING_FOAM_INPUT                 1   // Previous foam buffer
#define BINDING_FOAM_FLOW_MAP              2   // Flow map for advection
#define BINDING_FOAM_WAKE_DATA             3   // Wake source data

// =============================================================================
// SSR Compute Descriptor Set
// =============================================================================
#define BINDING_SSR_COLOR                  0   // Color texture input
#define BINDING_SSR_DEPTH                  1   // Depth texture input
#define BINDING_SSR_OUTPUT                 2   // SSR output image
#define BINDING_SSR_PREV                   3   // Previous frame SSR

// =============================================================================
// Tree Shader Descriptor Set
// =============================================================================
#define BINDING_TREE_BARK_COLOR            1   // Bark color texture
#define BINDING_TREE_BARK_NORMAL           2   // Bark normal map
#define BINDING_TREE_BARK_AO               3   // Bark ambient occlusion
#define BINDING_TREE_BARK_ROUGHNESS        4   // Bark roughness map
#define BINDING_TREE_LEAF                  5   // Leaf texture

// =============================================================================
// C++ Type-Safe Wrappers
// =============================================================================
#ifdef __cplusplus

#include <cstdint>

namespace Bindings {

// Main Rendering Descriptor Set
constexpr uint32_t UBO                    = BINDING_UBO;
constexpr uint32_t SNOW_UBO               = BINDING_SNOW_UBO;
constexpr uint32_t CLOUD_SHADOW_UBO       = BINDING_CLOUD_SHADOW_UBO;
constexpr uint32_t DIFFUSE_TEX            = BINDING_DIFFUSE_TEX;
constexpr uint32_t SHADOW_MAP             = BINDING_SHADOW_MAP;
constexpr uint32_t NORMAL_MAP             = BINDING_NORMAL_MAP;
constexpr uint32_t EMISSIVE_MAP           = BINDING_EMISSIVE_MAP;
constexpr uint32_t POINT_SHADOW_MAP       = BINDING_POINT_SHADOW_MAP;
constexpr uint32_t SPOT_SHADOW_MAP        = BINDING_SPOT_SHADOW_MAP;
constexpr uint32_t SNOW_MASK              = BINDING_SNOW_MASK;
constexpr uint32_t CLOUD_SHADOW_MAP       = BINDING_CLOUD_SHADOW_MAP;
constexpr uint32_t LIGHT_BUFFER           = BINDING_LIGHT_BUFFER;
constexpr uint32_t BONE_MATRICES          = BINDING_BONE_MATRICES;

// PBR Material Textures
constexpr uint32_t ROUGHNESS_MAP          = BINDING_ROUGHNESS_MAP;
constexpr uint32_t METALLIC_MAP           = BINDING_METALLIC_MAP;
constexpr uint32_t AO_MAP                 = BINDING_AO_MAP;
constexpr uint32_t HEIGHT_MAP             = BINDING_HEIGHT_MAP;

// Grass/Leaf System
constexpr uint32_t GRASS_INSTANCE_BUFFER  = BINDING_GRASS_INSTANCE_BUFFER;
constexpr uint32_t GRASS_WIND_UBO         = BINDING_GRASS_WIND_UBO;
constexpr uint32_t GRASS_SHADOW_WIND_UBO  = BINDING_GRASS_SHADOW_WIND_UBO;
constexpr uint32_t GRASS_SNOW_MASK        = BINDING_GRASS_SNOW_MASK;
constexpr uint32_t GRASS_CLOUD_SHADOW     = BINDING_GRASS_CLOUD_SHADOW;
constexpr uint32_t LEAF_PARTICLE_BUFFER   = BINDING_LEAF_PARTICLE_BUFFER;
constexpr uint32_t LEAF_WIND_UBO          = BINDING_LEAF_WIND_UBO;

// Sky Shader
constexpr uint32_t SKY_TRANSMITTANCE_LUT  = BINDING_SKY_TRANSMITTANCE_LUT;
constexpr uint32_t SKY_MULTISCATTER_LUT   = BINDING_SKY_MULTISCATTER_LUT;
constexpr uint32_t SKY_SKYVIEW_LUT        = BINDING_SKY_SKYVIEW_LUT;
constexpr uint32_t SKY_RAYLEIGH_IRR_LUT   = BINDING_SKY_RAYLEIGH_IRR_LUT;
constexpr uint32_t SKY_MIE_IRR_LUT        = BINDING_SKY_MIE_IRR_LUT;
constexpr uint32_t SKY_CLOUDMAP_LUT       = BINDING_SKY_CLOUDMAP_LUT;

// Terrain
constexpr uint32_t TERRAIN_CBT_BUFFER     = BINDING_TERRAIN_CBT_BUFFER;
constexpr uint32_t TERRAIN_DISPATCH       = BINDING_TERRAIN_DISPATCH;
constexpr uint32_t TERRAIN_DRAW           = BINDING_TERRAIN_DRAW;
constexpr uint32_t TERRAIN_HEIGHT_MAP     = BINDING_TERRAIN_HEIGHT_MAP;
constexpr uint32_t TERRAIN_UBO            = BINDING_TERRAIN_UBO;
constexpr uint32_t TERRAIN_VISIBLE_INDICES = BINDING_TERRAIN_VISIBLE_INDICES;
constexpr uint32_t TERRAIN_CULL_DISPATCH  = BINDING_TERRAIN_CULL_DISPATCH;
constexpr uint32_t TERRAIN_ALBEDO         = BINDING_TERRAIN_ALBEDO;
constexpr uint32_t TERRAIN_SHADOW_MAP     = BINDING_TERRAIN_SHADOW_MAP;
constexpr uint32_t TERRAIN_FAR_LOD_GRASS  = BINDING_TERRAIN_FAR_LOD_GRASS;
constexpr uint32_t TERRAIN_SNOW_MASK      = BINDING_TERRAIN_SNOW_MASK;
constexpr uint32_t TERRAIN_SNOW_CASCADE_0 = BINDING_TERRAIN_SNOW_CASCADE_0;
constexpr uint32_t TERRAIN_SNOW_CASCADE_1 = BINDING_TERRAIN_SNOW_CASCADE_1;
constexpr uint32_t TERRAIN_SNOW_CASCADE_2 = BINDING_TERRAIN_SNOW_CASCADE_2;
constexpr uint32_t TERRAIN_CLOUD_SHADOW   = BINDING_TERRAIN_CLOUD_SHADOW;
constexpr uint32_t TERRAIN_SHADOW_VISIBLE = BINDING_TERRAIN_SHADOW_VISIBLE;
constexpr uint32_t TERRAIN_SHADOW_DRAW    = BINDING_TERRAIN_SHADOW_DRAW;
constexpr uint32_t TERRAIN_HOLE_MASK      = BINDING_TERRAIN_HOLE_MASK;
constexpr uint32_t TERRAIN_SNOW_UBO       = BINDING_TERRAIN_SNOW_UBO;
constexpr uint32_t TERRAIN_CLOUD_SHADOW_UBO = BINDING_TERRAIN_CLOUD_SHADOW_UBO;

// Grass Compute
constexpr uint32_t GRASS_COMPUTE_INSTANCES   = BINDING_GRASS_COMPUTE_INSTANCES;
constexpr uint32_t GRASS_COMPUTE_INDIRECT    = BINDING_GRASS_COMPUTE_INDIRECT;
constexpr uint32_t GRASS_COMPUTE_UNIFORMS    = BINDING_GRASS_COMPUTE_UNIFORMS;
constexpr uint32_t GRASS_COMPUTE_HEIGHT_MAP  = BINDING_GRASS_COMPUTE_HEIGHT_MAP;
constexpr uint32_t GRASS_COMPUTE_DISPLACEMENT = BINDING_GRASS_COMPUTE_DISPLACEMENT;

// Grass Displacement
constexpr uint32_t GRASS_DISP_OUTPUT      = BINDING_GRASS_DISP_OUTPUT;
constexpr uint32_t GRASS_DISP_SOURCE      = BINDING_GRASS_DISP_SOURCE;
constexpr uint32_t GRASS_DISP_UNIFORMS    = BINDING_GRASS_DISP_UNIFORMS;

// Leaf Compute
constexpr uint32_t LEAF_COMPUTE_INPUT     = BINDING_LEAF_COMPUTE_INPUT;
constexpr uint32_t LEAF_COMPUTE_OUTPUT    = BINDING_LEAF_COMPUTE_OUTPUT;
constexpr uint32_t LEAF_COMPUTE_INDIRECT  = BINDING_LEAF_COMPUTE_INDIRECT;
constexpr uint32_t LEAF_COMPUTE_UNIFORMS  = BINDING_LEAF_COMPUTE_UNIFORMS;
constexpr uint32_t LEAF_COMPUTE_WIND      = BINDING_LEAF_COMPUTE_WIND;
constexpr uint32_t LEAF_COMPUTE_HEIGHT_MAP = BINDING_LEAF_COMPUTE_HEIGHT_MAP;
constexpr uint32_t LEAF_COMPUTE_DISPLACEMENT = BINDING_LEAF_COMPUTE_DISPLACEMENT;
constexpr uint32_t LEAF_COMPUTE_DISP_REGION = BINDING_LEAF_COMPUTE_DISP_REGION;

// Weather System
constexpr uint32_t WEATHER_PARTICLES      = BINDING_WEATHER_PARTICLES;
constexpr uint32_t WEATHER_INDIRECT       = BINDING_WEATHER_INDIRECT;
constexpr uint32_t WEATHER_UBO            = BINDING_WEATHER_UBO;
constexpr uint32_t WEATHER_UNIFORMS       = BINDING_WEATHER_UNIFORMS;
constexpr uint32_t WEATHER_WIND           = BINDING_WEATHER_WIND;
constexpr uint32_t WEATHER_FROXEL         = BINDING_WEATHER_FROXEL;

// Post-Process
constexpr uint32_t PP_HDR_INPUT           = BINDING_PP_HDR_INPUT;
constexpr uint32_t PP_UNIFORMS            = BINDING_PP_UNIFORMS;
constexpr uint32_t PP_DEPTH               = BINDING_PP_DEPTH;
constexpr uint32_t PP_FROXEL              = BINDING_PP_FROXEL;
constexpr uint32_t PP_BLOOM               = BINDING_PP_BLOOM;

// Bloom
constexpr uint32_t BLOOM_INPUT            = BINDING_BLOOM_INPUT;
constexpr uint32_t BLOOM_SECONDARY        = BINDING_BLOOM_SECONDARY;

// Histogram
constexpr uint32_t HISTOGRAM_IMAGE        = BINDING_HISTOGRAM_IMAGE;
constexpr uint32_t HISTOGRAM_BUFFER       = BINDING_HISTOGRAM_BUFFER;
constexpr uint32_t HISTOGRAM_PARAMS       = BINDING_HISTOGRAM_PARAMS;
constexpr uint32_t HISTOGRAM_EXPOSURE     = BINDING_HISTOGRAM_EXPOSURE;

// Hi-Z
constexpr uint32_t HIZ_SRC_DEPTH          = BINDING_HIZ_SRC_DEPTH;
constexpr uint32_t HIZ_SRC_MIP            = BINDING_HIZ_SRC_MIP;
constexpr uint32_t HIZ_DST_MIP            = BINDING_HIZ_DST_MIP;
constexpr uint32_t HIZ_CULL_UNIFORMS      = BINDING_HIZ_CULL_UNIFORMS;
constexpr uint32_t HIZ_CULL_OBJECTS       = BINDING_HIZ_CULL_OBJECTS;
constexpr uint32_t HIZ_CULL_INDIRECT      = BINDING_HIZ_CULL_INDIRECT;
constexpr uint32_t HIZ_CULL_COUNT         = BINDING_HIZ_CULL_COUNT;
constexpr uint32_t HIZ_CULL_PYRAMID       = BINDING_HIZ_CULL_PYRAMID;

// Atmosphere LUT
constexpr uint32_t ATMO_OUTPUT_LUT        = BINDING_ATMO_OUTPUT_LUT;
constexpr uint32_t ATMO_TRANSMITTANCE     = BINDING_ATMO_TRANSMITTANCE;
constexpr uint32_t ATMO_MULTISCATTER      = BINDING_ATMO_MULTISCATTER;
constexpr uint32_t ATMO_UNIFORMS          = BINDING_ATMO_UNIFORMS;

// Irradiance LUT
constexpr uint32_t IRRADIANCE_RAYLEIGH_OUT = BINDING_IRRADIANCE_RAYLEIGH_OUT;
constexpr uint32_t IRRADIANCE_MIE_OUT     = BINDING_IRRADIANCE_MIE_OUT;
constexpr uint32_t IRRADIANCE_TRANSMITTANCE = BINDING_IRRADIANCE_TRANSMITTANCE;
constexpr uint32_t IRRADIANCE_UNIFORMS    = BINDING_IRRADIANCE_UNIFORMS;

// Cloud Map LUT
constexpr uint32_t CLOUDMAP_OUTPUT        = BINDING_CLOUDMAP_OUTPUT;
constexpr uint32_t CLOUDMAP_UNIFORMS      = BINDING_CLOUDMAP_UNIFORMS;

// Cloud Shadow
constexpr uint32_t CLOUD_SHADOW_OUTPUT    = BINDING_CLOUD_SHADOW_OUTPUT;
constexpr uint32_t CLOUD_SHADOW_CLOUDMAP  = BINDING_CLOUD_SHADOW_CLOUDMAP;
constexpr uint32_t CLOUD_SHADOW_UNIFORMS  = BINDING_CLOUD_SHADOW_UNIFORMS;

// Snow Accumulation
constexpr uint32_t SNOW_MASK_OUTPUT       = BINDING_SNOW_MASK_OUTPUT;
constexpr uint32_t SNOW_MASK_UNIFORMS     = BINDING_SNOW_MASK_UNIFORMS;
constexpr uint32_t SNOW_MASK_INTERACTIONS = BINDING_SNOW_MASK_INTERACTIONS;

// Volumetric Snow
constexpr uint32_t VOL_SNOW_CASCADE_0     = BINDING_VOL_SNOW_CASCADE_0;
constexpr uint32_t VOL_SNOW_CASCADE_1     = BINDING_VOL_SNOW_CASCADE_1;
constexpr uint32_t VOL_SNOW_CASCADE_2     = BINDING_VOL_SNOW_CASCADE_2;
constexpr uint32_t VOL_SNOW_UNIFORMS      = BINDING_VOL_SNOW_UNIFORMS;
constexpr uint32_t VOL_SNOW_INTERACTIONS  = BINDING_VOL_SNOW_INTERACTIONS;

// Froxel
constexpr uint32_t FROXEL_SCATTERING      = BINDING_FROXEL_SCATTERING;
constexpr uint32_t FROXEL_INTEGRATED      = BINDING_FROXEL_INTEGRATED;
constexpr uint32_t FROXEL_UNIFORMS        = BINDING_FROXEL_UNIFORMS;
constexpr uint32_t FROXEL_SHADOW          = BINDING_FROXEL_SHADOW;
constexpr uint32_t FROXEL_LIGHTS          = BINDING_FROXEL_LIGHTS;
constexpr uint32_t FROXEL_HISTORY         = BINDING_FROXEL_HISTORY;

// Catmull-Clark Subdivision
constexpr uint32_t CC_SCENE_UBO           = BINDING_CC_SCENE_UBO;
constexpr uint32_t CC_CBT_BUFFER          = BINDING_CC_CBT_BUFFER;
constexpr uint32_t CC_VERTEX_BUFFER       = BINDING_CC_VERTEX_BUFFER;
constexpr uint32_t CC_HALFEDGE_BUFFER     = BINDING_CC_HALFEDGE_BUFFER;
constexpr uint32_t CC_FACE_BUFFER         = BINDING_CC_FACE_BUFFER;

// Water System
constexpr uint32_t WATER_UBO              = BINDING_WATER_UBO;
constexpr uint32_t WATER_SHADOW_MAP       = BINDING_WATER_SHADOW_MAP;
constexpr uint32_t WATER_TERRAIN_HEIGHT   = BINDING_WATER_TERRAIN_HEIGHT;
constexpr uint32_t WATER_FLOW_MAP         = BINDING_WATER_FLOW_MAP;
constexpr uint32_t WATER_DISPLACEMENT     = BINDING_WATER_DISPLACEMENT;
constexpr uint32_t WATER_FOAM_NOISE       = BINDING_WATER_FOAM_NOISE;
constexpr uint32_t WATER_TEMPORAL_FOAM    = BINDING_WATER_TEMPORAL_FOAM;
constexpr uint32_t WATER_CAUSTICS         = BINDING_WATER_CAUSTICS;
constexpr uint32_t WATER_SSR              = BINDING_WATER_SSR;
constexpr uint32_t WATER_SCENE_DEPTH      = BINDING_WATER_SCENE_DEPTH;

// Water Displacement
constexpr uint32_t WATER_DISP_OUTPUT      = BINDING_WATER_DISP_OUTPUT;
constexpr uint32_t WATER_DISP_PREV        = BINDING_WATER_DISP_PREV;
constexpr uint32_t WATER_DISP_PARTICLES   = BINDING_WATER_DISP_PARTICLES;

// Water Tile Cull
constexpr uint32_t WATER_CULL_DEPTH       = BINDING_WATER_CULL_DEPTH;
constexpr uint32_t WATER_CULL_TILES       = BINDING_WATER_CULL_TILES;
constexpr uint32_t WATER_CULL_COUNTER     = BINDING_WATER_CULL_COUNTER;
constexpr uint32_t WATER_CULL_INDIRECT    = BINDING_WATER_CULL_INDIRECT;

// Foam Blur
constexpr uint32_t FOAM_OUTPUT            = BINDING_FOAM_OUTPUT;
constexpr uint32_t FOAM_INPUT             = BINDING_FOAM_INPUT;
constexpr uint32_t FOAM_FLOW_MAP          = BINDING_FOAM_FLOW_MAP;
constexpr uint32_t FOAM_WAKE_DATA         = BINDING_FOAM_WAKE_DATA;

// SSR
constexpr uint32_t SSR_COLOR              = BINDING_SSR_COLOR;
constexpr uint32_t SSR_DEPTH              = BINDING_SSR_DEPTH;
constexpr uint32_t SSR_OUTPUT             = BINDING_SSR_OUTPUT;
constexpr uint32_t SSR_PREV               = BINDING_SSR_PREV;

// Tree Shader
constexpr uint32_t TREE_BARK_COLOR        = BINDING_TREE_BARK_COLOR;
constexpr uint32_t TREE_BARK_NORMAL       = BINDING_TREE_BARK_NORMAL;
constexpr uint32_t TREE_BARK_AO           = BINDING_TREE_BARK_AO;
constexpr uint32_t TREE_BARK_ROUGHNESS    = BINDING_TREE_BARK_ROUGHNESS;
constexpr uint32_t TREE_LEAF              = BINDING_TREE_LEAF;

} // namespace Bindings

#endif // __cplusplus

#endif // BINDINGS_H
