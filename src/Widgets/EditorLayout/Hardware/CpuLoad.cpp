#include "CpuLoad.h"

#include <Widgets/EditorLayout/Hardware/HardwareTypes.h>
#include <cmath>

namespace BECore {

    namespace {

        /// user, nice, system, idle: the fields every kernel has
        constexpr size_t kProcStatBasicFields = 4;
        /// user, nice, system, idle, iowait, irq, softirq, steal; the guest columns are already inside user and nice
        constexpr size_t kProcStatSummedFields = 8;
        constexpr size_t kProcStatIdleField = 3;
        constexpr size_t kProcStatIowaitField = 4;
        constexpr int64_t kMilliPerUnit = 1000;
        constexpr int64_t kHalfUnitMilli = kMilliPerUnit / 2;
        constexpr double kPercentScale = 100.0;

        constexpr Array<StringView, 4> kCpuHwmonNames = {"coretemp", "k10temp", "zenpower", "cpu_thermal"};

        constexpr bool IsBlank(char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        StringView TrimEnd(StringView text) {
            while (!text.empty() && IsBlank(text.back())) {
                text.remove_suffix(1);
            }
            return text;
        }

        /// The next blank-separated token; the rest of the line stays in @p line
        StringView NextToken(StringView& line) {
            while (!line.empty() && IsBlank(line.front())) {
                line.remove_prefix(1);
            }
            size_t length = 0;
            while (length < line.size() && !IsBlank(line[length])) {
                ++length;
            }
            const StringView token = line.substr(0, length);
            line.remove_prefix(length);
            return token;
        }

        /// The numbers after `cpu` of one line; false when fewer than the basic fields or a field is not a number
        bool ParseCpuFields(StringView fields, Array<uint64_t, kProcStatSummedFields>& values, size_t& count) {
            count = 0;
            for (StringView token = NextToken(fields); !token.empty() && count < kProcStatSummedFields; token = NextToken(fields)) {
                const Optional<uint64_t> value = StringUtils::ParseNumber<uint64_t>(token);
                if (!value) {
                    return false;
                }
                values[count++] = *value;
            }
            return count >= kProcStatBasicFields;
        }

    }  // namespace

    Optional<uint32_t> CpuLoadTracker::Update(CpuTicks now) {
        const Optional<CpuTicks> previous = _previous;
        _previous = now;
        if (!previous || now.total <= previous->total || now.busy < previous->busy) {
            return std::nullopt;
        }
        const uint64_t busyDelta = now.busy - previous->busy;
        const uint64_t totalDelta = now.total - previous->total;
        const double share = static_cast<double>(busyDelta) / static_cast<double>(totalDelta);
        const double percent = std::round(share * kPercentScale);
        return static_cast<uint32_t>(std::min(percent, kPercentScale));
    }

    Optional<CpuTicks> ParseProcStatCpu(StringView text) {
        constexpr StringView kPrefix = "cpu";
        while (!text.empty()) {
            const size_t end = text.find('\n');
            StringView line = text.substr(0, end);
            text = end == StringView::npos ? StringView{} : text.substr(end + 1);

            if (!line.starts_with(kPrefix) || line.size() == kPrefix.size() || !IsBlank(line[kPrefix.size()])) {
                continue;
            }
            Array<uint64_t, kProcStatSummedFields> values{};
            size_t count = 0;
            if (!ParseCpuFields(line.substr(kPrefix.size()), values, count)) {
                return std::nullopt;
            }
            uint64_t total = 0;
            for (size_t i = 0; i < count; ++i) {
                total += values[i];
            }
            uint64_t idle = values[kProcStatIdleField];
            if (count > kProcStatIowaitField) {
                idle += values[kProcStatIowaitField];
            }
            return CpuTicks{.busy = total - idle, .total = total};
        }
        return std::nullopt;
    }

    Optional<int32_t> ParseMilliCelsius(StringView text) {
        const Optional<int64_t> milli = StringUtils::ParseNumber<int64_t>(TrimEnd(text));
        if (!milli) {
            return std::nullopt;
        }
        int64_t rounded = *milli / kMilliPerUnit;
        const int64_t remainder = *milli % kMilliPerUnit;
        if (remainder >= kHalfUnitMilli) {
            ++rounded;
        } else if (remainder <= -kHalfUnitMilli) {
            --rounded;
        }
        if (rounded < kMinHardwareTemperatureC || rounded > kMaxHardwareTemperatureC) {
            return std::nullopt;
        }
        return static_cast<int32_t>(rounded);
    }

    bool IsCpuHwmonName(StringView name) {
        name = TrimEnd(name);
        for (const StringView known : kCpuHwmonNames) {
            if (name == known) {
                return true;
            }
        }
        return false;
    }

    StringView ParseCpuModelName(StringView text) {
        constexpr StringView kKey = "model name";
        while (!text.empty()) {
            const size_t end = text.find('\n');
            StringView line = text.substr(0, end);
            text = end == StringView::npos ? StringView{} : text.substr(end + 1);
            if (!line.starts_with(kKey)) {
                continue;
            }
            line.remove_prefix(kKey.size());
            const size_t colon = line.find(':');
            if (colon == StringView::npos || !TrimEnd(line.substr(0, colon)).empty()) {
                continue;
            }
            line.remove_prefix(colon + 1);
            while (!line.empty() && IsBlank(line.front())) {
                line.remove_prefix(1);
            }
            return TrimEnd(line);
        }
        return {};
    }

}  // namespace BECore
