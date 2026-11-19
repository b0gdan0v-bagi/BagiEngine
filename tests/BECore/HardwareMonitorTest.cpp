#include <Common/Printers.h>
#include <Common/Wait.h>
#include <Widgets/EditorLayout/Hardware/HardwareMonitor.h>
#include <Widgets/EditorLayout/Hardware/HardwarePlatform.h>

namespace BECore::Tests {

    namespace {

        /// The worker reads again at once, so a test that wants many reads does not wait for a period
        constexpr std::chrono::milliseconds kFastPeriod{1};
        constexpr std::chrono::hours kNeverPeriod{1};
        constexpr uint32_t kBaseLoad = 10;

        /// Answers from its fields, on the calling thread only
        class ScriptedReader : public IHardwareReader {
        public:
            bool Read(HardwareReadings& out) override {
                ++calls;
                out = readings;
                return answer;
            }

            bool answer = true;
            HardwareReadings readings;
            int calls = 0;
        };

        HardwareReadings WithLoad(uint32_t load) {
            HardwareReadings readings;
            readings.loadPercent = load;
            return readings;
        }

        /// What a reader of the thread tests shares with the test, so it outlives the monitor that owns the reader
        struct ReaderState {
            Mutex mutex{BE_LOCK_SITE("HardwareMonitorTest reader")};
            ConditionVariable changed;
            uint32_t reads = 0;
            /// A read from this number on waits for Open(); 0 never waits
            uint32_t blockFromRead = 0;
            bool open = false;
            /// True: every read differs from the last one, so every read publishes
            bool vary = true;

            bool WaitForReads(uint32_t count) {
                UniqueLock<Mutex> lock(mutex);
                return changed.wait_for(lock, kWaitLimit, [&] { return reads >= count; });
            }

            [[nodiscard]] uint32_t Reads() {
                LockGuard lock(mutex);
                return reads;
            }

            void Open() {
                {
                    LockGuard lock(mutex);
                    open = true;
                }
                changed.notify_all();
            }
        };

        /// Opens the gate when the test leaves its scope, so a failed assertion cannot leave a worker blocked and the join hanging
        class GateGuard {
        public:
            explicit GateGuard(ReaderState& state) : _state(state) {}
            ~GateGuard() {
                _state.Open();
            }
            GateGuard(const GateGuard&) = delete;
            GateGuard& operator=(const GateGuard&) = delete;

        private:
            ReaderState& _state;
        };

        class SignallingReader : public IHardwareReader {
        public:
            explicit SignallingReader(ReaderState& state) : _state(state) {}

            bool Read(HardwareReadings& out) override {
                UniqueLock<Mutex> lock(_state.mutex);
                const uint32_t read = ++_state.reads;
                out.loadPercent = _state.vary ? kBaseLoad + read : kBaseLoad;
                _state.changed.notify_all();
                if (_state.blockFromRead != 0 && read >= _state.blockFromRead) {
                    _state.changed.wait(lock, [this] { return _state.open; });
                }
                return true;
            }

        private:
            ReaderState& _state;
        };

        /// Fetch is allowed to answer false while the worker holds the lock for a moment (the frame tries again next time), so a test that
        /// races a fast worker tries again too
        bool FetchEventually(HardwareMonitor& monitor, HardwareReadings& out) {
            return WaitUntil([&] { return monitor.Fetch(out); });
        }

        UniquePtr<HardwareMonitor> MonitorOver(ReaderState& state) {
            return MakeUnique<HardwareMonitor>(MakeUnique<SignallingReader>(state), nullptr);
        }

    }  // namespace

    TEST(HardwareMonitorTest, TheGpuWinsWhenItHasAValue) {
        ScriptedReader gpu;
        ScriptedReader cpu;
        gpu.readings = WithLoad(80);
        cpu.readings = WithLoad(20);
        HardwareReadings out;
        SampleHardware(&gpu, &cpu, out);
        EXPECT_EQ(out.kind, HardwareKind::Gpu);
        EXPECT_EQ(out.loadPercent, 80u);
        EXPECT_EQ(cpu.calls, 0);
    }

