#pragma once

#include "debug/Flamegraph.h"
#include <imgui.h>

/**
 * ImGui-based flamegraph rendering utilities.
 */
namespace GuiFlamegraph {

/**
 * Configuration for flamegraph rendering.
 */
struct Config {
    float barHeight = 20.0f;     // Height of each bar
    float minBarWidth = 2.0f;    // Minimum bar width (for visibility)
    float padding = 2.0f;        // Padding between bars
    bool showLabels = true;      // Show zone names on bars
    bool showTooltips = true;    // Show detailed tooltips on hover
};

/**
 * Render a flamegraph capture.
 *
 * @param label Unique ImGui label for the widget
 * @param capture The flamegraph data to render
 * @param config Rendering configuration
 * @param width Width of the flamegraph (0 = use available width)
 */
void render(const char* label, const FlamegraphCapture& capture,
            const Config& config = Config{}, float width = 0.0f);

/**
 * Render a flamegraph history with navigation controls.
 *
 * @param label Unique ImGui label for the widget
 * @param history The ring buffer of captures
 * @param selectedIndex Current selected capture index (will be modified by navigation)
 * @param config Rendering configuration
 * @param width Width of the flamegraph (0 = use available width)
 */
template<size_t N>
void renderWithHistory(const char* label, const FlamegraphHistory<N>& history,
                       int& selectedIndex, const Config& config = Config{},
                       float width = 0.0f);

/**
 * Get color for a flamegraph entry based on its color hint.
 */
ImVec4 getEntryColor(FlamegraphEntry::ColorHint hint, float alpha = 1.0f);

} // namespace GuiFlamegraph
