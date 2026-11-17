#include <Common/Printers.h>
#include <Widgets/EditorLayout/Hardware/CpuLoad.h>
#include <Widgets/EditorLayout/Hardware/NvmlReader.h>
#include <cstring>

namespace BECore::Tests {

    namespace {

        constexpr uint32_t kFullLoad = 100;

        // A line of a modern kernel: user nice system idle iowait irq softirq steal guest guest_nice
        constexpr StringView kModernStat = "cpu  4705 150 1120 16250 520 20 30 40 0 0\n"
                                           "cpu0 2000 70 500 8000 200 10 15 20 0 0\n"
                                           "cpu1 2705 80 620 8250 320 10 15 20 0 0\n"
                                           "intr 123456 0 0\n";
        constexpr StringView kOldStat = "cpu  100 0 50 850\ncpu0 100 0 50 850\n";

        CpuTicks Ticks(uint64_t busy, uint64_t total) {
            return CpuTicks{.busy = busy, .total = total};
        }

        // ---- NVML over a table of functions of this file

        /// What the fake driver answers and what was asked of it; one at a time, reset by the fixture
        struct FakeNvml {
            int initResult = kNvmlSuccess;
            unsigned int deviceCount = 1;
            int handleResult = kNvmlSuccess;
            int nameResult = kNvmlSuccess;
            int temperatureResult = kNvmlSuccess;
            int utilizationResult = kNvmlSuccess;
            int memoryResult = kNvmlSuccess;
            unsigned int temperature = 64;
            unsigned int gpuUtilization = 87;
            unsigned long long memoryTotal = 12288ULL * 1024 * 1024;
            unsigned long long memoryUsed = 9318ULL * 1024 * 1024;
            size_t nameLength = 17;

            int initCalls = 0;
            int shutdownCalls = 0;
            int temperatureCalls = 0;
        };

        FakeNvml g_fake;
        int g_deviceToken = 0;

        int FakeInit() {
            ++g_fake.initCalls;
            return g_fake.initResult;
        }
        int FakeShutdown() {
            ++g_fake.shutdownCalls;
            return kNvmlSuccess;
        }
        int FakeCount(unsigned int* count) {
            *count = g_fake.deviceCount;
            return kNvmlSuccess;
        }
        int FakeHandle(unsigned int index, NvmlDeviceHandle* device) {
            if (index != 0) {
                return 1;
            }
            *device = &g_deviceToken;
            return g_fake.handleResult;
        }
        /// Like NVML, writes at most length - 1 characters and a terminator
        int FakeName(NvmlDeviceHandle, char* name, unsigned int length) {
            const size_t count = std::min<size_t>(g_fake.nameLength, length - 1);
            std::memset(name, 'N', count);
            name[count] = '\0';
            return g_fake.nameResult;
        }
        int FakeTemperature(NvmlDeviceHandle, int sensor, unsigned int* temperature) {
            ++g_fake.temperatureCalls;
            EXPECT_EQ(sensor, kNvmlTemperatureGpu);
            *temperature = g_fake.temperature;
            return g_fake.temperatureResult;
        }
        int FakeUtilization(NvmlDeviceHandle, NvmlUtilization* utilization) {
            utilization->gpu = g_fake.gpuUtilization;
            return g_fake.utilizationResult;
        }
        int FakeMemory(NvmlDeviceHandle, NvmlMemory* memory) {
            memory->total = g_fake.memoryTotal;
            memory->used = g_fake.memoryUsed;
            memory->free = g_fake.memoryTotal - g_fake.memoryUsed;
            return g_fake.memoryResult;
        }

        NvmlApi MakeFakeApi() {
            NvmlApi api;
            api.nvmlInit_v2 = &FakeInit;
            api.nvmlShutdown = &FakeShutdown;
            api.nvmlDeviceGetCount_v2 = &FakeCount;
            api.nvmlDeviceGetHandleByIndex_v2 = &FakeHandle;
            api.nvmlDeviceGetName = &FakeName;
            api.nvmlDeviceGetTemperature = &FakeTemperature;
            api.nvmlDeviceGetUtilizationRates = &FakeUtilization;
            api.nvmlDeviceGetMemoryInfo = &FakeMemory;
            return api;
        }

    }  // namespace

