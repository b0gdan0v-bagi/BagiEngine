#include <BECore/Control/Json.h>
#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/Profile.h>
#include <BECore/Profiler/ProfilerClock.h>
#include <BECore/Profiler/ProfilerSettings.h>
#include <BECore/Profiler/ScopedTrace.h>
#include <Common/TempPath.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <thread>

namespace BECore::Tests {

    namespace {

        using Args = eastl::vector<eastl::string>;

        constexpr ProfilerBuild kFullBuild{.profiler = true, .tracy = true};
        constexpr ProfilerBuild kChromeOnlyBuild{.profiler = true, .tracy = false};
        constexpr ProfilerBuild kNoProfilerBuild{};

        constexpr ProfileSite kOuterSite{"Outer", "Test", "ProfilerTest.cpp", 1, 0};
        constexpr ProfileSite kInnerSite{"Inner \"quoted\"\\", "Test", "C:\\dir\\ProfilerTest.cpp", 2, 0};

        std::string Text(const eastl::string& text) {
            return std::string(text.data(), text.size());
        }

        std::string Member(const Json& event, const char* key) {
            const auto value = GetJsonString(event, key);
            return value ? Text(*value) : std::string();
        }

        /// The events of a trace, parsed back (fails the test when the JSON is not valid)
        Json ParseEvents(const eastl::string& trace) {
            auto parsed = ParseJson(trace);
            EXPECT_TRUE(parsed.has_value()) << Text(trace);
            if (!parsed) {
                return Json::array();
            }
            const Json* events = FindJsonMember(*parsed, "traceEvents");
            EXPECT_NE(events, nullptr);
            return events != nullptr ? *events : Json::array();
        }

        size_t CountPhase(const Json& events, const char* phase, std::optional<int64_t> tid = std::nullopt) {
            size_t count = 0;
            for (const Json& event : events) {
                if (Member(event, "ph") == phase && (!tid || GetJsonInt(event, "tid") == tid)) {
                    ++count;
                }
            }
            return count;
        }

        /// Every thread's B and E events nest, in the order of their time stamps
        void ExpectBalanced(const Json& events) {
            std::map<int64_t, int> depth;
            std::map<int64_t, double> lastTs;
            for (const Json& event : events) {
                const std::string phase = Member(event, "ph");
                if (phase != "B" && phase != "E") {
                    continue;
                }
                const int64_t tid = GetJsonInt(event, "tid").value_or(-1);
                const double ts = GetJsonNumber(event, "ts").value_or(-1.0);
                EXPECT_GE(ts, lastTs[tid]);
                lastTs[tid] = ts;
                depth[tid] += phase == "B" ? 1 : -1;
                EXPECT_GE(depth[tid], 0) << "an E without its B on thread " << tid;
            }
            for (const auto& [tid, open] : depth) {
                EXPECT_EQ(open, 0) << "zones left open on thread " << tid;
            }
        }

        /// Installs @p backend for the test and puts none back after it
        class ScopedBackend {
        public:
            explicit ScopedBackend(IProfilerBackend* backend) {
                Profiler::SetBackend(backend);
            }
            ~ScopedBackend() {
                Profiler::SetBackend(nullptr);
            }
        };

    }  // namespace

    // ---- Flags ----

    TEST(ProfilerSettingsTest, NoFlagIsNoBackendOrTracyInATracyBuild) {
        const auto plain = ResolveProfilerSettings(Args{"--other"}, kChromeOnlyBuild);
        ASSERT_TRUE(plain.has_value());
        EXPECT_EQ(plain->backend, ProfilerBackendKind::None);
        EXPECT_TRUE(plain->tracePath.empty());

        const auto tracy = ResolveProfilerSettings(Args{}, kFullBuild);
        ASSERT_TRUE(tracy.has_value());
        EXPECT_EQ(tracy->backend, ProfilerBackendKind::Tracy);
    }

    TEST(ProfilerSettingsTest, TraceRecordsChromeAndOverridesTracyDefault) {
        const auto settings = ResolveProfilerSettings(Args{"--trace=out/trace.json"}, kFullBuild);
        ASSERT_TRUE(settings.has_value());
        EXPECT_EQ(settings->backend, ProfilerBackendKind::ChromeTrace);
        EXPECT_EQ(settings->tracePath, "out/trace.json");
    }

