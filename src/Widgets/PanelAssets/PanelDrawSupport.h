#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Widgets/ImGuiTheme.h>
#include <cfloat>
#include <imgui.h>

namespace BECore::PanelDraw {

    /// What a value that cannot be read is drawn as
    inline constexpr const char* kUnreadable = "?";
    /// Starts the label of a control whose visible text is empty: ImGui hides it and hashes the rest
    inline constexpr const char* kIdSuffix = "##";
    /// Starts the id part of a label whose visible text changes, so the control keeps its id
    inline constexpr const char* kStableIdSuffix = "###";
    /// The unlimited width of ImGui: the item ends at the right edge of the window
    inline constexpr float kFillWidth = -FLT_MIN;

    inline const char* OrNull(const String& text) {
        return text.empty() ? nullptr : text.c_str();
    }

    inline void DrawUnreadable() {
        ImGui::TextDisabled("%s", kUnreadable);
    }

    /// The colour of a text style; null for the normal one, which keeps the theme's text colour
    inline const ImVec4* ColorOf(PanelTextStyle style) {
        const ImGuiPalette& palette = GetImGuiPalette();
        switch (style) {
            case PanelTextStyle::Disabled:
                return &palette.textDisabled;
            case PanelTextStyle::Success:
                return &palette.success;
            case PanelTextStyle::Warning:
                return &palette.warning;
            case PanelTextStyle::Error:
                return &palette.error;
            default:
                return nullptr;
        }
    }

    /// Pushes the colour of a style for the next item; the destructor pops it
    class ScopedTextStyle {
    public:
        explicit ScopedTextStyle(PanelTextStyle style) {
            if (const ImVec4* color = ColorOf(style)) {
                ImGui::PushStyleColor(ImGuiCol_Text, *color);
                _pushed = true;
            }
        }
        ~ScopedTextStyle() {
            if (_pushed) {
                ImGui::PopStyleColor();
            }
        }
        ScopedTextStyle(const ScopedTextStyle&) = delete;
        ScopedTextStyle& operator=(const ScopedTextStyle&) = delete;

    private:
        bool _pushed = false;
    };

}  // namespace BECore::PanelDraw
