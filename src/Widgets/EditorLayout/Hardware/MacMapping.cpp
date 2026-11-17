#include "MacMapping.h"

#include <bit>
#include <cmath>

namespace BECore {

    namespace {

        constexpr uint64_t kBytesPerMib = 1024ULL * 1024ULL;
        /// `sp78` has 8 fraction bits and `fpe2` has 2
        constexpr double kSmcSp78Scale = 256.0;
        constexpr double kSmcFpe2Scale = 4.0;

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

        bool StartsWith(StringView text, StringView prefix) {
            return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
        }

        bool IsSensorOfKind(StringView name, HardwareKind kind) {
            if (kind == HardwareKind::Gpu) {
                return StartsWith(name, kAppleSiliconGpuSensorPrefix);
            }
            if (kind == HardwareKind::Cpu) {
                return std::any_of(std::begin(kAppleSiliconCpuSensorPrefixes), std::end(kAppleSiliconCpuSensorPrefixes), [name](StringView prefix) { return StartsWith(name, prefix); });
            }
            return false;
        }

        /// Whole °C from a value of the hardware; nothing for a NaN or a result outside @p minimum..kMaxMacSensorCelsius
        Optional<int32_t> ToCelsius(double value, int32_t minimum) {
            const double rounded = std::round(value);
            if (!(rounded >= minimum && rounded <= kMaxMacSensorCelsius)) {
                return std::nullopt;
            }
            return static_cast<int32_t>(rounded);
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

    Optional<int32_t> PickAppleSiliconTemperature(Span<const MacSensorSample> samples, HardwareKind kind) {
        double sum = 0.0;
        double hottest = 0.0;
        size_t count = 0;
        for (const MacSensorSample& sample : samples) {
            if (!IsSensorOfKind(BufferText(sample.name, sizeof(sample.name)), kind) || !(sample.celsius >= kMinMacSensorCelsius && sample.celsius <= kMaxMacSensorCelsius)) {
                continue;
            }
            sum += sample.celsius;
            hottest = count == 0 ? sample.celsius : std::max(hottest, sample.celsius);
            ++count;
        }
        if (count == 0) {
            return std::nullopt;
        }
        return ToCelsius(kind == HardwareKind::Gpu ? sum / static_cast<double>(count) : hottest, kMinMacSensorCelsius);
    }

    Optional<int32_t> DecodeSmcTemperature(const char type[kSmcTypeSize], Span<const uint8_t> bytes) {
        const StringView typeName(type, kSmcTypeSize);
        if (typeName == "sp78" || typeName == "fpe2") {
            if (bytes.size() < 2) {
                return std::nullopt;
            }
            const auto raw = static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
            return typeName == "sp78" ? ToCelsius(static_cast<int16_t>(raw) / kSmcSp78Scale, kMinMacSmcCelsius) : ToCelsius(raw / kSmcFpe2Scale, kMinMacSmcCelsius);
        }
        if (typeName == "flt ") {
            if (bytes.size() < sizeof(float)) {
                return std::nullopt;
            }
            const uint32_t raw = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) | (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
            return ToCelsius(static_cast<double>(std::bit_cast<float>(raw)), kMinMacSmcCelsius);
        }
        return std::nullopt;
    }

}  // namespace BECore
