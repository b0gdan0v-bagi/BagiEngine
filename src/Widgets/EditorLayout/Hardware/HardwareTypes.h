#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace BECore {

    /// Which device the readings of the status bar belong to; None means there is nothing to show
    CORE_ENUM(HardwareKind, uint8_t, None, Gpu, Cpu)

    /// Bytes of HardwareReadings::name, the terminating NUL included
    inline constexpr size_t kHardwareNameSize = 64;
    /// A temperature outside this range (°C) is a failed read, not a value
    inline constexpr int32_t kMinHardwareTemperatureC = -20;
    inline constexpr int32_t kMaxHardwareTemperatureC = 150;
    inline constexpr uint32_t kMaxHardwareLoadPercent = 100;

    /**
     * @brief What one poll of the hardware found: plain data the worker thread publishes and the frame copies.
     *
     * Integers only (°C, percent, MiB), so "changed" is exact equality. A value that could not be read is empty. The name is
     * zero-filled after its terminator (SetHardwareName), so equal names compare equal bytewise.
     */
    struct HardwareReadings {
        HardwareKind kind = HardwareKind::None;
        Optional<int32_t> temperatureC;
        /// 0 to 100
        Optional<uint32_t> loadPercent;
        Optional<uint32_t> memUsedMb;
        Optional<uint32_t> memTotalMb;
        /// The memory is the system's too (Apple Silicon), not a card's own
        bool unifiedMemory = false;
        /// NUL-terminated device name, empty when unknown
        char name[kHardwareNameSize] = {};

        bool operator==(const HardwareReadings&) const = default;
    };

    static_assert(std::is_trivially_copyable_v<HardwareReadings>);

    /// Copies @p name into the readings, cut to fit and terminated, the rest of the buffer zeroed
    inline void SetHardwareName(HardwareReadings& readings, StringView name) {
        std::memset(readings.name, 0, sizeof(readings.name));
        std::memcpy(readings.name, name.data(), std::min(name.size(), sizeof(readings.name) - 1));
    }

    /// True when at least one value (temperature, load, memory) is present
    [[nodiscard]] inline bool HasHardwareValue(const HardwareReadings& readings) {
        return readings.temperatureC || readings.loadPercent || readings.memUsedMb || readings.memTotalMb;
    }

    /// Keeps the load within 0..100, drops a temperature outside kMinHardwareTemperatureC..kMaxHardwareTemperatureC and terminates the name, zeroing what follows it
    inline void NormalizeReadings(HardwareReadings& readings) {
        if (readings.loadPercent) {
            readings.loadPercent = std::min(*readings.loadPercent, kMaxHardwareLoadPercent);
        }
        if (readings.temperatureC && (*readings.temperatureC < kMinHardwareTemperatureC || *readings.temperatureC > kMaxHardwareTemperatureC)) {
            readings.temperatureC.reset();
        }
        readings.name[kHardwareNameSize - 1] = '\0';
        const size_t length = std::strlen(readings.name);
        std::memset(readings.name + length, 0, sizeof(readings.name) - length);
    }

    /**
     * @brief A source of readings: one device, one platform.
     *
     * Read() is called by one thread only (the worker of HardwareMonitor), never concurrently, and may take milliseconds (a driver call).
     * A steady call allocates nothing. It fills everything but `kind` (the sampler sets it) and returns false, or a result with no value,
     * when there is nothing to show.
     */
    class IHardwareReader {
    public:
        virtual ~IHardwareReader() = default;

        virtual bool Read(HardwareReadings& out) = 0;
    };

}  // namespace BECore
