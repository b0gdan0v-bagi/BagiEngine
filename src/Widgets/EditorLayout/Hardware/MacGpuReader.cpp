#include <Widgets/EditorLayout/Hardware/MacCoreFoundation.h>
#include <Widgets/EditorLayout/Hardware/MacIoObject.h>
#include <Widgets/EditorLayout/Hardware/MacMapping.h>
#include <Widgets/EditorLayout/Hardware/MacReaders.h>

// Not compiled in the loop that wrote it (no Mac): it only copies numbers out of the IORegistry into MacGpuStats; what they mean is
// decided by MapMacGpu, which is tested everywhere (Hardware/CLAUDE.md, "macOS").
#ifdef PLATFORM_MACOS

#include <sys/sysctl.h>

namespace BECore {

    namespace {

        constexpr const char* kUtilizationKey = "Device Utilization %";
        constexpr const char* kInUseSystemMemoryKey = "In use system memory";
        constexpr const char* kVramUsedKey = "vramUsedBytes";
        constexpr const char* kVramFreeKey = "vramFreeBytes";

        Optional<uint64_t> ReadPhysicalMemoryBytes() {
            uint64_t bytes = 0;
            size_t size = sizeof(bytes);
            if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) != 0 || size != sizeof(bytes) || bytes == 0) {
                return std::nullopt;
            }
            return bytes;
        }

        /// Copies the PerformanceStatistics and the model of one accelerator; false when it has no statistics dictionary
        bool ReadAccelerator(io_registry_entry_t service, MacGpuStats& stats) {
            // Create: +1, owned from here
            const CfOwned<CFTypeRef> statistics(IORegistryEntryCreateCFProperty(service, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0));
            if (!statistics || CFGetTypeID(statistics.Get()) != CFDictionaryGetTypeID()) {
                return false;
            }
            const CFDictionaryRef dictionary = static_cast<CFDictionaryRef>(statistics.Get());
            stats.utilizationPercent = CfNumberAsDouble(dictionary, kUtilizationKey);
            stats.inUseSystemBytes = CfNumberAsUInt64(dictionary, kInUseSystemMemoryKey);
            stats.vramUsedBytes = CfNumberAsUInt64(dictionary, kVramUsedKey);
            stats.vramFreeBytes = CfNumberAsUInt64(dictionary, kVramFreeKey);

            // Create: +1. A CFString on Apple Silicon, a CFData on other Macs, which CfStringToBuffer takes alike; the model is optional
            const CfOwned<CFTypeRef> model(IORegistryEntryCreateCFProperty(service, CFSTR("model"), kCFAllocatorDefault, 0));
            (void)CfStringToBuffer(model.Get(), Span<char>(stats.model));
            return true;
        }

        class MacGpuReader final : public IHardwareReader {
        public:
            explicit MacGpuReader(MacSensorsPtr sensors) : _sensors(std::move(sensors)), _physicalMemoryBytes(ReadPhysicalMemoryBytes()) {}

            bool Read(HardwareReadings& out) override {
                // IOServiceMatching returns +1 and IOServiceGetMatchingServices consumes it whether it succeeds or not
                CFMutableDictionaryRef matching = IOServiceMatching("IOAccelerator");
                if (matching == nullptr) {
                    return false;
                }
                io_iterator_t rawIterator = IO_OBJECT_NULL;
                if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &rawIterator) != KERN_SUCCESS) {
                    return false;
                }
                const IoObject iterator(rawIterator);

                MacGpuStats best;
                bool found = false;
                while (true) {
                    const IoObject service(IOIteratorNext(iterator.Get()));
                    if (!service) {
                        break;
                    }
                    MacGpuStats candidate;
                    if (!ReadAccelerator(service.Get(), candidate)) {
                        continue;
                    }
                    // The busiest accelerator is the one the work runs on; the first wins a tie
                    if (!found || candidate.utilizationPercent.value_or(-1.0) > best.utilizationPercent.value_or(-1.0)) {
                        best = candidate;
                        found = true;
                    }
                }
                if (!found) {
                    return false;
                }
                best.physicalMemoryBytes = _physicalMemoryBytes;
                const bool mapped = MapMacGpu(best, out);
                out.temperatureC = _sensors->ReadGpuC();
                return mapped || out.temperatureC.has_value();
            }

        private:
            MacSensorsPtr _sensors;
            Optional<uint64_t> _physicalMemoryBytes;
        };

    }  // namespace

    UniquePtr<IHardwareReader> MakeMacGpuReader(MacSensorsPtr sensors) {
        return MakeUnique<MacGpuReader>(std::move(sensors));
    }

}  // namespace BECore

#endif  // PLATFORM_MACOS
