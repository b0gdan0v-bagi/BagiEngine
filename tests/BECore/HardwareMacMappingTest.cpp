#include <Common/Printers.h>
#include <Widgets/EditorLayout/Hardware/MacMapping.h>
#include <cstring>
#include <limits>

namespace BECore::Tests {

    namespace {

        constexpr uint64_t kMiB = 1024ULL * 1024ULL;
        constexpr uint64_t kGiB = 1024ULL * kMiB;

        void SetModel(MacGpuStats& stats, StringView model) {
            std::memcpy(stats.model, model.data(), model.size());
        }

        /// Apple Silicon: no VRAM keys, the accelerator's memory is the system's
        MacGpuStats AppleSilicon() {
            MacGpuStats stats;
            stats.utilizationPercent = 37.0;
            stats.inUseSystemBytes = 9 * kGiB;
            stats.physicalMemoryBytes = 32 * kGiB;
            SetModel(stats, "Apple M2 Pro");
            return stats;
        }

        /// An AMD card with its own memory
        MacGpuStats Discrete() {
            MacGpuStats stats;
            stats.utilizationPercent = 87.0;
            stats.vramUsedBytes = 6 * kGiB;
            stats.vramFreeBytes = 2 * kGiB;
            stats.inUseSystemBytes = 1 * kGiB;
            stats.physicalMemoryBytes = 32 * kGiB;
            SetModel(stats, "AMD Radeon Pro 5500M");
            return stats;
        }

        MacSensorSample Sample(StringView name, double celsius) {
            MacSensorSample sample;
            std::memcpy(sample.name, name.data(), std::min(name.size(), sizeof(sample.name) - 1));
            sample.celsius = celsius;
            return sample;
        }

        Optional<int32_t> Pick(std::initializer_list<MacSensorSample> samples, HardwareKind kind) {
            const Vector<MacSensorSample> list(samples);
            return PickAppleSiliconTemperature(Span<const MacSensorSample>(list.data(), list.size()), kind);
        }

        Optional<int32_t> Decode(const char (&type)[kSmcTypeSize + 1], std::initializer_list<uint8_t> bytes) {
            const Vector<uint8_t> list(bytes);
            return DecodeSmcTemperature(type, Span<const uint8_t>(list.data(), list.size()));
        }

    }  // namespace

    TEST(HardwareMacMappingTest, AppleSiliconIsUnifiedMemoryOfThePhysicalSize) {
        HardwareReadings readings;
        ASSERT_TRUE(MapMacGpu(AppleSilicon(), readings));
        EXPECT_EQ(readings.loadPercent, 37U);
        EXPECT_EQ(readings.memUsedMb, 9U * 1024U);
        EXPECT_EQ(readings.memTotalMb, 32U * 1024U);
        EXPECT_TRUE(readings.unifiedMemory);
        EXPECT_STREQ(readings.name, "Apple M2 Pro");
        EXPECT_FALSE(readings.temperatureC);
    }

    TEST(HardwareMacMappingTest, DiscreteGpuShowsItsOwnMemoryNotUnified) {
        HardwareReadings readings;
        ASSERT_TRUE(MapMacGpu(Discrete(), readings));
        EXPECT_EQ(readings.loadPercent, 87U);
        EXPECT_EQ(readings.memUsedMb, 6U * 1024U);
        EXPECT_EQ(readings.memTotalMb, 8U * 1024U);
        EXPECT_FALSE(readings.unifiedMemory);
        EXPECT_STREQ(readings.name, "AMD Radeon Pro 5500M");
    }

    TEST(HardwareMacMappingTest, IntelIntegratedGpuWithOnlySystemMemoryInUse) {
        MacGpuStats stats;
        stats.inUseSystemBytes = 1536 * kMiB;
        stats.physicalMemoryBytes = 16 * kGiB;
        HardwareReadings readings;
        ASSERT_TRUE(MapMacGpu(stats, readings));
        EXPECT_FALSE(readings.loadPercent);
        EXPECT_EQ(readings.memUsedMb, 1536U);
        EXPECT_EQ(readings.memTotalMb, 16U * 1024U);
        EXPECT_TRUE(readings.unifiedMemory);
        EXPECT_STREQ(readings.name, "");
    }

