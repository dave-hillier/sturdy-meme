# Multi-Threaded Rendering Migration Guide

This document describes how to migrate rendering code to use the multi-threaded infrastructure based on the [Vulkan multi-threading video](https://www.youtube.com/watch?v=...).

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        TaskScheduler                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────┐ │
│  │  Worker 0   │  │  Worker 1   │  │  Worker N   │  │ IO Thread│ │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────┘ │
└─────────────────────────────────────────────────────────────────┘
                              │
         ┌────────────────────┼────────────────────┐
         ▼                    ▼                    ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│ AsyncTransfer   │  │ ThreadedCommand │  │   FrameGraph    │
│ Manager         │  │ Pool            │  │                 │
│                 │  │                 │  │ Pass1 ─► Pass2  │
│ CPU→GPU uploads │  │ Per-frame       │  │   │       │     │
│ with fences     │  │ Per-thread      │  │   └──►Pass3◄┘   │
└─────────────────┘  └─────────────────┘  └─────────────────┘
```

## Component Summary

| Component | Purpose | Location |
|-----------|---------|----------|
| `TaskScheduler` | Thread pool with IO affinity | `src/core/threading/TaskScheduler.h` |
| `AsyncTransferManager` | Non-blocking GPU transfers | `src/core/vulkan/AsyncTransferManager.h` |
| `ThreadedCommandPool` | Per-thread command pools | `src/core/vulkan/ThreadedCommandPool.h` |
| `FrameGraph` | Dependency-driven pass scheduling | `src/core/pipeline/FrameGraph.h` |
| `AsyncTextureUploader` | Non-blocking texture uploads | `src/core/loading/LoadJobFactory.h` |

## Migration Steps

### 1. Async Asset Loading

**Before (blocking):**
```cpp
// In Texture::loadFromFile()
CommandScope scope(device, commandPool, queue);
copyBufferToImage(scope.buffer(), stagingBuffer, image, ...);
// Blocks until complete
```

**After (non-blocking):**
```cpp
// Submit transfer without blocking
auto& transferMgr = renderer.getAsyncTransferManager();
TransferHandle handle = transferMgr.submitImageTransfer(
    pixelData, dataSize,
    image, extent,
    vk::ImageLayout::eShaderReadOnlyOptimal
);

// Option A: Poll for completion
if (transferMgr.isComplete(handle)) {
    // Ready to use
}

// Option B: Wait when needed
transferMgr.wait(handle);

// Option C: Callback on completion
transferMgr.submitImageTransfer(..., []() {
    SDL_Log("Texture ready!");
});
```

**Integration with LoadJobQueue:**
```cpp
void processCompletedJob(Loading::LoadJob& job) {
    if (job.type == Loading::LoadJobType::Texture) {
        // Instead of immediate GPU upload, use async transfer
        auto handle = asyncTransferManager_.submitImageTransfer(
            job.data.data(), job.data.size(),
            targetImage, extent
        );
        pendingTextures_.push_back({job.id, handle, targetImage});
    }
}

// Each frame, check for completed transfers
void Renderer::processPendingTransfers() {
    asyncTransferManager_.processPendingTransfers();
}
```

### 2. Parallel Command Recording

**Before (single-threaded):**
```cpp
void Renderer::render() {
    vkCmd.begin(...);

    // All recording on main thread
    recordShadowPass(cmd);
    recordHDRPass(cmd);    // terrain, grass, trees, water...
    recordPostProcess(cmd);

    vkCmd.end();
    queue.submit(...);
}
```

**After (parallel secondary buffers):**
```cpp
void Renderer::render() {
    auto& pool = getThreadedCommandPool();
    auto& scheduler = TaskScheduler::instance();
    uint32_t frame = getCurrentFrameIndex();

    // Reset pools at frame start
    pool.resetFrame(frame);

    // Primary command buffer
    auto primary = pool.allocatePrimary(frame, 0);
    primary.begin(...);

    // Shadow pass (single-threaded, different render pass)
    recordShadowPass(primary);

    // HDR pass with parallel secondary recording
    primary.beginRenderPass(hdrRenderPassInfo,
        vk::SubpassContents::eSecondaryCommandBuffers);

    std::array<vk::CommandBuffer, 4> secondaries;
    TaskGroup group;

    // Thread 0: Terrain
    scheduler.submit([&, frame]() {
        uint32_t tid = 0;
        auto cmd = pool.allocateSecondary(frame, tid);
        SecondaryCommandBufferScope scope(cmd, hdrRenderPass, 0, framebuffer);
        terrainSystem.record(cmd);
        secondaries[tid] = cmd;
    }, &group);

    // Thread 1: Vegetation (grass + trees)
    scheduler.submit([&, frame]() {
        uint32_t tid = 1;
        auto cmd = pool.allocateSecondary(frame, tid);
        SecondaryCommandBufferScope scope(cmd, hdrRenderPass, 0, framebuffer);
        grassSystem.record(cmd);
        treeRenderer.record(cmd);
        secondaries[tid] = cmd;
    }, &group);

    // Thread 2: Water
    scheduler.submit([&, frame]() {
        uint32_t tid = 2;
        auto cmd = pool.allocateSecondary(frame, tid);
        SecondaryCommandBufferScope scope(cmd, hdrRenderPass, 0, framebuffer);
        waterSystem.record(cmd);
        secondaries[tid] = cmd;
    }, &group);

    // Thread 3: Characters + Objects
    scheduler.submit([&, frame]() {
        uint32_t tid = 3;
        auto cmd = pool.allocateSecondary(frame, tid);
        SecondaryCommandBufferScope scope(cmd, hdrRenderPass, 0, framebuffer);
        skinnedMeshRenderer.record(cmd);
        sceneManager.recordObjects(cmd);
        secondaries[tid] = cmd;
    }, &group);

    // Wait for all recording to complete
    group.wait();

    // Execute in order
    primary.executeCommands(secondaries);
    primary.endRenderPass();

    // Post-process (single-threaded)
    recordPostProcess(primary);

    primary.end();
    queue.submit(...);
}
```

### 3. Frame Graph Integration

**Before (hard-coded sequence):**
```cpp
void Renderer::render() {
    renderPipeline.computeStage.execute(ctx);
    recordShadowPass(cmd);
    renderPipeline.froxelStageFn(ctx);
    recordHDRPass(cmd);
    renderPipeline.postStage.execute(ctx);
}
```

**After (dependency-driven):**
```cpp
// Setup once during init
void Renderer::setupFrameGraph() {
    auto& graph = getFrameGraph();

    auto compute = graph.addPass("Compute", [this](FrameGraph::RenderContext& ctx) {
        terrainSystem.dispatchCompute(ctx.commandBuffer);
        grassSystem.dispatchCompute(ctx.commandBuffer);
    });

    auto shadow = graph.addPass("Shadow", [this](FrameGraph::RenderContext& ctx) {
        recordShadowPass(ctx.commandBuffer, ctx.frameIndex);
    });

    auto froxel = graph.addPass("Froxel", [this](FrameGraph::RenderContext& ctx) {
        froxelSystem.dispatch(ctx.commandBuffer);
    });

    auto hdr = graph.addPass({
        .name = "HDR",
        .execute = [this](FrameGraph::RenderContext& ctx) {
            recordHDRPassParallel(ctx);
        },
        .canUseSecondary = true,  // Enable parallel recording
        .mainThreadOnly = false
    });

    auto post = graph.addPass("PostProcess", [this](FrameGraph::RenderContext& ctx) {
        postProcessSystem.execute(ctx.commandBuffer);
    });

    // Define dependencies
    graph.addDependency(compute, shadow);   // Shadow needs compute results
    graph.addDependency(compute, froxel);   // Froxel can run parallel to shadow
    graph.addDependency(shadow, hdr);       // HDR needs shadow maps
    graph.addDependency(froxel, hdr);       // HDR needs froxel data
    graph.addDependency(hdr, post);         // Post needs HDR output

    graph.compile();

    // Results in execution levels:
    // Level 0: [Compute]
    // Level 1: [Shadow, Froxel]  <- can run in parallel!
    // Level 2: [HDR]
    // Level 3: [PostProcess]
}

// Each frame
void Renderer::render() {
    FrameGraph::RenderContext ctx{
        .commandBuffer = primaryCmd,
        .frameIndex = currentFrame,
        .imageIndex = swapchainImageIndex,
        .deltaTime = dt
    };

    frameGraph_.execute(ctx, &TaskScheduler::instance());
}
```

### 4. Migrating Individual Systems

Individual render systems don't need modification - they already accept `VkCommandBuffer` and
can record to either primary or secondary buffers. The parallelization is handled at the
FrameGraph pass level using slot-based recording:

```cpp
// Define slots in the pass configuration
auto hdr = frameGraph_.addPass({
    .name = "HDR",
    .execute = [this](FrameGraph::RenderContext& ctx) {
        // Called after secondary buffers are recorded
        if (ctx.secondaryBuffers && !ctx.secondaryBuffers->empty()) {
            vkCmd.beginRenderPass(hdrPassInfo, vk::SubpassContents::eSecondaryCommandBuffers);
            vkCmd.executeCommands(*ctx.secondaryBuffers);
            vkCmd.endRenderPass();
        } else {
            // Fallback inline recording
            recordHDRPass(ctx.commandBuffer, ...);
        }
    },
    .canUseSecondary = true,
    .secondarySlots = 3,
    .secondaryRecord = [this](FrameGraph::RenderContext& ctx, uint32_t slot) {
        // Called in parallel for each slot
        switch (slot) {
        case 0:
            systems_->sky().recordDraw(ctx.commandBuffer, ctx.frameIndex);
            systems_->terrain().recordDraw(ctx.commandBuffer, ctx.frameIndex);
            break;
        case 1:
            vkCmd.bindPipeline(...);
            recordSceneObjects(ctx.commandBuffer, ctx.frameIndex);
            break;
        case 2:
            systems_->grass().recordDraw(ctx.commandBuffer, ctx.frameIndex, grassTime);
            systems_->water().recordDraw(ctx.commandBuffer, ctx.frameIndex);
            break;
        }
    }
});
```

Systems are thread-safe for recording because:
- They use per-frame descriptor sets (no per-call allocation)
- They bind their own pipelines and descriptors
- They only read from immutable resources

## Thread Safety Considerations

### Safe Operations (no synchronization needed)
- Reading from immutable resources (textures, static buffers)
- Recording to per-thread command buffers
- Accessing per-frame uniform buffers with proper indexing

### Requires Synchronization
- Descriptor set updates (use per-frame sets)
- Dynamic buffer updates (use staging + transfer)
- Resource creation/destruction (queue to main thread)

### Best Practices

1. **Per-Frame Resources**: Use `FrameIndexedBuffers` for any data updated each frame
   ```cpp
   FrameIndexedBuffers<UniformData> uniforms;  // One buffer per frame
   ```

2. **Descriptor Sets**: Pre-allocate per-frame descriptor sets
   ```cpp
   std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> descriptorSets;
   ```

3. **Command Pools**: Never share command pools between threads
   ```cpp
   // ThreadedCommandPool handles this automatically
   auto cmd = pool.allocateSecondary(frameIndex, threadId);
   ```

4. **Resource Transitions**: Use proper barriers when crossing queue families
   ```cpp
   // AsyncTransferManager handles queue ownership transfer automatically
   ```

## Performance Expectations

Based on the video:
> "the rendering speed is exactly the same... the whole point of adding threading is so we can start adding more advanced rendering techniques"

Multi-threading provides:
- **Headroom** for adding more effects without frame drops
- **Async loading** without render stalls
- **Parallel recording** for draw-call-heavy scenes

The benefits scale with:
- Number of draw calls per frame
- Complexity of render passes
- Amount of runtime asset loading

## Debugging

### Logging
All components log initialization:
```
TaskScheduler: Initialized with 11 workers + 1 IO thread
AsyncTransferManager: Initialized (dedicated transfer: yes)
ThreadedCommandPool: Created 36 command pools
FrameGraph: Compiled with 4 levels
```

### Validation Layers
Vulkan validation layers catch threading errors:
- Command pool used from wrong thread
- Command buffer recorded while in-flight
- Resource accessed without synchronization

### Frame Graph Debug Output
```cpp
SDL_Log("%s", frameGraph_.debugString().c_str());
// Output:
// FrameGraph (5 passes):
//   Compute (id=0)
//   Shadow (id=1) <- [Compute]
//   Froxel (id=2) <- [Compute]
//   HDR (id=3) <- [Shadow, Froxel]
//   PostProcess (id=4) <- [HDR]
//
// Execution order (4 levels):
//   Level 0: Compute
//   Level 1: Shadow, Froxel
//   Level 2: HDR
//   Level 3: PostProcess
```

## Incremental Migration Path

1. **Phase 1**: Initialize infrastructure (done)
   - TaskScheduler starts at application launch
   - AsyncTransferManager and ThreadedCommandPool created in Renderer

2. **Phase 2**: Async loading (done)
   - `AsyncTextureUploader` class for non-blocking texture uploads via `AsyncTransferManager`
   - `processPendingTransfers()` called each frame in render loop
   - VirtualTexture tile loading already uses async pattern (loads tiles in worker threads)

   **Using AsyncTextureUploader:**
   ```cpp
   // Get uploader from renderer
   auto& uploader = renderer.getAsyncTextureUploader();

   // Submit staged texture for async upload
   AsyncTextureHandle handle = uploader.submitTexture(stagedTexture);

   // Each frame, check for completed uploads
   auto completed = uploader.takeAllCompleted();
   for (auto& tex : completed) {
       // tex.image, tex.view, tex.allocation are ready to use
       createDescriptorSet(tex.view);
   }
   ```

3. **Phase 3**: Frame graph (done)
   - Convert RenderPipeline stages to FrameGraph passes
   - Identify parallel opportunities between passes
   - Implementation in `Renderer::setupFrameGraph()`:
     ```
     ComputeStage ──┬──> ShadowPass ──┐
                    ├──> Froxel ──────┼──> HDR ──┬──> SSR ─────────┐
                    └──> WaterGBuffer ┘          ├──> WaterTileCull┼──> PostProcess
                                                 ├──> HiZ ──> Bloom┤
                                                 └──> BilateralGrid┘
     ```
   - Toggle with `Renderer::useFrameGraph` (default: true)
   - Shadow and Froxel can run in parallel after Compute completes
   - SSR, WaterTileCull, HiZ, and BilateralGrid can all run in parallel after HDR

4. **Phase 4**: Secondary command buffers (done)
   - HDR pass uses parallel secondary command buffer recording
   - Draw calls grouped into 3 parallel slots:
     - Slot 0: Sky + Terrain + Catmull-Clark (geometry base)
     - Slot 1: Scene Objects + Skinned Character (scene meshes)
     - Slot 2: Grass + Water + Leaves + Weather + Debug lines (vegetation/effects)
   - FrameGraph extended with `secondarySlots` and `secondaryRecord` in PassConfig
   - RenderContext extended with `threadedCommandPool`, `renderPass`, `framebuffer`, `secondaryBuffers`
   - Implementation:
     ```cpp
     // FrameGraph records secondary buffers in parallel via TaskScheduler
     // Each slot gets its own secondary buffer from ThreadedCommandPool
     // Primary buffer executes all secondaries with executeCommands()
     auto hdr = frameGraph_.addPass({
         .name = "HDR",
         .execute = [this](FrameGraph::RenderContext& ctx) {
             if (ctx.secondaryBuffers && !ctx.secondaryBuffers->empty()) {
                 recordHDRPassWithSecondaries(ctx.commandBuffer, ...);
             } else {
                 recordHDRPass(ctx.commandBuffer, ...);  // Fallback
             }
         },
         .canUseSecondary = true,
         .secondarySlots = 3,
         .secondaryRecord = [this](FrameGraph::RenderContext& ctx, uint32_t slot) {
             recordHDRPassSecondarySlot(ctx.commandBuffer, ..., slot);
         }
     });
     ```

5. **Phase 5**: Advanced parallelism
   - Physics on worker threads
   - AI/gameplay on worker threads
   - Animation updates parallel to rendering
