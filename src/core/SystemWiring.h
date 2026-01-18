#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vector>
#include <array>
#include <functional>

// Forward declarations
class RendererSystems;
class GlobalBufferManager;
class ShadowSystem;
class TerrainSystem;
class GrassSystem;
class LeafSystem;
class WeatherSystem;
class CloudShadowSystem;
class SnowMaskSystem;
class VolumetricSnowSystem;
class FroxelSystem;
class PostProcessSystem;
class WindSystem;
class RockSystem;
class DetritusSystem;
class SkinnedMeshRenderer;
class MaterialRegistry;
struct EnvironmentSettings;

/**
 * SystemWiring - Handles cross-system descriptor set updates and connections
 *
 * After individual systems are created, they need to be wired together:
 * - Descriptor sets need shadow maps, UBOs, wind buffers, etc.
 * - Systems need references to other systems' resources
 *
 * This class encapsulates all that wiring logic to reduce coupling and
 * complexity in RendererInitPhases.
 *
 * Usage:
 *   SystemWiring wiring(device, MAX_FRAMES_IN_FLIGHT);
 *   wiring.wireTerrainDescriptors(systems);
 *   wiring.wireGrassDescriptors(systems);
 *   wiring.wireLeafDescriptors(systems);
 *   // ... or use wireAll() for full wiring
 */
class SystemWiring {
public:
    SystemWiring(VkDevice device, uint32_t framesInFlight);

    /**
     * Wire all system descriptors at once.
     * Call after all systems are created.
     */
    void wireAll(RendererSystems& systems);

    /**
     * Wire terrain system descriptors.
     * Requires: GlobalBufferManager, ShadowSystem
     */
    void wireTerrainDescriptors(RendererSystems& systems);

    /**
     * Wire grass system descriptors.
     * Requires: GlobalBufferManager, ShadowSystem, WindSystem, TerrainSystem, CloudShadowSystem
     */
    void wireGrassDescriptors(RendererSystems& systems);

    /**
     * Wire leaf system descriptors.
     * Requires: GlobalBufferManager, WindSystem, TerrainSystem, GrassSystem
     */
    void wireLeafDescriptors(RendererSystems& systems);

    /**
     * Wire weather system descriptors.
     * Requires: GlobalBufferManager, WindSystem, PostProcessSystem, ShadowSystem
     */
    void wireWeatherDescriptors(RendererSystems& systems);

    /**
     * Wire snow systems to environment settings and other systems.
     * Requires: EnvironmentSettings, SnowMaskSystem, VolumetricSnowSystem, TerrainSystem, GrassSystem
     */
    void wireSnowSystems(RendererSystems& systems);

    /**
     * Wire froxel volume to weather system.
     * Requires: FroxelSystem, WeatherSystem
     */
    void wireFroxelToWeather(RendererSystems& systems);

    /**
     * Wire cloud shadow map to terrain.
     * Requires: CloudShadowSystem, TerrainSystem
     */
    void wireCloudShadowToTerrain(RendererSystems& systems);

    /**
     * Update cloud shadow bindings across all descriptor sets.
     * Requires: CloudShadowSystem, MaterialRegistry, RockSystem, DetritusSystem, SkinnedMeshRenderer
     */
    void wireCloudShadowBindings(RendererSystems& systems);

    /**
     * Wire underwater caustics from water to terrain.
     * Requires: WaterSystem, TerrainSystem
     */
    void wireCausticsToTerrain(RendererSystems& systems);

private:
    VkDevice device_;
    uint32_t framesInFlight_;

    // Helper to get wind buffers from WindSystem
    std::vector<VkBuffer> collectWindBuffers(const WindSystem& wind) const;

    // Helper to get tile info buffers from TerrainSystem
    std::array<VkBuffer, 3> collectTileInfoBuffers(const TerrainSystem& terrain) const;

    // Helper to convert raw buffers to vulkan-hpp buffers
    static std::vector<vk::Buffer> toVkBuffers(const std::vector<VkBuffer>& raw);
};