    TEST(HardwareMonitorTest, AGpuThatAnswersTrueWithoutValuesFallsToTheCpu) {
        ScriptedReader gpu;
        ScriptedReader cpu;
        SetHardwareName(gpu.readings, "A name is no value");
        cpu.readings = WithLoad(20);
        HardwareReadings out;
        SampleHardware(&gpu, &cpu, out);
        EXPECT_EQ(out.kind, HardwareKind::Cpu);
        EXPECT_EQ(out.loadPercent, 20u);
        EXPECT_EQ(out.name[0], '\0');
    }

    TEST(HardwareMonitorTest, AGpuThatAnswersFalseFallsToTheCpuAndLeavesNothingBehind) {
        ScriptedReader gpu;
        ScriptedReader cpu;
        gpu.answer = false;
        gpu.readings = WithLoad(80);
        gpu.readings.temperatureC = 70;
        cpu.readings = WithLoad(20);
        HardwareReadings out;
        SampleHardware(&gpu, &cpu, out);
        EXPECT_EQ(out.kind, HardwareKind::Cpu);
        EXPECT_EQ(out.loadPercent, 20u);
        EXPECT_FALSE(out.temperatureC.has_value());
    }

    TEST(HardwareMonitorTest, NothingFromBothGivesNone) {
        ScriptedReader gpu;
        ScriptedReader cpu;
        gpu.answer = false;
        cpu.answer = false;
        cpu.readings = WithLoad(20);
        HardwareReadings out = WithLoad(55);
        out.kind = HardwareKind::Gpu;
        SampleHardware(&gpu, &cpu, out);
        EXPECT_EQ(out, HardwareReadings{});
    }

    TEST(HardwareMonitorTest, ANullReaderIsSkipped) {
        ScriptedReader reader;
        reader.readings = WithLoad(30);
        HardwareReadings out;

        SampleHardware(nullptr, &reader, out);
        EXPECT_EQ(out.kind, HardwareKind::Cpu);

        SampleHardware(&reader, nullptr, out);
        EXPECT_EQ(out.kind, HardwareKind::Gpu);

        SampleHardware(nullptr, nullptr, out);
        EXPECT_EQ(out, HardwareReadings{});
    }

    TEST(HardwareMonitorTest, TheSamplerSetsTheKindAndNormalizes) {
        ScriptedReader gpu;
        gpu.readings = WithLoad(130);
        gpu.readings.kind = HardwareKind::Cpu;
        gpu.readings.temperatureC = -300;
        HardwareReadings out;
        SampleHardware(&gpu, nullptr, out);
        EXPECT_EQ(out.kind, HardwareKind::Gpu);
        EXPECT_EQ(out.loadPercent, 100u);
        EXPECT_FALSE(out.temperatureC.has_value());
    }

    TEST(HardwareMonitorTest, ReadingsWithOnlyAnAbsurdTemperatureAreNothing) {
        ScriptedReader gpu;
        gpu.readings.temperatureC = 400;
        ScriptedReader cpu;
        cpu.readings = WithLoad(5);
        HardwareReadings out;
        SampleHardware(&gpu, &cpu, out);
        EXPECT_EQ(out.kind, HardwareKind::Cpu);
    }

    TEST(HardwareMonitorTest, NothingIsFetchedBeforeAPoll) {
        HardwareMonitor monitor(MakeUnique<ScriptedReader>(), nullptr);
        HardwareReadings out;
        EXPECT_FALSE(monitor.Fetch(out));
        EXPECT_EQ(monitor.Version(), 0u);
    }

