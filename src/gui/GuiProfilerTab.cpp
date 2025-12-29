#include "GuiProfilerTab.h"
#include "GuiFlamegraph.h"
#include "core/interfaces/IProfilerControl.h"
#include "Profiler.h"
#include "InitProfiler.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <SDL3/SDL.h>

// Static state for flamegraph navigation
static int s_gpuFlamegraphIndex = 0;
static int s_cpuFlamegraphIndex = 0;

namespace {

std::string generateMarkdownReport(const Profiler& profiler) {
    std::ostringstream ss;

    const auto& gpuStats = profiler.getSmoothedGpuResults();
    const auto& cpuStats = profiler.getSmoothedCpuResults();
    const auto& initResults = InitProfiler::get().getResults();

    // GPU Timing
    ss << "## GPU Timing\n\n";
    ss << "**Total: " << std::fixed << std::setprecision(2) << gpuStats.totalGpuTimeMs << " ms**\n\n";
    if (!gpuStats.zones.empty()) {
        ss << "| Pass | Time (ms) | % |\n";
        ss << "|------|-----------|---|\n";
        for (const auto& zone : gpuStats.zones) {
            ss << "| " << zone.name << " | " << std::fixed << std::setprecision(2) << zone.gpuTimeMs
               << " | " << std::setprecision(1) << zone.percentOfFrame << "% |\n";
        }
    }
    ss << "\n";

    // CPU Timing
    ss << "## CPU Timing\n\n";
    ss << "**Total: " << std::fixed << std::setprecision(2) << cpuStats.totalCpuTimeMs << " ms** ";
    ss << "(Work: " << cpuStats.workTimeMs << " ms, Wait: " << cpuStats.waitTimeMs << " ms)\n\n";
    if (!cpuStats.zones.empty()) {
        ss << "| Zone | Time (ms) | % |\n";
        ss << "|------|-----------|---|\n";
        for (const auto& zone : cpuStats.zones) {
            ss << "| " << zone.name << " | " << std::fixed << std::setprecision(3) << zone.cpuTimeMs
               << " | " << std::setprecision(1) << zone.percentOfFrame << "% |\n";
        }
    }
    ss << "\n";

    // Startup Timing
    if (InitProfiler::get().isFinalized() && !initResults.phases.empty()) {
        ss << "## Startup Timing\n\n";
        ss << "**Total: " << std::fixed << std::setprecision(1) << initResults.totalTimeMs << " ms ("
           << std::setprecision(2) << initResults.totalTimeMs / 1000.0f << " s)**\n\n";
        ss << "| Phase | Time (ms) | % |\n";
        ss << "|-------|-----------|---|\n";
        for (const auto& phase : initResults.phases) {
            // Indent with spaces for hierarchy
            std::string indent(phase.depth * 2, ' ');
            ss << "| " << indent << phase.name << " | " << std::fixed << std::setprecision(1) << phase.timeMs
               << " | " << phase.percentOfTotal << "% |\n";
        }
    }

    return ss.str();
}

} // anonymous namespace

void GuiProfilerTab::render(IProfilerControl& profilerControl) {
    ImGui::Spacing();

    auto& profiler = profilerControl.getProfiler();

    // Enable/disable toggle
    bool enabled = profiler.isEnabled();
    if (ImGui::Checkbox("Enable Profiling", &enabled)) {
        profiler.setEnabled(enabled);
    }

    ImGui::SameLine();
    if (ImGui::Button("Copy to Clipboard (Markdown)")) {
        std::string markdown = generateMarkdownReport(profiler);
        ImGui::SetClipboardText(markdown.c_str());
        SDL_Log("Profiler data copied to clipboard:\n%s", markdown.c_str());
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

        // GPU Flamegraph section
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("GPU Flamegraph")) {
            const auto& gpuHistory = profiler.getGpuFlamegraphHistory();
            if (gpuHistory.count() > 0) {
                GuiFlamegraph::Config config;
                config.barHeight = 22.0f;
                GuiFlamegraph::renderWithHistory("gpu_flamegraph", gpuHistory,
                                                  s_gpuFlamegraphIndex, config);
            } else {
                ImGui::TextDisabled("No flamegraph captures yet (capturing every %d frames)",
                                   profiler.getCaptureInterval());
            }
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

        // CPU Flamegraph section
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("CPU Flamegraph")) {
            const auto& cpuHistory = profiler.getCpuFlamegraphHistory();
            if (cpuHistory.count() > 0) {
                GuiFlamegraph::Config config;
                config.barHeight = 22.0f;
                GuiFlamegraph::renderWithHistory("cpu_flamegraph", cpuHistory,
                                                  s_cpuFlamegraphIndex, config);
            } else {
                ImGui::TextDisabled("No flamegraph captures yet (capturing every %d frames)",
                                   profiler.getCaptureInterval());
            }
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

    // Initialization Timing Section (collapsed by default)
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const auto& initResults = InitProfiler::get().getResults();
    if (InitProfiler::get().isFinalized() && !initResults.phases.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.6f, 1.0f, 1.0f));
        if (ImGui::CollapsingHeader("STARTUP TIMING")) {
            ImGui::PopStyleColor();

            // Total init time
            ImGui::Text("Total: %.1f ms (%.2f s)", initResults.totalTimeMs, initResults.totalTimeMs / 1000.0f);

            ImGui::Spacing();

            // Init timing breakdown table
            if (ImGui::BeginTable("InitTimings", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Phase", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableHeadersRow();

                for (const auto& phase : initResults.phases) {
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    // Indent based on depth for hierarchical display
                    if (phase.depth > 0) {
                        ImGui::Indent(static_cast<float>(phase.depth) * 12.0f);
                    }
                    ImGui::Text("%s", phase.name.c_str());
                    if (phase.depth > 0) {
                        ImGui::Unindent(static_cast<float>(phase.depth) * 12.0f);
                    }

                    ImGui::TableNextColumn();
                    ImGui::Text("%.1f", phase.timeMs);

                    ImGui::TableNextColumn();
                    // Color code by percentage
                    if (phase.percentOfTotal > 30.0f) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    } else if (phase.percentOfTotal > 15.0f) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                    }
                    ImGui::Text("%.1f%%", phase.percentOfTotal);
                    ImGui::PopStyleColor();
                }

                ImGui::EndTable();
            }

            // Visual progress bars for top-level phases only (depth == 0)
            ImGui::Spacing();
            ImGui::Text("Top-level phases:");
            for (const auto& phase : initResults.phases) {
                if (phase.depth == 0) {
                    float fraction = (initResults.totalTimeMs > 0.0f)
                        ? (phase.timeMs / initResults.totalTimeMs) : 0.0f;
                    char label[128];
                    snprintf(label, sizeof(label), "%s: %.1f ms", phase.name.c_str(), phase.timeMs);
                    ImGui::ProgressBar(fraction, ImVec2(-1, 0), label);
                }
            }

            // Init Flamegraph (single capture, with hierarchical phases)
            ImGui::Spacing();
            ImGui::Text("Flamegraph:");
            const auto& initFlamegraph = profiler.getInitFlamegraph();
            if (!initFlamegraph.isEmpty()) {
                GuiFlamegraph::Config config;
                config.barHeight = 22.0f;
                GuiFlamegraph::render("init_flamegraph", initFlamegraph, config);
            } else {
                ImGui::TextDisabled("Init flamegraph not captured");
            }
        } else {
            ImGui::PopStyleColor();
        }
    }
}
