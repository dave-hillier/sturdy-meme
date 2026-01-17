# Renderer Core Refactoring Plan

**Goal:** Renderer's core responsibility should be:
- Frame synchronization (semaphores, fences, frame index)
- Acquiring swapchain images
- Calling `frameGraph.execute()`
- Presenting

## Implementation Steps

### Step 0: Extract RendererCore Class
Extract the core frame loop logic into a new `RendererCore` class that focuses solely on the target responsibilities. This establishes the clean interface first, with Renderer delegating to it.

**New class:** `src/core/RendererCore.h/cpp`
- Owns `TripleBuffering frameSync_`
- Owns swapchain image acquisition logic
- Calls `frameGraph.execute()`
- Handles presentation
- Renderer becomes a facade that sets up systems and delegates frame execution

### Step 1: Extract Render Pass Recorders
Move `recordShadowPass()` and `recordHDRPass()` (~1500+ lines) into dedicated classes.

**New classes:**
- `src/core/pipeline/passes/ShadowPassRecorder.h/cpp`
- `src/core/pipeline/passes/HDRPassRecorder.h/cpp`

These become proper FrameGraph pass implementations that own their recording logic, removing callback indirection from FrameGraphBuilder.

### Step 2: Move UBO Updates to GlobalBufferManager
Move `updateUniformBuffer()` and `updateLightBuffer()` logic into `GlobalBufferManager`.

**Change:** `GlobalBufferManager::updateFrame(frameData)` handles all UBO updates in one place.

### Step 3: Move Vulkan Infrastructure to VulkanContext
Move swapchain-dependent resource creation:
- `createRenderPass()` → `VulkanContext`
- `createDepthResources()` → `VulkanContext` or `SwapchainResources`
- `createFramebuffers()` → `VulkanContext` or `SwapchainResources`
- `createCommandPool/Buffers()` → `VulkanContext`

### Step 4: Extract FrameDataBuilder Utility
Move `buildFrameData()` to a standalone utility class.

**New class:** `src/core/FrameDataBuilder.h`
```cpp
class FrameDataBuilder {
public:
    static FrameData build(const Camera& camera, const RendererSystems& systems,
                           float deltaTime, float time);
};
```

### Step 5: Move State Sync to Control Subsystems
Move state synchronization methods to `EnvironmentControlSubsystem`:
- `setCloudCoverage()`
- `setCloudDensity()`
- `setSkyExposure()`

### Step 6: Move Debug Visualization to DebugControlSubsystem
Move `updateRoadRiverVisualization()` and `updatePhysicsDebug()` to `DebugControlSubsystem`.

### Step 7: Move Hi-Z Object Data to HiZSystem
Move `updateHiZObjectData()` to `HiZSystem::gatherObjects()`.

### Step 8: Cleanup Legacy Pipeline
Deprecate or move `descriptorSetLayout_`, `pipelineLayout_`, `graphicsPipeline_` once passes are self-contained.

### Step 9: Remove Interface Accessors
Remove getter methods that just delegate to `systems_->*()`. Callers can use `getSystems()` directly.

---

## Target Structure

After refactoring, `RendererCore` handles the frame loop (~200-300 lines):

```cpp
class RendererCore {
public:
    bool executeFrame(const FrameData& frameData, const RenderResources& resources);

private:
    TripleBuffering frameSync_;
    FrameGraph& frameGraph_;
    VulkanContext& vulkanContext_;
};
```

`Renderer` becomes a setup/facade class that:
- Initializes all subsystems
- Builds `FrameData` and `RenderResources`
- Delegates frame execution to `RendererCore`

---

## Progress Tracking

- [x] Step 0: Extract RendererCore class
- [x] Step 1: Extract render pass recorders (ShadowPassRecorder, HDRPassRecorder)
- [x] Step 2: Move UBO updates to UBOUpdater (following existing updater pattern)
- [ ] Step 3: Move Vulkan infrastructure to VulkanContext
- [x] Step 4: Extract FrameDataBuilder utility
- [x] Step 5: Move state sync to control subsystems (already done in main branch)
- [ ] Step 6: Move debug visualization
- [ ] Step 7: Move Hi-Z object data
- [ ] Step 8: Cleanup legacy pipeline
- [ ] Step 9: Remove interface accessors
