#pragma once

#include <memory>
#include <vulkan/vulkan.h>

#include "InitContext.h"
#include "CoreResources.h"

// Forward declarations for all subsystems
class SkySystem;
class GrassSystem;
class WindSystem;
class WeatherSystem;
class LeafSystem;
class PostProcessSystem;
class BloomSystem;
class FroxelSystem;
class AtmosphereLUTSystem;
class TerrainSystem;
class CatmullClarkSystem;
class SnowMaskSystem;
class VolumetricSnowSystem;
class RockSystem;
class CloudShadowSystem;
class HiZSystem;
class WaterSystem;
class WaterDisplacement;
class FlowMapGenerator;
class FoamBuffer;
class SSRSystem;
class WaterTileCull;
class WaterGBuffer;
class ErosionDataLoader;
class TreeEditSystem;
class UBOBuilder;
class Profiler;
class DebugLineSystem;
class ResizeCoordinator;
class ShadowSystem;
class SceneManager;
class GlobalBufferManager;
class SkinnedMeshRenderer;
class TimeSystem;
class CelestialCalculator;
struct EnvironmentSettings;
struct TerrainConfig;

#ifdef JPH_DEBUG_RENDERER
class PhysicsDebugRenderer;
#endif

/**
 * RendererSystems - Owns all rendering subsystems with automatic lifecycle management
 *
 * Design goals:
 * - Groups related systems together
 * - Uses unique_ptr for automatic cleanup
 * - Reduces Renderer's direct knowledge of subsystem internals
 * - Provides typed access when needed
 *
 * Lifecycle:
 * - Create with create() factory method
 * - Initialize with init() after VulkanContext is ready
 * - Destroy automatically via destructor
 */
class RendererSystems {
public:
    RendererSystems();
    ~RendererSystems();

    // Non-copyable, non-movable (owns complex GPU resources)
    RendererSystems(const RendererSystems&) = delete;
    RendererSystems& operator=(const RendererSystems&) = delete;
    RendererSystems(RendererSystems&&) = delete;
    RendererSystems& operator=(RendererSystems&&) = delete;

    /**
     * Initialize all subsystems in proper dependency order
     * Returns false if any critical system fails to initialize
     */
    bool init(const InitContext& initCtx,
              VkRenderPass swapchainRenderPass,
              VkFormat swapchainImageFormat,
              VkDescriptorSetLayout mainDescriptorSetLayout,
              VkFormat depthFormat,
              VkSampler depthSampler,
              const std::string& resourcePath);

    /**
     * Destroy all subsystems in reverse dependency order
     */
    void destroy(VkDevice device, VmaAllocator allocator);

    /**
     * Get tier-1 core resources for dependent system initialization
     * Only valid after init() completes Phase 1
     */
    CoreResources getCoreResources(uint32_t framesInFlight) const;

    // ========================================================================
    // System accessors (for external coordination)
    // ========================================================================

    // Tier 1 - Core rendering
    PostProcessSystem& postProcess() { return *postProcessSystem_; }
    const PostProcessSystem& postProcess() const { return *postProcessSystem_; }
    BloomSystem& bloom() { return *bloomSystem_; }
    const BloomSystem& bloom() const { return *bloomSystem_; }
    void setBloom(std::unique_ptr<BloomSystem> system);
    ShadowSystem& shadow() { return *shadowSystem_; }
    const ShadowSystem& shadow() const { return *shadowSystem_; }
    void setShadow(std::unique_ptr<ShadowSystem> system);
    TerrainSystem& terrain() { return *terrainSystem_; }
    const TerrainSystem& terrain() const { return *terrainSystem_; }

    // Sky and atmosphere
    SkySystem& sky() { return *skySystem_; }
    const SkySystem& sky() const { return *skySystem_; }
    AtmosphereLUTSystem& atmosphereLUT() { return *atmosphereLUTSystem_; }
    const AtmosphereLUTSystem& atmosphereLUT() const { return *atmosphereLUTSystem_; }
    FroxelSystem& froxel() { return *froxelSystem_; }
    const FroxelSystem& froxel() const { return *froxelSystem_; }
    CloudShadowSystem& cloudShadow() { return *cloudShadowSystem_; }
    const CloudShadowSystem& cloudShadow() const { return *cloudShadowSystem_; }

