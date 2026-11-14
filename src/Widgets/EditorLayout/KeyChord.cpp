#include "KeyChord.h"

namespace BECore {

    namespace {

        struct NamedKey {
            ImGuiKey key;
            StringView name;
        };

        // Keys with a fixed name; letters, digits and F1..F12 are laid out in ImGuiKey in a row and named by their offset
        constexpr NamedKey kNamedKeys[] = {
            {ImGuiKey_Enter, "Enter"},   {ImGuiKey_Space, "Space"},         {ImGuiKey_Tab, "Tab"},   {ImGuiKey_Escape, "Esc"}, {ImGuiKey_LeftArrow, "Left"}, {ImGuiKey_RightArrow, "Right"},
            {ImGuiKey_UpArrow, "Up"},    {ImGuiKey_DownArrow, "Down"},      {ImGuiKey_Home, "Home"}, {ImGuiKey_End, "End"},    {ImGuiKey_PageUp, "PageUp"},  {ImGuiKey_PageDown, "PageDown"},
            {ImGuiKey_Delete, "Delete"}, {ImGuiKey_Backspace, "Backspace"}, {ImGuiKey_Comma, ","},   {ImGuiKey_Period, "."},   {ImGuiKey_Slash, "/"},
        };

        // Other spellings that ParseKeyChord reads (ToString never writes them)
        constexpr NamedKey kKeyAliases[] = {
            {ImGuiKey_Escape, "Escape"}, {ImGuiKey_Enter, "Return"}, {ImGuiKey_Comma, "Comma"}, {ImGuiKey_Period, "Period"}, {ImGuiKey_Slash, "Slash"},
        };

        constexpr int kLetterCount = ImGuiKey_Z - ImGuiKey_A + 1;
        constexpr int kDigitCount = ImGuiKey_9 - ImGuiKey_0 + 1;
        constexpr int kFunctionKeyCount = ImGuiKey_F12 - ImGuiKey_F1 + 1;

        constexpr char ToLowerAscii(char c) {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool EqualsNoCase(StringView lhs, StringView rhs) {
            if (lhs.size() != rhs.size()) {
                return false;
            }
            for (size_t i = 0; i < lhs.size(); ++i) {
                if (ToLowerAscii(lhs[i]) != ToLowerAscii(rhs[i])) {
                    return false;
                }
            }
            return true;
        }

        StringView Trim(StringView text) {
            while (!text.empty() && text.front() == ' ') {
                text.remove_prefix(1);
            }
            while (!text.empty() && text.back() == ' ') {
                text.remove_suffix(1);
            }
            return text;
        }

        bool InRange(ImGuiKey key, ImGuiKey first, int count) {
            return key >= first && key < first + count;
        }

        bool IsFunctionKey(ImGuiKey key) {
            return InRange(key, ImGuiKey_F1, kFunctionKeyCount);
        }

        Optional<uint8_t> ParseModifier(StringView token) {
            if (EqualsNoCase(token, "Ctrl")) {
                return kChordModCtrl;
            }
            if (EqualsNoCase(token, "Alt")) {
                return kChordModAlt;
            }
            if (EqualsNoCase(token, "Shift")) {
                return kChordModShift;
            }
            return std::nullopt;
        }

        String KeyToString(ImGuiKey key) {
            if (InRange(key, ImGuiKey_A, kLetterCount)) {
                return String(1, static_cast<char>('A' + (key - ImGuiKey_A)));
            }
            if (InRange(key, ImGuiKey_0, kDigitCount)) {
                return String(1, static_cast<char>('0' + (key - ImGuiKey_0)));
            }
            if (IsFunctionKey(key)) {
                return Format("F{}", key - ImGuiKey_F1 + 1);
            }
            for (const NamedKey& named : kNamedKeys) {
                if (named.key == key) {
                    return String(named.name.data(), named.name.size());
                }
            }
            return {};
        }

        Optional<ImGuiKey> ParseKey(StringView token) {
            if (token.size() == 1) {
                const char c = ToLowerAscii(token[0]);
                if (c >= 'a' && c <= 'z') {
                    return static_cast<ImGuiKey>(ImGuiKey_A + (c - 'a'));
                }
                if (c >= '0' && c <= '9') {
                    return static_cast<ImGuiKey>(ImGuiKey_0 + (c - '0'));
                }
            }
            if (token.size() >= 2 && ToLowerAscii(token[0]) == 'f') {
                int number = 0;
                const auto [end, error] = std::from_chars(token.data() + 1, token.data() + token.size(), number);
                if (error == std::errc() && end == token.data() + token.size() && number >= 1 && number <= kFunctionKeyCount) {
                    return static_cast<ImGuiKey>(ImGuiKey_F1 + (number - 1));
                }
            }
            for (const NamedKey& named : kNamedKeys) {
                if (EqualsNoCase(token, named.name)) {
                    return named.key;
                }
            }
            for (const NamedKey& alias : kKeyAliases) {
                if (EqualsNoCase(token, alias.name)) {
                    return alias.key;
                }
            }
            return std::nullopt;
        }

    }  // namespace

    ImGuiKeyChord KeyChord::ToImGui() const {
        ImGuiKeyChord chord = key;
        if ((mods & kChordModCtrl) != 0) {
            chord |= ImGuiMod_Ctrl;
        }
        if ((mods & kChordModAlt) != 0) {
            chord |= ImGuiMod_Alt;
        }
        if ((mods & kChordModShift) != 0) {
            chord |= ImGuiMod_Shift;
        }
        return chord;
    }

    String ToString(const KeyChord& chord) {
        String keyText = KeyToString(chord.key);
        if (keyText.empty()) {
            return {};
        }
        String text;
        if ((chord.mods & kChordModCtrl) != 0) {
            text += "Ctrl+";
        }
        if ((chord.mods & kChordModAlt) != 0) {
            text += "Alt+";
        }
        if ((chord.mods & kChordModShift) != 0) {
            text += "Shift+";
        }
        text += keyText;
        return text;
    }

    Optional<KeyChord> ParseKeyChord(StringView text) {
        KeyChord chord;
        bool hasKey = false;
        size_t start = 0;
        while (true) {
            const size_t plus = text.find('+', start);
            const StringView token = Trim(text.substr(start, plus == StringView::npos ? StringView::npos : plus - start));
            if (token.empty()) {
                return std::nullopt;
            }
            if (const Optional<uint8_t> mod = ParseModifier(token)) {
                if (hasKey || (chord.mods & *mod) != 0) {
                    return std::nullopt;
                }
                chord.mods |= *mod;
            } else if (const Optional<ImGuiKey> key = ParseKey(token); key && !hasKey) {
                chord.key = *key;
                hasKey = true;
            } else {
                return std::nullopt;
            }
            if (plus == StringView::npos) {
                break;
            }
            start = plus + 1;
        }
        if (!hasKey) {
            return std::nullopt;
        }
        return chord;
    }

    bool IsNamedKey(ImGuiKey key) {
        return !KeyToString(key).empty();
    }

    bool IsAllowedAsHotkey(const KeyChord& chord) {
        if (!IsNamedKey(chord.key)) {
            return false;
        }
        return IsFunctionKey(chord.key) || (chord.mods & (kChordModCtrl | kChordModAlt)) != 0;
    }

}  // namespace BECore
