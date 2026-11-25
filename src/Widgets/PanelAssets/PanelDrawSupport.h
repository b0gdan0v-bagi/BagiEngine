#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/Singleton.h>
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
    /// The glyph of an icon and the text after it are one label
    inline constexpr const char* kGlyphGap = " ";
    /// The unlimited width of ImGui: the item ends at the right edge of the window
    inline constexpr float kFillWidth = -FLT_MIN;

    inline const char* OrNull(const String& text) {
        return text.empty() ? nullptr : text.c_str();
    }

    inline void DrawUnreadable() {
        ImGui::TextDisabled("%s", kUnreadable);
    }

    /// The label a control draws after itself (`text` of a number, a combo or an input), on its line; nothing for an empty one
    inline void DrawTrailingText(const String& text) {
        if (text.empty()) {
            return;
        }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
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

    /// Fills the next buttons with the palette's accent (`Accent`) or muted accent (`Muted`) while @p active; the destructor pops the colours. `None` pushes nothing
    class ScopedButtonFill {
    public:
        ScopedButtonFill(PanelButtonFill fill, bool active) {
            if (fill == PanelButtonFill::None || !active) {
                return;
            }
            const ImGuiPalette& palette = GetImGuiPalette();
            ImGui::PushStyleColor(ImGuiCol_Button, fill == PanelButtonFill::Accent ? palette.accent : palette.accentMuted);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.accentHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.accentActive);
            _pushed = true;
        }
        ~ScopedButtonFill() {
            if (_pushed) {
                ImGui::PopStyleColor(kPushedColors);
            }
        }
        ScopedButtonFill(const ScopedButtonFill&) = delete;
        ScopedButtonFill& operator=(const ScopedButtonFill&) = delete;

    private:
        static constexpr int kPushedColors = 3;
        bool _pushed = false;
    };

}  // namespace BECore::PanelDraw

namespace BECore {

    /// Where a menu item gets the text of its command's chord from: the host of the interpreter knows the hotkey map, the format does not
    class PanelChordText {
    public:
        virtual ~PanelChordText() = default;

        /// The chord of the editor command @p command as text, empty for none. The view is valid until the next call; the interpreter calls it for every item of an open menu
        /// every frame, so an implementation caches and allocates nothing in a steady frame
        [[nodiscard]] virtual StringView ChordOf(StringView command) = 0;
    };

    /// The provider of the editor's chord text, found by the panel widgets. Main thread only, so it takes no lock
    class PanelChordTextRegistry : public Singleton<PanelChordTextRegistry> {
    public:
        PanelChordTextRegistry() = default;

        /// Makes @p chords the provider `Get()` returns; the host calls it when it is made and `Uninstall` when it ends. A provider installed later replaces the first
        void Install(PanelChordText* chords) {
            _installed = chords;
        }

        /// Clears the provider when it is @p chords, so a replaced one does not remove its successor
        void Uninstall(const PanelChordText* chords) {
            if (_installed == chords) {
                _installed = nullptr;
            }
        }

        /// The provider of the editor, or null where no host installed one (a game's own panels, a test); a widget asks again every frame, because the host is made after the assets
        [[nodiscard]] PanelChordText* Get() const {
            return _installed;
        }

    private:
        PanelChordText* _installed = nullptr;
    };

}  // namespace BECore
