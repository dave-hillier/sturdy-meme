# Wind-Driven Motion Implementation Plan

Notes distilled from Bill Rockenbeck's GDC talk on Ghost of Tsushima's wind systems. Focused on practical, GPU-friendly techniques to make particles, foliage, grass, and cloth feel driven by an authored wind vector plus local disturbances.

## Table of Contents

1. [Guiding Principles](#guiding-principles)
2. [Global Wind Model](#global-wind-model)
3. [Particles and Verticals](#particles-and-verticals)
4. [Animated Particles for Crowds and Wildlife](#animated-particles-for-crowds-and-wildlife)
5. [Trees and Foliage Sway](#trees-and-foliage-sway)
6. [Procedural Grass Response](#procedural-grass-response)
7. [Cloth Simulation](#cloth-simulation)
8. [Collision and Layering](#collision-and-layering)
9. [Performance and Pipeline Notes](#performance-and-pipeline-notes)
10. [Implementation Order](#implementation-order)

---

## Guiding Principles

- Wind is motion, not a separate render pass; every system embeds wind response into its own shaders or simulation.
- Prefer procedural or simulated motion so direction changes instantly reflect gameplay (e.g., guiding wind waypoint changes).
- Heuristics over heavy simulation: prioritize volume and coverage across millions of vertices on modest hardware.

---

## Global Wind Model

- Single primary wind vector, usually pointing from the hero to the current goal; direction is stable, magnitude varies with time-varying Perlin noise.
- Add higher-frequency curl noise for particles to create small swirls without changing overall direction.
- Author override vectors for non-guiding-wind situations (cutscenes, weather presets); scale magnitude for storms.
- Local disturbances (footfalls, character motion) feed the same wind inputs used by foliage and grass displacement.

**Obstacle deflection:** When particles near terrain, sample height a few steps ahead and apply upward bias so gusts rise over hills instead of tunneling into the ground.

---

## Particles and Verticals

- Fully GPU-driven particle system (compute only, async) with expression-based behaviors compiled to shader code.
- Leaves and debris: ~10k active nearby; modeled as discs so they torque and settle believably when hitting ground.
- Terrain snap: particles use a height-map query to clamp to ground or align with slopes.

**Verticals (local wind emitters):**

- Special particle type that only emits wind influence (position, radius, local direction basis).
- All verticals append into a small global array; other emitters sample contributions brute-force.
- Uses: campfire updrafts on leaves, sword-strike gusts to blow out candle particles, scaring wildlife, footfall puffs.

---

## Animated Particles for Crowds and Wildlife

- GPU particles can play skinned animation via a texture of per-joint matrices per frame.
- Each particle picks clip and frame independently; good for armies at distance or lightweight critters (crabs, birds, frogs).
- Keep joint counts small (tens) and textures compact; ideal when CPU-driven rigs would be too expensive.

---

## Trees and Foliage Sway

- Homegrown vertex-skinning on a simple three-level skeleton (chunk, branch, sub-branch); no CPU joint updates.
- Per-vertex data stores branch endpoints; vertex shader computes rotation away from wind and adds sinusoidal bounce with hashed phase to decorrelate neighbors.
- Runs on nearly all verts, including impostors for distant hillsides.
- Optional displacement buffer near the player bends branches aside during traversal using the same skeleton math.

---

## Procedural Grass Response

- GPU pipeline: per-tile compute shader generates potential blades each frame, prunes by view frustum, occlusion, distance, and grass masks; survivors append to a blade buffer for indirect draw.
- Typical scene: ~1M candidate blades, ~100k rendered; double-buffered blade lists let generation for frame N+1 overlap drawing frame N.
- Vertex shader bends blades with wind bias plus per-blade sinusoid; hash phases to avoid lockstep motion.
- Additional influences: bend away from clump center, lean downhill, blend low/high-frequency wind noise.
- Local interaction: displacement buffer around the camera written by special particles (e.g., player footfalls) tilts nearby blades.

---

## Cloth Simulation

- GPU cloth per piece runs in one compute dispatch (single thread group, async). Nodes store current/previous positions (Verlet-style integration) plus per-node max distance from skinned bind pose.
- Sim mesh vs draw mesh: Sim drives motion; draw mesh can have extra detail and is skinned to per-node matrices emitted by the sim.
- Stiffness hack: instead of Hooke's law forces, each spring restores some percentage toward rest length each iteration; stable at larger time steps but not time-step invariant.
- Spring sets: color the constraint graph so no two springs in a set share a node; process sets sequentially to avoid write hazards.
- Anti-stretch anchors: nodes with zero max distance are anchors; each node caches nearest (or two) anchors and enforces a max distance to them, preventing banners and tassels from rubber-banding when attachment points move.
- LOD: reuse the same sim while swapping simpler draw meshes; fade sim motion out at distance (large banners fade later than tassels).

---

## Collision and Layering

- Collision shapes: ellipsoids and sphere pairs (capsules), optionally skinned to character joints; plus one ground plane under the actor.
- Collide nodes only (not triangles). For fast movements, keep cloth on the correct side by also constraining against a plane tangent to the closest point of the original skinned position.
- Limited cloth-over-cloth: upper layer uses offset max-distance spheres skinned to the lower layer, allowing mild relative motion without deep interpenetration; works when deformation is moderate.

---

## Performance and Pipeline Notes

- Everything that moves with wind (particles, foliage, grass, cloth) favors GPU compute in async queues to minimize CPU cost.
- Use small shared buffers (vertical list, blade lists) with atomic append; rely on indirect draws to avoid CPU readback.
- Hash-based phase offsets and noise sampling prevent coherent waves across large fields.
- Height-map access is central: particles snap to ground, wind gusts deflect before hitting terrain, grass placement and bending reference the same data.

---

## Implementation Order

1. Global wind uniform + noise fields (magnitude modulation, curl noise) shared across shaders.
2. Terrain height-map sampling helpers usable by particles/grass/wind deflection.
3. Verticals system: authorable wind emitters appended to a GPU buffer and sampled by particles.
4. Particle wind support: acceleration from wind, height-aware updraft hack, optional animated particles via matrix textures.
5. Tree/foliage vertex sway: per-vertex branch metadata, wind bend + sinusoidal bounce, displacement buffer hooks.
6. Grass integration: compute-driven blade list, wind bend, displacement buffer for footsteps, double buffering.
7. Cloth sim port: per-node data, spring-set processing, anchor constraints, skinned collisions, layering offsets.
8. LOD polish: shared sim with swapped draw meshes, fade distances per asset type.
