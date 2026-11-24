#pragma once

#include <BECore/Control/ControlTypes.h>
#include <expected>

namespace BECore::Detail {

    /// The syntax of an RFC 6901 pointer, shared by FindJsonPointer and CheckJsonPointer (JsonViewOps.h) so
    /// they answer the same errors with the same messages.
    inline constexpr char kPointerSeparator = '/';

    /// The error `invalid_params` "bad JSON pointer: <why>"
    [[nodiscard]] ControlError BadPointer(StringView why);

    /// Checks every `~` of @p pointer, which is not empty and starts with `/`: each is followed by `0` or `1`
    [[nodiscard]] Optional<ControlError> CheckPointerEscapes(StringView pointer);

    /// The key a token names: `~1` is `/`, `~0` is `~`. @p storage holds the text when there is something to unescape
    [[nodiscard]] StringView UnescapePointerToken(StringView token, String& storage);

    /// The index a token names in an array of @p size elements; nullopt when it is past the end. An error when the token is not an array index
    [[nodiscard]] std::expected<Optional<size_t>, ControlError> ParseArrayIndex(StringView token, size_t size);

}  // namespace BECore::Detail
