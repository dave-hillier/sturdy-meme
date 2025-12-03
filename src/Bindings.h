#pragma once

// Central binding definitions for the main rendering descriptor set
// Keep in sync with shaders/bindings.glsl for GLSL code

namespace Bindings {

// =============================================================================
// Main Rendering Descriptor Set (Set 0)
// =============================================================================

// Uniform Buffers
constexpr uint32_t UBO                    = 0;   // UniformBufferObject - core rendering data
constexpr uint32_t SNOW_UBO               = 10;  // SnowUBO - snow rendering parameters
constexpr uint32_t CLOUD_SHADOW_UBO       = 11;  // CloudShadowUBO - cloud shadow parameters

// Textures
constexpr uint32_t DIFFUSE_TEX            = 1;   // Diffuse/albedo texture
constexpr uint32_t SHADOW_MAP             = 2;   // Cascaded shadow map
constexpr uint32_t NORMAL_MAP             = 3;   // Normal map texture
constexpr uint32_t EMISSIVE_MAP           = 5;   // Emissive texture
constexpr uint32_t POINT_SHADOW_MAP       = 6;   // Point light shadow cubemap
constexpr uint32_t SPOT_SHADOW_MAP        = 7;   // Spot light shadow map
constexpr uint32_t SNOW_MASK              = 8;   // Snow coverage mask
constexpr uint32_t CLOUD_SHADOW_MAP       = 9;   // Cloud shadow projection

// Storage Buffers
constexpr uint32_t LIGHT_BUFFER           = 4;   // Point/spot light array

// =============================================================================
// Skinned Mesh Descriptor Set (extends main set)
// =============================================================================
constexpr uint32_t BONE_MATRICES          = 10;  // Note: Shares binding 10 with SNOW_UBO
                                                  // but in a different descriptor set layout

// =============================================================================
// Terrain Descriptor Set
// Note: Terrain uses a separate descriptor set with different layout
// =============================================================================
constexpr uint32_t TERRAIN_UBO            = 5;   // Terrain uses binding 5 for main UBO

} // namespace Bindings
