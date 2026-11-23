#pragma once

#include <chrono>
#include <ctime>

namespace BECore {

    /// The local wall-clock time of a moment, split the way the log lines print it. Only the six calendar fields of `tm` are set
    /// (year, month, day, hour, minute, second); `tm_wday`, `tm_yday` and `tm_isdst` stay 0.
    struct LocalLogTime {
        std::tm tm{};
        int millisecond = 0;
    };

    /// Reads the UTC offset of the local zone, once; idempotent and thread-safe. `LoggerManager::Initialize` calls it before the sinks
    /// are built, because the first `localtime_r` of a process allocates (zone tables) and a later call to it would show up in a
    /// steady frame. `GetLocalLogTime` calls it when nobody did.
    void InitLogClock();

    /// The local time now: the system clock plus the offset `InitLogClock` read, so a change of the zone or a DST switch during a
    /// run shows from the next start. Thread-safe, allocates nothing after the first call.
    LocalLogTime GetLocalLogTime();

    /// The fields of @p moment in a zone @p utcOffset ahead of UTC. Pure (no zone, no clock).
    LocalLogTime MakeLocalLogTime(std::chrono::system_clock::time_point moment, std::chrono::seconds utcOffset);

    /// The offset `GetLocalLogTime` adds (`InitLogClock` read it).
    std::chrono::seconds GetLogUtcOffset();

}  // namespace BECore
