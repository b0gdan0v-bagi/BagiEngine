#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>

namespace BECore {

    /// The first @p maxBytes of @p text, never cut inside a UTF-8 sequence. A pooled string may be any length, and drawing one must not cost
    /// what it is long.
    [[nodiscard]] inline StringView PoolStringPrefix(StringView text, size_t maxBytes) {
        if (text.size() <= maxBytes) {
            return text;
        }
        constexpr unsigned char kContinuationMask = 0xC0;
        constexpr unsigned char kContinuation = 0x80;
        size_t end = maxBytes;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & kContinuationMask) == kContinuation) {
            --end;
        }
        return text.substr(0, end);
    }

    /// What one line of the panel shows of @p text: its `PoolStringPrefix` up to the first line break, so a cell never grows its row
    [[nodiscard]] inline StringView PoolStringLine(StringView text, size_t maxBytes) {
        const StringView prefix = PoolStringPrefix(text, maxBytes);
        return prefix.substr(0, prefix.find_first_of("\r\n"));
    }

}  // namespace BECore
