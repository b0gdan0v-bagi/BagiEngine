#include <Common/Printers.h>
#include <Widgets/EditorLayout/Hardware/HardwareText.h>

namespace BECore::Tests {

    namespace {

        constexpr uint32_t kMb = 1024;

        HardwareReadings Gpu() {
            HardwareReadings readings;
            readings.kind = HardwareKind::Gpu;
            return readings;
        }

        HardwareReadings Cpu() {
            HardwareReadings readings;
            readings.kind = HardwareKind::Cpu;
            return readings;
        }

        /// The GPU of the entry: 64 °C, 87 %, 9318 of 12288 MiB
        HardwareReadings FullGpu() {
            HardwareReadings readings = Gpu();
            readings.temperatureC = 64;
            readings.loadPercent = 87;
            readings.memUsedMb = 9318;
            readings.memTotalMb = 12 * kMb;
            return readings;
        }

        HardwareText Format(const HardwareReadings& readings) {
            HardwareText text;
            FormatHardware(readings, text);
            return text;
        }

        String Line(const HardwareReadings& readings) {
            const HardwareText text = Format(readings);
            char buffer[128];
            FormatHardwareLine(text, buffer);
            return buffer;
        }

        bool Contains(const char* haystack, const char* needle) {
            return StringView(haystack).find(needle) != StringView::npos;
        }

        String MemoryOf(uint32_t usedMb, Optional<uint32_t> totalMb) {
            HardwareReadings readings = Gpu();
            readings.memUsedMb = usedMb;
            readings.memTotalMb = totalMb;
            const HardwareText text = Format(readings);
            return text.segmentCount == 1 ? String(text.segments[0].text) : String("<no segment>");
        }

    }  // namespace

    TEST(HardwareTextTest, TheGpuLineOfTheEntry) {
        EXPECT_EQ(Line(FullGpu()), "GPU 64\xC2\xB0"
                                   "C 87% 9.1/12 GB");
    }

    TEST(HardwareTextTest, TheCpuLineOfTheEntry) {
        HardwareReadings readings = Cpu();
        readings.loadPercent = 45;
        readings.temperatureC = 58;
        EXPECT_EQ(Line(readings), "CPU 45% 58\xC2\xB0"
                                  "C");
    }

    TEST(HardwareTextTest, CpuMemoryIsNotShown) {
        HardwareReadings readings = Cpu();
        readings.loadPercent = 45;
        readings.memUsedMb = 4 * kMb;
        readings.memTotalMb = 16 * kMb;
        EXPECT_EQ(Line(readings), "CPU 45%");
    }

    TEST(HardwareTextTest, AMissingValueLeavesItsSegmentOut) {
        HardwareReadings noTemperature = FullGpu();
        noTemperature.temperatureC.reset();
        EXPECT_EQ(Line(noTemperature), "GPU 87% 9.1/12 GB");

        HardwareReadings noLoad = FullGpu();
        noLoad.loadPercent.reset();
        EXPECT_EQ(Line(noLoad), "GPU 64\xC2\xB0"
                                "C 9.1/12 GB");

        HardwareReadings noMemory = FullGpu();
        noMemory.memUsedMb.reset();
        EXPECT_EQ(Line(noMemory), "GPU 64\xC2\xB0"
                                  "C 87%");

        HardwareReadings noCpuTemperature = Cpu();
        noCpuTemperature.loadPercent = 3;
        EXPECT_EQ(Line(noCpuTemperature), "CPU 3%");
    }

    TEST(HardwareTextTest, AGpuWithOnlyATemperature) {
        HardwareReadings readings = Gpu();
        readings.temperatureC = 71;
        const HardwareText text = Format(readings);
        EXPECT_TRUE(text.visible);
        EXPECT_EQ(text.segmentCount, 1u);
        EXPECT_EQ(String(text.segments[0].text), "71\xC2\xB0"
                                                 "C");
        EXPECT_TRUE(Contains(text.tooltip, "load not available"));
        EXPECT_TRUE(Contains(text.tooltip, "memory not available"));
    }

