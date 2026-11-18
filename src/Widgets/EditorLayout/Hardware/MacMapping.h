#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/EditorLayout/Hardware/HardwareTypes.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// Bytes of MacGpuStats::model, the terminating NUL included
    inline constexpr size_t kMacGpuModelSize = 64;

    /**
     * @brief What the IORegistry said about one accelerator, copied as it is: an absent key is an empty value.
     *
     * MacGpuReader.cpp fills it from the C APIs and MapMacGpu decides what it means, so every decision of the macOS GPU readout is
     * a pure function that is tested on every platform.
     */
    struct MacGpuStats {
        /// `Device Utilization %` of the PerformanceStatistics dictionary
        Optional<double> utilizationPercent;
        /// `In use system memory`: the system memory the accelerator uses (the only memory of an integrated GPU)
        Optional<uint64_t> inUseSystemBytes;
        /// `vramUsedBytes` and `vramFreeBytes`: the own memory of a discrete GPU
        Optional<uint64_t> vramUsedBytes;
        Optional<uint64_t> vramFreeBytes;
        /// `hw.memsize`: the total of a unified memory
        Optional<uint64_t> physicalMemoryBytes;
        /// The accelerator's `model`, NUL-terminated, empty when unknown
        char model[kMacGpuModelSize] = {};
    };

    /// Bytes of MacSensorSample::name, the terminating NUL included
    inline constexpr size_t kMacSensorNameSize = 48;
    /// A sensor reading outside this range (°C) is a failed read. An SMC key that is not there reads 0, so for the SMC the range starts at 1.
    inline constexpr int32_t kMinMacSensorCelsius = 0;
    inline constexpr int32_t kMinMacSmcCelsius = 1;
    inline constexpr int32_t kMaxMacSensorCelsius = 150;

    /// Name prefixes of the IOHID temperature sensors of Apple Silicon (the `Product` property of a service)
    inline constexpr StringView kAppleSiliconGpuSensorPrefix = "GPU MTR Temp Sensor";
    inline constexpr StringView kAppleSiliconCpuSensorPrefixes[] = {"pACC MTR Temp Sensor", "eACC MTR Temp Sensor", "PMU tdie"};

    /// The SMC keys of the Intel Macs in the order tried: the first that decodes wins
    inline constexpr StringView kSmcCpuTemperatureKeys[] = {"TC0P", "TC0D", "TC0E", "TCXC"};
    inline constexpr StringView kSmcGpuTemperatureKeys[] = {"TG0P", "TG0D", "TGXC"};

    /// Bytes of the type of an SMC value (`sp78`, `flt `, `fpe2`)
    inline constexpr size_t kSmcTypeSize = 4;

    /// One temperature sensor as the IOHID services gave it: the product name, cut to fit and terminated, and the value in °C
    struct MacSensorSample {
        char name[kMacSensorNameSize] = {};
        double celsius = 0.0;
    };

    /**
     * @brief The temperature (whole °C) of @p kind from the Apple Silicon sensors, nothing for any other kind.
     *
     * The GPU is the mean of the `GPU MTR Temp Sensor*` samples, the CPU the highest of the `pACC` / `eACC MTR Temp Sensor*` and
     * `PMU tdie*` ones (the hottest core is what throttles). A value outside 0..150 or a NaN is skipped; no sample left gives no value.
     */
    [[nodiscard]] Optional<int32_t> PickAppleSiliconTemperature(Span<const MacSensorSample> samples, HardwareKind kind);

    /**
     * @brief Decodes an SMC value of @p type (4 characters) from its raw @p bytes into whole °C.
     *
     * `sp78` is a signed 8.8 fixed point and `fpe2` an unsigned 14.2 one, both big endian; `flt ` is the little-endian float of the
     * Intel host. Another type, too few bytes or a result outside 1..150 gives no value.
     */
    [[nodiscard]] Optional<int32_t> DecodeSmcTemperature(const char type[kSmcTypeSize], Span<const uint8_t> bytes);

    /**
     * @brief Turns the statistics into load, memory and name of @p out (the temperature and `kind` are not touched).
     *
     * The load is rounded and clamped to 0..100. Memory: a discrete GPU (VRAM used and free both present) shows its own memory;
     * otherwise `In use system memory` is the used part of the unified memory, whose total is the machine's physical memory (without
     * it only the used part is shown) and `unifiedMemory` is set. A missing piece leaves its field empty. False when no load and no
     * memory could be made.
     */
    [[nodiscard]] bool MapMacGpu(const MacGpuStats& stats, HardwareReadings& out);

}  // namespace BECore