    TEST(ProfilerSettingsTest, ProfilerFlagPicksTheBackend) {
        EXPECT_EQ(ResolveProfilerSettings(Args{"--profiler=chrome"}, kChromeOnlyBuild)->backend, ProfilerBackendKind::ChromeTrace);
        EXPECT_EQ(ResolveProfilerSettings(Args{"--profiler=none"}, kFullBuild)->backend, ProfilerBackendKind::None);
        EXPECT_EQ(ResolveProfilerSettings(Args{"--profiler=tracy"}, kFullBuild)->backend, ProfilerBackendKind::Tracy);
        EXPECT_EQ(ResolveProfilerSettings(Args{"--profiler=chrome", "--trace=t.json"}, kChromeOnlyBuild)->backend, ProfilerBackendKind::ChromeTrace);
    }

    TEST(ProfilerSettingsTest, BareEmptyUnknownAndConflictingFlagsAreErrors) {
        for (const Args& args : {Args{"--trace"}, Args{"--trace="}, Args{"--profiler"}, Args{"--profiler="}, Args{"--profiler=perfetto"}, Args{"--trace=t.json", "--profiler=none"},
                                 Args{"--trace=t.json", "--profiler=tracy"}}) {
            const auto settings = ResolveProfilerSettings(args, kFullBuild);
            EXPECT_FALSE(settings.has_value()) << Text(args.front());
            if (!settings) {
                EXPECT_FALSE(settings.error().empty());
            }
        }
    }

    TEST(ProfilerSettingsTest, TracyNotBuiltIsAnError) {
        const auto settings = ResolveProfilerSettings(Args{"--profiler=tracy"}, kChromeOnlyBuild);
        ASSERT_FALSE(settings.has_value());
        EXPECT_NE(settings.error().find("ENGINE_PROFILER_TRACY"), eastl::string::npos);
    }

    TEST(ProfilerSettingsTest, AnyFlagWithoutTheProfilerIsAnError) {
        EXPECT_TRUE(ResolveProfilerSettings(Args{"--control"}, kNoProfilerBuild).has_value());
        for (const Args& args : {Args{"--trace=t.json"}, Args{"--profiler=none"}}) {
            const auto settings = ResolveProfilerSettings(args, kNoProfilerBuild);
            ASSERT_FALSE(settings.has_value());
            EXPECT_NE(settings.error().find("ENGINE_PROFILER=ON"), eastl::string::npos);
        }
    }

    // ---- The Chrome trace recorder ----

    TEST(ChromeTraceBackendTest, WritesNestedZonesCountersFramesAndThreadNames) {
        ChromeTraceBackend recorder;
        Profiler::SetThreadName("Test main");
        const uint64_t outer = recorder.BeginZone(kOuterSite);
        const uint64_t inner = recorder.BeginZone(kInnerSite);
        recorder.Counter("Score", 2.5);
        recorder.EndZone(inner, kInnerSite, {});
        recorder.EndZone(outer, kOuterSite, {});
        recorder.FrameMark();

        const Json events = ParseEvents(recorder.WriteJson());
        ExpectBalanced(events);
        ASSERT_EQ(CountPhase(events, "B"), 2u);
        EXPECT_EQ(CountPhase(events, "E"), 2u);
        EXPECT_EQ(CountPhase(events, "C"), 1u);
        EXPECT_EQ(CountPhase(events, "i"), 1u);

        std::vector<std::string> begun;
        for (const Json& event : events) {
            const std::string phase = Member(event, "ph");
            if (phase == "B") {
                begun.push_back(Member(event, "name"));
            } else if (phase == "C") {
                EXPECT_EQ(Member(event, "name"), "Score");
                EXPECT_EQ(GetJsonNumber(*FindJsonMember(event, "args"), "value"), 2.5);
            } else if (phase == "M") {
                EXPECT_EQ(Member(*FindJsonMember(event, "args"), "name"), "Test main");
            }
        }
        EXPECT_EQ(begun, (std::vector<std::string>{"Outer", "Inner \"quoted\"\\"}));
        EXPECT_EQ(CountPhase(events, "M"), 1u);
    }

    TEST(ChromeTraceBackendTest, ZonesOpenAtTheDumpAreClosed) {
        ChromeTraceBackend recorder;
        recorder.BeginZone(kOuterSite);
        recorder.BeginZone(kInnerSite);

        const Json events = ParseEvents(recorder.WriteJson());
        EXPECT_EQ(CountPhase(events, "B"), 2u);
        ExpectBalanced(events);
    }

