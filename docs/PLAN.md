# Development Plan

Current state and next steps for the Vulkan outdoor rendering engine.

---

## Completed Systems

These major systems are fully implemented and feature-complete:

### Water Rendering ✅
All 17 phases complete (see `WATER_RENDERING_PLAN.md`):
- Flow maps, foam system, mini G-buffer
- Vector displacement, FBM surface detail
- Screen-space tessellation, PBR lighting
- Refraction, caustics, SSR
- Dual depth buffer, material blending
- Jacobian foam, temporal persistence
- Intersection foam, wake/trail system
- Enhanced subsurface scattering

### Tree Rendering ✅
All 6 phases complete (see `TREE_RENDERING_ROADMAP.md`):
- Spatial partitioning (uniform grid cells)
- Hi-Z occlusion culling
- Two-phase tree-to-leaf culling
- Screen-space error LOD selection
- Temporal coherence caching
- Octahedral impostor mapping

### Grass System ✅
All 7 phases complete (see `plans/grass-system-improvements.md`):
- DisplacementSystem extraction
- VegetationRenderContext
- ParticleSystem decoupling
- TileManager split (tracker + resource pool)
- LOD strategy interface (4 presets)
- Async tile loading with priority queue
- Debug visualization

---

## In Progress: Procedural Cities

Major multi-phase project for generating medieval settlements. See `procedural_cities/` for full documentation.

### Current Status: Not Started

All implementation checklist items remain incomplete.

### Recommended Order

#### Quick Wins (Start Here)
1. **SVG Export from BiomeGenerator** - Export settlements as circles, roads as lines
2. **Blockout cubes** - Place `Mesh::createCube()` at settlement positions
3. **Extend RoadType** - Add Street/Lane/Alley to existing enum
4. **Config files** - Start JSON templates with nlohmann::json

#### Phase 0: 2D Preview Tool (TypeScript)
Browser-based tool for rapid visual iteration:
- Spatial types and polygon operations
- Subdivision algorithms (BSP, grid, frontage)
- Path network with space colonization
- Layout generator
- SVG renderer with pan/zoom

#### Phase 1: C++ Foundation
- Port spatial utilities from TypeScript
- Config system with JSON schema
- Terrain integration interface
- SVG exporter for debugging

#### Phase 2: Geometry Generation
- Path network system
- Water crossing detection (fords vs bridges)
- Layout system with terrain awareness
- Mesh generation (extrusion, roofs)
- Shape grammar system
- Placement system for props

#### Phase 3: Assembly
- Building assembler (footprint → LODs → props)
- Settlement assembler (layout → roads → buildings → navmesh)

#### Phase 4: Runtime
- Streaming system for load/unload
- LOD system with impostor atlas
- Renderer integration following `TreeSystem` pattern

### Visual Milestones
Progress checkpoints from overview:

| Milestone | Description |
|-----------|-------------|
| M1 | World markers - visualize settlement positions |
| M2 | Footprints - 2D layout with colored quads |
| **M2.5** | **Roamable world - character can walk between settlements** |
| M3 | Blockout volumes - extruded boxes with collision |
| M4 | Silhouettes - pitched roofs, towers, crenellations |
| M5 | Structural articulation - timber frames, openings |
| M6 | Material assignment - base colors, weathering |
| M7 | Facade detail - windows, doors, chimneys |
| M8 | Props and ground detail - carts, fences, gardens |
| M9 | Interiors - floor plans, furniture |
| M10 | Polish - full PBR, baked AO |

---

## Backlog: Future Work

From `FUTURE_WORK.md` - features not yet implemented:

### Camera Improvements
- Smoothing (interpolated yaw/pitch/distance)
- Occlusion handling (fade instead of clip)
- Orientation lock (strafe mode)
- Dynamic FOV during sprint

### Animation - Combat & Locomotion ✅
FBX animations are integrated. Available animations include:
- Combat attacks (sword combos, kicks)
- Defense (block, block idle)
- Reactions (hit, death)
- Strafing, turning in place
- Crouch, casting, power-up, sheath

### Procedural Trees
GPU-driven tree generation:
- L-systems or Space Colonization
- Bark textures with normal mapping
- Branch sway with WindSystem

### Painterly Tree Rendering
Stylized approach inspired by The Witness:
- Spherical normals for leaf clumps
- Edge fade at perpendicular angles
- Shadow proxy spheres
- Interior shading with SSS
- Layered wind animation

### Atmosphere - Missing Features
- Paraboloid cloud maps (triple-buffered)
- Cloud temporal reprojection
- Improved Perlin-Worley 3D noise
- Irradiance LUTs

### Post-Processing - Missing Features
- Local tone mapping (bilateral grid)
- Color grading (LUT support)
- Additional tone mappers (GT, AgX)
- Vignette
- Full Purkinje effect (LMSR)

### Wet Surfaces
- Material changes (roughness, albedo, normals)
- Puddle formation in concave areas
- Drying simulation

### Advanced Threading
- Physics on worker threads
- AI/gameplay parallelization
- Animation updates parallel to rendering

---

## Maintenance Tasks

### Tree Rendering Cleanup
Legacy code that can be removed once tested (see `TREE_RENDERING_ROADMAP.md`):
- Debug flags (elevation override, cell index display)
- Single-phase leaf culling shader
- Distance-based LOD code paths
- 17-view impostor atlas code

### Code Quality
- Continue vulkan-hpp migration (run `./scripts/analyze-vulkan-usage.sh`)
- RAII refactoring per `RAII_REFACTOR_PLAN.md`
- Eliminate two-phase init per `PLAN-eliminate-two-phase-init.md`

---

## Priority Recommendations

### High Priority (Major User-Visible Impact)
1. **Procedural Cities M2.5** - Roamable world is the key milestone
2. **Camera smoothing** - Low effort, high polish

### Medium Priority (Quality Improvements)
3. **Painterly tree rendering** - Visual differentiation
4. **Wet surfaces** - Weather immersion

### Lower Priority (Technical Debt)
5. **Tree rendering cleanup** - Remove legacy toggles
6. **Atmosphere improvements** - Cloud quality
7. **Post-processing additions** - Color grading, vignette

---

## Testing Reminders

From CLAUDE.md:
- Build: `cmake --preset debug && cmake --build build/debug`
- Run: `./run-debug.sh`
- Shaders compile via cmake
- Always ensure build compiles AND runs without crashing
