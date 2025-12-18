# Async Upload Pattern

Pattern for uploading data to the GPU without blocking the render loop. Used for terrain meshes, textures, and streaming systems.

## Overview

The async upload pattern eliminates GPU stalls by:
1. **Separating CPU work from GPU work** - Data preparation happens on CPU without GPU waits
2. **Using per-frame staging buffers** - Each frame-in-flight gets its own staging buffer
3. **Recording uploads as commands** - `recordUpload()` records transfer commands without blocking
4. **Relying on frame fences** - Natural frame synchronization handles all GPU waits

```
┌─────────────────────────────────────────────────────────────┐
│                    ASYNC UPLOAD TIMELINE                    │
├─────────────────────────────────────────────────────────────┤
│ Frame N-2: GPU executes upload commands from staging[N-2]   │
│ Frame N-1: GPU executes upload commands from staging[N-1]   │
│ Frame N:   CPU writes to staging[N], records upload cmds    │
│            Frame fence ensures staging[N-2] is now safe     │
└─────────────────────────────────────────────────────────────┘
```

## Core Components

### 1. Per-Frame Staging Buffers

Each frame-in-flight needs its own staging buffer to avoid race conditions:

```cpp
class AsyncUploader {
    // Per-frame staging buffers (one per frame in flight)
    std::vector<ManagedBuffer> stagingBuffers_;
    std::vector<void*> stagingMapped_;
    uint32_t framesInFlight_;

    // Device-local destination
    ManagedBuffer deviceBuffer_;
};
```

### 2. Upload State Tracking

Track what needs uploading and for how many frames:

```cpp
// Pending data in CPU memory
std::vector<uint8_t> pendingData_;
bool pendingUpload_ = false;
uint32_t pendingUploadFrames_ = 0;  // Countdown: framesInFlight → 0
```

### 3. The recordUpload() API

The key API that records GPU commands without blocking:

```cpp
void recordUpload(VkCommandBuffer cmd, uint32_t frameIndex);
```

## Implementation Pattern

### Step 1: Initialize Per-Frame Staging

```cpp
bool init(VmaAllocator allocator, VkDeviceSize maxSize, uint32_t framesInFlight) {
    framesInFlight_ = framesInFlight;
    stagingBuffers_.resize(framesInFlight);
    stagingMapped_.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        stagingBuffers_[i] = ManagedBuffer::create(
            allocator,
            maxSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT,
            &stagingMapped_[i]
        );
        if (!stagingBuffers_[i]) return false;
    }

    // Create device-local destination buffer
    deviceBuffer_ = ManagedBuffer::create(
        allocator,
        maxSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        0, nullptr
    );

    return deviceBuffer_.isValid();
}
```

### Step 2: Request Upload (No GPU Wait)

```cpp
void requestUpload(const void* data, size_t size) {
    // Copy to CPU-side pending buffer (no GPU interaction)
    pendingData_.resize(size);
    memcpy(pendingData_.data(), data, size);

    // Mark upload as pending for all frames in flight
    pendingUpload_ = true;
    pendingUploadFrames_ = framesInFlight_;
}
```

### Step 3: Record Upload Commands

```cpp
void recordUpload(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!pendingUpload_) return;

    // Copy pending data to this frame's staging buffer
    memcpy(stagingMapped_[frameIndex], pendingData_.data(), pendingData_.size());

    // Record copy command: staging → device-local
    VkBufferCopy copyRegion{};
    copyRegion.size = pendingData_.size();
    vkCmdCopyBuffer(cmd, stagingBuffers_[frameIndex].get(),
                    deviceBuffer_.get(), 1, &copyRegion);

    // Add appropriate barrier
    Barriers::transferToVertexInput(cmd, deviceBuffer_.get(), copyRegion.size);

    // Track upload progress
    pendingUploadFrames_--;
    if (pendingUploadFrames_ == 0) {
        pendingUpload_ = false;
        pendingData_.clear();
    }
}
```

## Usage in Render Loop

```cpp
void Renderer::render(uint32_t frameIndex) {
    VkCommandBuffer cmd = beginFrame();

    // Record any pending uploads
    meshUploader_.recordUpload(cmd, frameIndex);
    textureCache_.recordTileUpload(cmd, frameIndex);

    // ... rest of rendering ...

    endFrame();  // Frame fence protects staging buffers
}
```

## Texture Upload Variant