    TEST(HardwareMonitorTest, PollOncePublishesAndFetchAnswersTrueOnce) {
        auto reader = MakeUnique<ScriptedReader>();
        reader->readings = WithLoad(42);
        HardwareMonitor monitor(std::move(reader), nullptr);

        monitor.PollOnce();
        EXPECT_EQ(monitor.Version(), 1u);

        HardwareReadings out;
        ASSERT_TRUE(monitor.Fetch(out));
        EXPECT_EQ(out.kind, HardwareKind::Gpu);
        EXPECT_EQ(out.loadPercent, 42u);
        EXPECT_FALSE(monitor.Fetch(out));
    }

    TEST(HardwareMonitorTest, AnUnchangedReadingDoesNotMoveTheVersion) {
        auto reader = MakeUnique<ScriptedReader>();
        ScriptedReader* script = reader.get();
        script->readings = WithLoad(42);
        HardwareMonitor monitor(std::move(reader), nullptr);

        monitor.PollOnce();
        HardwareReadings out;
        ASSERT_TRUE(monitor.Fetch(out));
        const uint32_t version = monitor.Version();

        monitor.PollOnce();
        monitor.PollOnce();
        EXPECT_EQ(script->calls, 3);
        EXPECT_EQ(monitor.Version(), version);
        EXPECT_FALSE(monitor.Fetch(out));

        script->readings = WithLoad(43);
        monitor.PollOnce();
        EXPECT_EQ(monitor.Version(), version + 1);
        ASSERT_TRUE(monitor.Fetch(out));
        EXPECT_EQ(out.loadPercent, 43u);
    }

    TEST(HardwareMonitorTest, ReadingsThatDifferOnlyByAnAbsurdAmountAreTheSame) {
        auto reader = MakeUnique<ScriptedReader>();
        ScriptedReader* script = reader.get();
        script->readings = WithLoad(130);
        HardwareMonitor monitor(std::move(reader), nullptr);
        monitor.PollOnce();
        const uint32_t version = monitor.Version();
        script->readings = WithLoad(250);
        monitor.PollOnce();
        EXPECT_EQ(monitor.Version(), version);
    }

    TEST(HardwareMonitorTest, AReaderThatGoesFromValuesToNothingPublishesNone) {
        auto reader = MakeUnique<ScriptedReader>();
        ScriptedReader* script = reader.get();
        script->readings = WithLoad(42);
        HardwareMonitor monitor(std::move(reader), nullptr);

        monitor.PollOnce();
        HardwareReadings out;
        ASSERT_TRUE(monitor.Fetch(out));
        ASSERT_EQ(out.kind, HardwareKind::Gpu);

        script->answer = false;
        monitor.PollOnce();
        ASSERT_TRUE(monitor.Fetch(out));
        EXPECT_EQ(out, HardwareReadings{});
    }

    TEST(HardwareMonitorTest, AMonitorWithNoReaderPublishesNothing) {
        HardwareMonitor monitor(nullptr, nullptr);
        monitor.PollOnce();
        HardwareReadings out;
        EXPECT_FALSE(monitor.Fetch(out));
        EXPECT_EQ(monitor.Version(), 0u);
    }

    TEST(HardwareMonitorTest, ThePlatformMonitorIsMadeAndNeedsNoStart) {
        const UniquePtr<HardwareMonitor> monitor = MakePlatformHardwareMonitor();
        ASSERT_TRUE(monitor != nullptr);
        HardwareReadings out;
        EXPECT_FALSE(monitor->Fetch(out));
    }

#if !BE_SINGLE_THREAD

    TEST(HardwareMonitorTest, TheWorkerReadsAtOnceAndThenAgain) {
        ReaderState state;
        UniquePtr<HardwareMonitor> monitor = MonitorOver(state);
        monitor->Start(kFastPeriod);
        // No fixed pause: the second read begins after the first one was published
        ASSERT_TRUE(state.WaitForReads(2));

        HardwareReadings out;
        ASSERT_TRUE(FetchEventually(*monitor, out));
        EXPECT_EQ(out.kind, HardwareKind::Gpu);
        EXPECT_TRUE(out.loadPercent.has_value());

        monitor->Stop();
        const uint32_t readsAtStop = state.Reads();
        EXPECT_GE(readsAtStop, 2u);
        EXPECT_EQ(state.Reads(), readsAtStop);
    }

