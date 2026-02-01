#pragma once

// ============================================================================
// ITemporalSystem.h - Interface for systems with temporal state
// ============================================================================
//
// This interface allows centralized management of systems that maintain
// temporal state (history buffers, ping-pong buffers, frame counters, etc.)
// that needs to be reset when the window is restored from background.
//
// Benefits:
// - Single point of control for temporal reset on window focus
// - Self-documenting: systems declare they have temporal state
// - Prevents ghost frames from stale temporal history
// - New temporal systems automatically get reset handling
//
// Usage:
// 1. Have your system inherit from ITemporalSystem
// 2. Implement resetTemporalHistory() to reset all temporal state
// 3. Register with RendererSystems::registerTemporalSystem()
// 4. System will be automatically reset on window focus regain
//

/**
 * Interface for systems that maintain temporal state across frames.
 *
 * Temporal state includes:
 * - History buffers for temporal filtering/reprojection
 * - Ping-pong buffers for multi-frame effects
 * - Frame counters for temporal accumulation
 * - Previous frame data for motion/temporal effects
 *
 * When a window loses focus and is restored (especially on macOS),
 * temporal state can become stale and cause ghost frames. Systems
 * implementing this interface will have their temporal state reset
 * automatically when the window regains focus.
 */
class ITemporalSystem {
public:
    virtual ~ITemporalSystem() = default;

    /**
     * Reset all temporal state to prevent ghost frames.
     *
     * This is called when the window regains focus after being in the
     * background. Implementations should:
     * - Reset frame counters to 0
     * - Invalidate history validity flags
     * - Reset ping-pong buffer indices
     * - Clear any accumulated temporal data
     *
     * The next frame after reset should behave as if it's the first
     * frame, not blending with any previous temporal history.
     */
    virtual void resetTemporalHistory() = 0;
};
