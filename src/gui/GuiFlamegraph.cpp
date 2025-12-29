#include "GuiFlamegraph.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace GuiFlamegraph {

ImVec4 getEntryColor(FlamegraphEntry::ColorHint hint, float alpha) {
    switch (hint) {
        case FlamegraphEntry::ColorHint::Wait:
            return ImVec4(0.3f, 0.7f, 0.9f, alpha);   // Cyan for wait zones
        case FlamegraphEntry::ColorHint::Shadow:
            return ImVec4(0.5f, 0.4f, 0.7f, alpha);   // Purple for shadow
        case FlamegraphEntry::ColorHint::Water:
            return ImVec4(0.3f, 0.5f, 0.9f, alpha);   // Blue for water
        case FlamegraphEntry::ColorHint::Terrain:
            return ImVec4(0.5f, 0.7f, 0.3f, alpha);   // Green for terrain
        case FlamegraphEntry::ColorHint::PostProcess:
            return ImVec4(0.9f, 0.6f, 0.3f, alpha);   // Orange for post-process
        case FlamegraphEntry::ColorHint::Default:
        default:
            return ImVec4(0.8f, 0.4f, 0.3f, alpha);   // Red-orange default (flame-like)
    }
}

// Hash function to generate varied colors for entries
static ImVec4 hashColor(const std::string& name, FlamegraphEntry::ColorHint hint, float alpha) {
    // Use hint color if not default
    if (hint != FlamegraphEntry::ColorHint::Default) {
        return getEntryColor(hint, alpha);
    }

    // Generate varied color based on name hash
    unsigned int hash = 0;
    for (char c : name) {
        hash = hash * 31 + static_cast<unsigned int>(c);
    }

    // Generate warm flame-like colors (reds, oranges, yellows)
    float hue = static_cast<float>(hash % 60) / 60.0f * 0.15f; // 0.0 to 0.15 (red to orange-yellow)
    float sat = 0.7f + static_cast<float>((hash >> 8) % 30) / 100.0f; // 0.7 to 1.0
    float val = 0.6f + static_cast<float>((hash >> 16) % 30) / 100.0f; // 0.6 to 0.9

    // HSV to RGB conversion
    float h = hue * 6.0f;
    int i = static_cast<int>(h);
    float f = h - i;
    float p = val * (1 - sat);
    float q = val * (1 - sat * f);
    float t = val * (1 - sat * (1 - f));

    float r, g, b;
    switch (i % 6) {
        case 0: r = val; g = t; b = p; break;
        case 1: r = q; g = val; b = p; break;
        case 2: r = p; g = val; b = t; break;
        case 3: r = p; g = q; b = val; break;
        case 4: r = t; g = p; b = val; break;
        case 5: r = val; g = p; b = q; break;
        default: r = g = b = val; break;
    }

    return ImVec4(r, g, b, alpha);
}