    // ---- CpuLoadTracker

    TEST(CpuLoadTrackerTest, FirstCallGivesNothing) {
        CpuLoadTracker tracker;
        EXPECT_FALSE(tracker.Update(Ticks(10, 100)).has_value());
    }

    TEST(CpuLoadTrackerTest, LoadIsTheBusyShareOfTheDelta) {
        CpuLoadTracker tracker;
        (void)tracker.Update(Ticks(100, 1000));
        EXPECT_EQ(tracker.Update(Ticks(150, 1100)), 50u);
        EXPECT_EQ(tracker.Update(Ticks(150, 1200)), 0u);
        EXPECT_EQ(tracker.Update(Ticks(250, 1300)), kFullLoad);
    }

    TEST(CpuLoadTrackerTest, LoadIsRoundedAndNeverAboveOneHundred) {
        CpuLoadTracker tracker;
        (void)tracker.Update(Ticks(0, 0));
        EXPECT_EQ(tracker.Update(Ticks(2, 3)), 67u);
        EXPECT_EQ(tracker.Update(Ticks(2 + 500, 3 + 300)), kFullLoad);
    }

    TEST(CpuLoadTrackerTest, AnUnchangedTotalGivesNothingAndKeepsTheBaseline) {
        CpuLoadTracker tracker;
        (void)tracker.Update(Ticks(100, 1000));
        EXPECT_FALSE(tracker.Update(Ticks(100, 1000)).has_value());
        EXPECT_EQ(tracker.Update(Ticks(125, 1100)), 25u);
    }

    TEST(CpuLoadTrackerTest, CountersGoingBackwardsRestartTheBaseline) {
        CpuLoadTracker tracker;
        (void)tracker.Update(Ticks(5000, 10000));
        EXPECT_FALSE(tracker.Update(Ticks(10, 100)).has_value());
        EXPECT_EQ(tracker.Update(Ticks(60, 200)), 50u);
        // Busy time alone going back is a reset as well
        EXPECT_FALSE(tracker.Update(Ticks(10, 300)).has_value());
    }

    // ---- /proc/stat and hwmon parsers

    TEST(CpuParsersTest, ProcStatOfAModernKernel) {
        const Optional<CpuTicks> ticks = ParseProcStatCpu(kModernStat);
        ASSERT_TRUE(ticks.has_value());
        // The sum of the eight fields is 22835; idle and iowait are 16770 of it
        EXPECT_EQ(ticks->total, 22835u);
        EXPECT_EQ(ticks->busy, 6065u);
    }

    TEST(CpuParsersTest, ProcStatOfAnOldKernelHasFourFields) {
        const Optional<CpuTicks> ticks = ParseProcStatCpu(kOldStat);
        ASSERT_TRUE(ticks.has_value());
        EXPECT_EQ(ticks->total, 1000u);
        EXPECT_EQ(ticks->busy, 150u);
    }

    TEST(CpuParsersTest, ProcStatTakesTheAggregateLineNotACore) {
        EXPECT_FALSE(ParseProcStatCpu("cpu0 1 2 3 4 5 6 7 8\ncpu1 1 2 3 4 5 6 7 8\n").has_value());
        const Optional<CpuTicks> ticks = ParseProcStatCpu("intr 1 2\ncpu0 9 9 9 9\ncpu 1 1 1 7\n");
        ASSERT_TRUE(ticks.has_value());
        EXPECT_EQ(ticks->total, 10u);
        EXPECT_EQ(ticks->busy, 3u);
    }

