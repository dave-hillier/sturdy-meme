# Claude

- See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for rendering architecture overview
- dont include estimates, either time or lines of code, in any plans
- use opengameart.org for texture asset placeholders
- keep an emphasis when planning on incremental progress, each state should be working and look decent when rendered
- always ensure that the build both compiles and runs without crashing before considering it done
- shaders can be compiled by running cmake
- UBO structs are automatically generated from shaders during the cmake build
- compile with `cmake --preset debug && cmake --build build/debug`
- run with `./run-debug.sh` do not attempt to combine with timeouts or sleeps
- Prefer composition over inheritance - assume pretty much all of the time you want to use inheritance you are wrong.
- ShaderLoader API: use `ShaderLoader::loadShaderModule(device, path)` or the two-step `readFile` + `createShaderModule`. There is NO `loadShader` method.
- Terrain height uses `h * heightScale` where h is normalized [0,1]. Use functions from `shaders/terrain_height_common.glsl` (shaders) or `src/TerrainHeight.h` (C++). Do NOT duplicate the formula.
- Logging: Use SDL_Log consistently throughout the codebase. Do NOT use std::cout, std::cerr, printf, or fprintf for logging. Use the appropriate SDL log functions:
  - `SDL_Log()` for general info messages
  - `SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, ...)` for errors
  - `SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, ...)` for warnings
  - `SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, ...)` for debug output