void render(const char* label, const FlamegraphCapture& capture,
            const Config& config, float width) {
    if (capture.isEmpty()) {
        ImGui::TextDisabled("No data captured");
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    float availWidth = (width > 0.0f) ? width : ImGui::GetContentRegionAvail().x;

    // Find max depth for height calculation
    int maxDepth = 0;
    for (const auto& entry : capture.entries) {
        maxDepth = std::max(maxDepth, entry.depth);
    }

    float totalHeight = (maxDepth + 1) * (config.barHeight + config.padding);

    // Reserve space for the flamegraph
    ImGui::InvisibleButton(label, ImVec2(availWidth, totalHeight));
    bool isHovered = ImGui::IsItemHovered();

    // Scale factor: pixels per millisecond
    float scale = (capture.totalTimeMs > 0.0f) ? (availWidth / capture.totalTimeMs) : 1.0f;

    // Track which entry is hovered
    const FlamegraphEntry* hoveredEntry = nullptr;

    // Draw entries from bottom to top (lowest depth = bottom of flamegraph)
    for (const auto& entry : capture.entries) {
        float x = canvasPos.x + entry.startOffsetMs * scale;
        float y = canvasPos.y + totalHeight - (entry.depth + 1) * (config.barHeight + config.padding);
        float barWidth = std::max(entry.timeMs * scale, config.minBarWidth);

        // Get color for this entry
        ImVec4 color = hashColor(entry.name, entry.colorHint, 1.0f);
        ImU32 barColor = ImGui::ColorConvertFloat4ToU32(color);

        // Darker border color
        ImVec4 borderColorV = color;
        borderColorV.x *= 0.6f;
        borderColorV.y *= 0.6f;
        borderColorV.z *= 0.6f;
        ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(borderColorV);

        // Draw filled rectangle
        ImVec2 min(x, y);
        ImVec2 max(x + barWidth, y + config.barHeight);
        drawList->AddRectFilled(min, max, barColor);
        drawList->AddRect(min, max, borderColor);

        // Check hover
        if (isHovered) {
            ImVec2 mousePos = ImGui::GetIO().MousePos;
            if (mousePos.x >= min.x && mousePos.x < max.x &&
                mousePos.y >= min.y && mousePos.y < max.y) {
                hoveredEntry = &entry;

                // Highlight hovered entry
                drawList->AddRect(min, max, IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
            }
        }

        // Draw label if bar is wide enough
        if (config.showLabels && barWidth > 30.0f) {
            const char* text = entry.name.c_str();
            ImVec2 textSize = ImGui::CalcTextSize(text);
            if (textSize.x < barWidth - 4.0f) {
                float textX = x + (barWidth - textSize.x) * 0.5f;
                float textY = y + (config.barHeight - textSize.y) * 0.5f;
                drawList->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 255, 255), text);
            } else {
                // Truncate label
                char truncated[32];
                size_t maxChars = static_cast<size_t>((barWidth - 8.0f) / 7.0f);
                if (maxChars > 0 && maxChars < sizeof(truncated) - 1) {
                    snprintf(truncated, std::min(maxChars + 1, sizeof(truncated)), "%s", text);
                    float textY = y + (config.barHeight - ImGui::CalcTextSize(truncated).y) * 0.5f;
                    drawList->AddText(ImVec2(x + 2.0f, textY), IM_COL32(255, 255, 255, 200), truncated);
                }
            }
        }
    }

    // Show tooltip for hovered entry
    if (config.showTooltips && hoveredEntry) {
        ImGui::BeginTooltip();
        ImGui::Text("%s", hoveredEntry->name.c_str());
        ImGui::Separator();
        ImGui::Text("Time: %.3f ms", hoveredEntry->timeMs);
        if (capture.totalTimeMs > 0.0f) {
            float pct = (hoveredEntry->timeMs / capture.totalTimeMs) * 100.0f;
            ImGui::Text("Percent: %.1f%%", pct);
        }
        if (hoveredEntry->isWaitZone) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.7f, 0.9f, 1.0f));
            ImGui::Text("(Wait zone - CPU idle)");
            ImGui::PopStyleColor();
        }
        ImGui::EndTooltip();
    }
}

// Template implementation for renderWithHistory
template<size_t N>
void renderWithHistory(const char* label, const FlamegraphHistory<N>& history,
                       int& selectedIndex, const Config& config, float width) {
    if (history.count() == 0) {
        ImGui::TextDisabled("No captures yet");
        return;
    }

    // Clamp selected index
    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(history.count()) - 1);

    // Navigation controls
    ImGui::PushID(label);

    bool canGoNewer = selectedIndex > 0;
    bool canGoOlder = selectedIndex < static_cast<int>(history.count()) - 1;

    if (!canGoNewer) ImGui::BeginDisabled();
    if (ImGui::ArrowButton("##newer", ImGuiDir_Left)) {
        selectedIndex--;
    }
    if (!canGoNewer) ImGui::EndDisabled();

    ImGui::SameLine();

    if (!canGoOlder) ImGui::BeginDisabled();
    if (ImGui::ArrowButton("##older", ImGuiDir_Right)) {
        selectedIndex++;
    }
    if (!canGoOlder) ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Text("Frame %d/%zu", selectedIndex + 1, history.count());

    const FlamegraphCapture* capture = history.get(selectedIndex);
    if (capture) {
        ImGui::SameLine();
        ImGui::Text("(%.2f ms)", capture->totalTimeMs);

        // Render the flamegraph
        char childLabel[64];
        snprintf(childLabel, sizeof(childLabel), "%s_flame", label);
        render(childLabel, *capture, config, width);
    }

    ImGui::PopID();
}

// Explicit template instantiations
template void renderWithHistory<10>(const char*, const FlamegraphHistory<10>&,
                                    int&, const Config&, float);

} // namespace GuiFlamegraph