    TEST(CpuParsersTest, ProcStatToleratesExtraColumnsAndNoNewline) {
        const Optional<CpuTicks> ticks = ParseProcStatCpu("cpu 1 1 1 1 1 1 1 1 5 6 7 8");
        ASSERT_TRUE(ticks.has_value());
        EXPECT_EQ(ticks->total, 8u);
        EXPECT_EQ(ticks->busy, 6u);
    }

    TEST(CpuParsersTest, ProcStatRejectsGarbage) {
        EXPECT_FALSE(ParseProcStatCpu("").has_value());
        EXPECT_FALSE(ParseProcStatCpu("\n\n").has_value());
        EXPECT_FALSE(ParseProcStatCpu("cpu").has_value());
        EXPECT_FALSE(ParseProcStatCpu("cpu \n").has_value());
        EXPECT_FALSE(ParseProcStatCpu("cpu 1 2 3\n").has_value());
        EXPECT_FALSE(ParseProcStatCpu("cpu a b c d\n").has_value());
        EXPECT_FALSE(ParseProcStatCpu("cpu 1 2 3 -4\n").has_value());
        EXPECT_FALSE(ParseProcStatCpu("cpufreq 1 2 3 4\n").has_value());
        EXPECT_FALSE(ParseProcStatCpu("not a stat file at all").has_value());
    }

    TEST(CpuParsersTest, MilliCelsiusIsRoundedToWholeDegrees) {
        EXPECT_EQ(ParseMilliCelsius("45000\n"), 45);
        EXPECT_EQ(ParseMilliCelsius("45499"), 45);
        EXPECT_EQ(ParseMilliCelsius("45500"), 46);
        EXPECT_EQ(ParseMilliCelsius("-5000"), -5);
        EXPECT_EQ(ParseMilliCelsius("-5500"), -6);
        EXPECT_EQ(ParseMilliCelsius("0"), 0);
    }

    TEST(CpuParsersTest, MilliCelsiusRejectsWhatIsNotPlausible) {
        EXPECT_FALSE(ParseMilliCelsius("abc").has_value());
        EXPECT_FALSE(ParseMilliCelsius("").has_value());
        EXPECT_FALSE(ParseMilliCelsius("\n").has_value());
        EXPECT_FALSE(ParseMilliCelsius("45.5").has_value());
        EXPECT_FALSE(ParseMilliCelsius("999999").has_value());
        EXPECT_FALSE(ParseMilliCelsius("-30000").has_value());
        EXPECT_FALSE(ParseMilliCelsius("9223372036854775807").has_value());
        EXPECT_FALSE(ParseMilliCelsius("-9223372036854775808").has_value());
        EXPECT_EQ(ParseMilliCelsius("150000"), 150);
        EXPECT_FALSE(ParseMilliCelsius("150500").has_value());
    }

    TEST(CpuParsersTest, ModelNameIsTheValueOfTheFirstModelNameLine) {
        EXPECT_EQ(ParseCpuModelName("processor\t: 0\nvendor_id\t: GenuineIntel\nmodel name\t: Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz\ncpu MHz\t\t: 3600\n"),
                  "Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz");
        EXPECT_EQ(ParseCpuModelName("model name: A\nmodel name: B\n"), "A");
        EXPECT_EQ(ParseCpuModelName("model name\t:   spaced  \r\n"), "spaced");
        EXPECT_EQ(ParseCpuModelName("model name : no newline"), "no newline");
    }

    TEST(CpuParsersTest, ModelNameIsEmptyWhenThereIsNone) {
        EXPECT_TRUE(ParseCpuModelName("").empty());
        EXPECT_TRUE(ParseCpuModelName("processor : 0\nBogoMIPS : 38.40\n").empty());
        EXPECT_TRUE(ParseCpuModelName("model name without a colon\n").empty());
        EXPECT_TRUE(ParseCpuModelName("model names: x\n").empty());
        EXPECT_TRUE(ParseCpuModelName("model name:\n").empty());
    }

