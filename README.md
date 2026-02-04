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

The game supports two camera modes: **Free Camera** and **Third Person**. Press **Tab** (keyboard) or **Right Stick Click** (gamepad) to toggle between them.

### Keyboard - Global Controls

| Key | Action |
|-----|--------|
| Escape | Quit |
| F1 | Toggle GUI visibility |
| Tab | Toggle camera mode (Free Cam ↔ Third Person) |
| M | Toggle mouse look |
| 1 | Set time to sunrise |
| 2 | Set time to noon |
| 3 | Set time to sunset |
| 4 | Set time to midnight |
| + | Speed up time (2x) |
| - | Slow down time (0.5x) |
| R | Reset to real-time |
| 6 | Toggle cascade debug visualization |
| 7 | Toggle snow depth debug visualization |
| 8 | Toggle Hi-Z occlusion culling |
| 9 | Terrain height diagnostic |
| Z | Decrease weather intensity |
| X | Increase weather intensity |
| C | Cycle weather (Clear → Rain → Snow) |
| F | Spawn confetti at player location |
| V | Toggle cloud style (Procedural ↔ Paraboloid LUT) |
| [ | Decrease fog density |
| ] | Increase fog density |
| \ | Toggle fog on/off |
| , | Decrease snow amount |
| . | Increase snow amount |
| / | Toggle snow (0.0 ↔ 1.0) |
| T | Toggle terrain wireframe |

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
| Left Shift | Sprint (5x speed) |

### Keyboard - Third Person Mode

| Key | Action |
|-----|--------|
| W | Move player forward (relative to camera) |
| S | Move player backward (relative to camera) |
| A | Move player left (relative to camera) |
| D | Move player right (relative to camera) |
| Space | Jump |
| Left Shift | Sprint |
| Arrow Up | Orbit camera up |
| Arrow Down | Orbit camera down |
| Arrow Left | Orbit camera left |
| Arrow Right | Orbit camera right |
| Q | Zoom camera in |
| E | Zoom camera out |
| Caps Lock | Toggle orientation lock |
| Middle Mouse | Hold orientation lock |

### Mouse Controls

| Input | Action |
|-------|--------|
| M key | Toggle mouse look mode |
| Mouse motion | Camera look (when mouse look enabled) |
| Mouse wheel | Camera zoom |

### Gamepad (SDL3) - Global Controls

| Input | Action |
|-------|--------|
| Right Stick Click | Toggle camera mode (Free Cam ↔ Third Person) |
| Left Stick Click | Toggle sprint |
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
| B / Circle | Toggle orientation lock |
| Left Trigger | Hold orientation lock |
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
├── tools/                # Preprocessing tools
│   ├── terrain_preprocess.cpp
│   ├── erosion_preprocess.cpp
│   └── biome_preprocess.cpp
└── assets/
    └── textures/
        └── crate.png     # Wooden crate texture
```

## Preprocessing Tools

The project includes standalone tools for terrain data preprocessing. Build them with:

```bash
cmake --build build/debug --target terrain_preprocess erosion_preprocess biome_preprocess
```

### terrain_preprocess

Generates tile cache from a 16-bit PNG heightmap.

```bash
./build/debug/tools/terrain_preprocess <heightmap.png> <cache_dir> [options]
  --min-altitude <value>     Altitude for height value 0 (default: 0.0)
  --max-altitude <value>     Altitude for height value 65535 (default: 200.0)
  --meters-per-pixel <value> World scale (default: 1.0)
  --tile-resolution <value>  Output tile resolution (default: 512)
  --lod-levels <value>       Number of LOD levels (default: 4)
```

### erosion_preprocess

Simulates hydraulic erosion and generates flow accumulation data for rivers.

```bash
./build/debug/tools/erosion_preprocess <heightmap.png> <cache_dir> [options]
  --num-droplets <value>       Water droplets to simulate (default: 500000)
  --sea-level <value>          Height below which is sea (default: 0.0)
  --terrain-size <value>       World size in meters (default: 16384.0)
  --output-resolution <value>  Flow map resolution (default: 4096)
```

Outputs: `flow_accumulation.bin`, `flow_direction.bin`, `erosion_preview.png`, `rivers.svg`

### biome_preprocess

Generates biome classification for south coast of England terrain types.

```bash
./build/debug/tools/biome_preprocess <heightmap.png> <erosion_cache> <output_dir> [options]
  --sea-level <value>         Height below which is sea (default: 0.0)
  --terrain-size <value>      World size in meters (default: 16384.0)
  --min-altitude <value>      Min altitude in heightmap (default: 0.0)
  --max-altitude <value>      Max altitude in heightmap (default: 200.0)
  --output-resolution <value> Biome map resolution (default: 1024)
  --num-settlements <value>   Target number of settlements (default: 20)
```

**Biome zones:**
| ID | Zone | Description |
|----|------|-------------|
| 0 | Sea | Below sea level |
| 1 | Beach | Low coastal, gentle slope |
| 2 | Chalk Cliff | Steep coastal slopes |
| 3 | Salt Marsh | Low-lying coastal wetland |
| 4 | River | River channels |
| 5 | Wetland | Inland wet areas near rivers |
| 6 | Grassland | Chalk downs, higher elevation |
| 7 | Agricultural | Flat lowland fields |
| 8 | Woodland | Valleys and sheltered slopes |

**Outputs:**
- `biome_map.png` - RGBA8 data (R=zone, G=subzone, B=settlement_distance)
- `biome_debug.png` - Colored visualization
- `settlements.json` - Settlement locations with metadata

## Dependencies

- SDL3 - Windowing and input
- Vulkan - Graphics API
- GLM - Mathematics
- vk-bootstrap - Vulkan initialization
- VMA - Memory allocation
- stb_image - Texture loading

## License

Crate texture from OpenGameArt.org (CC0).
