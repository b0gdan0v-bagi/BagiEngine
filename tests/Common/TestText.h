#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>

namespace BECore::Tests {

    inline bool Contains(StringView text, StringView part) {
        return text.find(part) != StringView::npos;
    }

    /// The number of characters (code points) of a UTF-8 text: the length of a word, whatever the language
    [[nodiscard]] inline size_t Characters(StringView text) {
        size_t count = 0;
        for (const char c : text) {
            if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u) {
                ++count;
            }
        }
        return count;
    }

}  // namespace BECore::Tests
