#include "GuiProfilerTab.h"
#include "core/interfaces/IProfilerControl.h"
#include "Profiler.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>

void GuiProfilerTab::render(IProfilerControl& profilerControl) {
    ImGui::Spacing();

    auto& profiler = profilerControl.getProfiler();

    // Enable/disable toggle
    bool enabled = profiler.isEnabled();
    if (ImGui::Checkbox("Enable Profiling", &enabled)) {
        profiler.setEnabled(enabled);
    }

    if (!enabled) {
        ImGui::TextDisabled("Profiling disabled");
        return;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // GPU Profiling Section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("GPU TIMING");
    ImGui::PopStyleColor();

    const auto& gpuStats = profiler.getSmoothedGpuResults();

    if (gpuStats.zones.empty()) {
        ImGui::TextDisabled("No GPU data yet (waiting for frames)");
    } else {
        // Total GPU time
        ImGui::Text("Total GPU: %.2f ms", gpuStats.totalGpuTimeMs);

        ImGui::Spacing();

        // GPU timing breakdown table
        if (ImGui::BeginTable("GPUTimings", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableHeadersRow();

            for (const auto& zone : gpuStats.zones) {
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("%s", zone.name.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%.2f", zone.gpuTimeMs);

                ImGui::TableNextColumn();
                // Color code by percentage
                if (zone.percentOfFrame > 30.0f) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                } else if (zone.percentOfFrame > 15.0f) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                }
                ImGui::Text("%.1f%%", zone.percentOfFrame);
                ImGui::PopStyleColor();
            }

            ImGui::EndTable();
        }

        // Visual bar chart of GPU zones
        ImGui::Spacing();
        float maxTime = gpuStats.totalGpuTimeMs;
        for (const auto& zone : gpuStats.zones) {
            float fraction = (maxTime > 0.0f) ? (zone.gpuTimeMs / maxTime) : 0.0f;
            ImGui::ProgressBar(fraction, ImVec2(-1, 0), zone.name.c_str());
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // CPU Profiling Section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
    ImGui::Text("CPU TIMING");
    ImGui::PopStyleColor();

    const auto& cpuStats = profiler.getSmoothedCpuResults();

    if (cpuStats.zones.empty()) {
        ImGui::TextDisabled("No CPU data yet");
    } else {
        // Total CPU time with work/wait breakdown
        ImGui::Text("Total CPU: %.2f ms", cpuStats.totalCpuTimeMs);

        // Work vs Wait breakdown with visual bars
        ImGui::Spacing();
        float workPct = (cpuStats.totalCpuTimeMs > 0.0f) ? (cpuStats.workTimeMs / cpuStats.totalCpuTimeMs) : 0.0f;
        float waitPct = (cpuStats.totalCpuTimeMs > 0.0f) ? (cpuStats.waitTimeMs / cpuStats.totalCpuTimeMs) : 0.0f;
        float overheadPct = (cpuStats.totalCpuTimeMs > 0.0f) ? (cpuStats.overheadTimeMs / cpuStats.totalCpuTimeMs) : 0.0f;

        // Work time bar (green)
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
        char workLabel[64];
        snprintf(workLabel, sizeof(workLabel), "Work: %.2f ms (%.0f%%)", cpuStats.workTimeMs, workPct * 100.0f);
        ImGui::ProgressBar(workPct, ImVec2(-1, 14), workLabel);
        ImGui::PopStyleColor();

        // Wait time bar (cyan - indicates idle CPU waiting for GPU)
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        char waitLabel[64];
        snprintf(waitLabel, sizeof(waitLabel), "Wait: %.2f ms (%.0f%%)", cpuStats.waitTimeMs, waitPct * 100.0f);
        ImGui::ProgressBar(waitPct, ImVec2(-1, 14), waitLabel);
        ImGui::PopStyleColor();

        // Overhead time bar (gray - untracked time)
        if (cpuStats.overheadTimeMs > 0.1f) {
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            char overheadLabel[64];
            snprintf(overheadLabel, sizeof(overheadLabel), "Other: %.2f ms (%.0f%%)", cpuStats.overheadTimeMs, overheadPct * 100.0f);
            ImGui::ProgressBar(overheadPct, ImVec2(-1, 14), overheadLabel);
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        // CPU timing breakdown table
        if (ImGui::BeginTable("CPUTimings", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Zone", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableHeadersRow();

            for (const auto& zone : cpuStats.zones) {
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                // Color wait zones cyan to make them stand out
                if (zone.isWaitZone) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                }
                ImGui::Text("%s", zone.name.c_str());
                if (zone.isWaitZone) {
                    ImGui::PopStyleColor();
                }

                ImGui::TableNextColumn();
                ImGui::Text("%.3f", zone.cpuTimeMs);

                ImGui::TableNextColumn();
                // Color code by percentage (but wait zones always cyan)
                if (zone.isWaitZone) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                } else if (zone.percentOfFrame > 30.0f) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                } else if (zone.percentOfFrame > 15.0f) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                }
                ImGui::Text("%.1f%%", zone.percentOfFrame);
                ImGui::PopStyleColor();
            }

            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Frame budget indicator
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("FRAME BUDGET");
    ImGui::PopStyleColor();

    float targetMs = 16.67f;  // 60 FPS target
    float gpuTime = gpuStats.totalGpuTimeMs;
    float cpuTime = cpuStats.totalCpuTimeMs;
    float maxTimeVal = std::max(gpuTime, cpuTime);

    // Budget bar
    float budgetUsed = maxTimeVal / targetMs;
    ImVec4 budgetColor;
    if (budgetUsed < 0.8f) {
        budgetColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);  // Green
    } else if (budgetUsed < 1.0f) {
        budgetColor = ImVec4(1.0f, 0.8f, 0.4f, 1.0f);  // Yellow
    } else {
        budgetColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);  // Red
    }

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, budgetColor);
    char budgetText[64];
    snprintf(budgetText, sizeof(budgetText), "%.1f / %.1f ms (%.0f%%)",
             maxTimeVal, targetMs, budgetUsed * 100.0f);
    ImGui::ProgressBar(std::min(budgetUsed, 1.5f) / 1.5f, ImVec2(-1, 20), budgetText);
    ImGui::PopStyleColor();

    // Determine bottleneck
    bool gpuBound = gpuTime > cpuTime && gpuTime > cpuStats.waitTimeMs;
    bool cpuWorkBound = cpuStats.workTimeMs > gpuTime && cpuStats.workTimeMs > cpuStats.waitTimeMs;
    bool waitBound = cpuStats.waitTimeMs > cpuStats.workTimeMs && cpuStats.waitTimeMs > 0.5f;

    if (gpuBound) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::Text("Status: GPU Bound");
        ImGui::PopStyleColor();
    } else if (cpuWorkBound) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
        ImGui::Text("Status: CPU Bound");
        ImGui::PopStyleColor();
    } else if (waitBound) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        ImGui::Text("Status: Wait Bound (CPU idle, waiting for GPU)");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
        ImGui::Text("Status: Balanced");
        ImGui::PopStyleColor();
    }
}