    TEST(CpuParsersTest, HwmonNamesOfCpus) {
        EXPECT_TRUE(IsCpuHwmonName("coretemp"));
        EXPECT_TRUE(IsCpuHwmonName("k10temp\n"));
        EXPECT_TRUE(IsCpuHwmonName("zenpower"));
        EXPECT_TRUE(IsCpuHwmonName("cpu_thermal\n"));
        EXPECT_FALSE(IsCpuHwmonName("nvme"));
        EXPECT_FALSE(IsCpuHwmonName("amdgpu"));
        EXPECT_FALSE(IsCpuHwmonName("coretempx"));
        EXPECT_FALSE(IsCpuHwmonName(""));
    }

    // ---- NvmlReader

    class NvmlReaderTest : public ::testing::Test {
    protected:
        void SetUp() override {
            g_fake = FakeNvml{};
        }
    };

    TEST_F(NvmlReaderTest, ReadsTemperatureLoadMemoryAndName) {
        NvmlReader reader(MakeFakeApi());
        HardwareReadings readings;
        ASSERT_TRUE(reader.Read(readings));
        EXPECT_EQ(readings.temperatureC, 64);
        EXPECT_EQ(readings.loadPercent, 87u);
        EXPECT_EQ(readings.memUsedMb, 9318u);
        EXPECT_EQ(readings.memTotalMb, 12288u);
        EXPECT_FALSE(readings.unifiedMemory);
        EXPECT_EQ(std::strlen(readings.name), g_fake.nameLength);
    }

    TEST_F(NvmlReaderTest, MemoryIsRoundedToMib) {
        g_fake.memoryUsed = 1000ULL * 1024 * 1024 + 600 * 1024;
        g_fake.memoryTotal = 2000ULL * 1024 * 1024 + 100 * 1024;
        NvmlReader reader(MakeFakeApi());
        HardwareReadings readings;
        ASSERT_TRUE(reader.Read(readings));
        EXPECT_EQ(readings.memUsedMb, 1001u);
        EXPECT_EQ(readings.memTotalMb, 2000u);
    }

    TEST_F(NvmlReaderTest, InitIsDoneOnceOnTheFirstRead) {
        NvmlReader reader(MakeFakeApi());
        EXPECT_EQ(g_fake.initCalls, 0);
        HardwareReadings readings;
        EXPECT_TRUE(reader.Read(readings));
        EXPECT_TRUE(reader.Read(readings));
        EXPECT_EQ(g_fake.initCalls, 1);
    }

    TEST_F(NvmlReaderTest, TheDestructorShutsNvmlDownOnceAfterAnInit) {
        {
            NvmlReader reader(MakeFakeApi());
            HardwareReadings readings;
            EXPECT_TRUE(reader.Read(readings));
            EXPECT_EQ(g_fake.shutdownCalls, 0);
        }
        EXPECT_EQ(g_fake.shutdownCalls, 1);
    }

    TEST_F(NvmlReaderTest, NeverReadMeansNeverInitialized) {
        {
            NvmlReader reader(MakeFakeApi());
        }
        EXPECT_EQ(g_fake.initCalls, 0);
        EXPECT_EQ(g_fake.shutdownCalls, 0);
    }

    TEST_F(NvmlReaderTest, AFailedInitIsFinal) {
        g_fake.initResult = 9;
        {
            NvmlReader reader(MakeFakeApi());
            HardwareReadings readings;
            EXPECT_FALSE(reader.Read(readings));
            EXPECT_FALSE(reader.Read(readings));
            EXPECT_FALSE(reader.Read(readings));
            EXPECT_EQ(g_fake.initCalls, 1);
            EXPECT_EQ(g_fake.temperatureCalls, 0);
        }
        // Nothing was initialized, so nothing is shut down
        EXPECT_EQ(g_fake.shutdownCalls, 0);
    }

    TEST_F(NvmlReaderTest, NoDeviceIsFinal) {
        g_fake.deviceCount = 0;
        {
            NvmlReader reader(MakeFakeApi());
            HardwareReadings readings;
            EXPECT_FALSE(reader.Read(readings));
            EXPECT_FALSE(reader.Read(readings));
            EXPECT_EQ(g_fake.initCalls, 1);
        }
        EXPECT_EQ(g_fake.shutdownCalls, 1);
    }