    TEST(HardwareTextTest, NoneAndEmptyReadingsAreNotVisible) {
        EXPECT_FALSE(Format(HardwareReadings{}).visible);
        HardwareReadings noneWithValues;
        noneWithValues.loadPercent = 50;
        EXPECT_FALSE(Format(noneWithValues).visible);
        EXPECT_FALSE(Format(Gpu()).visible);
        EXPECT_EQ(Line(Gpu()), "");
    }

    TEST(HardwareTextTest, TemperatureBoundaries) {
        EXPECT_EQ(ClassifyTemperature(HardwareKind::Gpu, 79), HardwareLevel::Normal);
        EXPECT_EQ(ClassifyTemperature(HardwareKind::Gpu, 80), HardwareLevel::Warning);
        EXPECT_EQ(ClassifyTemperature(HardwareKind::Gpu, 89), HardwareLevel::Warning);
        EXPECT_EQ(ClassifyTemperature(HardwareKind::Gpu, 90), HardwareLevel::Error);
        EXPECT_EQ(ClassifyTemperature(HardwareKind::Cpu, 84), HardwareLevel::Normal);
        EXPECT_EQ(ClassifyTemperature(HardwareKind::Cpu, 85), HardwareLevel::Warning);
        EXPECT_EQ(ClassifyTemperature(HardwareKind::Cpu, 94), HardwareLevel::Warning);
        EXPECT_EQ(ClassifyTemperature(HardwareKind::Cpu, 95), HardwareLevel::Error);
        EXPECT_EQ(ClassifyTemperature(HardwareKind::None, 120), HardwareLevel::Normal);
    }

    TEST(HardwareTextTest, MemoryBoundaries) {
        // 90 % and 97 % of 1000 MiB
        EXPECT_EQ(ClassifyMemory(899, 1000), HardwareLevel::Normal);
        EXPECT_EQ(ClassifyMemory(900, 1000), HardwareLevel::Warning);
        EXPECT_EQ(ClassifyMemory(969, 1000), HardwareLevel::Warning);
        EXPECT_EQ(ClassifyMemory(970, 1000), HardwareLevel::Error);
        EXPECT_EQ(ClassifyMemory(2000, 1000), HardwareLevel::Error);
        EXPECT_EQ(ClassifyMemory(0, 1000), HardwareLevel::Normal);
    }

    TEST(HardwareTextTest, MemoryWithAZeroTotalDoesNotDivide) {
        EXPECT_EQ(ClassifyMemory(500, 0), HardwareLevel::Normal);
        EXPECT_EQ(ClassifyMemory(0, 0), HardwareLevel::Normal);
        EXPECT_EQ(MemoryOf(2 * kMb, 0u), "2.0 GB");
    }

    TEST(HardwareTextTest, MemoryDoesNotOverflowWithHugeValues) {
        constexpr uint32_t kMax = std::numeric_limits<uint32_t>::max();
        EXPECT_EQ(ClassifyMemory(kMax, kMax), HardwareLevel::Error);
        EXPECT_EQ(ClassifyMemory(kMax / 2, kMax), HardwareLevel::Normal);
    }

    TEST(HardwareTextTest, LevelsReachTheSegments) {
        HardwareReadings readings = FullGpu();
        readings.temperatureC = 85;
        readings.memUsedMb = 12000;
        const HardwareText text = Format(readings);
        ASSERT_EQ(text.segmentCount, 3u);
        EXPECT_EQ(text.segments[0].level, HardwareLevel::Warning);
        EXPECT_EQ(text.segments[1].level, HardwareLevel::Normal);
        EXPECT_EQ(text.segments[2].level, HardwareLevel::Error);

        HardwareReadings cpu = Cpu();
        cpu.loadPercent = 100;
        cpu.temperatureC = 96;
        const HardwareText cpuText = Format(cpu);
        ASSERT_EQ(cpuText.segmentCount, 2u);
        EXPECT_EQ(cpuText.segments[0].level, HardwareLevel::Normal);
        EXPECT_EQ(cpuText.segments[1].level, HardwareLevel::Error);
    }