    // Environment (grass, wind, weather)
    GrassSystem& grass() { return *grassSystem_; }
    const GrassSystem& grass() const { return *grassSystem_; }
    WindSystem& wind() { return *windSystem_; }
    const WindSystem& wind() const { return *windSystem_; }
    WeatherSystem& weather() { return *weatherSystem_; }
    const WeatherSystem& weather() const { return *weatherSystem_; }
    LeafSystem& leaf() { return *leafSystem_; }
    const LeafSystem& leaf() const { return *leafSystem_; }

    // Snow
    SnowMaskSystem& snowMask() { return *snowMaskSystem_; }
    const SnowMaskSystem& snowMask() const { return *snowMaskSystem_; }
    VolumetricSnowSystem& volumetricSnow() { return *volumetricSnowSystem_; }
    const VolumetricSnowSystem& volumetricSnow() const { return *volumetricSnowSystem_; }

    // Water
    WaterSystem& water() { return *waterSystem_; }
    const WaterSystem& water() const { return *waterSystem_; }
    WaterDisplacement& waterDisplacement() { return *waterDisplacement_; }
    const WaterDisplacement& waterDisplacement() const { return *waterDisplacement_; }
    FlowMapGenerator& flowMap() { return *flowMapGenerator_; }
    const FlowMapGenerator& flowMap() const { return *flowMapGenerator_; }
    FoamBuffer& foam() { return *foamBuffer_; }
    const FoamBuffer& foam() const { return *foamBuffer_; }
    SSRSystem& ssr() { return *ssrSystem_; }
    const SSRSystem& ssr() const { return *ssrSystem_; }
    void setSSR(std::unique_ptr<SSRSystem> system);
    WaterTileCull& waterTileCull() { return *waterTileCull_; }
    const WaterTileCull& waterTileCull() const { return *waterTileCull_; }
    WaterGBuffer& waterGBuffer() { return *waterGBuffer_; }
    const WaterGBuffer& waterGBuffer() const { return *waterGBuffer_; }

    // Geometry processing
    CatmullClarkSystem& catmullClark() { return *catmullClarkSystem_; }
    const CatmullClarkSystem& catmullClark() const { return *catmullClarkSystem_; }
    RockSystem& rock() { return *rockSystem_; }
    const RockSystem& rock() const { return *rockSystem_; }

    // Culling and optimization
    HiZSystem& hiZ() { return *hiZSystem_; }
    const HiZSystem& hiZ() const { return *hiZSystem_; }

    // Scene and resources
    SceneManager& scene() { return *sceneManager_; }
    const SceneManager& scene() const { return *sceneManager_; }
    GlobalBufferManager& globalBuffers() { return *globalBufferManager_; }
    const GlobalBufferManager& globalBuffers() const { return *globalBufferManager_; }
    void setGlobalBuffers(std::unique_ptr<GlobalBufferManager> buffers);
    ErosionDataLoader& erosionData() { return *erosionDataLoader_; }
    const ErosionDataLoader& erosionData() const { return *erosionDataLoader_; }

    // Animation and skinning
    SkinnedMeshRenderer& skinnedMesh() { return *skinnedMeshRenderer_; }
    const SkinnedMeshRenderer& skinnedMesh() const { return *skinnedMeshRenderer_; }

    // Tools and debug
    TreeEditSystem& treeEdit() { return *treeEditSystem_; }
    const TreeEditSystem& treeEdit() const { return *treeEditSystem_; }
    DebugLineSystem& debugLine() { return *debugLineSystem_; }
    const DebugLineSystem& debugLine() const { return *debugLineSystem_; }
    void setDebugLineSystem(std::unique_ptr<DebugLineSystem> system);
    void setProfiler(std::unique_ptr<Profiler> profiler);
    Profiler& profiler() { return *profiler_; }
    const Profiler& profiler() const { return *profiler_; }