    TEST_F(NvmlReaderTest, AFailedHandleOrNameIsFinal) {
        g_fake.handleResult = 3;
        NvmlReader noHandle(MakeFakeApi());
        HardwareReadings readings;
        EXPECT_FALSE(noHandle.Read(readings));

        g_fake = FakeNvml{};
        g_fake.nameResult = 3;
        NvmlReader noName(MakeFakeApi());
        EXPECT_FALSE(noName.Read(readings));
        EXPECT_FALSE(noName.Read(readings));
        EXPECT_EQ(g_fake.temperatureCalls, 0);
    }

    TEST_F(NvmlReaderTest, AFailingQueryLeavesOnlyItsFieldMissing) {
        HardwareReadings readings;
        {
            g_fake.temperatureResult = 15;
            NvmlReader reader(MakeFakeApi());
            ASSERT_TRUE(reader.Read(readings));
            EXPECT_FALSE(readings.temperatureC.has_value());
            EXPECT_EQ(readings.loadPercent, 87u);
            EXPECT_EQ(readings.memTotalMb, 12288u);
        }
        readings = HardwareReadings{};
        {
            g_fake.temperatureResult = kNvmlSuccess;
            g_fake.utilizationResult = 15;
            NvmlReader reader(MakeFakeApi());
            ASSERT_TRUE(reader.Read(readings));
            EXPECT_EQ(readings.temperatureC, 64);
            EXPECT_FALSE(readings.loadPercent.has_value());
        }
        readings = HardwareReadings{};
        {
            g_fake.utilizationResult = kNvmlSuccess;
            g_fake.memoryResult = 15;
            NvmlReader reader(MakeFakeApi());
            ASSERT_TRUE(reader.Read(readings));
            EXPECT_EQ(readings.loadPercent, 87u);
            EXPECT_FALSE(readings.memUsedMb.has_value());
            EXPECT_FALSE(readings.memTotalMb.has_value());
        }
    }

    TEST_F(NvmlReaderTest, AReadWithNoQueryAnsweringHasNoValue) {
        g_fake.temperatureResult = 15;
        g_fake.utilizationResult = 15;
        g_fake.memoryResult = 15;
        NvmlReader reader(MakeFakeApi());
        HardwareReadings readings;
        EXPECT_FALSE(reader.Read(readings));
    }

    TEST_F(NvmlReaderTest, AnAbsurdTemperatureIsMissing) {
        g_fake.temperature = 4000000000u;
        NvmlReader reader(MakeFakeApi());
        HardwareReadings readings;
        ASSERT_TRUE(reader.Read(readings));
        EXPECT_FALSE(readings.temperatureC.has_value());
    }

    TEST_F(NvmlReaderTest, ALongNameIsCutToTheBufferAndTerminated) {
        g_fake.nameLength = 200;
        NvmlReader reader(MakeFakeApi());
        HardwareReadings readings;
        ASSERT_TRUE(reader.Read(readings));
        EXPECT_EQ(std::strlen(readings.name), kHardwareNameSize - 1);
        EXPECT_EQ(readings.name[kHardwareNameSize - 1], '\0');
    }

    // ---- The real loader and the platform readers

    TEST(NvmlLoaderTest, ABogusLibraryNameLoadsNothing) {
        const char* const names[] = {"bagi-no-such-library-3f9a.dll", "libbagi-no-such-library-3f9a.so.1"};
        EXPECT_FALSE(LoadNvmlApi(Span<const char* const>(names)).has_value());
        EXPECT_FALSE(LoadNvmlApi(Span<const char* const>()).has_value());
    }

    TEST(NvmlLoaderTest, TheDefaultReaderLoadsNothingUntilItIsRead) {
        // Never read here: that would load the real library of whatever machine runs the test
        NvmlReader reader;
        SUCCEED();
    }

}  // namespace BECore::Tests
