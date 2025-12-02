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
