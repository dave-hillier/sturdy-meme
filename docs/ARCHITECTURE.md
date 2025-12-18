# Architecture

Vulkan rendering engine for realistic outdoor environments. Core flow: `Application` → `Renderer` → `VulkanContext`.

## High-Level Structure

- **Application** - Main loop, input, physics, GUI, camera, player state
- **Renderer** - Orchestrates ~30 self-contained rendering subsystems
- **VulkanContext** - Low-level Vulkan setup, device management, VMA allocator
- **SceneManager** - Central hub for visual objects, physics bodies, lighting

## Subsystem Organization

`RendererSystems` manages all subsystems in organized tiers:

### Core Rendering
- `PostProcessSystem` - HDR target, tonemapping, final composite
- `BloomSystem` - Bloom and exposure/luminance histogram
- `ShadowSystem` - Cascaded shadow maps (4 levels)
- `TerrainSystem` - Virtual texture system with tile cache, LOD meshlets, CBT subdivision

### Sky & Atmosphere
- `SkySystem` - Sky/clouds rendering
- `AtmosphereLUTSystem` - Atmospheric scattering LUTs (transmittance, skyview, irradiance, multiscatter)
- `FroxelSystem` - Volumetric fog in froxel grid
- `CloudShadowSystem` - Cloud shadow maps

### Environment
- `GrassSystem` - Procedural grass with displacement, wind interaction
- `WindSystem` - Wind simulation and gusts
- `WeatherSystem` - Rain/snow particle effects
- `LeafSystem` - Leaf/confetti particles
- `SnowMaskSystem` + `VolumetricSnowSystem` - Snow accumulation

### Water
- `WaterSystem` - Main water rendering with multi-material blending
- `WaterDisplacement` - Ocean FFT-based wave displacement
- `OceanFFT` - FFT ocean spectrum computation
- `FlowMapGenerator` - Flow map for UV distortion
- `FoamBuffer` - Foam texture generation
- `SSRSystem` - Screen-space reflections

### Geometry & Optimization
- `CatmullClarkSystem` - Subdivision surface rendering
- `RockSystem` - Rock/static geometry
- `HiZSystem` - Hi-Z pyramid culling
- `TreeEditSystem` - Interactive tree creation with presets

## Rendering Pipeline

`RenderPipeline` executes stages in order:

```
1. ComputeStage    - Terrain LOD, grass sim, weather
2. ShadowStage     - Shadow map rendering (conditional on sun intensity)
3. FroxelStage     - Volumetric fog update
4. HDRStage        - Main scene: sky, terrain, water, grass, trees, objects, particles
5. PostStage       - HiZ downsampling, bloom, SSR, final composite
```

Each stage holds lambdas populated by Renderer, allowing stages to orchestrate system rendering without owning systems directly.

## Key Patterns

### Builder Pattern
- `GraphicsPipelineFactory` - Fluent pipeline construction with presets (Default, FullscreenQuad, Shadow, Particle)
- `DescriptorManager::LayoutBuilder` + `SetWriter` - Declarative descriptor management
- `RenderableBuilder` - Renderable object construction
- `ImageBuilder` - Image/mipmap construction

### InitInfo Pattern
Systems initialized via explicit `init(InitContext&, SystemInitInfo&)`:
- `InitContext` bundles common setup resources (device, allocator, descriptor pool, paths)
- Each system has its own `InitInfo` struct with dependencies

### System Lifecycle
- `init()` → `destroy()` with predictable ordering
- Systems register with `ResizeCoordinator` for window resize callbacks
- Double-buffering for uniform buffers (MAX_FRAMES_IN_FLIGHT = 2)

### RAII Resource Management
`VulkanRAII.h` provides automatic cleanup wrappers:
- `ManagedImage`, `ManagedBuffer`, `ManagedSampler`, `ManagedDescriptorSet`
- `RAIIAdapter.h` for converting POD objects to RAII-managed versions

### Per-Frame Data
- `FrameData` - Consolidated state (frame index, deltaTime, camera, lighting, player, terrain, wind/weather)
- `RenderContext` - Execution context (cmd buffer, frame data, render resources)
- `RenderResources` - Snapshot of shared GPU resources (HDR/shadow/bloom targets, pipelines)

## Resources

- `Mesh` - CPU vertices + GPU buffers via VMA
- `Texture` - VkImage + view + sampler
- `Renderable` - POD struct (transform, mesh*, texture*, PBR params)

## Scene Management

**SceneManager** coordinates:
- Visual objects (Renderables)
- Physics integration (Jolt Physics backend)
- Lighting (LightManager for point/directional lights)
- Terrain height queries for object placement
- Player object tracking and transformation updates

**Physics Integration**:
- Per-frame sync: physics → visuals
- Terrain physics with tiled heightfield data
- Player IK solvers: TwoBoneIK, FootPlacement, LookAt, Climbing, Straddle

## Shader Organization

Located in `/shaders/`:
- **Core**: `shader.vert/frag` (main scene), `shadow.vert/frag`
- **Specialized**: `grass.*`, `water.*`, `sky.*`, `weather.*`, `tree.*`, `postprocess.frag`
- **Compute**: `terrain_*.comp`, `grass_*.comp`, `water_*.comp`, `atmosphere_*.comp`, `cloud_*.comp`
- **Common includes**: `*_common.glsl` (fbm, flow, lighting, atmosphere, terrain height)
- **Generated**: `bindings.h`, `UBOs.h` (from SPIRV-Reflect)

Build: `cmake --preset debug && cmake --build build/debug`

The `shader_reflect` tool (in `tools/`) parses compiled SPIR-V and outputs `generated/UBOs.h` with std140-aligned C++ structs.

## Key Files

### Core
- `src/core/Renderer.h` - Main orchestrator
- `src/core/VulkanContext.h` - Vulkan setup
- `src/core/RendererSystems.h` - Subsystem container
- `src/core/RenderContext.h`, `RenderPipeline.h` - Stage execution
- `src/core/GraphicsPipelineFactory.h` - Pipeline builder
- `src/core/DescriptorManager.h` - Descriptor sets
- `src/core/VulkanRAII.h` - RAII wrappers
- `src/core/ImageBuilder.h` - GPU image creation

### Terrain
- `src/terrain/TerrainSystem.h` - LOD manager
- `src/terrain/TerrainTileCache.h` - Virtual texture cache
- `src/terrain/VirtualTextureSystem.h` - Tile atlas management

### Water
- `src/water/WaterSystem.h` - Main water rendering
- `src/water/OceanFFT.h` - FFT wave simulation

### Vegetation
- `src/vegetation/GrassSystem.h` - Procedural grass
- `src/vegetation/TreeEditSystem.h` - Tree editor

### Atmosphere & Lighting
- `src/atmosphere/AtmosphereLUTSystem.h` - Scattering LUTs
- `src/lighting/ShadowSystem.h` - Cascaded shadows
- `src/lighting/FroxelSystem.h` - Volumetric fog
