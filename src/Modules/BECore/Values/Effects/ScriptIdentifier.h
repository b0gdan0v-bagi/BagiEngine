#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    /// A word of a script or a command name: letters, digits and `_`, no digit first
    [[nodiscard]] constexpr bool IsScriptIdentifierStart(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    [[nodiscard]] constexpr bool IsScriptIdentifierPart(char c) {
        return IsScriptIdentifierStart(c) || (c >= '0' && c <= '9');
    }

    [[nodiscard]] constexpr bool IsScriptIdentifier(StringView text) {
        if (text.empty() || !IsScriptIdentifierStart(text.front())) {
            return false;
        }
        for (const char c : text) {
            if (!IsScriptIdentifierPart(c)) {
                return false;
            }
        }
        return true;
    }

}  // namespace BECore