    TEST(ChromeTraceBackendTest, MarkTraceStartLeavesOutWhatCameBefore) {
        ChromeTraceBackend recorder;
        const uint64_t before = recorder.BeginZone(kOuterSite);
        recorder.FrameMark();
        recorder.MarkTraceStart();
        const uint64_t after = recorder.BeginZone(kInnerSite);
        recorder.EndZone(after, kInnerSite, {});
        recorder.EndZone(before, kOuterSite, {});  // its B is not in the trace: dropped too

        const Json events = ParseEvents(recorder.WriteJson());
        EXPECT_EQ(CountPhase(events, "B"), 1u);
        EXPECT_EQ(CountPhase(events, "E"), 1u);
        EXPECT_EQ(CountPhase(events, "i"), 0u);
        ExpectBalanced(events);
    }

    TEST(ChromeTraceBackendTest, ThreadsRecordWhileTheTraceIsWritten) {
        constexpr int kThreads = 4;
        constexpr int kZonesPerThread = 1000;
        ChromeTraceBackend recorder;
        std::atomic<int> started{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&recorder, &started] {
                started.fetch_add(1);
                for (int i = 0; i < kZonesPerThread; ++i) {
                    const uint64_t outer = recorder.BeginZone(kOuterSite);
                    const uint64_t inner = recorder.BeginZone(kInnerSite);
                    recorder.EndZone(inner, kInnerSite, {});
                    recorder.EndZone(outer, kOuterSite, {});
                }
            });
        }
        while (started.load() < kThreads) {
            std::this_thread::yield();
        }
        ExpectBalanced(ParseEvents(recorder.WriteJson()));  // in the middle of the run: a balanced prefix of each thread

        for (std::thread& thread : threads) {
            thread.join();
        }
        const Json events = ParseEvents(recorder.WriteJson());
        ExpectBalanced(events);
        EXPECT_EQ(CountPhase(events, "B"), static_cast<size_t>(kThreads * kZonesPerThread * 2));
        EXPECT_EQ(recorder.GetDroppedCount(), 0u);
    }

    TEST(ChromeTraceBackendTest, AFullThreadBufferDropsTheRest) {
        ChromeTraceBackend recorder;
        constexpr uint32_t kExtra = 10;
        for (uint32_t i = 0; i < ChromeTraceBackend::kMaxEventsPerThread + kExtra; ++i) {
            recorder.FrameMark();
        }
        EXPECT_EQ(recorder.GetDroppedCount(), kExtra);
    }

    TEST(ChromeTraceBackendTest, WriteFileReportsAnUnwritablePath) {
        ChromeTraceBackend recorder;
        const std::filesystem::path missing = TestTempDirectory("profiler") / "no-such-folder" / "trace.json";
        const auto written = recorder.WriteFile(eastl::string(missing.string().c_str()));
        ASSERT_FALSE(written.has_value());
        EXPECT_FALSE(written.error().empty());
    }

    // ---- The macros ----