    TEST(HardwareMonitorTest, TheFirstReadComesBeforeTheFirstPeriodEnds) {
        ReaderState state;
        UniquePtr<HardwareMonitor> monitor = MonitorOver(state);
        monitor->Start(kNeverPeriod);
        ASSERT_TRUE(state.WaitForReads(1));
        // A stop that had to wait for the period would hang here
        monitor->Stop();
        EXPECT_EQ(state.Reads(), 1u);
    }

    TEST(HardwareMonitorTest, StopIsIdempotentAndAMonitorThatNeverStartedIsHarmless) {
        ReaderState state;
        {
            UniquePtr<HardwareMonitor> neverStarted = MonitorOver(state);
            neverStarted->Stop();
            neverStarted->Stop();
        }
        EXPECT_EQ(state.Reads(), 0u);

        UniquePtr<HardwareMonitor> monitor = MonitorOver(state);
        monitor->Start(kFastPeriod);
        ASSERT_TRUE(state.WaitForReads(1));
        monitor->Stop();
        monitor->Stop();
    }

    TEST(HardwareMonitorTest, StartTwiceStartsOneThread) {
        ReaderState state;
        UniquePtr<HardwareMonitor> monitor = MonitorOver(state);
        monitor->Start(kFastPeriod);
        // A second thread would be assigned over the first, and a joinable Thread that is assigned over terminates the process
        monitor->Start(kFastPeriod);
        ASSERT_TRUE(state.WaitForReads(2));
        monitor->Stop();
    }

    TEST(HardwareMonitorTest, AMonitorCanBeStartedAgainAfterAStop) {
        ReaderState state;
        UniquePtr<HardwareMonitor> monitor = MonitorOver(state);
        monitor->Start(kFastPeriod);
        ASSERT_TRUE(state.WaitForReads(2));
        monitor->Stop();

        const uint32_t before = state.Reads();
        monitor->Start(kFastPeriod);
        ASSERT_TRUE(state.WaitForReads(before + 2));
        monitor->Stop();
    }

    TEST(HardwareMonitorTest, DestroyingARunningMonitorJoinsTheWorker) {
        ReaderState state;
        {
            UniquePtr<HardwareMonitor> monitor = MonitorOver(state);
            monitor->Start(kNeverPeriod);
            ASSERT_TRUE(state.WaitForReads(1));
        }
        // The reader is gone with the monitor; nothing reads after the destructor returned
        const uint32_t reads = state.Reads();
        EXPECT_EQ(reads, 1u);
    }

    TEST(HardwareMonitorTest, TheLockIsNotHeldWhileAReaderReads) {
        ReaderState state;
        state.blockFromRead = 2;
        UniquePtr<HardwareMonitor> monitor = MonitorOver(state);
        // After the monitor, so it is destroyed first: the gate opens before the destructor joins
        const GateGuard gate(state);
        monitor->Start(kFastPeriod);

        // The worker is inside the second read and stays there: the first one was published before it began
        ASSERT_TRUE(state.WaitForReads(2));
        HardwareReadings out;
        EXPECT_TRUE(monitor->Fetch(out));
        EXPECT_EQ(out.loadPercent, kBaseLoad + 1);
        EXPECT_FALSE(monitor->Fetch(out));

        state.Open();
        monitor->Stop();
    }

    TEST(HardwareMonitorTest, AnUnchangingReaderMovesTheVersionOnce) {
        ReaderState state;
        state.vary = false;
        UniquePtr<HardwareMonitor> monitor = MonitorOver(state);
        monitor->Start(kFastPeriod);
        ASSERT_TRUE(state.WaitForReads(5));
        monitor->Stop();
        EXPECT_EQ(monitor->Version(), 1u);
    }

#endif

}  // namespace BECore::Tests
