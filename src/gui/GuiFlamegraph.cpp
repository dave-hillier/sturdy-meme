#include "GuiFlamegraph.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace GuiFlamegraph {

ImVec4 getNodeColor(FlamegraphColorHint hint, float alpha) {
    switch (hint) {
        case FlamegraphColorHint::Wait:
            return ImVec4(0.3f, 0.7f, 0.9f, alpha);   // Cyan for wait zones
        case FlamegraphColorHint::Shadow:
            return ImVec4(0.5f, 0.4f, 0.7f, alpha);   // Purple for shadow
        case FlamegraphColorHint::Water:
            return ImVec4(0.3f, 0.5f, 0.9f, alpha);   // Blue for water
        case FlamegraphColorHint::Terrain:
            return ImVec4(0.5f, 0.7f, 0.3f, alpha);   // Green for terrain
        case FlamegraphColorHint::PostProcess:
            return ImVec4(0.9f, 0.6f, 0.3f, alpha);   // Orange for post-process
        case FlamegraphColorHint::Default:
        default:
            return ImVec4(0.8f, 0.4f, 0.3f, alpha);   // Red-orange default (flame-like)
    }
}

// Generate varied color based on name hash for default-colored nodes
static ImVec4 hashColor(const std::string& name, FlamegraphColorHint hint, float alpha) {
    // Use hint color if not default
    if (hint != FlamegraphColorHint::Default) {
        return getNodeColor(hint, alpha);
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

// Recursive function to render a node and its children
static void renderNode(
    ImDrawList* drawList,
    const FlamegraphNode& node,
    float canvasX, float canvasY,  // Top-left of flamegraph area
    float totalTimeMs,
    float scale,                    // Pixels per ms
    int depth,
    int maxDepth,
    const Config& config,
    const FlamegraphNode** hoveredNode,
    ImVec2 mousePos)
{
    // Calculate bar position (flamegraphs have roots at bottom, so higher depth = lower y)
    float barY = canvasY + (maxDepth - depth - 1) * (config.barHeight + config.padding);
    float barX = canvasX + node.startMs * scale;
    float barWidth = std::max(node.durationMs * scale, config.minBarWidth);

    // Get color for this node
    ImVec4 color = hashColor(node.name, node.colorHint, 1.0f);
    ImU32 barColor = ImGui::ColorConvertFloat4ToU32(color);

    // Darker border color
    ImVec4 borderColorV = color;
    borderColorV.x *= 0.6f;
    borderColorV.y *= 0.6f;
    borderColorV.z *= 0.6f;
    ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(borderColorV);

    // Draw filled rectangle
    ImVec2 min(barX, barY);
    ImVec2 max(barX + barWidth, barY + config.barHeight);
    drawList->AddRectFilled(min, max, barColor);
    drawList->AddRect(min, max, borderColor);

    // Check hover
    if (mousePos.x >= min.x && mousePos.x < max.x &&
        mousePos.y >= min.y && mousePos.y < max.y) {
        *hoveredNode = &node;
        // Highlight hovered entry
        drawList->AddRect(min, max, IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
    }

    // Draw label if bar is wide enough
    if (config.showLabels && barWidth > 30.0f) {
        const char* text = node.name.c_str();
        ImVec2 textSize = ImGui::CalcTextSize(text);
        if (textSize.x < barWidth - 4.0f) {
            float textX = barX + (barWidth - textSize.x) * 0.5f;
            float textY = barY + (config.barHeight - textSize.y) * 0.5f;
            drawList->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 255, 255), text);
        } else if (barWidth > 20.0f) {
            // Truncate label
            char truncated[32];
            size_t maxChars = static_cast<size_t>((barWidth - 8.0f) / 7.0f);
            if (maxChars > 0 && maxChars < sizeof(truncated) - 1) {
                snprintf(truncated, std::min(maxChars + 1, sizeof(truncated)), "%s", text);
                float textY = barY + (config.barHeight - ImGui::CalcTextSize(truncated).y) * 0.5f;
                drawList->AddText(ImVec2(barX + 2.0f, textY), IM_COL32(255, 255, 255, 200), truncated);
            }
        }
    }

    // Recursively render children
    for (const auto& child : node.children) {
        renderNode(drawList, child, canvasX, canvasY, totalTimeMs, scale,
                   depth + 1, maxDepth, config, hoveredNode, mousePos);
    }
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

    // Calculate max depth for height
    int maxDepth = capture.maxDepth();
    if (maxDepth == 0) maxDepth = 1;  // At least one level

    float totalHeight = maxDepth * (config.barHeight + config.padding);

    // Reserve space for the flamegraph
    ImGui::InvisibleButton(label, ImVec2(availWidth, totalHeight));
    bool isHovered = ImGui::IsItemHovered();
    ImVec2 mousePos = ImGui::GetIO().MousePos;

    // Scale factor: pixels per millisecond
    float scale = (capture.totalTimeMs > 0.0f) ? (availWidth / capture.totalTimeMs) : 1.0f;

    // Track hovered node for tooltip
    const FlamegraphNode* hoveredNode = nullptr;

    // Render all root nodes
    for (const auto& root : capture.roots) {
        renderNode(drawList, root, canvasPos.x, canvasPos.y, capture.totalTimeMs,
                   scale, 0, maxDepth, config,
                   isHovered ? &hoveredNode : nullptr, mousePos);
    }

    // Show tooltip for hovered node
    if (config.showTooltips && hoveredNode) {
        ImGui::BeginTooltip();
        ImGui::Text("%s", hoveredNode->name.c_str());
        ImGui::Separator();
        ImGui::Text("Duration: %.3f ms", hoveredNode->durationMs);
        if (capture.totalTimeMs > 0.0f) {
            float pct = (hoveredNode->durationMs / capture.totalTimeMs) * 100.0f;
            ImGui::Text("Percent: %.1f%%", pct);
        }
        ImGui::Text("Start: %.3f ms", hoveredNode->startMs);
        if (hoveredNode->isWaitZone) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.7f, 0.9f, 1.0f));
            ImGui::Text("(Wait zone - CPU idle)");
            ImGui::PopStyleColor();
        }
        if (!hoveredNode->children.empty()) {
            ImGui::Text("Children: %zu", hoveredNode->children.size());
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
    ImGui::Text("Capture %d/%zu", selectedIndex + 1, history.count());

    const FlamegraphCapture* capture = history.get(selectedIndex);
    if (capture) {
        ImGui::SameLine();
        ImGui::Text("(%.2f ms, frame %llu)", capture->totalTimeMs,
                    static_cast<unsigned long long>(capture->frameNumber));

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
