#pragma once

#include <imgui.h>

namespace BECore {

    /// The colour roles of the editor's one theme: a Godot-like dark blue-grey with a violet accent. Panels take their
    /// colours from here (the warning of "(unsaved changes)"), never from a literal.
    struct ImGuiPalette {
        // Surfaces, from the deepest to the nearest: the gaps between docked panels, a panel, a field inside it
        ImVec4 windowBg;
        ImVec4 panelBg;
        ImVec4 frameBg;
        ImVec4 border;
        ImVec4 text;
        ImVec4 textDisabled;
        // The violet accent: selection, active tab, focused frame, check mark, slider grab, the Play button
        ImVec4 accent;
        ImVec4 accentHover;
        ImVec4 accentActive;
        ImVec4 accentMuted;  ///< A fill that sits behind text: a selected row, a pressed field
        // Status
        ImVec4 success;
        ImVec4 warning;
        ImVec4 error;
    };

    /// The roles of the theme; a static table, so the reference stays valid
    const ImGuiPalette& GetImGuiPalette();

    /// Sets the whole style: colours from the palette, rounding, padding and spacing from named constants, all sizes scaled
    /// by @p uiScale (0, a negative or a non-finite value counts as 1). It starts from a fresh ImGuiStyle, so applying it
    /// again at a new scale does not scale twice, and keeps only the font fields of @p style (FontSizeBase, FontScaleMain,
    /// FontScaleDpi), which belong to the user's preference and the window.
    void ApplyImGuiTheme(ImGuiStyle& style, float uiScale);

}  // namespace BECore
