#include <Widgets/EditorLayout/Hardware/CpuLoad.h>
#include <Widgets/EditorLayout/Hardware/CpuReaders.h>

#ifdef PLATFORM_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace BECore {

    namespace {

        uint64_t ToTicks(const FILETIME& time) {
            return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        }

        /// The load of all cores together from the system's cumulative times (units of 100 ns)
        class CpuReaderWindows final : public IHardwareReader {
        public:
            bool Read(HardwareReadings& out) override {
                FILETIME idle{};
                FILETIME kernel{};
                FILETIME user{};
                if (GetSystemTimes(&idle, &kernel, &user) == FALSE) {
                    return false;
                }
                // The kernel time contains the idle time
                const uint64_t total = ToTicks(kernel) + ToTicks(user);
                const uint64_t idleTicks = ToTicks(idle);
                out.loadPercent = _tracker.Update(CpuTicks{.busy = total > idleTicks ? total - idleTicks : 0, .total = total});
                return out.loadPercent.has_value();
            }

        private:
            CpuLoadTracker _tracker;
        };

    }  // namespace

    UniquePtr<IHardwareReader> MakeWindowsCpuReader() {
        return MakeUnique<CpuReaderWindows>();
    }

}  // namespace BECore

#endif  // PLATFORM_WINDOWS
