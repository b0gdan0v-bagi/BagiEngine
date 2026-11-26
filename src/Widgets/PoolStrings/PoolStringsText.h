#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>

namespace BECore {

    /// The part of @p text that one line of the panel shows: up to the first line break and at most @p maxBytes, never cut inside a UTF-8
    /// sequence. A pooled string may hold anything, and a drawn cell must neither grow its row nor cost what the string is long.
    [[nodiscard]] inline StringView PoolStringLine(StringView text, size_t maxBytes) {
        size_t end = 0;
        while (end < text.size() && end < maxBytes && text[end] != '\n' && text[end] != '\r') {
            ++end;
        }
        if (end < text.size() && end == maxBytes) {
            constexpr unsigned char kContinuationMask = 0xC0;
            constexpr unsigned char kContinuation = 0x80;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & kContinuationMask) == kContinuation) {
                --end;
            }
        }
        return text.substr(0, end);
    }

}  // namespace BECore
