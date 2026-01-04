# town_generator Faithful Port: Remaining Implementation Plan

## Current Status

The port has complete implementations for:
- All geometry primitives (Point, Polygon, Segment, Circle, Spline, Graph)
- Voronoi tessellation and Delaunay triangulation
- A* pathfinding in Graph
- Topology infrastructure for street networks
- CurtainWall for wall/gate/tower generation
- All 13 ward types

**What's simplified/disabled:**
1. `buildPatches()` uses grid layout instead of Voronoi spiral
2. `buildWalls()` is a stub that sets `wallsNeeded = false`
3. `buildStreets()` is empty
4. `buildRoads()` is empty
5. Ward geometry uses generic `createAlleys()` fallback

## Phase 1: Voronoi-Based Patch Generation

### 1.1 Implement Spiral Point Generation
**File:** `src/building/Model.cpp` - `buildPatches()`

Replace grid-based layout with original spiral algorithm:
```
sa = random angle in [0, 2π]
for i in 0..nPatches:
    a = sa + sqrt(i) * 5
    r = (i == 0) ? 0 : 10 + i * (2 + random())
    point = (cos(a) * r, sin(a) * r)
```

### 1.2 Apply Lloyd Relaxation to Central Patches
Relax the inner patches (those within city walls) to create more uniform cell sizes:
- Perform 3 iterations of Lloyd relaxation on inner patches only
- Use `Voronoi::relax()` which is already implemented

### 1.3 Convert Voronoi Regions to Patches
For each Region from `voronoi.partitioning()`:
- Create Polygon from circumcenters of the region's triangles
- Create Patch with that shape
- Sort patches by distance from center
- Mark first patch as plaza location if `plazaNeeded`
- Mark last inner patch as citadel location if `citadelNeeded`

### 1.4 Establish Patch Neighbor Topology
For each pair of adjacent Voronoi regions:
- Record shared edge vertices
- Store neighbor relationships in `Patch::neighbors` vector
- This is critical for wall boundary detection

---

## Phase 2: Wall Building

### 2.1 Determine Inner vs Outer Patches
**File:** `src/building/Model.cpp` - `buildWalls()`

- Sort patches by distance from center
- Inner patches = first N patches based on city size
- Calculate convex hull of inner patches
- Use `findCircumference()` to get wall boundary polygon

### 2.2 Create CurtainWall Objects
For `wallsNeeded = true`:
```cpp
wall = new CurtainWall(true, this, inner, citadelPatches);
```

For `citadelNeeded = true`:
```cpp
citadel = new CurtainWall(false, this, citadelPatches, {});
```

### 2.3 Build Gates and Towers
CurtainWall already implements:
- `buildGates()` - places gates on wall segments facing outward
- `buildTowers()` - places towers at wall vertices

Collect all gates into `Model::gates` vector for street routing.

---

## Phase 3: Street Network

### 3.1 Instantiate Topology
**File:** `src/building/Model.cpp` - `buildStreets()`

```cpp
topology = std::make_unique<Topology>(this);
```

Topology constructor must:
- Process all patch vertices as graph nodes
- Mark nodes blocked by walls/citadel
- Separate inner/outer node sets

### 3.2 Create Arteries (Main Streets)
For each gate in `gates`:
- Find path from gate to plaza center using A*
- Store path in `arteries` vector
- These become the main road network

### 3.3 Smooth Street Paths
Apply equalization to street vertices:
- Distribute points evenly along path length
- This creates smoother curved streets

### 3.4 Build Exterior Roads
For each gate:
- Find outer topology node in gate direction
- Build path from gate outward
- Store in `roads` vector

---

## Phase 4: Ward Geometry Improvements

### 4.1 Fix getCityBlock() for Street Insets
**File:** `src/wards/Ward.cpp`

The ward shape should be inset by:
- `streetWidth` on edges adjacent to streets
- `wallWidth` on edges adjacent to walls
- 0 on edges adjacent to other wards

This requires knowing which patch edges touch streets vs walls.

### 4.2 Ward-Specific Geometry Overrides

**Castle** (`src/wards/Castle.cpp`):
- Create central keep polygon (rectangular)
- Add corner towers at keep vertices
- Add wall towers along citadel perimeter
- Current: ~40% complete, needs corner towers on keep

**Cathedral** (`src/wards/Cathedral.cpp`):
- Create cross-shaped floor plan
- Add tower at intersection
- Add entrance portico
- Current: ~50% complete, basic cross shape works

**Market** (`src/wards/Market.cpp`):
- Create ring of market stalls around central plaza
- Add central feature (fountain/well)
- Create varied stall sizes
- Current: ~60% complete

**GateWard** (`src/wards/GateWard.cpp`):
- Create buildings flanking gate entrance
- Leave passage clear through ward center
- Add guard structures
- Current: Not used (gates not created)

**Farm** (`src/wards/Farm.cpp`):
- Create farmhouse
- Add barn/outbuildings
- Create field divisions (fences)
- Current: ~30% complete, just farmhouse + barn

**Park** (`src/wards/Park.cpp`):
- Create paths through park
- Add trees/vegetation markers
- Add benches/features
- Current: Empty (no geometry)

---

## Phase 5: SVG Output Enhancements

### 5.1 Render Streets
**File:** `src/svg/SVGWriter.cpp`

Add rendering for:
- `model.streets` - secondary streets (thinner)
- `model.arteries` - main roads (thicker)
- `model.roads` - exterior roads (dashed or different color)

### 5.2 Render Walls
- Render `wall->shape` as thick line
- Render tower positions as circles/squares
- Render gates as breaks in wall line

### 5.3 Render Citadel
- Render `citadel->shape` as thick line (different color)
- Render citadel towers

---

## Implementation Order

1. **Phase 1.1-1.3**: Voronoi patches (core change)
2. **Phase 1.4**: Patch neighbors
3. **Phase 2.1-2.2**: Wall creation
4. **Phase 5.2-5.3**: Wall rendering (verify walls visually)
5. **Phase 2.3**: Gates and towers
6. **Phase 3.1-3.2**: Street arteries
7. **Phase 3.3-3.4**: Street smoothing and roads
8. **Phase 5.1**: Street rendering
9. **Phase 4.1**: Street-aware ward insets
10. **Phase 4.2**: Ward geometry improvements

Each phase should produce a working, renderable output.

---

## Key Files to Modify

| File | Changes |
|------|---------|
| `src/building/Model.cpp` | buildPatches, buildWalls, buildStreets, buildRoads |
| `src/building/Patch.cpp` | Add neighbors vector, fromRegion() factory |
| `include/town_generator/building/Patch.h` | Add neighbors member |
| `src/building/Topology.cpp` | Verify processPoint works with Voronoi patches |
| `src/svg/SVGWriter.cpp` | Add street/wall/gate rendering |
| `src/wards/Ward.cpp` | Fix getCityBlock() for proper insets |
| `src/wards/*.cpp` | Improve individual ward geometry |

---

## Testing Strategy

After each phase:
1. Generate cities with various seeds (42, 12345, 99999)
2. Generate different sizes (small=15, medium=30, large=60 patches)
3. Verify SVG renders without errors
4. Visual inspection for:
   - Organic patch shapes (not grid-like)
   - Wall surrounds inner city
   - Streets connect gates to center
   - Buildings don't overlap streets/walls