    // Coordination
    ResizeCoordinator& resizeCoordinator() { return *resizeCoordinator_; }
    const ResizeCoordinator& resizeCoordinator() const { return *resizeCoordinator_; }
    UBOBuilder& uboBuilder() { return *uboBuilder_; }
    const UBOBuilder& uboBuilder() const { return *uboBuilder_; }

    // Time and celestial
    TimeSystem& time() { return *timeSystem_; }
    const TimeSystem& time() const { return *timeSystem_; }
    CelestialCalculator& celestial() { return *celestialCalculator_; }
    const CelestialCalculator& celestial() const { return *celestialCalculator_; }

    // Environment settings
    EnvironmentSettings& environmentSettings() { return *environmentSettings_; }
    const EnvironmentSettings& environmentSettings() const { return *environmentSettings_; }

#ifdef JPH_DEBUG_RENDERER
    PhysicsDebugRenderer* physicsDebugRenderer() { return physicsDebugRenderer_.get(); }
    const PhysicsDebugRenderer* physicsDebugRenderer() const { return physicsDebugRenderer_.get(); }
    void createPhysicsDebugRenderer(const InitContext& ctx, VkRenderPass hdrRenderPass);
#endif

private:
    // Tier 1 - Core rendering systems (initialize first)
    std::unique_ptr<PostProcessSystem> postProcessSystem_;
    std::unique_ptr<BloomSystem> bloomSystem_;
    std::unique_ptr<ShadowSystem> shadowSystem_;
    std::unique_ptr<TerrainSystem> terrainSystem_;

    // Tier 2 - Sky and atmosphere
    std::unique_ptr<SkySystem> skySystem_;
    std::unique_ptr<AtmosphereLUTSystem> atmosphereLUTSystem_;
    std::unique_ptr<FroxelSystem> froxelSystem_;
    std::unique_ptr<CloudShadowSystem> cloudShadowSystem_;

    // Tier 2 - Environment
    std::unique_ptr<GrassSystem> grassSystem_;
    std::unique_ptr<WindSystem> windSystem_;
    std::unique_ptr<WeatherSystem> weatherSystem_;
    std::unique_ptr<LeafSystem> leafSystem_;

    // Tier 2 - Snow
    std::unique_ptr<SnowMaskSystem> snowMaskSystem_;
    std::unique_ptr<VolumetricSnowSystem> volumetricSnowSystem_;

    // Tier 2 - Water
    std::unique_ptr<WaterSystem> waterSystem_;
    std::unique_ptr<WaterDisplacement> waterDisplacement_;
    std::unique_ptr<FlowMapGenerator> flowMapGenerator_;
    std::unique_ptr<FoamBuffer> foamBuffer_;
    std::unique_ptr<SSRSystem> ssrSystem_;
    std::unique_ptr<WaterTileCull> waterTileCull_;
    std::unique_ptr<WaterGBuffer> waterGBuffer_;

    // Tier 2 - Geometry
    std::unique_ptr<CatmullClarkSystem> catmullClarkSystem_;
    std::unique_ptr<RockSystem> rockSystem_;

    // Tier 2 - Culling
    std::unique_ptr<HiZSystem> hiZSystem_;

    // Infrastructure (needed throughout)
    std::unique_ptr<SceneManager> sceneManager_;
    std::unique_ptr<GlobalBufferManager> globalBufferManager_;
    std::unique_ptr<ErosionDataLoader> erosionDataLoader_;
    std::unique_ptr<SkinnedMeshRenderer> skinnedMeshRenderer_;

    // Tools and debug
    std::unique_ptr<TreeEditSystem> treeEditSystem_;
    std::unique_ptr<DebugLineSystem> debugLineSystem_;
    std::unique_ptr<Profiler> profiler_;

    // Coordination
    std::unique_ptr<ResizeCoordinator> resizeCoordinator_;
    std::unique_ptr<UBOBuilder> uboBuilder_;

    // Time and environment
    std::unique_ptr<TimeSystem> timeSystem_;
    std::unique_ptr<CelestialCalculator> celestialCalculator_;
    std::unique_ptr<EnvironmentSettings> environmentSettings_;

#ifdef JPH_DEBUG_RENDERER
    std::unique_ptr<PhysicsDebugRenderer> physicsDebugRenderer_;
#endif

    bool initialized_ = false;
};
