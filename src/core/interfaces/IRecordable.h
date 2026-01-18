#pragma once

// ============================================================================
// IRecordable.h - Interface for systems that record draw commands
// ============================================================================
//
// This interface allows pass recorders to work with any system that can
// record draw commands, enabling polymorphic rendering and easier testing.
//
// Benefits:
// - Decouples pass recorders from concrete system types
// - Enables mock implementations for unit testing
// - Makes system contracts explicit
// - Allows future systems to integrate without modifying pass recorders
//

#include <vulkan/vulkan.h>
#include <cstdint>

/**
 * Interface for systems that record draw commands to a command buffer.
 *
 * Implement this interface for systems that participate in render passes.
 * The interface is intentionally minimal to reduce coupling.
 */
class IRecordable {
public:
    virtual ~IRecordable() = default;

    /**
     * Record draw commands to the command buffer.
     *
     * @param cmd Command buffer to record to (must be in recording state)
     * @param frameIndex Current frame index for triple-buffered resources
     */
    virtual void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) = 0;
};

/**
 * Extended interface for systems that need time for animation.
 *
 * Use this for systems with animated content (grass, weather, leaves, etc.)
 */
class IRecordableAnimated : public IRecordable {
public:
    /**
     * Record draw commands with animation time.
     *
     * @param cmd Command buffer to record to
     * @param frameIndex Current frame index
     * @param time Animation time in seconds
     */
    virtual void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time) = 0;

    // Default implementation calls the time-based version with time=0
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) override {
        recordDraw(cmd, frameIndex, 0.0f);
    }
};
