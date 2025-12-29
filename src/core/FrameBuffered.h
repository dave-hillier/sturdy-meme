#pragma once

// ============================================================================
// FrameBuffered.h - Generic template for triple-buffered (or N-buffered) resources
// ============================================================================
//
// This template provides a generic way to manage per-frame resources with
// automatic frame cycling. It encapsulates the common pattern of having
// N copies of a resource (one per frame-in-flight) and cycling through them.
//
// Key design principles:
// - Generic: Works with any type T (buffers, descriptors, sync primitives, etc.)
// - Frame index as source of truth: All access is based on frame index
// - Safe modulo arithmetic: Handles wraparound correctly
// - Non-owning by default: Doesn't manage lifecycle of T (use RAII types for T)
//
// Usage examples:
//
// 1. Simple per-frame buffers:
//    FrameBuffered<VkBuffer> uniformBuffers(3);
//    uniformBuffers[frameIndex] = createBuffer(...);
//    vkCmdBindBuffer(cmd, uniformBuffers.current());
//    uniformBuffers.advance();
//
// 2. Per-frame descriptor sets:
//    FrameBuffered<VkDescriptorSet> descriptorSets;
//    descriptorSets.resize(3, allocateDescriptorSet());
//    vkCmdBindDescriptorSets(..., descriptorSets.current(), ...);
//
// 3. Aggregate per-frame data:
//    struct FrameData { VkBuffer ubo; VkDescriptorSet desc; };
//    FrameBuffered<FrameData> frames(3);
//    frames.current().ubo = ...;
//
// 4. With initialization factory:
//    auto frames = FrameBuffered<MyResource>::create(3, []() {
//        return MyResource::create();
//    });
//

#include <cstdint>
#include <vector>
#include <functional>
#include <cassert>

template<typename T>
class FrameBuffered {
public:
    // Default frame count for triple buffering
    static constexpr uint32_t DEFAULT_FRAME_COUNT = 3;

    // =========================================================================
    // Constructors
    // =========================================================================

    // Default constructor - must call resize() before use
    FrameBuffered() = default;

    // Construct with frame count (default-constructs T for each frame)
    explicit FrameBuffered(uint32_t frameCount)
        : resources_(frameCount)
        , frameCount_(frameCount)
        , currentFrame_(0) {}

    // Construct with frame count and initial value (copies value to each frame)
    FrameBuffered(uint32_t frameCount, const T& initialValue)
        : resources_(frameCount, initialValue)
        , frameCount_(frameCount)
        , currentFrame_(0) {}

    // Factory function: create with a generator for each frame
    static FrameBuffered create(uint32_t frameCount, std::function<T(uint32_t)> generator) {
        FrameBuffered fb;
        fb.resources_.reserve(frameCount);
        for (uint32_t i = 0; i < frameCount; ++i) {
            fb.resources_.push_back(generator(i));
        }
        fb.frameCount_ = frameCount;
        fb.currentFrame_ = 0;
        return fb;
    }

    // =========================================================================
    // Initialization / Resize
    // =========================================================================

    // Resize with default-constructed T
    void resize(uint32_t frameCount) {
        resources_.resize(frameCount);
        frameCount_ = frameCount;
        currentFrame_ = 0;
    }

    // Resize with initial value
    void resize(uint32_t frameCount, const T& initialValue) {
        resources_.resize(frameCount, initialValue);
        frameCount_ = frameCount;
        currentFrame_ = 0;
    }

    // Resize with factory function
    void resize(uint32_t frameCount, std::function<T(uint32_t)> generator) {
        resources_.clear();
        resources_.reserve(frameCount);
        for (uint32_t i = 0; i < frameCount; ++i) {
            resources_.push_back(generator(i));
        }
        frameCount_ = frameCount;
        currentFrame_ = 0;
    }

    // Clear all resources
    void clear() {
        resources_.clear();
        frameCount_ = 0;
        currentFrame_ = 0;
    }

