#include <BECore/Profiler/Profile.h>
#include <BECore/Profiler/ProfilerSettings.h>
#include <gtest/gtest.h>

#if BE_PROFILER_TRACY
#include <BECore/Profiler/Backends/TracyProfilerBackend.h>
#include <thread>
#endif

namespace BECore::Tests {

#if BE_PROFILER_TRACY

    // The layout of ProfileSite against Tracy's source location is checked by static_asserts in TracyProfilerBackend.cpp,
    // the one file that sees Tracy's headers
    TEST(TracyBackendTest, StartsRecordsWithoutAViewerAndShutsDown) {
        TracyProfilerBackend& tracy = GetTracyBackend();
        ASSERT_TRUE(Profiler::SetBackend(&tracy));
        EXPECT_TRUE(TracyProfilerBackend::IsRunning());
        EXPECT_EQ(Profiler::GetBackend(), &tracy);

        BE_PROFILE_THREAD("Tracy test");
        for (int frame = 0; frame < 3; ++frame) {
            BE_PROFILE_SCOPE("Tracy frame");
            {
                BE_PROFILE_SCOPE("Tracy inner");
                void* block = ::operator new(256);
                ::operator delete(block);
            }
            BE_PROFILE_COUNTER("Tracy value", frame);
            BE_PROFILE_FRAME();
        }
        std::thread worker([] {
            BE_PROFILE_THREAD("Tracy worker");
            BE_PROFILE_SCOPE("Tracy worker zone");
        });
        worker.join();

        Profiler::SetBackend(nullptr);
        TracyProfilerBackend::Shutdown();
        EXPECT_FALSE(TracyProfilerBackend::IsRunning());
        // A zone after the shutdown is dropped, not a crash; Tracy does not start twice in one process
        static constexpr ProfileSite kSite{"After shutdown", "Test", __FILE__, 1, 0};
        const uint64_t token = tracy.BeginZone(kSite);
        tracy.EndZone(token, kSite, {});
        EXPECT_FALSE(Profiler::SetBackend(&tracy));
        EXPECT_EQ(Profiler::GetBackend(), nullptr);
    }

    TEST(TracyBackendTest, ZonesDeeperThanTheStackAreSkippedAndBalanced) {
        TracyProfilerBackend& tracy = GetTracyBackend();
        if (!Profiler::SetBackend(&tracy)) {
            GTEST_SKIP() << "Tracy was shut down by another test of this process (ctest runs each test alone)";
        }
        static constexpr ProfileSite kSite{"Deep", "Test", __FILE__, 1, 0};
        eastl::vector<uint64_t> tokens;
        for (uint32_t i = 0; i < TracyProfilerBackend::kMaxZoneDepth + 4; ++i) {
            tokens.push_back(tracy.BeginZone(kSite));
        }
        for (auto token = tokens.rbegin(); token != tokens.rend(); ++token) {
            tracy.EndZone(*token, kSite, {});
        }
        const uint64_t again = tracy.BeginZone(kSite);
        EXPECT_EQ(again, 0u);
        tracy.EndZone(again, kSite, {});
        Profiler::SetBackend(nullptr);
        TracyProfilerBackend::Shutdown();
    }

#else

    TEST(TracyBackendTest, NotInThisBuild) {
        EXPECT_FALSE(kProfilerBuild.tracy);
    }

#endif

}  // namespace BECore::Tests