    TEST(HardwareTextTest, MemoryFormats) {
        EXPECT_EQ(MemoryOf(9318, 12 * kMb), "9.1/12 GB");
        EXPECT_EQ(MemoryOf(0, 12 * kMb), "0.0/12 GB");
        // A total within 0.05 GB of a whole number is whole, otherwise one decimal
        EXPECT_EQ(MemoryOf(1000, 8 * kMb + 40), "1.0/8 GB");
        EXPECT_EQ(MemoryOf(1000, 11008), "1.0/10.8 GB");
        // Below 1 GiB of total the unit is MB and the numbers are whole
        EXPECT_EQ(MemoryOf(512, 896), "512/896 MB");
        EXPECT_EQ(MemoryOf(0, 896), "0/896 MB");
        // The total decides the unit; the used amount alone does when there is no total
        EXPECT_EQ(MemoryOf(300, 12 * kMb), "0.3/12 GB");
        EXPECT_EQ(MemoryOf(9318, std::nullopt), "9.1 GB");
        EXPECT_EQ(MemoryOf(512, std::nullopt), "512 MB");
        // Used above the total is shown as it is
        EXPECT_EQ(MemoryOf(13 * kMb, 12 * kMb), "13.0/12 GB");
    }

    TEST(HardwareTextTest, AMissingUsedAmountHasNoMemorySegment) {
        HardwareReadings readings = Gpu();
        readings.memTotalMb = 12 * kMb;
        EXPECT_FALSE(Format(readings).visible);
        EXPECT_EQ(MemoryOf(0, std::nullopt), "0 MB");
    }

    TEST(HardwareTextTest, StrayValuesAreClamped) {
        HardwareReadings readings = FullGpu();
        readings.loadPercent = 130;
        readings.temperatureC = -300;
        EXPECT_EQ(Line(readings), "GPU 100% 9.1/12 GB");

        HardwareReadings hot = FullGpu();
        hot.temperatureC = 151;
        EXPECT_EQ(Line(hot), "GPU 87% 9.1/12 GB");

        HardwareReadings edges = FullGpu();
        edges.temperatureC = kMaxHardwareTemperatureC;
        EXPECT_EQ(Line(edges), "GPU 150\xC2\xB0"
                               "C 87% 9.1/12 GB");
        edges.temperatureC = kMinHardwareTemperatureC;
        EXPECT_EQ(Line(edges), "GPU -20\xC2\xB0"
                               "C 87% 9.1/12 GB");
    }

    TEST(HardwareTextTest, NormalizeKeepsEqualReadingsEqual) {
        HardwareReadings a = FullGpu();
        HardwareReadings b = FullGpu();
        a.loadPercent = 130;
        b.loadPercent = 255;
        NormalizeReadings(a);
        NormalizeReadings(b);
        EXPECT_EQ(a, b);
    }

    TEST(HardwareTextTest, TheLabelAndTheDeviceName) {
        HardwareReadings readings = FullGpu();
        EXPECT_EQ(String(Format(readings).label), "GPU");
        HardwareReadings cpu = Cpu();
        cpu.loadPercent = 1;
        EXPECT_EQ(String(Format(cpu).label), "CPU");

        // Without a name the tooltip starts with the label
        EXPECT_EQ(String(Format(readings).tooltip).substr(0, 3), "GPU");
        SetHardwareName(readings, "NVIDIA GeForce RTX 4070");
        EXPECT_EQ(String(Format(readings).tooltip).substr(0, 23), "NVIDIA GeForce RTX 4070");
    }

    TEST(HardwareTextTest, ANameIsCutAndTerminated) {
        HardwareReadings readings = FullGpu();
        const String exact(kHardwareNameSize - 1, 'a');
        SetHardwareName(readings, exact);
        EXPECT_EQ(String(readings.name), exact);

        const String longName(200, 'b');
        SetHardwareName(readings, longName);
        EXPECT_EQ(String(readings.name).size(), kHardwareNameSize - 1);
        EXPECT_EQ(readings.name[kHardwareNameSize - 1], '\0');

        const HardwareText text = Format(readings);
        EXPECT_TRUE(Contains(text.tooltip, String(kHardwareNameSize - 1, 'b').c_str()));
        EXPECT_LT(std::strlen(text.tooltip), kHardwareTooltipSize);
    }

    TEST(HardwareTextTest, AnUnterminatedNameIsTerminatedByNormalize) {
        HardwareReadings readings = FullGpu();
        std::memset(readings.name, 'x', sizeof(readings.name));
        const HardwareText text = Format(readings);
        EXPECT_LT(std::strlen(text.tooltip), kHardwareTooltipSize);
        EXPECT_TRUE(Contains(text.tooltip, "xxxx"));
    }

