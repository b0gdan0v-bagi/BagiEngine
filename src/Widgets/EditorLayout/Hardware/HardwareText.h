#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <Widgets/EditorLayout/Hardware/HardwareTypes.h>

namespace BECore {

    /// How a reading compares with its thresholds; the colours are the widget's business
    CORE_ENUM(HardwareLevel, uint8_t, Normal, Warning, Error)

    /// Temperature thresholds (°C): at or above the value. The load is never coloured: a saturated GPU is the normal state while it works.
    inline constexpr int32_t kGpuTemperatureWarningC = 80;
    inline constexpr int32_t kGpuTemperatureErrorC = 90;
    inline constexpr int32_t kCpuTemperatureWarningC = 85;
    inline constexpr int32_t kCpuTemperatureErrorC = 95;
    /// Memory thresholds: used share of the total in percent, at or above the value
    inline constexpr uint32_t kMemoryWarningPercent = 90;
    inline constexpr uint32_t kMemoryErrorPercent = 97;

    inline constexpr size_t kHardwareMaxSegments = 3;
    inline constexpr size_t kHardwareSegmentSize = 24;
    inline constexpr size_t kHardwareLabelSize = 8;
    inline constexpr size_t kHardwareTooltipSize = 224;

    /// Normal for a kind without thresholds (None)
    [[nodiscard]] HardwareLevel ClassifyTemperature(HardwareKind kind, int32_t celsius);

    /// Normal when the total is 0 (unknown); a used amount above the total is Error
    [[nodiscard]] HardwareLevel ClassifyMemory(uint32_t usedMb, uint32_t totalMb);

    struct HardwareSegment {
        char text[kHardwareSegmentSize] = {};
        HardwareLevel level = HardwareLevel::Normal;
    };

    /// The readings as the status bar shows them, in fixed buffers (made again only when the readings change, so no heap)
    struct HardwareText {
        /// "GPU" or "CPU"
        char label[kHardwareLabelSize] = {};
        Array<HardwareSegment, kHardwareMaxSegments> segments{};
        size_t segmentCount = 0;
        /// Several lines: the device, what is shown with its unit, what is missing
        char tooltip[kHardwareTooltipSize] = {};
        /// False when the kind is None or no segment could be made
        bool visible = false;
    };

    /**
     * @brief Makes the segments, the label and the tooltip of @p readings; every formatter cuts at its buffer and allocates nothing.
     *
     * Segment order: GPU temperature, load, memory; CPU load, temperature (a CPU's memory is not shown). A missing value leaves its
     * segment out. The readings are normalized first (NormalizeReadings), so a stray value cannot reach the screen.
     */
    void FormatHardware(const HardwareReadings& readings, HardwareText& out);

    /// "GPU 64°C 87% 9.1/12 GB": the label and the segments joined by single spaces, for tests and logs; empty when not visible
    void FormatHardwareLine(const HardwareText& text, Span<char> buffer);

}  // namespace BECore
