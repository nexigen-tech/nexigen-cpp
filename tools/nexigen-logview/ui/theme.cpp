#include "theme.hpp"

#include <imgui.h>

namespace nexigen::logview {

void apply_dark_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 3.0F;
    style.FrameRounding = 3.0F;
    style.GrabRounding = 3.0F;
    style.ScrollbarRounding = 3.0F;
    style.WindowBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;
    style.ItemSpacing = ImVec2(8.0F, 4.0F);
    style.CellPadding = ImVec2(6.0F, 2.0F);
    style.ScrollbarSize = 14.0F;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.90F, 0.92F, 0.95F, 1.00F);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.48F, 0.50F, 0.55F, 1.00F);
    colors[ImGuiCol_WindowBg] = ImVec4(0.075F, 0.082F, 0.095F, 1.00F);
    colors[ImGuiCol_ChildBg] = ImVec4(0.085F, 0.092F, 0.105F, 1.00F);
    colors[ImGuiCol_PopupBg] = ImVec4(0.10F, 0.11F, 0.13F, 0.98F);
    colors[ImGuiCol_Border] = ImVec4(0.24F, 0.26F, 0.30F, 0.70F);
    colors[ImGuiCol_FrameBg] = ImVec4(0.14F, 0.15F, 0.18F, 1.00F);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20F, 0.22F, 0.26F, 1.00F);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25F, 0.28F, 0.33F, 1.00F);
    colors[ImGuiCol_TitleBg] = ImVec4(0.06F, 0.07F, 0.08F, 1.00F);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.10F, 0.12F, 0.15F, 1.00F);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.09F, 0.10F, 0.12F, 1.00F);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.06F, 0.07F, 0.08F, 1.00F);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.28F, 0.31F, 0.36F, 1.00F);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.36F, 0.40F, 0.46F, 1.00F);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.44F, 0.49F, 0.56F, 1.00F);
    colors[ImGuiCol_CheckMark] = ImVec4(0.36F, 0.78F, 0.62F, 1.00F);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.36F, 0.78F, 0.62F, 1.00F);
    colors[ImGuiCol_Button] = ImVec4(0.16F, 0.18F, 0.22F, 1.00F);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.24F, 0.28F, 0.34F, 1.00F);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.30F, 0.36F, 0.44F, 1.00F);
    colors[ImGuiCol_Header] = ImVec4(0.18F, 0.21F, 0.26F, 1.00F);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.25F, 0.30F, 0.37F, 1.00F);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.30F, 0.36F, 0.44F, 1.00F);
    colors[ImGuiCol_Separator] = ImVec4(0.24F, 0.26F, 0.30F, 0.70F);
    colors[ImGuiCol_Tab] = ImVec4(0.10F, 0.12F, 0.15F, 1.00F);
    colors[ImGuiCol_TabHovered] = ImVec4(0.22F, 0.27F, 0.33F, 1.00F);
    colors[ImGuiCol_TabSelected] = ImVec4(0.18F, 0.23F, 0.29F, 1.00F);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.11F, 0.13F, 0.16F, 1.00F);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.26F, 0.29F, 0.34F, 1.00F);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.18F, 0.20F, 0.24F, 1.00F);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00F, 0.00F, 0.00F, 0.00F);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00F, 1.00F, 1.00F, 0.025F);
    colors[ImGuiCol_NavCursor] = ImVec4(0.36F, 0.78F, 0.62F, 1.00F);
}

} // namespace nexigen::logview