#if BE_PROFILER

    namespace {

        int Recurse(int depth) {
            BE_PROFILE_SCOPE("Recurse");
            return depth == 0 ? 0 : 1 + Recurse(depth - 1);
        }

        int EarlyReturn(bool early) {
            BE_PROFILE_FUNCTION();
            if (early) {
                return 1;
            }
            BE_PROFILE_SCOPE("Late");
            return 2;
        }

    }  // namespace

    TEST(ProfileMacrosTest, ZonesBalanceOnEveryPath) {
        ChromeTraceBackend recorder;
        {
            ScopedBackend installed(&recorder);
            EXPECT_EQ(EarlyReturn(true), 1);
            EXPECT_EQ(EarlyReturn(false), 2);
            for (int i = 0; i < 3; ++i) {
                BE_PROFILE_SCOPE("Loop");
                if (i == 1) {
                    continue;
                }
            }
            const auto lambda = [] {
                BE_PROFILE_SCOPE("Lambda");
                return 7;
            };
            EXPECT_EQ(lambda(), 7);
            EXPECT_EQ(Recurse(4), 4);
            BE_PROFILE_COUNTER("Value", 3);
            BE_PROFILE_FRAME();
        }
        const Json events = ParseEvents(recorder.WriteJson());
        ExpectBalanced(events);
        // EarlyReturn twice, Late once, three loops, the lambda, five levels of Recurse
        EXPECT_EQ(CountPhase(events, "B"), 2u + 1u + 3u + 1u + 5u);
        EXPECT_EQ(CountPhase(events, "C"), 1u);
        EXPECT_EQ(CountPhase(events, "i"), 1u);
    }

    TEST(ProfileMacrosTest, NothingIsRecordedWithoutABackend) {
        ChromeTraceBackend recorder;
        Profiler::SetBackend(nullptr);
        EXPECT_EQ(EarlyReturn(false), 2);
        BE_PROFILE_FRAME();
        EXPECT_EQ(CountPhase(ParseEvents(recorder.WriteJson()), "B"), 0u);
    }

    TEST(ProfileMacrosTest, AZoneEndsOnTheBackendItBeganOn) {
        ChromeTraceBackend first;
        ChromeTraceBackend second;
        {
            ScopedBackend installed(&first);
            BE_PROFILE_SCOPE("Spans the switch");
            Profiler::SetBackend(&second);
        }
        ExpectBalanced(ParseEvents(first.WriteJson()));
        EXPECT_EQ(CountPhase(ParseEvents(first.WriteJson()), "E"), 1u);
        EXPECT_EQ(CountPhase(ParseEvents(second.WriteJson()), "E"), 0u);
    }

    TEST(ScopedTraceTest, WritesTheZonesOfItsScope) {
        const std::filesystem::path folder = TestTempDirectory("profiler");
        std::filesystem::create_directories(folder);
        const std::filesystem::path file = folder / "trace.json";
        {
            ScopedTrace trace(eastl::string(file.string().c_str()));
            EXPECT_EQ(EarlyReturn(false), 2);
        }
        EXPECT_EQ(Profiler::GetBackend(), nullptr);
        std::ifstream stream(file);
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        const Json events = ParseEvents(eastl::string(text.c_str()));
        EXPECT_EQ(CountPhase(events, "B"), 2u);
        ExpectBalanced(events);
        std::filesystem::remove_all(folder);
    }

#else

    TEST(ProfileMacrosTest, CompiledOutMacrosRecordAndEvaluateNothing) {
        ChromeTraceBackend recorder;
        Profiler::SetBackend(&recorder);
        int evaluated = 0;
        {
            BE_PROFILE_SCOPE("Off");
            BE_PROFILE_FUNCTION();
            BE_PROFILE_COUNTER("Off", ++evaluated);
            BE_PROFILE_THREAD("Off");
            BE_PROFILE_FRAME();
        }
        Profiler::SetBackend(nullptr);
        EXPECT_EQ(evaluated, 0);
        const Json events = ParseEvents(recorder.WriteJson());
        EXPECT_EQ(events.size(), 0u);
    }

#endif

    // ---- Cost (run by hand: --gtest_also_run_disabled_tests --gtest_filter='ProfilerCost*') ----

    namespace {

        template <typename TBody>
        double NanosecondsPerCall(int calls, TBody body) {
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < calls; ++i) {
                body();
            }
            const auto elapsed = std::chrono::steady_clock::now() - start;
            return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()) / calls;
        }

        constexpr int kCostCalls = 500000;

    }  // namespace

    TEST(ProfilerCostTest, DISABLED_ZoneAndClock) {
        ChromeTraceBackend recorder;
        const double steady = NanosecondsPerCall(kCostCalls, [] { static_cast<void>(std::chrono::steady_clock::now()); });
        const double clock = NanosecondsPerCall(kCostCalls, [] { static_cast<void>(ProfilerClock::Now()); });
        const double zoneNone = NanosecondsPerCall(kCostCalls, [] { const ProfileZone zone(kOuterSite); });
        Profiler::SetBackend(&recorder);
        const double zoneChrome = NanosecondsPerCall(kCostCalls / 4, [] { const ProfileZone zone(kOuterSite); });
        Profiler::SetBackend(nullptr);
        std::printf("steady_clock::now %.1f ns, ProfilerClock::Now %.1f ns, zone without backend %.1f ns, zone into the Chrome trace %.1f ns\n", steady, clock, zoneNone, zoneChrome);
    }

}  // namespace BECore::Tests
