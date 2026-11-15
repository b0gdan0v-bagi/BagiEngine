#pragma once

#include <cstdint>

namespace BECore {

    /// Where a log call is written. `file` must outlive the program (the `__FILE__` literal): the ring buffer
    /// stores the pointer, not a copy of the text. A line logged without a site (an assert handler, a test) has none.
    struct LogSite {
        const char* file = nullptr;
        uint32_t line = 0;
    };

    /// What the log macros pass: `__LINE__` is an int
    constexpr LogSite MakeLogSite(const char* file, int line) {
        return LogSite{file, static_cast<uint32_t>(line)};
    }

}  // namespace BECore
