# 1 Million Trees Architecture

## Overview

Target: Render 1,000,000 trees at 60fps using GPU-driven instancing, following the grass system's proven pattern.

## Current System Analysis

### Grass System (working at 1M scale)
- 1000x1000 grid dispatch = 1M potential blades
- GPU compute performs all culling
- `atomicAdd` to output buffer
- `vkCmdDrawIndirect` for GPU-determined instance count
- ~100k rendered after culling (90% culled)

### Current Tree System Bottlenecks
- CPU-side per-tree iteration O(n)
- CPU sorting for budget LOD
- CPU impostor instance collection
- CPU instance buffer upload each frame

## Proposed Architecture

### Phase 1: GPU Tree Data Buffer

Store all tree source data on GPU:
```cpp
struct TreeSourceGPU {
    vec4 positionScale;    // xyz = position, w = scale
    vec4 rotationArchetype; // x = rotation, y = archetypeIndex, zw = unused
};
```

Buffer size for 1M trees: 1M × 32 bytes = 32 MB (trivial)

### Phase 2: GPU Culling Compute Shader

Similar to grass.comp but for trees:
```glsl
// Inputs
- TreeSourceGPU sourceBuffer[]  // 1M trees
- ClusterGPU clusters[]         // Cluster bounds for hierarchical culling
- ClusterVisibility clusterVis[] // Pre-computed cluster visibility

// Outputs
- TreeFullDetailGPU fullDetailBuffer[]  // Visible full-detail trees
- TreeImpostorGPU impostorBuffer[]      // Visible impostors
- DrawIndirectCommand fullDetailCmd
- DrawIndirectCommand impostorCmd
```

Each thread:
1. Load tree data
2. Check cluster visibility (early out)
3. Frustum cull
4. Distance cull
5. Determine LOD (full detail vs impostor)
6. atomicAdd to appropriate output
7. Write instance data

### Phase 3: Dual Indirect Draw

```cpp
// Full detail pass (complex meshes)
vkCmdDrawIndexedIndirect(cmd, fullDetailIndirectBuffer, ...);

// Impostor pass (simple billboards)
vkCmdDrawIndirect(cmd, impostorIndirectBuffer, ...);
```

### Phase 4: Hierarchical Cluster Culling (GPU)

Pre-pass compute shader:
```glsl
// One thread per cluster (~400 for 1km² with 50m cells)
void main() {
    Cluster c = clusters[gl_GlobalInvocationID.x];

    // Frustum test cluster bounds
    bool visible = isClusterInFrustum(c.bounds);

    // Distance cull cluster
    float dist = distance(cameraPos, c.center);
    if (dist > clusterCullDistance) visible = false;

    // Write visibility
    clusterVisibility[gl_GlobalInvocationID.x] = visible;

    // Determine if cluster forces impostor
    clusterForceImpostor[gl_GlobalInvocationID.x] =
        (dist > clusterImpostorDistance);
}
```

Then tree culling shader reads cluster visibility for early-out.

## Memory Budget

| Buffer | Size | Notes |
|--------|------|-------|
| Tree source data | 32 MB | 1M × 32 bytes |
| Full detail output | 6.4 MB | 100k × 64 bytes max |
| Impostor output | 6.4 MB | 100k × 64 bytes max |
| Cluster data | 19 KB | 400 × 48 bytes |
| Indirect commands | 40 bytes | 2 × 20 bytes |
| **Total** | ~45 MB | Trivial |

## Expected Performance

With 90% culling (matching grass):
- 1M source trees → ~100k visible
- Of those, ~95% impostors, ~5% full detail
- Full detail: ~5k trees (budgeted)
- Impostors: ~95k billboards (cheap)

## Implementation Order

1. **tree_source_buffer.comp** - Generate/load 1M tree positions to GPU buffer
2. **tree_cluster_cull.comp** - GPU cluster visibility pre-pass
3. **tree_instance_cull.comp** - Main GPU culling/LOD shader
4. **TreeGPUForest class** - Orchestrate buffers and dispatches
5. **Indirect draw integration** - Wire up to renderer

## Archetype Handling

With 4 tree archetypes (oak, pine, ash, aspen):
- Store archetype index per tree (2 bits, packed)
- Compute shader outputs archetype in instance data
- Renderer uses multi-draw indirect or instanced draws per archetype

## LOD Blending

For smooth transitions:
- Store blend factor in impostor instance
- Render both full detail AND impostor during blend
- Fade impostor in/out with alpha

## Comparison to AAA Engines

| Feature | Our Target | UE5 Nanite | SpeedTree |
|---------|------------|------------|-----------|
| Tree count | 1M | Millions | Millions |
| Culling | GPU hierarchical | GPU hierarchical | GPU/CPU hybrid |
| LOD | 2-level + blend | Continuous | Multi-level |
| Draw calls | 2 indirect | 1 indirect | Many |
