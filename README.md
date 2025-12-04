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

### Claude Build (self-bootstrapping)

For environments without vcpkg pre-installed:

```bash
./build-claude.sh
```

This script:
1. Clones and builds vcpkg from source if not present
2. Uses a custom triplet optimized for restricted environments
3. Configures and builds the project

Requirements:
- CMake 3.20+, Ninja, GCC/Clang
- Network access to GitHub (for vcpkg and dependencies)
- Network access to freedesktop.org (for dbus), gnu.org (for some tools)

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

The game supports two camera modes: **Free Camera** and **Third Person**. Press **Tab** (keyboard) or **Left Stick Click** (gamepad) to toggle between them.

### Keyboard - Global Controls

| Key | Action |
|-----|--------|
| Escape | Quit |
| Tab | Toggle camera mode (Free Cam ↔ Third Person) |
| 1 | Set time to sunrise |
| 2 | Set time to noon |
| 3 | Set time to sunset |
| 4 | Set time to midnight |
| + | Speed up time (2x) |
| - | Slow down time (0.5x) |
| R | Reset to real-time |
| 6 | Toggle cascade debug visualization |
| Z | Decrease weather intensity |
| X | Increase weather intensity |
| C | Cycle weather (Clear → Rain → Snow) |
| F | Spawn confetti at player location |
| V | Toggle cloud style (Procedural ↔ Paraboloid LUT) |

### Keyboard - Free Camera Mode

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
| Space | Move up (fly) |
| Left Ctrl / Q | Move down (fly) |

### Keyboard - Third Person Mode

| Key | Action |
|-----|--------|
| W | Move player forward (relative to camera) |
| S | Move player backward (relative to camera) |
| A | Move player left (relative to camera) |
| D | Move player right (relative to camera) |
| Space | Jump |
| Arrow Up | Orbit camera up |
| Arrow Down | Orbit camera down |
| Arrow Left | Orbit camera left |
| Arrow Right | Orbit camera right |
| Q | Zoom camera in |
| E | Zoom camera out |

### Gamepad (SDL3) - Global Controls

| Input | Action |
|-------|--------|
| Left Stick Click | Toggle camera mode (Free Cam ↔ Third Person) |
| A / Cross | Set time to sunrise |
| B / Circle | Set time to noon |
| X / Square | Set time to sunset |
| Y / Triangle | Set time to midnight |
| Right Trigger | Speed up time |
| Left Trigger | Slow down time |
| Start | Reset to real-time |
| Back / Select | Quit |

### Gamepad - Free Camera Mode

| Input | Action |
|-------|--------|
| Left Stick | Movement (forward/backward, strafe) |
| Right Stick | Camera look (pitch/yaw) |
| Right Bumper (RB/R1) | Move up |
| Left Bumper (LB/L1) | Move down |

### Gamepad - Third Person Mode

| Input | Action |
|-------|--------|
| Left Stick | Player movement (relative to camera) |
| Right Stick | Orbit camera around player |
| A / Cross | Jump |
| Right Bumper (RB/R1) | Zoom camera out |
| Left Bumper (LB/L1) | Zoom camera in |

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
