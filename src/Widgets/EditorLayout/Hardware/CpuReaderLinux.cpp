#include <Widgets/EditorLayout/Hardware/CpuLoad.h>
#include <Widgets/EditorLayout/Hardware/CpuReaders.h>

// Not compiled in the loop that wrote it (no Linux machine): POSIX calls into stack buffers only, every decision is a tested
// parser of CpuLoad.h (Hardware/CLAUDE.md, "Compiled where").
#ifdef PLATFORM_LINUX

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

namespace BECore {

    namespace {

        constexpr const char* kProcStatPath = "/proc/stat";
        constexpr const char* kProcCpuInfoPath = "/proc/cpuinfo";
        /// The aggregate line is the first and about a hundred bytes; the per-core lines after it are not needed
        constexpr size_t kProcStatBytes = 1024;
        /// `model name` is among the first lines of the first core's block
        constexpr size_t kProcCpuInfoBytes = 4096;
        constexpr size_t kHwmonValueBytes = 64;
        constexpr int kMaxHwmonDevices = 16;
        constexpr size_t kPathBytes = 64;

        /// Reads at most @p capacity - 1 bytes of the file with one read and terminates them; the length, or -1 when the file cannot be read.
        ssize_t ReadSmallFile(const char* path, char* buffer, size_t capacity) {
            const int file = open(path, O_RDONLY | O_CLOEXEC);
            if (file < 0) {
                return -1;
            }
            ssize_t count = 0;
            do {
                count = read(file, buffer, capacity - 1);
            } while (count < 0 && errno == EINTR);
            close(file);
            if (count < 0) {
                return -1;
            }
            buffer[count] = '\0';
            return count;
        }

        class CpuReaderLinux final : public IHardwareReader {
        public:
            bool Read(HardwareReadings& out) override {
                if (!_opened) {
                    Open();
                    _opened = true;
                }
                SetHardwareName(out, StringView(_name));

                char statText[kProcStatBytes];
                const ssize_t statLength = ReadSmallFile(kProcStatPath, statText, sizeof(statText));
                if (statLength > 0) {
                    const Optional<CpuTicks> ticks = ParseProcStatCpu(StringView(statText, static_cast<size_t>(statLength)));
                    if (ticks) {
                        out.loadPercent = _tracker.Update(*ticks);
                    }
                }
                if (_temperaturePath[0] != '\0') {
                    char value[kHwmonValueBytes];
                    const ssize_t valueLength = ReadSmallFile(_temperaturePath, value, sizeof(value));
                    if (valueLength > 0) {
                        out.temperatureC = ParseMilliCelsius(StringView(value, static_cast<size_t>(valueLength)));
                    }
                }
                return HasHardwareValue(out);
            }

        private:
            /// Once, on the first read: the hwmon device of the CPU and the model name
            void Open() {
                for (int index = 0; index < kMaxHwmonDevices; ++index) {
                    char path[kPathBytes];
                    char name[kHwmonValueBytes];
                    std::snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon%d/name", index);
                    const ssize_t length = ReadSmallFile(path, name, sizeof(name));
                    if (length > 0 && IsCpuHwmonName(StringView(name, static_cast<size_t>(length)))) {
                        std::snprintf(_temperaturePath, sizeof(_temperaturePath), "/sys/class/hwmon/hwmon%d/temp1_input", index);
                        break;
                    }
                }

                char info[kProcCpuInfoBytes];
                const ssize_t infoLength = ReadSmallFile(kProcCpuInfoPath, info, sizeof(info));
                if (infoLength > 0) {
                    const StringView model = ParseCpuModelName(StringView(info, static_cast<size_t>(infoLength)));
                    const size_t length = model.size() < sizeof(_name) - 1 ? model.size() : sizeof(_name) - 1;
                    std::memcpy(_name, model.data(), length);
                    _name[length] = '\0';
                }
            }

            CpuLoadTracker _tracker;
            bool _opened = false;
            char _temperaturePath[kPathBytes] = {};
            char _name[kHardwareNameSize] = {};
        };

    }  // namespace

    UniquePtr<IHardwareReader> MakeLinuxCpuReader() {
        return MakeUnique<CpuReaderLinux>();
    }

}  // namespace BECore

#endif  // PLATFORM_LINUX
