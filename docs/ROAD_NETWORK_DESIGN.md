# Road Network Generation Design

This document outlines the road network generation system.

## Implementation Status

- [x] Space colonization algorithm (`SpaceColonization.h/cpp`)
- [x] SVG output for roads (`RoadSVG.h/cpp`)
- [x] A* pathfinding with terrain awareness
- [x] Settlement areas with geography masking
- [ ] Bridge/ford detection at river crossings
- [ ] Settlement internal streets generator
- [ ] Integration of colonization topology with A* routing

## Overview

Road generation operates at two distinct levels with different algorithms:

1. **Regional Network** - Connections between settlements
2. **Settlement Streets** - Internal road layout within each settlement

## Regional Network

### Network Topology: Space Colonization

Use space colonization algorithm to determine which settlements connect, creating organic branching patterns rather than exhaustive pairwise connections.

**Algorithm:**
1. Initialize with largest settlements (towns) as root nodes
2. All other settlements are "attraction points"
3. Grow network branches toward nearest attraction points
4. When a branch reaches an attraction point, it becomes a new growth node
5. Continue until all settlements are connected

**Benefits:**
- Natural hierarchical structure (main roads branch into secondary roads)
- Avoids redundant connections (two hamlets near a village share a branch point)
- Creates explicit junction nodes in the network
- Road type naturally follows hierarchy depth

### Route Finding: A* Pathfinding

Once topology is determined, use A* to find terrain-aware routes between connected nodes.

**Cost factors (existing in `RoadPathfinder`):**
- Slope penalty (`slopeCostMultiplier`)
- Water crossing penalty (`waterPenalty`)
- Cliff avoidance (`cliffPenalty`)

**Enhancements needed:**
- Valley preference (follow contours)
- Ford/bridge detection at river crossings
- Prefer existing road segments for shared routes

### Bridge and Ford Detection

When A* path crosses water zones:
1. Mark crossing point as potential bridge/ford
2. Classify based on:
   - River width (stream order from watershed data)
   - Road type (main roads get bridges, footpaths get fords)
   - Slope at crossing (fords need gentle banks)
3. Output bridge/ford locations in network data

## Settlement Streets

Generated separately, constrained to settlement area (radius + geography mask).

### Layout Patterns by Settlement Type

**Town:**
- Central market square
- Radial streets from center
- Ring road near edge
- Multiple entry points for regional roads

**Village:**
- Main street through center
- Side lanes branching off
- Church/green as focal point
- 2-3 regional road connections

**Fishing Village:**
- Linear layout along waterfront
- Perpendicular lanes to shore
- Harbour area at water's edge
- Single main road inland

**Hamlet:**
- Simple crossroads or T-junction
- Few buildings, minimal internal roads
- Single connection point to regional network

### Connection Points

Each settlement exports "entry points" at its edge where regional roads connect. These are:
- Located where internal main streets meet settlement boundary
- Positioned to face connected settlements
- Avoid water/cliff zones

## Data Pipeline

```
1. biome_preprocess
   └── settlements.json (positions, radii, types, features)

2. settlement_streets (new tool)
   ├── Input: settlements.json, biome_map.png
   └── Output: settlement_streets.json (internal layouts + entry points)

3. road_generator (enhanced)
   ├── Input: settlements.json, settlement_streets.json, heightmap, biome_map
   ├── Algorithm: Space colonization → A* routing
   └── Output: roads.json, roads.bin (includes bridges/fords)
```

## Output Format Extensions

### roads.json additions

```json
{
  "junctions": [
    {"id": 0, "x": 1234.5, "z": 5678.9, "type": "branch_point"},
    {"id": 1, "x": 2000.0, "z": 3000.0, "type": "bridge", "river_width": 15.0}
  ],
  "roads": [
    {
      "from_junction": 0,
      "to_junction": 1,
      "type": "road",
      "control_points": [...]
    }
  ]
}
```

### settlement_streets.json

```json
{
  "settlements": [
    {
      "id": 0,
      "entry_points": [
        {"x": 100.0, "z": 200.0, "direction": 45.0, "connects_to": "regional"}
      ],
      "streets": [
        {"type": "main_street", "points": [...]}
      ],
      "landmarks": [
        {"type": "market_square", "x": 50.0, "z": 50.0}
      ]
    }
  ]
}
```

## Current Usage

```bash
# Basic usage (A* pathfinding with existing topology)
./road_generator heightmap.png biome_map.png settlements.json ./output

# With space colonization for network topology
./road_generator heightmap.png biome_map.png settlements.json ./output --use-colonization
```

**Output files:**
- `roads.json` - Road network in JSON format
- `roads.bin` - Binary format for runtime loading
- `roads.svg` - SVG visualization with Catmull-Rom splines
- `roads_debug.png` - Debug raster visualization
- `network.svg` - Network topology (if `--use-colonization`)

## References

- Space Colonization: Runions et al. "Modeling Trees with a Space Colonization Algorithm"
- A* Pathfinding: existing `RoadPathfinder` implementation
- Settlement patterns: Historic English village morphology
