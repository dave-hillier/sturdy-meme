# Procedural Cities Implementation Checklist

Based on the analysis in [PROCEDURAL_CITIES_ANALYSIS.md](./PROCEDURAL_CITIES_ANALYSIS.md).

---

## Phase 0: Design Decisions (Answer Before Implementation)

### Scope & Ambition
- [ ] **Q1**: Target scale - choose one:
  - [ ] A) Few hand-placed hero settlements with procedural details
  - [ ] B) Dozens of fully procedural settlements across terrain
  - [ ] C) Hybrid: major towns hand-authored, hamlets procedural

- [ ] **Q2**: Level of detail - choose one:
  - [ ] A) Distant silhouettes only (impostor cards)
  - [ ] B) Walkable exteriors with simple geometry
  - [ ] C) Enterable interiors for some buildings
  - [ ] D) Full interior detail throughout

- [ ] **Q3**: Static or dynamic - choose one:
  - [ ] A) Baked at build time, immutable
  - [ ] B) Runtime procedural generation (streaming)
  - [ ] C) Player-modifiable (construction/destruction)

### Historical Period & Style
- [ ] **Q4**: Historical period - choose one:
  - [ ] A) Medieval (1066-1485)
  - [ ] B) Tudor (1485-1603)
  - [ ] C) Georgian (1714-1830)
  - [ ] D) Fantasy/anachronistic mix

- [ ] **Q5**: Required building types - check all needed:
  - [ ] Houses (cottages, townhouses, manor)
  - [ ] Agricultural (barns, granaries, mills)
  - [ ] Commercial (shops, inns, markets)
  - [ ] Religious (churches, chapels)
  - [ ] Civic (guild halls, town halls)
  - [ ] Defensive (walls, gates, castles)
  - [ ] Industrial (smithies, tanneries)

### Technical Approach
- [ ] **Q6**: Urban layout algorithm - choose one:
  - [ ] A) Template-based (fast, predictable)
  - [ ] B) L-system/grammar (organic, complex)
  - [ ] C) Agent simulation (emergent, slow)
  - [ ] D) Simple grid with noise

- [ ] **Q7**: Building generation method - choose one:
  - [ ] A) Prefab library
  - [ ] B) Modular kit (snap-together pieces)
  - [ ] C) Full procedural (shape grammar)
  - [ ] D) Hybrid: prefab base + procedural details

- [ ] **Q8**: Generation timing - choose one:
  - [ ] A) Offline (build-time tool)
  - [ ] B) Runtime (on-demand)
  - [ ] C) Hybrid (coarse baked, details runtime)

### Integration & Performance
- [ ] **Q9**: LOD strategy - choose one:
  - [ ] A) Discrete LOD meshes
  - [ ] B) Impostor billboards at distance
  - [ ] C) HLOD (merge distant buildings)
  - [ ] D) Combination

- [ ] **Q10**: Terrain interaction - choose one:
  - [ ] A) Flatten terrain under buildings
  - [ ] B) Buildings adapt to slope
  - [ ] C) Retaining walls/foundations
  - [ ] D) Restrict to flat areas only

- [ ] **Q11**: Culling/streaming - choose one:
  - [ ] A) Per-building frustum culling
  - [ ] B) Cluster-based culling
  - [ ] C) Streaming tiles
  - [ ] D) GPU-driven indirect rendering

### Art Direction
- [ ] **Q12**: Visual fidelity - choose one:
  - [ ] A) Stylized/low-poly
  - [ ] B) Realistic PBR
  - [ ] C) Painterly (match tree style)

- [ ] **Q13**: Texture approach - choose one:
  - [ ] A) Unique textures per building
  - [ ] B) Texture atlas
  - [ ] C) Procedural textures
  - [ ] D) Trim sheets + tiling materials

---

## Phase 1: Single Building Prototype

- [ ] Create basic building mesh (cottage)
- [ ] Add building textures (albedo, normal, roughness)
- [ ] Create `BuildingSystem` class following InitInfo pattern
- [ ] Place single building at one settlement point
- [ ] Integrate with shadow system
- [ ] Test building renders correctly with terrain

---

## Phase 2: Multiple Building Types

- [ ] Define building type enum (Cottage, Barn, Church, Inn, etc.)
- [ ] Create/obtain meshes for each building type
- [ ] Add textures for each building type
- [ ] Implement random building selection per settlement
- [ ] Test variety across multiple settlement points

---

## Phase 3: Simple Layout

- [ ] Generate building positions around settlement center
- [ ] Implement minimum spacing between buildings
- [ ] Add rotation variation for buildings
- [ ] Ensure buildings don't overlap
- [ ] Test layout looks natural

---

## Phase 4: Street Network

- [ ] Design street data structure
- [ ] Generate main street through settlement
- [ ] Add side streets/lanes
- [ ] Integrate streets with road network from RoadPathfinder
- [ ] Render street surfaces (modify virtual texture or separate mesh)

---

## Phase 5: Plot Subdivision

- [ ] Divide settlement area into building plots
- [ ] Assign building types to plots based on rules
- [ ] Implement plot boundary visualization (fences/walls)
- [ ] Handle irregular plot shapes

---

## Phase 6: LOD System

- [ ] Create LOD0-2 meshes for buildings
- [ ] Implement LOD selection based on distance
- [ ] Add impostor billboards for distant buildings
- [ ] Integrate with HiZ occlusion culling
- [ ] Profile and optimize draw calls

---

## Phase 7: Texture Variety

- [ ] Create texture variations for building types
- [ ] Add weathering/aging variation
- [ ] Implement procedural color tinting
- [ ] Test visual variety across settlements

---

## Phase 8: Settlement Character

- [ ] Differentiate coastal vs inland settlements
- [ ] Add settlement-specific features (harbour, market square)
- [ ] Implement settlement size variation
- [ ] Add landmark buildings (church tower visible from distance)

---

## Future Enhancements (Post-MVP)

- [ ] Building interiors
- [ ] NPC pathfinding within settlements
- [ ] Day/night lighting in buildings
- [ ] Destructible buildings
- [ ] Player construction
- [ ] Population simulation

---

## Integration Points

### Existing Systems to Connect
- [ ] `TerrainSystem` - height queries for placement
- [ ] `GrassSystem` - suppress grass under buildings
- [ ] `ShadowSystem` - buildings cast shadows
- [ ] `FroxelSystem` - fog integration
- [ ] `VirtualTextureSystem` - potential street textures

### New Files to Create
- [ ] `src/settlement/BuildingSystem.h`
- [ ] `src/settlement/BuildingSystem.cpp`
- [ ] `src/settlement/SettlementLayout.h`
- [ ] `src/settlement/SettlementLayout.cpp`
- [ ] `tools/settlement_generator/main.cpp` (if build-time)
- [ ] `shaders/building.vert`
- [ ] `shaders/building.frag`

---

## Testing Checklist

### Per Milestone
- [ ] Build compiles without errors
- [ ] Build runs without crashing
- [ ] Visual output looks correct
- [ ] No performance regression (check frame time)
- [ ] Shadows work correctly
- [ ] Fog/atmosphere integrates properly

### Manual Testing Steps
- [ ] Fly camera to settlement locations
- [ ] Verify buildings appear at correct positions
- [ ] Check building variety
- [ ] Test LOD transitions (approach/recede)
- [ ] Verify street network connectivity
- [ ] Check building-terrain interaction
