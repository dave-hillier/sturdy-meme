# Dwelling Generator

A procedural house floor plan generator inspired by [Watabou's Dwellings](https://watabou.itch.io/dwellings). Generates multi-story house layouts with rooms, doors, windows, and outputs SVG visualizations.

## Features

- Polyomino-based building footprints (irregular, organic shapes)
- Recursive room subdivision algorithm
- Automatic room type assignment (kitchen, bedroom, bathroom, etc.)
- Door placement (rendered as gaps in walls)
- Window placement on exterior walls
- SVG output with:
  - Individual floor plans
  - Combined multi-floor view
  - Orthographic 3D visualization

## Building

```bash
# From project root
cmake --preset debug && cmake --build build/debug --target dwelling_generator
```

## Usage

```bash
# Basic usage with default parameters
./build/debug/dwelling_generator

# Specify output directory
./build/debug/dwelling_generator -o /path/to/output

# Set random seed for reproducibility
./build/debug/dwelling_generator -s 42

# Generate multi-story house
./build/debug/dwelling_generator -f 3
```

## Command Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `-o, --output <path>` | Output directory | Current directory |
| `-s, --seed <number>` | Random seed | Time-based |
| `-f, --floors <number>` | Number of floors (1-6) | 1 |
| `--min-size <number>` | Minimum cell dimension | 3 |
| `--max-size <number>` | Maximum cell dimension | 7 |
| `--room-size <number>` | Average room size in cells | 6 |
| `--cell-size <number>` | Cell size in pixels | 30 |
| `--windows <0-1>` | Window density | 0.7 |
| `--show-grid` | Show debug grid lines | Off |
| `-h, --help` | Show help message | - |

## Output Files

The generator creates three SVG files:

1. **`dwelling_floor_N.svg`** - Individual floor plan for each floor
   - Shows rooms with colored fills
   - Walls with door gaps
   - Windows as blue lines on exterior walls
   - Room labels

2. **`dwelling_all.svg`** - All floors in a grid layout
   - Useful for comparing floor layouts
   - Shows up to 3 floors per row

3. **`dwelling_3d.svg`** - Orthographic 3D view
   - Isometric projection of the house
   - Stacked floors with walls and roof
   - Windows visible on walls

## Examples

### Simple cottage (seed 12345)
```bash
./dwelling_generator -s 12345 -o examples/cottage
```

### Large manor house
```bash
./dwelling_generator -s 54321 -f 3 --max-size 10 --room-size 8 -o examples/manor
```

### Small cabin
```bash
./dwelling_generator -s 99999 --min-size 2 --max-size 4 --room-size 4 -o examples/cabin
```

### Debug view with grid
```bash
./dwelling_generator -s 11111 --show-grid -o examples/debug
```

## Room Types

The generator assigns these room types based on size and position:

| Type | Color | Notes |
|------|-------|-------|
| Hall | Warm beige | Entry room (has exterior door) |
| Living Room | Wheat | Largest room |
| Kitchen | Moccasin | Second largest |
| Dining Room | Burlywood | - |
| Bedroom | Lavender | - |
| Bathroom | Pale turquoise | Small rooms, always has door |
| Study | Light gray | - |
| Storage | Silver | Smallest rooms |

## Algorithm Overview

1. **Footprint Generation**: Uses polyomino patterns (tetrominos/pentominos) to create irregular building shapes
2. **Room Division**: Recursively divides the floor area by placing interior walls at notch points
3. **Room Connection**: Finds shared edges between rooms and places doors
4. **Type Assignment**: Assigns room types based on size and connectivity
5. **Window Placement**: Places windows on exterior walls that don't have doors

## SVG Structure

The generated SVGs use semantic grouping:

```xml
<g id="room-fills">...</g>      <!-- Room polygons -->
<g id="walls">...</g>           <!-- Wall lines with door gaps -->
<g id="windows">...</g>         <!-- Window markers -->
<g id="room-labels">...</g>     <!-- Room type labels -->
```

Doors are represented as gaps in the wall lines (both interior and exterior walls).
