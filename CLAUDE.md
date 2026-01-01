# Claude

- See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for rendering architecture overview
- dont include estimates, either time or lines of code, in any plans
- when a feature is completed describe how to test it. It doesnt have to be a unit test - it can be some example and manual testing.
- use opengameart.org for texture asset placeholders
- keep an emphasis when planning on incremental progress, each state should be working and look decent when rendered
- always ensure that the build both compiles and runs without crashing before considering it done
- shaders can be compiled by running cmake
- UBO structs are automatically generated from shaders during the cmake build. The `shader_reflect` tool (in `tools/`) parses compiled SPIR-V files using SPIRV-Reflect and outputs `generated/UBOs.h` with std140-aligned C++ structs matching the shader uniform buffer layouts.
- compile with `cmake --preset debug && cmake --build build/debug`
- run with `./run-debug.sh` do not attempt to combine with timeouts or sleeps
- DO NOT use `timeout` or `gtimeout` to attempt to run - instead run in the background and use pkill
- Prefer composition over inheritance - assume pretty much all of the time you want to use inheritance you are wrong.
- ShaderLoader API: use `ShaderLoader::loadShaderModule(device, path)` or the two-step `readFile` + `createShaderModule`. There is NO `loadShader` method.
- Terrain height uses `h * heightScale` where h is normalized [0,1]. Use functions from `shaders/terrain_height_common.glsl` (shaders) or `src/TerrainHeight.h` (C++). Do NOT duplicate the formula.
- Logging: Use SDL_Log consistently throughout the codebase. Do NOT use std::cout, std::cerr, printf, or fprintf for logging. Use the appropriate SDL log functions:
  - `SDL_Log()` for general info messages
  - `SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, ...)` for errors
  - `SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, ...)` for warnings
  - `SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, ...)` for debug output
- procedurally generated content should be generated as part of the build process.
- generated textures should be saved in png format
- Vulkan-hpp: Use vulkan-hpp (`#include <vulkan/vulkan.hpp>`) for new Vulkan code. Prefer builder pattern with `.set*()` methods over positional constructors (e.g., `vk::BufferCreateInfo{}.setSize(...).setUsage(...)`). Run `./scripts/analyze-vulkan-usage.sh` to see migration guidance.

## Preprocessing Tool Output Formats

Preprocessing tools in `tools/` must use standard, widely-supported file formats. Do NOT use custom binary formats (`.dat`, `.bin`). Preferred formats:

- **GeoJSON** (`.geojson`): For vector/path data such as rivers, lakes, roads. Use `FeatureCollection` with `LineString` geometry. Enables GIS tool compatibility and human readability.
- **OpenEXR** (`.exr`): For float grids like flow accumulation. Uses tinyexr library. Provides lossless 32-bit float storage with industry-standard tooling support.
- **PNG** (`.png`): For integer grids. Use 8-bit grayscale for small value ranges (e.g., flow direction 0-7). For larger integers like uint32 watershed labels, encode as RGBA (R=byte0, G=byte1, B=byte2, A=byte3).

Current file outputs:
- `rivers.geojson`, `lakes.geojson` - Water feature paths with flow/width metadata
- `roads.geojson` - Road network with type and settlement connections
- `flow_accumulation.exr` - Float grid of accumulated water flow
- `flow_direction.png` - 8-bit D8 flow direction (values 0-7)
- `watershed_labels.png` - RGBA-encoded uint32 watershed basin IDs

## Web Claude Code Environment

When running in web Claude Code (Linux environment without vcpkg pre-installed):
- Use `./build-claude.sh` to build - it bootstraps vcpkg automatically
- The claude preset uses a local vcpkg installation in the project directory
- Custom triplets in `triplets/` disable problematic features for restricted environments

Build options:
- `./build-claude.sh` - Fast build: shaders, headers, and C++ (skips terrain preprocessing tools)
- `./build-claude.sh --full` - Complete build including terrain preprocessing
- `./build-claude.sh --shaders` - Shaders and generated headers only (no C++)