For images, use `vkCmdCopyBufferToImage` with layout transitions:

```cpp
void recordTileUpload(TileId id, const void* pixels, uint32_t width, uint32_t height,
                      VkCommandBuffer cmd, uint32_t frameIndex) {
    size_t dataSize = width * height * 4;  // RGBA8

    // Copy to staging buffer
    memcpy(stagingMapped_[frameIndex], pixels, dataSize);

    // Transition to transfer destination
    Barriers::transitionImageLayout(cmd, image_,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        subresourceRange);

    // Copy staging buffer to image region
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {offsetX, offsetY, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(cmd, stagingBuffers_[frameIndex].get(),
                           image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);

    // Transition back to shader read
    Barriers::transitionImageLayout(cmd, image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        subresourceRange);
}
```

## Async Disk Loading (Worker Threads)

For streaming systems, combine with async disk loading:

```cpp
class AsyncLoader {
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{true};

    // Thread-safe request queue
    std::mutex queueMutex_;
    std::priority_queue<LoadRequest> requestQueue_;
    std::condition_variable queueCondition_;

    // Thread-safe results
    std::mutex loadedMutex_;
    std::vector<LoadedData> loadedItems_;

    void workerLoop() {
        while (running_) {
            LoadRequest request;
            {
                std::unique_lock lock(queueMutex_);
                queueCondition_.wait(lock, [&] {
                    return !running_ || !requestQueue_.empty();
                });
                if (!running_) return;
                request = requestQueue_.top();
                requestQueue_.pop();
            }

            // Load from disk (CPU-only, no GPU)
            LoadedData data = loadFromDisk(request);

            {
                std::lock_guard lock(loadedMutex_);
                loadedItems_.push_back(std::move(data));
            }
        }
    }

    std::vector<LoadedData> getLoadedItems() {
        std::lock_guard lock(loadedMutex_);
        return std::exchange(loadedItems_, {});
    }
};
```

## Complete Flow for Streaming System

```
┌───────────────────┐     ┌───────────────────┐     ┌───────────────────┐
│   Main Thread     │     │   Worker Thread   │     │       GPU         │
├───────────────────┤     ├───────────────────┤     ├───────────────────┤
│ 1. Process GPU    │     │                   │     │                   │
│    feedback from  │     │                   │     │                   │
│    frame N-2      │     │                   │     │                   │
│                   │     │                   │     │                   │
│ 2. Queue tiles    │────▶│ 3. Load tiles     │     │                   │
│    for loading    │     │    from disk      │     │                   │
│                   │     │    (async)        │     │                   │
│ 4. Get loaded     │◀────│                   │     │                   │
│    tiles          │     │                   │     │                   │
│                   │     │                   │     │                   │
│ 5. recordUpload() │     │                   │     │                   │
│    for each tile  │─────────────────────────────▶│ 6. Execute copy   │
│                   │     │                   │     │    commands       │
│ 7. Render terrain │─────────────────────────────▶│ 8. Use new tiles  │
│    (writes        │     │                   │     │                   │
│    feedback)      │     │                   │     │                   │
└───────────────────┘     └───────────────────┘     └───────────────────┘
```

## Barrier Helpers

Use `VulkanBarriers.h` for common transitions:

```cpp
// After buffer copy to vertex/index buffer
Barriers::transferToVertexInput(cmd, buffer, size);

// After buffer copy to compute-read buffer
Barriers::transferToCompute(cmd, buffer, size);

// After texture copy
Barriers::transferToFragmentRead(cmd, image, subresourceRange);
```

## Key Invariants

1. **Never wait on GPU in upload path** - All waits happen at frame boundaries
2. **One staging buffer per frame in flight** - Prevents race conditions
3. **Track upload frames** - Continue uploading until all in-flight frames have the new data
4. **Clear pending data only after full upload** - Keep CPU copy until all frames updated

## Existing Implementations

- `TerrainMeshlet` - Fence-free mesh upload (`src/terrain/TerrainMeshlet.h`)
- `TerrainTileCache` - Height tile streaming to array texture (`src/terrain/TerrainTileCache.h`)
- `VirtualTextureCache` - Tile upload to atlas (`src/terrain/VirtualTextureCache.h`)
- `VirtualTexturePageTable` - Page table updates (`src/terrain/VirtualTexturePageTable.h`)
- `VirtualTextureTileLoader` - Async disk loading (`src/terrain/VirtualTextureTileLoader.h`)
