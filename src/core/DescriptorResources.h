#pragma once

#include <vulkan/vulkan.hpp>
#include <array>
#include <vector>

namespace BufferUtils {
struct DynamicUniformBuffer;
}

// Context structs to bundle related descriptor resources, reducing parameter bloat
// in updateDescriptorSets() methods. These structs are passed by const reference.

// Scene-level uniform buffers (renderer UBO, dynamic UBO)
struct SceneResources {
    std::vector<vk::Buffer> uniformBuffers;
    const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO = nullptr;
};

// Shadow map resources
struct ShadowResources {
    vk::ImageView shadowMapView;
    vk::Sampler shadowSampler;
};

// Wind system buffers
struct WindResources {
    std::vector<vk::Buffer> windBuffers;
};

// Light buffers (SSBO)
struct LightResources {
    std::vector<vk::Buffer> lightBuffers;
};

// Terrain heightmap and tile cache resources
struct TerrainResources {
    vk::ImageView heightMapView;
    vk::Sampler heightMapSampler;
    vk::ImageView tileArrayView;
    vk::Sampler tileSampler;
    std::array<vk::Buffer, 3> tileInfoBuffers;
};

// Atmospheric effects (snow, cloud shadows)
struct AtmosphereResources {
    std::vector<vk::Buffer> snowBuffers;
    std::vector<vk::Buffer> cloudShadowBuffers;
    vk::ImageView cloudShadowMapView;
    vk::Sampler cloudShadowMapSampler;
};

// Grass displacement texture (for leaf system to read)
struct DisplacementResources {
    vk::ImageView displacementView;
    vk::Sampler displacementSampler;
};

// Depth buffer for weather particles
struct DepthResources {
    vk::ImageView depthView;
    vk::Sampler depthSampler;
};

// Combined bundle for grass system (which needs most resources)
struct GrassDescriptorContext {
    const SceneResources& scene;
    const ShadowResources& shadow;
    const WindResources& wind;
    const LightResources& light;
    const TerrainResources& terrain;
    const AtmosphereResources& atmosphere;
};

// Combined bundle for leaf system
struct LeafDescriptorContext {
    const SceneResources& scene;
    const WindResources& wind;
    const TerrainResources& terrain;
    const DisplacementResources& displacement;
};

// Combined bundle for weather system
struct WeatherDescriptorContext {
    const SceneResources& scene;
    const WindResources& wind;
    const DepthResources& depth;
};

// Combined bundle for terrain system
struct TerrainDescriptorContext {
    const SceneResources& scene;
    const ShadowResources& shadow;
    const AtmosphereResources& atmosphere;
};