    TEST(HardwareMacMappingTest, WithoutThePhysicalSizeOnlyTheUsedMemoryIsShown) {
        MacGpuStats stats = AppleSilicon();
        stats.physicalMemoryBytes.reset();
        HardwareReadings readings;
        ASSERT_TRUE(MapMacGpu(stats, readings));
        EXPECT_EQ(readings.memUsedMb, 9U * 1024U);
        EXPECT_FALSE(readings.memTotalMb);
        EXPECT_TRUE(readings.unifiedMemory);

        stats.physicalMemoryBytes = 0;
        ASSERT_TRUE(MapMacGpu(stats, readings));
        EXPECT_FALSE(readings.memTotalMb);
    }

    TEST(HardwareMacMappingTest, NothingPresentGivesFalse) {
        HardwareReadings readings;
        EXPECT_FALSE(MapMacGpu(MacGpuStats{}, readings));
        EXPECT_FALSE(readings.loadPercent);
        EXPECT_FALSE(readings.memUsedMb);
        EXPECT_FALSE(readings.memTotalMb);
        EXPECT_FALSE(readings.unifiedMemory);

        // The model alone and the physical size alone are no reading
        MacGpuStats named;
        SetModel(named, "Apple M1");
        named.physicalMemoryBytes = 8 * kGiB;
        EXPECT_FALSE(MapMacGpu(named, readings));
    }

    TEST(HardwareMacMappingTest, LoadOnlyIsAReading) {
        MacGpuStats stats;
        stats.utilizationPercent = 12.0;
        HardwareReadings readings;
        ASSERT_TRUE(MapMacGpu(stats, readings));
        EXPECT_EQ(readings.loadPercent, 12U);
        EXPECT_FALSE(readings.memUsedMb);
    }

    TEST(HardwareMacMappingTest, LoadIsRoundedAndClamped) {
        HardwareReadings readings;
        MacGpuStats stats;
        stats.utilizationPercent = 100.4;
        ASSERT_TRUE(MapMacGpu(stats, readings));
        EXPECT_EQ(readings.loadPercent, 100U);

        stats.utilizationPercent = 250.0;
        ASSERT_TRUE(MapMacGpu(stats, readings));
        EXPECT_EQ(readings.loadPercent, 100U);

        stats.utilizationPercent = 36.5;
        ASSERT_TRUE(MapMacGpu(stats, readings));
        EXPECT_EQ(readings.loadPercent, 37U);

        stats.utilizationPercent = -3.0;
        ASSERT_TRUE(MapMacGpu(stats, readings));
        EXPECT_EQ(readings.loadPercent, 0U);

        stats.utilizationPercent = std::numeric_limits<double>::infinity();
        ASSERT_TRUE(MapMacGpu(stats, readings));
        EXPECT_EQ(readings.loadPercent, 100U);
    }

    TEST(HardwareMacMappingTest, ANotANumberLoadIsMissingNotZero) {
        MacGpuStats stats = AppleSilicon();
        stats.utilizationPercent = std::numeric_limits<double>::quiet_NaN();
        HardwareReadings readings;
        ASSERT_TRUE(MapMacGpu(stats, readings));
        EXPECT_FALSE(readings.loadPercent);
        EXPECT_EQ(readings.memUsedMb, 9U * 1024U);
    }

    TEST(HardwareMacMappingTest, OnlyOneOfVramUsedAndFreeIsNotADiscreteReading) {
        MacGpuStats stats;
        stats.vramUsedBytes = 6 * kGiB;
        stats.physicalMemoryBytes = 32 * kGiB;
        HardwareReadings readings;
        EXPECT_FALSE(MapMacGpu(stats, readings));

        // ... and it does not hide the unified numbers that are there
        stats.inUseSystemBytes = 2 * kGiB;
        ASSERT_TRUE(MapMacGpu(stats, readings));
        EXPECT_TRUE(readings.unifiedMemory);
        EXPECT_EQ(readings.memUsedMb, 2U * 1024U);
    }

