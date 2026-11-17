#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /// Cumulative CPU time in the platform's tick unit: the busy part and the whole
    struct CpuTicks {
        uint64_t busy = 0;
        uint64_t total = 0;
    };

    /**
     * @brief Turns two samples of cumulative ticks into the busy share between them.
     *
     * Update() gives no value for the first call, for a total that did not move and for counters that went backwards (a reset, a
     * resume); the last two keep the new sample as the baseline, so the next call has a delta again.
     */
    class CpuLoadTracker {
    public:
        /// The percent (0..100, rounded) of the busy share since the previous call
        [[nodiscard]] Optional<uint32_t> Update(CpuTicks now);

    private:
        Optional<CpuTicks> _previous;
    };

    /**
     * @brief The aggregate `cpu ` line of the text of `/proc/stat`: idle (idle + iowait) is not busy, total is the sum of the first
     * eight fields. Lines of a single core (`cpu0`) are skipped; a line with fewer than the four basic fields or a non-number is no line.
     */
    [[nodiscard]] Optional<CpuTicks> ParseProcStatCpu(StringView text);

    /// A hwmon `tempN_input` (thousandths of a degree, a trailing newline allowed) as whole °C; nothing outside the plausible range of HardwareReadings
    [[nodiscard]] Optional<int32_t> ParseMilliCelsius(StringView text);

    /// True for the `name` file of a hwmon device that reports the CPU's temperature (a trailing newline allowed)
    [[nodiscard]] bool IsCpuHwmonName(StringView name);

    /// The value of the first `model name` line of the text of `/proc/cpuinfo` (a view into @p text), empty when there is none
    [[nodiscard]] StringView ParseCpuModelName(StringView text);

}  // namespace BECore
