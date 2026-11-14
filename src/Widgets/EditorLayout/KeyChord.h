#pragma once

#include <imgui.h>

namespace BECore {

    /// Modifier bits of a KeyChord. Ctrl is Cmd on macOS: ImGui maps it (ImGuiMod_Ctrl), the chord only names it "Ctrl".
    inline constexpr uint8_t kChordModCtrl = 1 << 0;
    inline constexpr uint8_t kChordModAlt = 1 << 1;
    inline constexpr uint8_t kChordModShift = 1 << 2;

    /**
     * @brief A keyboard shortcut of the editor: one key and a set of modifiers.
     *
     * Plain data without an ImGui context, so a test can drive it. The text form is `Ctrl+Alt+Shift+Key` (ToString):
     * modifiers in that order, then the key (a letter or digit, F1..F12, Enter, Space, Tab, Esc, Left, Right, Up, Down,
     * Home, End, PageUp, PageDown, Delete, Backspace, `,` `.` `/`).
     */
    struct KeyChord {
        ImGuiKey key = ImGuiKey_None;
        uint8_t mods = 0;

        [[nodiscard]] constexpr bool operator==(const KeyChord& other) const = default;

        /// The value for ImGui::IsKeyChordPressed and ImGui::Shortcut
        [[nodiscard]] ImGuiKeyChord ToImGui() const;
    };

    /// The canonical text of @p chord ("Ctrl+Shift+K"). A chord with a key that has no name gives an empty text.
    [[nodiscard]] String ToString(const KeyChord& chord);

    /// Reads a chord from @p text: case-insensitive, spaces around a part are ignored, a few aliases are read (Escape,
    /// Return, Comma, Period, Slash). An empty optional for anything else: no key, two keys, a repeated modifier, a
    /// modifier after the key, an unknown name. Never asserts.
    [[nodiscard]] Optional<KeyChord> ParseKeyChord(StringView text);

    /// True when ImGui key @p key has a name in the chord text (a key that ToString and ParseKeyChord handle)
    [[nodiscard]] bool IsNamedKey(ImGuiKey key);

    /// True when @p chord may be a hotkey: a named key with Ctrl or Alt held, or an F-key (alone or with any modifiers).
    /// A bare letter or Shift+letter would fire while the user types in a text field, so it is refused, and so is a bare
    /// modifier (no key).
    [[nodiscard]] bool IsAllowedAsHotkey(const KeyChord& chord);

}  // namespace BECore
