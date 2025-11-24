# Vulkan Game

A simple Vulkan-based 3D game rendering a textured cube with camera controls.

## Prerequisites

- CMake 3.20+
- C++17 compiler
- Vulkan SDK (includes MoltenVK on macOS)
- vcpkg
- Ninja (recommended)

### macOS Setup

1. Install MoltenVK (required for Vulkan on macOS):
   ```bash
   brew install molten-vk
   ```

   Or install the full [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)

2. Install vcpkg:
   ```bash
   git clone https://github.com/microsoft/vcpkg.git
   ./vcpkg/bootstrap-vcpkg.sh
   export VCPKG_ROOT=$(pwd)/vcpkg
   ```

## Building

### Debug Build

```bash
cmake --preset debug
cmake --build build/debug
```

### Release Build

```bash
cmake --preset release
cmake --build build/release
```

## Running

```bash
# Debug
open build/debug/vulkan-game.app
# or
./build/debug/vulkan-game.app/Contents/MacOS/vulkan-game

# Release
open build/release/vulkan-game.app
```

## Controls

| Key | Action |
|-----|--------|
| Arrow Up | Move forward |
| Arrow Down | Move backward |
| Arrow Left | Strafe left |
| Arrow Right | Strafe right |
| W | Look up (pitch) |
| S | Look down (pitch) |
| A | Look left (yaw) |
| D | Look right (yaw) |
| Page Up | Move up |
| Page Down | Move down |
| Escape | Quit |

## Project Structure

```
vulkan-game/
├── CMakeLists.txt        # Build configuration
├── CMakePresets.json     # Debug/Release presets
├── vcpkg.json            # Dependencies
├── README.md
├── src/
│   ├── main.cpp          # Entry point
│   ├── Application.h/cpp # Window, input, game loop
│   ├── Renderer.h/cpp    # Vulkan rendering
│   ├── Camera.h/cpp      # Camera system
│   ├── Mesh.h/cpp        # Geometry
│   └── Texture.h/cpp     # Texture loading
├── shaders/
│   ├── shader.vert       # Vertex shader
│   └── shader.frag       # Fragment shader
└── assets/
    └── textures/
        └── crate.png     # Wooden crate texture
```

## Dependencies

- SDL3 - Windowing and input
- Vulkan - Graphics API
- GLM - Mathematics
- vk-bootstrap - Vulkan initialization
- VMA - Memory allocation
- stb_image - Texture loading

## License

Crate texture from OpenGameArt.org (CC0).
