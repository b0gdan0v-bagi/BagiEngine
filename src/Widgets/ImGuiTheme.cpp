#include "ImGuiTheme.h"

#include <cmath>

namespace BECore {

    namespace {

        constexpr ImVec4 Rgb(float r, float g, float b, float a = 1.0f) {
            return ImVec4(r, g, b, a);
        }

        constexpr ImGuiPalette kPalette{
            .windowBg = Rgb(0.106f, 0.122f, 0.153f),
            .panelBg = Rgb(0.141f, 0.165f, 0.208f),
            .frameBg = Rgb(0.200f, 0.231f, 0.286f),
            .border = Rgb(0.255f, 0.294f, 0.361f),
            .text = Rgb(0.875f, 0.890f, 0.918f),
            .textDisabled = Rgb(0.490f, 0.522f, 0.580f),
            .accent = Rgb(0.545f, 0.424f, 0.941f),
            .accentHover = Rgb(0.651f, 0.545f, 1.000f),
            .accentActive = Rgb(0.463f, 0.337f, 0.855f),
            .accentMuted = Rgb(0.243f, 0.208f, 0.451f),
            .success = Rgb(0.298f, 0.765f, 0.541f),
            .warning = Rgb(0.949f, 0.722f, 0.294f),
            .error = Rgb(0.937f, 0.357f, 0.357f),
        };

        // Rounding, padding and spacing in pixels at scale 1 (the stock ImGui values are 0..3 and 4..8)
        constexpr float kWindowRounding = 0.0f;  // an OS window of a floating viewport stays rectangular
        constexpr float kChildRounding = 4.0f;
        constexpr float kFrameRounding = 4.0f;
        constexpr float kPopupRounding = 6.0f;
        constexpr float kScrollbarRounding = 6.0f;
        constexpr float kGrabRounding = 4.0f;
        constexpr float kTabRounding = 5.0f;
        constexpr ImVec2 kWindowPadding{10.0f, 8.0f};
        constexpr ImVec2 kFramePadding{8.0f, 5.0f};
        constexpr ImVec2 kItemSpacing{8.0f, 6.0f};
        constexpr ImVec2 kItemInnerSpacing{6.0f, 5.0f};
        constexpr ImVec2 kCellPadding{6.0f, 4.0f};
        constexpr float kIndentSpacing = 18.0f;
        constexpr float kScrollbarSize = 13.0f;
        constexpr float kGrabMinSize = 12.0f;
        constexpr float kTabBarOverlineSize = 2.0f;
        constexpr float kDockingSeparatorSize = 2.0f;

        // Alphas of the colours that are meant to let what is behind show through
        constexpr float kDockingPreviewAlpha = 0.70f;
        constexpr float kTextSelectionAlpha = 0.45f;
        constexpr float kDimAlpha = 0.55f;
        constexpr float kRowAltAlpha = 0.03f;

        constexpr float kHoverLift = 0.06f;  ///< how much lighter a hovered neutral fill is
        constexpr float kScrollbarLift = 0.12f;

        constexpr ImVec4 WithAlpha(const ImVec4& colour, float alpha) {
            return ImVec4(colour.x, colour.y, colour.z, alpha);
        }

        constexpr float Clamp01(float value) {
            return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        }

        constexpr ImVec4 Lighter(const ImVec4& colour, float amount) {
            return ImVec4(Clamp01(colour.x + amount), Clamp01(colour.y + amount), Clamp01(colour.z + amount), colour.w);
        }

