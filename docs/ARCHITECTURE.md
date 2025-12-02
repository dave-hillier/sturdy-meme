# Architecture

Vulkan rendering engine with modular subsystems. Core flow: `Application` → `Renderer` → `VulkanContext`.

## Key Components

- `Renderer` - Orchestrates ~20 self-contained systems (Sky, Terrain, Grass, Weather, PostProcess, etc.)
- `VulkanContext` - Low-level Vulkan setup + VMA allocator
- `SceneManager` - Central scene hub (not a true ECS), holds `SceneBuilder` + `LightManager`
- `Renderable` - Simple POD struct (transform, mesh*, texture*, PBR params)

## Patterns

- **Builder pattern**: `RenderableBuilder`, `GraphicsPipelineFactory`, `DescriptorManager.LayoutBuilder`
- **InitInfo pattern**: Systems initialized via explicit `init(InitInfo&)` structs
- **System pattern**: Each subsystem owns its own pipelines, descriptors, uniforms with `init()`, `destroy()`, `recordDraw()` lifecycle

## Pipeline Creation

Use `GraphicsPipelineFactory` with fluent API and presets (Default, FullscreenQuad, Shadow, Particle).

## Descriptor Management

Use `DescriptorManager` with `LayoutBuilder` and `SetWriter` for declarative binding.

## Resources

- `Mesh` - CPU vertices + GPU buffers via VMA
- `Texture` - VkImage + view + sampler
- Double-buffered uniforms (MAX_FRAMES_IN_FLIGHT = 2)

## Rendering Flow

Shadow pass → HDR pass (terrain, objects, effects) → Post-process → GUI → Present
