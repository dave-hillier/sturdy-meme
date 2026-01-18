#pragma once

#include <cstdint>

/**
 * BufferSetManager - Manages double/triple-buffered resource sets
 *
 * Used by systems that need to swap between compute-write and graphics-read
 * buffer sets each frame to avoid GPU read/CPU write conflicts.
 *
 * This is a lightweight utility extracted from ParticleSystem to allow
 * both GrassSystem and ParticleSystem to compose from the same parts.
 *
 * Usage:
 *   BufferSetManager bufferSets(3);  // Triple-buffered
 *
 *   // In record loop:
 *   uint32_t writeSet = bufferSets.getComputeSet();  // Compute writes here
 *   uint32_t readSet = bufferSets.getRenderSet();    // Graphics reads here
 *
 *   // At frame end:
 *   bufferSets.advance();  // Swap sets for next frame
 */
class BufferSetManager {
public:
    explicit BufferSetManager(uint32_t setCount = 2)
        : setCount_(setCount)
        , computeSet_(0)
        , renderSet_(setCount > 1 ? 1 : 0) {}

    /**
     * Advance to next buffer set configuration.
     * Call at frame start or end to swap compute/render sets.
     */
    void advance() {
        computeSet_ = (computeSet_ + 1) % setCount_;
        renderSet_ = (renderSet_ + 1) % setCount_;
    }

    /**
     * Get the buffer set index for compute writes.
     */
    uint32_t getComputeSet() const { return computeSet_; }

    /**
     * Get the buffer set index for graphics reads.
     */
    uint32_t getRenderSet() const { return renderSet_; }

    /**
     * Get total number of buffer sets.
     */
    uint32_t getSetCount() const { return setCount_; }

    /**
     * Reset to initial state (compute=0, render=1).
     */
    void reset() {
        computeSet_ = 0;
        renderSet_ = setCount_ > 1 ? 1 : 0;
    }

private:
    uint32_t setCount_;
    uint32_t computeSet_;
    uint32_t renderSet_;
};
