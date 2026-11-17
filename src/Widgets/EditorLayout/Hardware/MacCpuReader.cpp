#include <Widgets/EditorLayout/Hardware/CpuLoad.h>
#include <Widgets/EditorLayout/Hardware/MacReaders.h>

// Not compiled in the loop that wrote it (no Mac): host_statistics ticks into the tested CpuLoadTracker (Hardware/CLAUDE.md,
// "macOS").
#ifdef PLATFORM_MACOS

#include <mach/mach.h>
#include <sys/sysctl.h>

namespace BECore {

    namespace {

        class MacCpuReader final : public IHardwareReader {
        public:
            explicit MacCpuReader(MacSensorsPtr sensors) : _sensors(std::move(sensors)), _host(mach_host_self()) {
                size_t size = sizeof(_name) - 1;
                if (sysctlbyname("machdep.cpu.brand_string", _name, &size, nullptr, 0) != 0) {
                    _name[0] = '\0';
                }
            }
            ~MacCpuReader() override {
                // mach_host_self() adds a send right on every call; this is the one it added
                mach_port_deallocate(mach_task_self(), _host);
            }
            MacCpuReader(const MacCpuReader&) = delete;
            MacCpuReader& operator=(const MacCpuReader&) = delete;

            bool Read(HardwareReadings& out) override {
                SetHardwareName(out, StringView(_name));
                // The result is a plain struct: unlike host_processor_info there is nothing to deallocate
                host_cpu_load_info_data_t info{};
                mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
                if (host_statistics(_host, HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&info), &count) == KERN_SUCCESS) {
                    // Ticks are 32-bit and cumulative; a wrap looks like a reset to the tracker, which restarts its baseline
                    const uint64_t busy = static_cast<uint64_t>(info.cpu_ticks[CPU_STATE_USER]) + info.cpu_ticks[CPU_STATE_SYSTEM] + info.cpu_ticks[CPU_STATE_NICE];
                    out.loadPercent = _tracker.Update(CpuTicks{.busy = busy, .total = busy + info.cpu_ticks[CPU_STATE_IDLE]});
                }
                out.temperatureC = _sensors->ReadCpuC();
                return HasHardwareValue(out);
            }

        private:
            MacSensorsPtr _sensors;
            host_t _host;
            char _name[kHardwareNameSize] = {};
            CpuLoadTracker _tracker;
        };

    }  // namespace

    UniquePtr<IHardwareReader> MakeMacCpuReader(MacSensorsPtr sensors) {
        return MakeUnique<MacCpuReader>(std::move(sensors));
    }

}  // namespace BECore

#endif  // PLATFORM_MACOS
