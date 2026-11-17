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

}  // namespace BECore::Tests
