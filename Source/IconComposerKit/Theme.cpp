#include "Source/IconComposerKit/Theme.h"

namespace ick::theme {

void apply(float dpiScale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();

    s.WindowPadding = ImVec2(12, 10);
    s.FramePadding = ImVec2(8, 4);
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.IndentSpacing = 16;
    s.ScrollbarSize = 10;
    s.GrabMinSize = 10;

    s.WindowBorderSize = 0;
    s.ChildBorderSize = 0;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.TabBorderSize = 0;
    s.DockingSeparatorSize = 1;

    s.WindowRounding = 0;
    s.ChildRounding = kControlRadius;
    s.FrameRounding = kControlRadius;
    s.PopupRounding = kPopupRadius;
    s.ScrollbarRounding = 6;
    s.GrabRounding = kControlRadius;
    s.TabRounding = kControlRadius;
    s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.SeparatorTextBorderSize = 1;

    ImVec4* c = s.Colors;
    const ImVec4 none(0, 0, 0, 0);
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kText3;
    c[ImGuiCol_WindowBg] = kPanel;
    c[ImGuiCol_ChildBg] = none;
    c[ImGuiCol_PopupBg] = kPopover;
    c[ImGuiCol_Border] = kSep;
    c[ImGuiCol_BorderShadow] = none;
    c[ImGuiCol_FrameBg] = kBox;
    c[ImGuiCol_FrameBgHovered] = kBoxStrong;
    c[ImGuiCol_FrameBgActive] = kControl;
    c[ImGuiCol_TitleBg] = kPanel;
    c[ImGuiCol_TitleBgActive] = kPanel;
    c[ImGuiCol_TitleBgCollapsed] = kPanel;
    c[ImGuiCol_MenuBarBg] = kPanel;
    c[ImGuiCol_ScrollbarBg] = none;
    c[ImGuiCol_ScrollbarGrab] = kTrack;
    c[ImGuiCol_ScrollbarGrabHovered] = rgba(255, 255, 255, 0.24f);
    c[ImGuiCol_ScrollbarGrabActive] = rgba(255, 255, 255, 0.32f);
    c[ImGuiCol_CheckMark] = kAccentText;
    c[ImGuiCol_SliderGrab] = kText;
    c[ImGuiCol_SliderGrabActive] = kText;
    c[ImGuiCol_Button] = kControl;
    c[ImGuiCol_ButtonHovered] = rgba(255, 255, 255, 0.16f);
    c[ImGuiCol_ButtonActive] = rgba(255, 255, 255, 0.22f);
    c[ImGuiCol_Header] = kCapOn;
    c[ImGuiCol_HeaderHovered] = kBoxStrong;
    c[ImGuiCol_HeaderActive] = kCapOn;
    c[ImGuiCol_Separator] = kSep;
    c[ImGuiCol_SeparatorHovered] = kAccent;
    c[ImGuiCol_SeparatorActive] = kAccent;
    c[ImGuiCol_ResizeGrip] = none;
    c[ImGuiCol_ResizeGripHovered] = none;
    c[ImGuiCol_ResizeGripActive] = none;
    c[ImGuiCol_InputTextCursor] = kText;
    c[ImGuiCol_Tab] = none;
    c[ImGuiCol_TabHovered] = kBoxStrong;
    c[ImGuiCol_TabSelected] = kCapOn;
    c[ImGuiCol_TabSelectedOverline] = none;
    c[ImGuiCol_TabDimmed] = none;
    c[ImGuiCol_TabDimmedSelected] = kBox;
    c[ImGuiCol_TabDimmedSelectedOverline] = none;
    c[ImGuiCol_DockingPreview] = rgba(0x0a, 0x84, 0xff, 0.35f);
    c[ImGuiCol_DockingEmptyBg] = kCanvas;
    c[ImGuiCol_PlotLines] = kText2;
    c[ImGuiCol_PlotLinesHovered] = kAccent;
    c[ImGuiCol_PlotHistogram] = kAccent;
    c[ImGuiCol_PlotHistogramHovered] = kAccent;
    c[ImGuiCol_TableHeaderBg] = kBox;
    c[ImGuiCol_TableBorderStrong] = kSep;
    c[ImGuiCol_TableBorderLight] = kSep;
    c[ImGuiCol_TableRowBg] = none;
    c[ImGuiCol_TableRowBgAlt] = kBox;
    c[ImGuiCol_TextLink] = kAccent;
    c[ImGuiCol_TextSelectedBg] = rgba(0x0a, 0x84, 0xff, 0.40f);
    c[ImGuiCol_TreeLines] = kSep;
    c[ImGuiCol_DragDropTarget] = kAccent;
    c[ImGuiCol_NavCursor] = kAccent;
    c[ImGuiCol_NavWindowingHighlight] = kText;
    c[ImGuiCol_NavWindowingDimBg] = rgba(0, 0, 0, 0.3f);
    c[ImGuiCol_ModalWindowDimBg] = rgba(0, 0, 0, 0.45f);

    s.ScaleAllSizes(dpiScale);
    s.FontSizeBase = kFontSize;
    s.FontScaleDpi = dpiScale;
}

}  // namespace ick::theme