    TEST(HardwareTextTest, TheTooltipListsWhatIsShown) {
        HardwareReadings readings = FullGpu();
        SetHardwareName(readings, "Test GPU");
        const String tooltip = Format(readings).tooltip;
        EXPECT_EQ(tooltip, String("Test GPU\ntemperature 64 \xC2\xB0"
                                  "C\nload 87 %\nmemory 9.1/12 GB"));
    }

    TEST(HardwareTextTest, TheTooltipSaysWhatIsMissing) {
        HardwareReadings readings = Cpu();
        readings.loadPercent = 45;
        const String tooltip = Format(readings).tooltip;
        EXPECT_EQ(tooltip, "CPU\nload 45 %\ntemperature not available");
    }

    TEST(HardwareTextTest, TheTooltipOfUnifiedMemoryEndsWithTheNote) {
        HardwareReadings readings = FullGpu();
        readings.unifiedMemory = true;
        const String tooltip = Format(readings).tooltip;
        const String note = "unified memory, shared with the system";
        ASSERT_GE(tooltip.size(), note.size());
        EXPECT_EQ(tooltip.substr(tooltip.size() - note.size()), note);

        readings.unifiedMemory = false;
        EXPECT_FALSE(Contains(Format(readings).tooltip, "unified"));
    }

    TEST(HardwareTextTest, NoFormatterOverflowsItsBuffer) {
        HardwareReadings readings = FullGpu();
        readings.unifiedMemory = true;
        SetHardwareName(readings, String(200, 'n'));
        readings.memUsedMb = std::numeric_limits<uint32_t>::max();
        readings.memTotalMb = std::numeric_limits<uint32_t>::max();
        const HardwareText text = Format(readings);
        for (size_t i = 0; i < text.segmentCount; ++i) {
            EXPECT_LT(std::strlen(text.segments[i].text), kHardwareSegmentSize);
        }
        EXPECT_LT(std::strlen(text.tooltip), kHardwareTooltipSize);

        char tiny[6];
        FormatHardwareLine(text, tiny);
        EXPECT_EQ(std::strlen(tiny), 5u);
        EXPECT_EQ(String(tiny), "GPU 6");

        char one[1] = {'x'};
        FormatHardwareLine(text, one);
        EXPECT_EQ(one[0], '\0');
        FormatHardwareLine(text, Span<char>());
    }

    TEST(HardwareTextTest, ReadingsCompareFieldByField) {
        const HardwareReadings base = FullGpu();
        EXPECT_EQ(base, FullGpu());
        EXPECT_EQ(HardwareReadings{}, HardwareReadings{});

        HardwareReadings other = base;
        other.kind = HardwareKind::Cpu;
        EXPECT_NE(base, other);
        other = base;
        other.temperatureC = 65;
        EXPECT_NE(base, other);
        other = base;
        other.temperatureC.reset();
        EXPECT_NE(base, other);
        other = base;
        other.loadPercent = 88;
        EXPECT_NE(base, other);
        other = base;
        other.memUsedMb = 9319;
        EXPECT_NE(base, other);
        other = base;
        other.memTotalMb = 12289;
        EXPECT_NE(base, other);
        other = base;
        other.unifiedMemory = true;
        EXPECT_NE(base, other);
        other = base;
        SetHardwareName(other, "x");
        EXPECT_NE(base, other);

        HardwareReadings named = base;
        SetHardwareName(named, "same");
        HardwareReadings alsoNamed = base;
        SetHardwareName(alsoNamed, "same");
        EXPECT_EQ(named, alsoNamed);
    }

    TEST(HardwareTextTest, HasValueNeedsOneOfTheValues) {
        EXPECT_FALSE(HasHardwareValue(Gpu()));
        HardwareReadings readings = Gpu();
        readings.memTotalMb = 1;
        EXPECT_TRUE(HasHardwareValue(readings));
        readings = Gpu();
        readings.unifiedMemory = true;
        SetHardwareName(readings, "name only");
        EXPECT_FALSE(HasHardwareValue(readings));
    }

}  // namespace BECore::Tests