    TEST(HardwareMacMappingTest, BytesAreRoundedToWholeMebibytesWithoutOverflow) {
        MacGpuStats stats;
        stats.inUseSystemBytes = 10 * kMiB + kMiB / 2;
        HardwareReadings readings;
        ASSERT_TRUE(MapMacGpu(stats, readings));
        EXPECT_EQ(readings.memUsedMb, 11U);

        stats.inUseSystemBytes = 10 * kMiB + kMiB / 2 - 1;
        ASSERT_TRUE(MapMacGpu(stats, readings));
        EXPECT_EQ(readings.memUsedMb, 10U);

        stats.inUseSystemBytes = std::numeric_limits<uint64_t>::max();
        ASSERT_TRUE(MapMacGpu(stats, readings));
        EXPECT_EQ(readings.memUsedMb, std::numeric_limits<uint32_t>::max());

        MacGpuStats vram;
        vram.vramUsedBytes = std::numeric_limits<uint64_t>::max();
        vram.vramFreeBytes = std::numeric_limits<uint64_t>::max();
        ASSERT_TRUE(MapMacGpu(vram, readings));
        EXPECT_EQ(readings.memTotalMb, std::numeric_limits<uint32_t>::max());
    }

    TEST(HardwareMacMappingTest, AModelWithoutATerminatorIsCutAndTheTemperatureIsLeftAlone) {
        MacGpuStats stats = AppleSilicon();
        std::memset(stats.model, 'A', sizeof(stats.model));
        HardwareReadings readings;
        readings.temperatureC = 55;
        readings.kind = HardwareKind::Gpu;
        ASSERT_TRUE(MapMacGpu(stats, readings));
        EXPECT_EQ(std::strlen(readings.name), kHardwareNameSize - 1);
        EXPECT_EQ(readings.temperatureC, 55);
        EXPECT_EQ(readings.kind, HardwareKind::Gpu);
    }

    TEST(HardwareMacMappingTest, AnEarlierReadingIsNotKept) {
        HardwareReadings readings;
        ASSERT_TRUE(MapMacGpu(Discrete(), readings));
        MacGpuStats loadOnly;
        loadOnly.utilizationPercent = 5.0;
        ASSERT_TRUE(MapMacGpu(loadOnly, readings));
        EXPECT_FALSE(readings.memUsedMb);
        EXPECT_FALSE(readings.memTotalMb);
        EXPECT_STREQ(readings.name, "");
    }

    TEST(HardwareMacMappingTest, TheGpuTemperatureIsTheMeanOfTheGpuSensors) {
        const Optional<int32_t> celsius =
            Pick({Sample("GPU MTR Temp Sensor1", 50.0), Sample("GPU MTR Temp Sensor2", 52.0), Sample("GPU MTR Temp Sensor3", 57.0), Sample("pACC MTR Temp Sensor1", 90.0)}, HardwareKind::Gpu);
        EXPECT_EQ(celsius, 53);
    }

    TEST(HardwareMacMappingTest, TheCpuTemperatureIsTheHighestOfThreeFamilies) {
        const Optional<int32_t> celsius =
            Pick({Sample("pACC MTR Temp Sensor1", 61.0), Sample("eACC MTR Temp Sensor4", 48.0), Sample("PMU tdie3", 72.4), Sample("PMU tdie9", 66.0), Sample("GPU MTR Temp Sensor1", 95.0)},
                 HardwareKind::Cpu);
        EXPECT_EQ(celsius, 72);
    }

    TEST(HardwareMacMappingTest, ASensorOfTheNameIsFoundByItsPrefixOnly) {
        EXPECT_FALSE(Pick({Sample("the GPU MTR Temp Sensor1", 50.0), Sample("NAND CH0 temp", 40.0), Sample("", 40.0)}, HardwareKind::Gpu));
        EXPECT_FALSE(Pick({Sample("PMU tdev1", 50.0)}, HardwareKind::Cpu));
    }

    TEST(HardwareMacMappingTest, ANotANumberAndAnAbsurdValueAreSkipped) {
        const double notANumber = std::numeric_limits<double>::quiet_NaN();
        EXPECT_EQ(Pick({Sample("pACC MTR Temp Sensor1", notANumber), Sample("pACC MTR Temp Sensor2", 200.0), Sample("pACC MTR Temp Sensor3", -5.0), Sample("pACC MTR Temp Sensor4", 55.0)},
                       HardwareKind::Cpu),
                  55);
        EXPECT_FALSE(Pick({Sample("pACC MTR Temp Sensor1", notANumber), Sample("pACC MTR Temp Sensor2", 150.5)}, HardwareKind::Cpu));
        // The ends of the range are values
        EXPECT_EQ(Pick({Sample("GPU MTR Temp Sensor1", 0.0), Sample("GPU MTR Temp Sensor2", 150.0)}, HardwareKind::Gpu), 75);
    }