        void ApplyColours(ImGuiStyle& style) {
            const ImGuiPalette& p = kPalette;
            ImVec4* c = style.Colors;
            const ImVec4 transparent = WithAlpha(p.windowBg, 0.0f);
            const ImVec4 frameHovered = Lighter(p.frameBg, kHoverLift);
            const ImVec4 popupBg = Lighter(p.panelBg, kHoverLift * 0.5f);

            c[ImGuiCol_Text] = p.text;
            c[ImGuiCol_TextDisabled] = p.textDisabled;
            c[ImGuiCol_WindowBg] = p.panelBg;
            c[ImGuiCol_ChildBg] = transparent;
            c[ImGuiCol_PopupBg] = popupBg;
            c[ImGuiCol_Border] = p.border;
            c[ImGuiCol_BorderShadow] = transparent;
            c[ImGuiCol_FrameBg] = p.frameBg;
            c[ImGuiCol_FrameBgHovered] = frameHovered;
            c[ImGuiCol_FrameBgActive] = p.accentMuted;
            c[ImGuiCol_TitleBg] = p.windowBg;
            c[ImGuiCol_TitleBgActive] = p.windowBg;
            c[ImGuiCol_TitleBgCollapsed] = p.windowBg;
            c[ImGuiCol_MenuBarBg] = p.windowBg;
            c[ImGuiCol_ScrollbarBg] = transparent;
            c[ImGuiCol_ScrollbarGrab] = p.frameBg;
            c[ImGuiCol_ScrollbarGrabHovered] = Lighter(p.frameBg, kScrollbarLift);
            c[ImGuiCol_ScrollbarGrabActive] = p.accent;
            c[ImGuiCol_CheckMark] = p.accent;
            c[ImGuiCol_SliderGrab] = p.accent;
            c[ImGuiCol_SliderGrabActive] = p.accentHover;
            c[ImGuiCol_Button] = p.frameBg;
            c[ImGuiCol_ButtonHovered] = frameHovered;
            c[ImGuiCol_ButtonActive] = p.accentActive;
            c[ImGuiCol_Header] = p.accentMuted;
            c[ImGuiCol_HeaderHovered] = Lighter(p.accentMuted, kHoverLift);
            c[ImGuiCol_HeaderActive] = p.accent;
            c[ImGuiCol_Separator] = p.border;
            c[ImGuiCol_SeparatorHovered] = p.accentHover;
            c[ImGuiCol_SeparatorActive] = p.accent;
            c[ImGuiCol_ResizeGrip] = p.accentMuted;
            c[ImGuiCol_ResizeGripHovered] = p.accentHover;
            c[ImGuiCol_ResizeGripActive] = p.accent;
            c[ImGuiCol_InputTextCursor] = p.text;
            c[ImGuiCol_Tab] = p.windowBg;
            c[ImGuiCol_TabHovered] = p.accentMuted;
            c[ImGuiCol_TabSelected] = p.panelBg;
            c[ImGuiCol_TabSelectedOverline] = p.accent;
            c[ImGuiCol_TabDimmed] = p.windowBg;
            c[ImGuiCol_TabDimmedSelected] = p.panelBg;
            c[ImGuiCol_TabDimmedSelectedOverline] = p.accentMuted;
            c[ImGuiCol_DockingPreview] = WithAlpha(p.accent, kDockingPreviewAlpha);
            c[ImGuiCol_DockingEmptyBg] = p.windowBg;
            c[ImGuiCol_PlotLines] = p.accentHover;
            c[ImGuiCol_PlotLinesHovered] = p.warning;
            c[ImGuiCol_PlotHistogram] = p.accent;
            c[ImGuiCol_PlotHistogramHovered] = p.accentHover;
            c[ImGuiCol_TableHeaderBg] = p.frameBg;
            c[ImGuiCol_TableBorderStrong] = p.border;
            c[ImGuiCol_TableBorderLight] = WithAlpha(p.border, 0.6f);
            c[ImGuiCol_TableRowBg] = transparent;
            c[ImGuiCol_TableRowBgAlt] = WithAlpha(p.text, kRowAltAlpha);
            c[ImGuiCol_TextLink] = p.accentHover;
            c[ImGuiCol_TextSelectedBg] = WithAlpha(p.accent, kTextSelectionAlpha);
            c[ImGuiCol_TreeLines] = p.border;
            c[ImGuiCol_DragDropTarget] = p.accentHover;
            c[ImGuiCol_DragDropTargetBg] = WithAlpha(p.accent, kTextSelectionAlpha * 0.5f);
            c[ImGuiCol_UnsavedMarker] = p.warning;
            c[ImGuiCol_NavCursor] = p.accentHover;
            c[ImGuiCol_NavWindowingHighlight] = WithAlpha(p.text, 0.7f);
            c[ImGuiCol_NavWindowingDimBg] = WithAlpha(p.windowBg, kDimAlpha);
            c[ImGuiCol_ModalWindowDimBg] = WithAlpha(p.windowBg, kDimAlpha);
        }

        void ApplySizes(ImGuiStyle& style) {
            style.WindowRounding = kWindowRounding;
            style.ChildRounding = kChildRounding;
            style.FrameRounding = kFrameRounding;
            style.PopupRounding = kPopupRounding;
            style.ScrollbarRounding = kScrollbarRounding;
            style.GrabRounding = kGrabRounding;
            style.TabRounding = kTabRounding;
            style.WindowPadding = kWindowPadding;
            style.FramePadding = kFramePadding;
            style.ItemSpacing = kItemSpacing;
            style.ItemInnerSpacing = kItemInnerSpacing;
            style.CellPadding = kCellPadding;
            style.IndentSpacing = kIndentSpacing;
            style.ScrollbarSize = kScrollbarSize;
            style.GrabMinSize = kGrabMinSize;
            style.TabBarOverlineSize = kTabBarOverlineSize;
            style.DockingSeparatorSize = kDockingSeparatorSize;
        }

    }  // namespace

    const ImGuiPalette& GetImGuiPalette() {
        return kPalette;
    }

    void ApplyImGuiTheme(ImGuiStyle& style, float uiScale) {
        if (!std::isfinite(uiScale) || uiScale <= 0.0f) {
            uiScale = 1.0f;
        }

        const float fontSizeBase = style.FontSizeBase;
        const float fontScaleMain = style.FontScaleMain;
        const float fontScaleDpi = style.FontScaleDpi;

        style = ImGuiStyle();
        ApplyColours(style);
        ApplySizes(style);
        style.ScaleAllSizes(uiScale);

        style.FontSizeBase = fontSizeBase;
        style.FontScaleMain = fontScaleMain;
        style.FontScaleDpi = fontScaleDpi;
    }

}  // namespace BECore
