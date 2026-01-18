#pragma once

// ============================================================================
// IShadowCaster.h - Interface for systems that cast shadows
// ============================================================================
//
// This interface allows the shadow pass to work with any system that can
// render shadow geometry, enabling polymorphic shadow rendering.
//
// Benefits:
// - Shadow pass recorder can iterate over IShadowCaster implementations
// - New shadow-casting systems can be added without modifying shadow pass
// - Enables testing with mock shadow casters
//

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>

/**
 * Interface for systems that cast shadows.
 *
 * Implement this interface for systems that need to render geometry
 * into the shadow map (terrain, trees, grass, scene objects, etc.)
 */
class IShadowCaster {
public:
    virtual ~IShadowCaster() = default;

    /**
     * Record shadow draw commands for a specific cascade.
     *
     * @param cmd Command buffer to record to (must be in recording state within shadow render pass)
     * @param frameIndex Current frame index for triple-buffered resources
     * @param lightMatrix Light view-projection matrix for this cascade
     * @param cascade Cascade index (0 = nearest, higher = farther)
     */
    virtual void recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                                   const glm::mat4& lightMatrix, int cascade) = 0;

    /**
     * Check if this system should cast shadows.
     * Can be used to skip shadow rendering based on settings or state.
     *
     * @return true if shadows should be rendered, false to skip
     */
    virtual bool shouldCastShadows() const { return true; }
};

/**
 * Extended interface for animated shadow casters.
 *
 * Use this for systems with animated content that affects shadows (grass, trees with wind).
 */
class IShadowCasterAnimated : public IShadowCaster {
public:
    /**
     * Record shadow draw commands with animation time.
     *
     * @param cmd Command buffer to record to
     * @param frameIndex Current frame index
     * @param time Animation time in seconds
     * @param cascade Cascade index
     */
    virtual void recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                                   float time, int cascade) = 0;

    // Default implementation ignores time
    void recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                          const glm::mat4& lightMatrix, int cascade) override {
        (void)lightMatrix;  // Light matrix typically accessed via UBO
        recordShadowDraw(cmd, frameIndex, 0.0f, cascade);
    }
};