    TEST(HardwareMacMappingTest, NoSamplesAndNoKindGiveNothing) {
        EXPECT_FALSE(Pick({}, HardwareKind::Cpu));
        EXPECT_FALSE(Pick({Sample("pACC MTR Temp Sensor1", 61.0), Sample("GPU MTR Temp Sensor1", 50.0)}, HardwareKind::None));
    }

    TEST(HardwareMacMappingTest, ASensorNameLongerThanItsBufferIsCutAndStillMatches) {
        MacSensorSample sample;
        std::memset(sample.name, 'x', sizeof(sample.name));
        std::memcpy(sample.name, "PMU tdie", 8);
        sample.celsius = 64.0;
        EXPECT_EQ(PickAppleSiliconTemperature(Span<const MacSensorSample>(&sample, 1), HardwareKind::Cpu), 64);

        // No terminator at all and no prefix: nothing reads past the buffer
        std::memset(sample.name, 'x', sizeof(sample.name));
        EXPECT_FALSE(PickAppleSiliconTemperature(Span<const MacSensorSample>(&sample, 1), HardwareKind::Cpu));
    }

    TEST(HardwareMacMappingTest, Sp78IsSignedEightPointEightBigEndian) {
        EXPECT_EQ(Decode("sp78", {0x2D, 0x40}), 45);
        // 45.5 rounds up
        EXPECT_EQ(Decode("sp78", {0x2D, 0x80}), 46);
        EXPECT_EQ(Decode("sp78", {0x00, 0x80}), 1);
        // -1.0, and what an absent key reads as
        EXPECT_FALSE(Decode("sp78", {0xFF, 0x00}));
        EXPECT_FALSE(Decode("sp78", {0x00, 0x00}));
        // 160 is above the range
        EXPECT_FALSE(Decode("sp78", {0xA0, 0x00}));
    }

    TEST(HardwareMacMappingTest, FltIsTheLittleEndianFloatOfTheHost) {
        // 61.5f is 0x42760000
        EXPECT_EQ(Decode("flt ", {0x00, 0x00, 0x76, 0x42}), 62);
        // 45.0f is 0x42340000
        EXPECT_EQ(Decode("flt ", {0x00, 0x00, 0x34, 0x42}), 45);
        // NaN, and a float below the range
        EXPECT_FALSE(Decode("flt ", {0x00, 0x00, 0xC0, 0x7F}));
        EXPECT_FALSE(Decode("flt ", {0x00, 0x00, 0x80, 0xBF}));
        EXPECT_FALSE(Decode("flt ", {0x00, 0x00, 0x00, 0x00}));
        EXPECT_FALSE(Decode("flt ", {0x00, 0x00, 0x76}));
    }

    TEST(HardwareMacMappingTest, Fpe2IsUnsignedFourteenPointTwoBigEndian) {
        EXPECT_EQ(Decode("fpe2", {0x00, 0xB4}), 45);
        EXPECT_EQ(Decode("fpe2", {0x00, 0xB6}), 46);
        EXPECT_FALSE(Decode("fpe2", {0x00, 0x00}));
        EXPECT_FALSE(Decode("fpe2", {0xFF, 0xFF}));
    }

    TEST(HardwareMacMappingTest, AnUnknownTypeAndAShortBufferGiveNothing) {
        EXPECT_FALSE(Decode("ui8 ", {0x2D}));
        EXPECT_FALSE(Decode("    ", {0x2D, 0x40}));
        EXPECT_FALSE(Decode("sp78", {0x2D}));
        EXPECT_FALSE(Decode("sp78", {}));
        EXPECT_FALSE(Decode("fpe2", {0xB4}));
        // Only the first four characters of the type are looked at
        const char longer[] = {'s', 'p', '7', '8', 'x'};
        const uint8_t bytes[] = {0x2D, 0x40};
        EXPECT_EQ(DecodeSmcTemperature(longer, Span<const uint8_t>(bytes)), 45);
    }

    TEST(HardwareMacMappingTest, TheSmcKeysAreFourCharactersEach) {
        for (const StringView key : kSmcCpuTemperatureKeys) {
            EXPECT_EQ(key.size(), kSmcTypeSize);
        }
        for (const StringView key : kSmcGpuTemperatureKeys) {
            EXPECT_EQ(key.size(), kSmcTypeSize);
        }
    }

}  // namespace BECore::Tests
