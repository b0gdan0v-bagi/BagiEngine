#pragma once

#include <ctime>

namespace BECore {

    /// The local wall-clock time of a moment, split the way the log lines print it.
    struct LocalLogTime {
        std::tm tm{};
        int millisecond = 0;
    };

    /// The local time now. Thread-safe (`localtime_r` / `localtime_s`).
    LocalLogTime GetLocalLogTime();

}  // namespace BECore
