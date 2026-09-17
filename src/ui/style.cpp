#include "style.h"

#include <imgui.h>

namespace ui {

void applyPvmStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Flat: no rounding anywhere, no shadows, no borders. A PVM OSD is a
    // handful of square pixels, not a modern rounded-corner UI.
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 0.0f;

    style.WindowPadding = ImVec2(20.0f, 16.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.IndentSpacing = 24.0f;

    ImVec4* colors = style.Colors;
    const ImVec4 black(0.0f, 0.0f, 0.0f, 1.0f);
    const ImVec4 white(1.0f, 1.0f, 1.0f, 1.0f);
    const ImVec4 dim(0.55f, 0.55f, 0.55f, 1.0f);

    colors[ImGuiCol_WindowBg] = black;
    colors[ImGuiCol_ChildBg] = black;
    colors[ImGuiCol_PopupBg] = black;
    colors[ImGuiCol_Border] = black;
    colors[ImGuiCol_FrameBg] = black;
    colors[ImGuiCol_FrameBgHovered] = black;
    colors[ImGuiCol_FrameBgActive] = black;
    colors[ImGuiCol_TitleBg] = black;
    colors[ImGuiCol_TitleBgActive] = black;
    colors[ImGuiCol_TitleBgCollapsed] = black;
    colors[ImGuiCol_MenuBarBg] = black;
    colors[ImGuiCol_ScrollbarBg] = black;

    colors[ImGuiCol_Text] = white;
    colors[ImGuiCol_TextDisabled] = dim;

    // Reverse-video selection bar (white background, black text applied
    // per-row by the caller): see App::drawMenuRow.
    colors[ImGuiCol_Header] = white;
    colors[ImGuiCol_HeaderHovered] = white;
    colors[ImGuiCol_HeaderActive] = white;

    colors[ImGuiCol_Separator] = dim;
    colors[ImGuiCol_SeparatorHovered] = dim;
    colors[ImGuiCol_SeparatorActive] = dim;
}

}  // namespace ui
