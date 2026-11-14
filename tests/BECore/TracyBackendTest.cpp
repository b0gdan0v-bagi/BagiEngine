#include <BECore/Profiler/ProfilerSettings.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Threading/SharedMutex.h>
#include <gtest/gtest.h>

#if BE_PROFILER_TRACY
#include <BECore/Profiler/Backends/TracyProfilerBackend.h>
#include <thread>
#include <vector>
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
        Vector<uint64_t> tokens;
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

    namespace {

        /// Locks, shared-locks, try-locks and contends a lock of each kind from four threads; the counters tell the locks held up
        void UseBothLocks(Mutex& mutex, SharedMutex& shared) {
            constexpr int kThreads = 4;
            constexpr int kRounds = 500;
            int counter = 0;
            int sharedCounter = 0;
            std::vector<std::thread> threads;
            for (int i = 0; i < kThreads; ++i) {
                threads.emplace_back([&] {
                    for (int n = 0; n < kRounds; ++n) {
                        {
                            const std::lock_guard<Mutex> lock(mutex);
                            ++counter;
                        }
                        {
                            const std::lock_guard<SharedMutex> writer(shared);
                            ++sharedCounter;
                        }
                        {
                            const std::shared_lock<SharedMutex> reader(shared);
                            static_cast<void>(sharedCounter);
                        }
                        if (std::unique_lock<Mutex> attempt(mutex, std::try_to_lock); attempt.owns_lock()) {
                            ++counter;
                        }
                    }
                });
            }
            for (std::thread& thread : threads) {
                thread.join();
            }
            EXPECT_GE(counter, kThreads * kRounds);
            EXPECT_EQ(sharedCounter, kThreads * kRounds);
        }

    }  // namespace

    TEST(TracyBackendTest, LocksOfBothKindsRunUnderTracyAndAfterItsShutdown) {
        Mutex outlives{BE_LOCK_SITE("Tracy test outlives")};  // made before Tracy runs
        SharedMutex sharedOutlives{BE_LOCK_SITE("Tracy test shared outlives")};
        {
            const std::lock_guard<Mutex> lock(outlives);  // used before Tracy runs: never announced
        }

        TracyProfilerBackend& tracy = GetTracyBackend();
        ASSERT_TRUE(Profiler::SetBackend(&tracy));
        {
            Mutex mutex{BE_LOCK_SITE("Tracy test lock")};
            SharedMutex shared{BE_LOCK_SITE("Tracy test shared lock")};
            UseBothLocks(mutex, shared);
            UseBothLocks(outlives, sharedOutlives);  // a lock made before the backend started works after
        }  // destroyed with Tracy running: their contexts end

        Profiler::SetBackend(nullptr);
        {
            Mutex between{BE_LOCK_SITE("Tracy test between")};
            SharedMutex sharedBetween{BE_LOCK_SITE("Tracy test shared between")};
            UseBothLocks(between, sharedBetween);  // Tracy runs but is not the backend
            UseBothLocks(outlives, sharedOutlives);
        }
        ASSERT_TRUE(Profiler::SetBackend(&tracy));
        UseBothLocks(outlives, sharedOutlives);  // installed again: the context made earlier is used again
        Profiler::SetBackend(nullptr);
        TracyProfilerBackend::Shutdown();

        // Tracy is gone: the locks work, and ending their contexts at the end of the test is a no-op
        UseBothLocks(outlives, sharedOutlives);
    }

    TEST(TracyBackendTest, ALockEndedAfterTheShutdownIsSafe) {
        TracyProfilerBackend& tracy = GetTracyBackend();
        ASSERT_TRUE(Profiler::SetBackend(&tracy));
        auto* mutex = new Mutex(BE_LOCK_SITE("Tracy test late end"));
        auto* shared = new SharedMutex(BE_LOCK_SITE("Tracy test shared late end"));
        UseBothLocks(*mutex, *shared);
        Profiler::SetBackend(nullptr);
        TracyProfilerBackend::Shutdown();
        delete mutex;  // its context cannot be ended any more; nothing is called into the stopped client
        delete shared;
    }

    TEST(TracyBackendTest, TheTracyBackendAsksForLockEventsAndTheFanOutPassesThemOn) {
        TracyProfilerBackend& tracy = GetTracyBackend();
        EXPECT_TRUE(tracy.WantsLockEvents());
        ASSERT_TRUE(Profiler::SetBackend(&tracy));
        EXPECT_TRUE(LockHooks::EventsActive());
        Profiler::SetRingEnabled(true);
        EXPECT_NE(Profiler::GetBackend(), &tracy);
        EXPECT_TRUE(Profiler::GetBackend()->WantsLockEvents());
        Mutex mutex{BE_LOCK_SITE("Tracy test fan-out")};
        SharedMutex shared{BE_LOCK_SITE("Tracy test shared fan-out")};
        UseBothLocks(mutex, shared);
        Profiler::SetRingEnabled(false);
        Profiler::SetBackend(nullptr);
        EXPECT_FALSE(LockHooks::EventsActive());
        TracyProfilerBackend::Shutdown();
    }

#else

    TEST(TracyBackendTest, NotInThisBuild) {
        EXPECT_FALSE(kProfilerBuild.tracy);
    }

#endif

}  // namespace BECore::Tests
