#include "MacMapping.h"

#include <cmath>

namespace BECore {

    namespace {

        constexpr uint64_t kBytesPerMib = 1024ULL * 1024ULL;

        /// Rounded to a whole MiB without the overflow of adding half a MiB to a huge value
        uint32_t BytesToMib(uint64_t bytes) {
            const uint64_t mib = bytes / kBytesPerMib + (bytes % kBytesPerMib >= kBytesPerMib / 2 ? 1 : 0);
            return static_cast<uint32_t>(std::min<uint64_t>(mib, std::numeric_limits<uint32_t>::max()));
        }

        Optional<uint32_t> ToLoadPercent(double percent) {
            if (std::isnan(percent)) {
                return std::nullopt;
            }
            return static_cast<uint32_t>(std::clamp(std::round(percent), 0.0, static_cast<double>(kMaxHardwareLoadPercent)));
        }

        uint64_t SaturatingAdd(uint64_t first, uint64_t second) {
            return first > std::numeric_limits<uint64_t>::max() - second ? std::numeric_limits<uint64_t>::max() : first + second;
        }

        /// The text of a NUL-terminated buffer that may lack its terminator
        StringView BufferText(const char* buffer, size_t size) {
            return StringView(buffer, static_cast<size_t>(std::find(buffer, buffer + size, '\0') - buffer));
        }

    }  // namespace

    bool MapMacGpu(const MacGpuStats& stats, HardwareReadings& out) {
        out.loadPercent.reset();
        if (stats.utilizationPercent) {
            out.loadPercent = ToLoadPercent(*stats.utilizationPercent);
        }

        out.memUsedMb.reset();
        out.memTotalMb.reset();
        out.unifiedMemory = false;
        if (stats.vramUsedBytes && stats.vramFreeBytes) {
            out.memUsedMb = BytesToMib(*stats.vramUsedBytes);
            out.memTotalMb = BytesToMib(SaturatingAdd(*stats.vramUsedBytes, *stats.vramFreeBytes));
        } else if (stats.inUseSystemBytes) {
            out.unifiedMemory = true;
            out.memUsedMb = BytesToMib(*stats.inUseSystemBytes);
            if (stats.physicalMemoryBytes && *stats.physicalMemoryBytes > 0) {
                out.memTotalMb = BytesToMib(*stats.physicalMemoryBytes);
            }
        }

        SetHardwareName(out, BufferText(stats.model, sizeof(stats.model)));
        return out.loadPercent || out.memUsedMb || out.memTotalMb;
    }

}  // namespace BECore
