# Vulkan Game

A simple Vulkan-based 3D game rendering a textured cube with camera controls.

## Prerequisites

- CMake 3.20+
- C++17 compiler
- Vulkan SDK (includes MoltenVK on macOS)
- vcpkg
- Ninja

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

### Keyboard

| Key | Action |
|-----|--------|
| W | Move forward |
| S | Move backward |
| A | Strafe left |
| D | Strafe right |
| Arrow Up | Look up (pitch) |
| Arrow Down | Look down (pitch) |
| Arrow Left | Look left (yaw) |
| Arrow Right | Look right (yaw) |
| Space / E | Move up |
| C / Q | Move down |
| 1 | Set time to sunrise |
| 2 | Set time to noon |
| 3 | Set time to sunset |
| 4 | Set time to midnight |
| + | Speed up time (2x) |
| - | Slow down time (0.5x) |
| R | Reset to real-time |
| Escape | Quit |

### Gamepad (SDL3)

| Input | Action |
|-------|--------|
| Left Stick | Movement |
| Right Stick | Camera look |
| Right Bumper (RB/R1) | Move up |
| Left Bumper (LB/L1) | Move down |
| A / Cross | Set time to sunrise |
| B / Circle | Set time to noon |
| X / Square | Set time to sunset |
| Y / Triangle | Set time to midnight |
| Right Trigger | Speed up time |
| Left Trigger | Slow down time |
| Start | Reset to real-time |
| Back / Select | Quit |

Gamepads are automatically detected when connected (hot-plug supported).

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
