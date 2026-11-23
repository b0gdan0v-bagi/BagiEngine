#include "LogTime.h"


namespace BECore {

    namespace {

        constexpr int64_t kSecondsPerDay = 86400;
        constexpr int64_t kSecondsPerHour = 3600;
        constexpr int64_t kSecondsPerMinute = 60;
        constexpr int kTmYearBase = 1900;

        struct CivilDate {
            int64_t year = 0;
            int month = 1;
            int day = 1;
        };

        /// Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's days_from_civil).
        int64_t DaysFromCivil(int64_t year, int month, int day) {
            year -= month <= 2 ? 1 : 0;
            const int64_t era = (year >= 0 ? year : year - 399) / 400;
            const int64_t yearOfEra = year - era * 400;
            const int64_t dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
            const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
            return era * 146097 + dayOfEra - 719468;
        }

        /// The inverse of DaysFromCivil (H. Hinnant's civil_from_days).
        CivilDate CivilFromDays(int64_t days) {
            days += 719468;
            const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
            const int64_t dayOfEra = days - era * 146097;
            const int64_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
            const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
            const int64_t shiftedMonth = (5 * dayOfYear + 2) / 153;
            CivilDate date;
            date.day = static_cast<int>(dayOfYear - (153 * shiftedMonth + 2) / 5 + 1);
            date.month = static_cast<int>(shiftedMonth < 10 ? shiftedMonth + 3 : shiftedMonth - 9);
            date.year = yearOfEra + era * 400 + (date.month <= 2 ? 1 : 0);
            return date;
        }

        /// Seconds of the local zone ahead of UTC at this moment, from the C library once (the one place that touches a zone).
        std::chrono::seconds ReadUtcOffset() {
            const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm local{};
#ifdef _WIN32
            localtime_s(&local, &now);
#else
            localtime_r(&now, &local);
#endif
            const int64_t localSeconds =
                DaysFromCivil(local.tm_year + kTmYearBase, local.tm_mon + 1, local.tm_mday) * kSecondsPerDay + local.tm_hour * kSecondsPerHour + local.tm_min * kSecondsPerMinute + local.tm_sec;
            return std::chrono::seconds{localSeconds - static_cast<int64_t>(now)};
        }

    }  // namespace

    std::chrono::seconds GetLogUtcOffset() {
        // A function-local static is initialized once and thread-safely
        static const std::chrono::seconds offset = ReadUtcOffset();
        return offset;
    }

    void InitLogClock() {
        (void)GetLogUtcOffset();
    }

    LocalLogTime MakeLocalLogTime(std::chrono::system_clock::time_point moment, std::chrono::seconds utcOffset) {
        const auto wholeSeconds = std::chrono::floor<std::chrono::seconds>(moment);
        const int64_t local = wholeSeconds.time_since_epoch().count() + utcOffset.count();

        // Floor division: a moment before the epoch still lands on the right day
        int64_t days = local / kSecondsPerDay;
        int64_t secondOfDay = local % kSecondsPerDay;
        if (secondOfDay < 0) {
            secondOfDay += kSecondsPerDay;
            --days;
        }
        const CivilDate date = CivilFromDays(days);

        LocalLogTime result;
        result.tm.tm_year = static_cast<int>(date.year) - kTmYearBase;
        result.tm.tm_mon = date.month - 1;
        result.tm.tm_mday = date.day;
        result.tm.tm_hour = static_cast<int>(secondOfDay / kSecondsPerHour);
        result.tm.tm_min = static_cast<int>(secondOfDay % kSecondsPerHour / kSecondsPerMinute);
        result.tm.tm_sec = static_cast<int>(secondOfDay % kSecondsPerMinute);
        result.millisecond = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(moment - wholeSeconds).count());
        return result;
    }

    LocalLogTime GetLocalLogTime() {
        return MakeLocalLogTime(std::chrono::system_clock::now(), GetLogUtcOffset());
    }

}  // namespace BECore