    // =========================================================================
    // Frame Index Management
    // =========================================================================

    // Get frame count
    uint32_t frameCount() const { return frameCount_; }

    // Get current frame index
    uint32_t currentIndex() const { return currentFrame_; }

    // Get previous frame index (wraps around)
    uint32_t previousIndex() const {
        return (currentFrame_ + frameCount_ - 1) % frameCount_;
    }

    // Get next frame index (wraps around)
    uint32_t nextIndex() const {
        return (currentFrame_ + 1) % frameCount_;
    }

    // Advance to next frame (call at end of render loop)
    void advance() {
        currentFrame_ = (currentFrame_ + 1) % frameCount_;
    }

    // Reset to first frame
    void reset() {
        currentFrame_ = 0;
    }

    // Apply modulo to wrap any index
    uint32_t wrapIndex(uint32_t index) const {
        return index % frameCount_;
    }

    // Get pointer to current frame index (for legacy code needing pointer access)
    const uint32_t* currentIndexPtr() const { return &currentFrame_; }

    // =========================================================================
    // Resource Access
    // =========================================================================

    // Get current frame's resource
    T& current() {
        assert(frameCount_ > 0 && "FrameBuffered not initialized");
        return resources_[currentFrame_];
    }

    const T& current() const {
        assert(frameCount_ > 0 && "FrameBuffered not initialized");
        return resources_[currentFrame_];
    }

    // Get previous frame's resource
    T& previous() {
        assert(frameCount_ > 0 && "FrameBuffered not initialized");
        return resources_[previousIndex()];
    }

    const T& previous() const {
        assert(frameCount_ > 0 && "FrameBuffered not initialized");
        return resources_[previousIndex()];
    }

    // Get next frame's resource
    T& next() {
        assert(frameCount_ > 0 && "FrameBuffered not initialized");
        return resources_[nextIndex()];
    }

    const T& next() const {
        assert(frameCount_ > 0 && "FrameBuffered not initialized");
        return resources_[nextIndex()];
    }

    // Get resource at specific frame index (with automatic wraparound)
    T& at(uint32_t frameIndex) {
        assert(frameCount_ > 0 && "FrameBuffered not initialized");
        return resources_[frameIndex % frameCount_];
    }

    const T& at(uint32_t frameIndex) const {
        assert(frameCount_ > 0 && "FrameBuffered not initialized");
        return resources_[frameIndex % frameCount_];
    }

    // Array-style access (no modulo - caller must ensure valid index)
    T& operator[](uint32_t index) {
        assert(index < frameCount_ && "Index out of bounds");
        return resources_[index];
    }

    const T& operator[](uint32_t index) const {
        assert(index < frameCount_ && "Index out of bounds");
        return resources_[index];
    }

    // =========================================================================
    // Iteration
    // =========================================================================

    auto begin() { return resources_.begin(); }
    auto end() { return resources_.end(); }
    auto begin() const { return resources_.begin(); }
    auto end() const { return resources_.end(); }

    // Check if initialized
    bool empty() const { return resources_.empty(); }
    uint32_t size() const { return frameCount_; }

    // =========================================================================
    // Bulk Operations
    // =========================================================================

    // Apply a function to all frames
    template<typename Func>
    void forEach(Func&& func) {
        for (uint32_t i = 0; i < frameCount_; ++i) {
            func(i, resources_[i]);
        }
    }

    template<typename Func>
    void forEach(Func&& func) const {
        for (uint32_t i = 0; i < frameCount_; ++i) {
            func(i, resources_[i]);
        }
    }

private:
    std::vector<T> resources_;
    uint32_t frameCount_ = 0;
    uint32_t currentFrame_ = 0;
};

// ============================================================================
// Type alias for triple buffering specifically
// ============================================================================
template<typename T>
using TripleBuffered = FrameBuffered<T>;
