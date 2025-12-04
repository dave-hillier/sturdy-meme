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

#endif // BINDINGS_GLSL
