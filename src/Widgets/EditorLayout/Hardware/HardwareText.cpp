#include "HardwareText.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace BECore {

    namespace {

        constexpr uint32_t kMbPerGb = 1024;
        constexpr uint32_t kPercent = 100;
        /// A total within this many GB of a whole number is shown as that number
        constexpr double kWholeGbTolerance = 0.05;

        constexpr const char* kGpuLabel = "GPU";
        constexpr const char* kCpuLabel = "CPU";
        constexpr const char* kTemperatureName = "temperature";
        constexpr const char* kLoadName = "load";
        constexpr const char* kMemoryName = "memory";
        constexpr const char* kUnifiedMemoryNote = "unified memory, shared with the system";

        // The degree sign is UTF-8 (U+00B0, inside the Latin-1 range of the engine's fonts). In a format it is a literal of its own:
        // "\xB0C" would read the C as one more hex digit.

        /// Appends printf-style text to a fixed buffer and cuts at its end; the buffer is always terminated
        class BufferWriter {
        public:
            explicit BufferWriter(Span<char> buffer) : _buffer(buffer) {
                if (!_buffer.empty()) {
                    _buffer[0] = '\0';
                }
            }

            void Append(const char* format, ...) {
                if (_length + 1 >= _buffer.size()) {
                    return;
                }
                va_list args;
                va_start(args, format);
                const int written = std::vsnprintf(_buffer.data() + _length, _buffer.size() - _length, format, args);
                va_end(args);
                if (written > 0) {
                    _length = std::min(_length + static_cast<size_t>(written), _buffer.size() - 1);
                }
            }

        private:
            Span<char> _buffer;
            size_t _length = 0;
        };

        const char* LabelOf(HardwareKind kind) {
            return kind == HardwareKind::Gpu ? kGpuLabel : kCpuLabel;
        }

        /// A zero total is an unknown one
        bool HasTotal(const HardwareReadings& readings) {
            return readings.memTotalMb && *readings.memTotalMb > 0;
        }

        /// The segments and the tooltip of one HardwareText, made together so they cannot disagree about what is shown
        class TextBuilder {
        public:
            TextBuilder(const HardwareReadings& readings, HardwareText& out) : _readings(readings), _out(out), _tooltip(Span<char>(out.tooltip)) {
                _tooltip.Append("%s", readings.name[0] != '\0' ? readings.name : LabelOf(readings.kind));
            }

            void AddTemperature() {
                if (!_readings.temperatureC) {
                    NotAvailable(kTemperatureName);
                    return;
                }
                const int32_t celsius = *_readings.temperatureC;
                AddSegment(ClassifyTemperature(_readings.kind, celsius),
                           "%d\xC2\xB0"
                           "C",
                           celsius);
                _tooltip.Append("\n%s %d \xC2\xB0"
                                "C",
                                kTemperatureName, celsius);
            }

            void AddLoad() {
                if (!_readings.loadPercent) {
                    NotAvailable(kLoadName);
                    return;
                }
                AddSegment(HardwareLevel::Normal, "%u%%", *_readings.loadPercent);
                _tooltip.Append("\n%s %u %%", kLoadName, *_readings.loadPercent);
            }

            void AddMemory() {
                if (!_readings.memUsedMb) {
                    NotAvailable(kMemoryName);
                    return;
                }
                const uint32_t used = *_readings.memUsedMb;
                const bool hasTotal = HasTotal(_readings);
                const uint32_t total = hasTotal ? *_readings.memTotalMb : 0;
                const HardwareLevel level = ClassifyMemory(used, total);
                // The unit follows the total, so a pair never mixes MB and GB; without a total it follows the used amount
                if ((hasTotal ? total : used) < kMbPerGb) {
                    if (hasTotal) {
                        AddSegment(level, "%u/%u MB", used, total);
                    } else {
                        AddSegment(level, "%u MB", used);
                    }
                } else {
                    const double usedGb = static_cast<double>(used) / kMbPerGb;
                    const double totalGb = static_cast<double>(total) / kMbPerGb;
                    const double wholeGb = std::round(totalGb);
                    if (!hasTotal) {
                        AddSegment(level, "%.1f GB", usedGb);
                    } else if (std::abs(totalGb - wholeGb) < kWholeGbTolerance) {
                        AddSegment(level, "%.1f/%.0f GB", usedGb, wholeGb);
                    } else {
                        AddSegment(level, "%.1f/%.1f GB", usedGb, totalGb);
                    }
                }
                _tooltip.Append("\n%s %s", kMemoryName, _out.segments[_out.segmentCount - 1].text);
            }

            void Finish() {
                if (_readings.unifiedMemory && _readings.memUsedMb) {
                    _tooltip.Append("\n%s", kUnifiedMemoryNote);
                }
            }

        private:
            void NotAvailable(const char* what) {
                _tooltip.Append("\n%s not available", what);
            }

            // At most one segment per metric and three metrics, so the array cannot overflow
            void AddSegment(HardwareLevel level, const char* format, ...) {
                HardwareSegment& segment = _out.segments[_out.segmentCount++];
                segment.level = level;
                va_list args;
                va_start(args, format);
                std::vsnprintf(segment.text, sizeof(segment.text), format, args);
                va_end(args);
            }

            const HardwareReadings& _readings;
            HardwareText& _out;
            BufferWriter _tooltip;
        };

    }  // namespace

    HardwareLevel ClassifyTemperature(HardwareKind kind, int32_t celsius) {
        int32_t warning = 0;
        int32_t error = 0;
        switch (kind) {
            case HardwareKind::Gpu:
                warning = kGpuTemperatureWarningC;
                error = kGpuTemperatureErrorC;
                break;
            case HardwareKind::Cpu:
                warning = kCpuTemperatureWarningC;
                error = kCpuTemperatureErrorC;
                break;
            case HardwareKind::None:
                return HardwareLevel::Normal;
        }
        if (celsius >= error) {
            return HardwareLevel::Error;
        }
        return celsius >= warning ? HardwareLevel::Warning : HardwareLevel::Normal;
    }

    HardwareLevel ClassifyMemory(uint32_t usedMb, uint32_t totalMb) {
        if (totalMb == 0) {
            return HardwareLevel::Normal;
        }
        const uint64_t scaledUsed = static_cast<uint64_t>(usedMb) * kPercent;
        if (scaledUsed >= static_cast<uint64_t>(totalMb) * kMemoryErrorPercent) {
            return HardwareLevel::Error;
        }
        return scaledUsed >= static_cast<uint64_t>(totalMb) * kMemoryWarningPercent ? HardwareLevel::Warning : HardwareLevel::Normal;
    }

    void FormatHardware(const HardwareReadings& readings, HardwareText& out) {
        out = HardwareText{};
        if (readings.kind == HardwareKind::None) {
            return;
        }
        HardwareReadings clean = readings;
        NormalizeReadings(clean);

        std::snprintf(out.label, sizeof(out.label), "%s", LabelOf(clean.kind));
        TextBuilder builder(clean, out);
        if (clean.kind == HardwareKind::Gpu) {
            builder.AddTemperature();
            builder.AddLoad();
            builder.AddMemory();
        } else {
            builder.AddLoad();
            builder.AddTemperature();
        }
        builder.Finish();
        out.visible = out.segmentCount > 0;
        if (!out.visible) {
            out = HardwareText{};
        }
    }

    void FormatHardwareLine(const HardwareText& text, Span<char> buffer) {
        if (buffer.empty()) {
            return;
        }
        buffer[0] = '\0';
        if (!text.visible) {
            return;
        }
        BufferWriter writer(buffer);
        writer.Append("%s", text.label);
        for (size_t i = 0; i < text.segmentCount; ++i) {
            writer.Append(" %s", text.segments[i].text);
        }
    }

}  // namespace BECore
