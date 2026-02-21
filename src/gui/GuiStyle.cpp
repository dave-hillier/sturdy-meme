#include "GuiStyle.h"

#include <imgui.h>

void GuiStyle::apply() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Modern dark theme with blue accent
    ImVec4 bgDark = ImVec4(0.08f, 0.08f, 0.10f, 0.95f);
    ImVec4 bgMid = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    ImVec4 bgLight = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
    ImVec4 accent = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
    ImVec4 accentHover = ImVec4(0.36f, 0.69f, 1.0f, 1.0f);
    ImVec4 accentActive = ImVec4(0.16f, 0.49f, 0.88f, 1.0f);
    ImVec4 textBright = ImVec4(0.95f, 0.95f, 0.97f, 1.0f);
    ImVec4 textDim = ImVec4(0.60f, 0.60f, 0.65f, 1.0f);

    colors[ImGuiCol_WindowBg] = bgDark;
    colors[ImGuiCol_PopupBg] = bgMid;
    colors[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.30f, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_FrameBg] = bgMid;
    colors[ImGuiCol_FrameBgHovered] = bgLight;
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.22f, 0.28f, 1.0f);

    colors[ImGuiCol_TitleBg] = bgDark;
    colors[ImGuiCol_TitleBgActive] = bgMid;
    colors[ImGuiCol_TitleBgCollapsed] = bgDark;

    colors[ImGuiCol_MenuBarBg] = bgMid;
    colors[ImGuiCol_ScrollbarBg] = bgDark;
    colors[ImGuiCol_ScrollbarGrab] = bgLight;
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.28f, 0.34f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = accent;

    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentActive;

    colors[ImGuiCol_Button] = bgLight;
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.28f, 0.34f, 1.0f);
    colors[ImGuiCol_ButtonActive] = accent;

    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.24f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.26f, 0.32f, 1.0f);
    colors[ImGuiCol_HeaderActive] = accent;

    colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.30f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = accent;
    colors[ImGuiCol_SeparatorActive] = accentActive;

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] = accentHover;
    colors[ImGuiCol_ResizeGripActive] = accentActive;

    colors[ImGuiCol_Tab] = bgLight;
    colors[ImGuiCol_TabHovered] = accentHover;
    colors[ImGuiCol_TabSelected] = accent;
    colors[ImGuiCol_TabDimmed] = bgMid;
    colors[ImGuiCol_TabDimmedSelected] = bgLight;

    colors[ImGuiCol_PlotLines] = accent;
    colors[ImGuiCol_PlotLinesHovered] = accentHover;
    colors[ImGuiCol_PlotHistogram] = accent;
    colors[ImGuiCol_PlotHistogramHovered] = accentHover;

    colors[ImGuiCol_TableHeaderBg] = bgMid;
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.25f, 0.25f, 0.30f, 1.0f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.20f, 0.20f, 0.24f, 1.0f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);

    colors[ImGuiCol_Text] = textBright;
    colors[ImGuiCol_TextDisabled] = textDim;
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);

    colors[ImGuiCol_DragDropTarget] = accentHover;
    colors[ImGuiCol_NavHighlight] = accent;

    // Rounding and spacing for modern look
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.IndentSpacing = 20.0f;

    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;

    // Scale for high DPI
    style.ScaleAllSizes(1.0f);
}
