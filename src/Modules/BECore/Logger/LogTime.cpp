#include "LogTime.h"

#include <chrono>

namespace BECore {

    LocalLogTime GetLocalLogTime() {
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        LocalLogTime result;
        result.millisecond = static_cast<int>(ms.count());
#ifdef _WIN32
        localtime_s(&result.tm, &time);
#else
        localtime_r(&time, &result.tm);
#endif
        return result;
    }

}  // namespace BECore
