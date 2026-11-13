#include <BECore/Control/Json.h>
#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/Profile.h>
#include <BECore/Profiler/ProfilerClock.h>
#include <BECore/Profiler/Ring/FrameRingBackend.h>
#include <BECore/Threading/LockGuards.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Threading/SharedMutex.h>
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace BECore::Tests {

#if BE_PROFILER

    namespace {

        constexpr auto kPollLimit = std::chrono::seconds(20);
        constexpr auto kHold = std::chrono::milliseconds(20);
        constexpr double kMinWaitUs = 10000.0;  // the waiter is blocked for about kHold; the margin is for a slow start of the thread
        constexpr uint32_t kTestThresholdUs = 1000;
        constexpr const char* kWaitZoneName = "lock wait: Test lock";

        /// Sets the threshold of lock wait zones for a test and puts the default back after it
        class ScopedThreshold {
        public:
            explicit ScopedThreshold(uint32_t microseconds) {
                Profiler::SetLockWaitThresholdUs(microseconds);
            }
            ~ScopedThreshold() {
                Profiler::SetLockWaitThresholdUs(Profiler::kDefaultLockWaitThresholdUs);
            }
            ScopedThreshold(const ScopedThreshold&) = delete;
            ScopedThreshold& operator=(const ScopedThreshold&) = delete;
        };

        /// Leaves the process with no backend and the ring off, whatever the test did
        class ProfilerStateGuard {
        public:
            ProfilerStateGuard() = default;
            ~ProfilerStateGuard() {
                Profiler::SetRingEnabled(false);
                Profiler::SetBackend(nullptr);
            }
            ProfilerStateGuard(const ProfilerStateGuard&) = delete;
            ProfilerStateGuard& operator=(const ProfilerStateGuard&) = delete;
        };

        bool WaitUntil(const std::atomic<bool>& flag) {
            const auto deadline = std::chrono::steady_clock::now() + kPollLimit;
            while (!flag.load()) {
                if (std::chrono::steady_clock::now() > deadline) {
                    return false;
                }
                std::this_thread::yield();
            }
            return true;
        }

        /**
         * A thread takes a lock with @p acquire (it returns the guard that keeps it) and holds it until the waiting thread is about to
         * call @p wait, then for kHold more, so @p wait blocks for about kHold. Returns the profiler id of the waiting thread.
         */
        template <typename TAcquire, typename TWait>
        uint32_t RunContended(TAcquire acquire, TWait wait) {
            std::atomic<bool> held{false};
            std::atomic<bool> waiterReady{false};
            std::thread holder([&] {
                auto guard = acquire();
                held = true;
                WaitUntil(waiterReady);
                std::this_thread::sleep_for(kHold);
            });
            EXPECT_TRUE(WaitUntil(held));
            uint32_t waiterId = 0;
            std::thread waiter([&] {
                waiterId = Profiler::CurrentThreadId();
                waiterReady = true;
                wait();
            });
            waiter.join();
            holder.join();
            return waiterId;
        }

        /// A wait zone of the JSON trace as the test sees it: the thread, how long, how many zones were open around it
        struct WaitZone {
            int64_t tid = 0;
            double durationUs = 0.0;
            size_t depth = 0;
        };

        /// The zones named @p name of a Chrome trace, matched B to E per thread; also fails the test when a thread's B and E do not nest
        std::vector<WaitZone> FindZones(const eastl::string& trace, const char* name) {
            std::vector<WaitZone> found;
            auto parsed = ParseJson(trace);
            EXPECT_TRUE(parsed.has_value());
            const Json* events = parsed ? FindJsonMember(*parsed, "traceEvents") : nullptr;
            if (events == nullptr) {
                return found;
            }
            struct Open {
                bool wanted;
                double begin;
            };
            std::map<int64_t, std::vector<Open>> open;
            for (const Json& event : *events) {
                const auto phase = GetJsonString(event, "ph");
                if (!phase || (*phase != "B" && *phase != "E")) {
                    continue;
                }
                const int64_t tid = GetJsonInt(event, "tid").value_or(-1);
                const double ts = GetJsonNumber(event, "ts").value_or(-1.0);
                std::vector<Open>& stack = open[tid];
                if (*phase == "B") {
                    const auto eventName = GetJsonString(event, "name");
                    stack.push_back({eventName && *eventName == name, ts});
                } else {
                    EXPECT_FALSE(stack.empty()) << "an E without its B on thread " << tid;
                    if (stack.empty()) {
                        continue;
                    }
                    const Open top = stack.back();
                    stack.pop_back();
                    if (top.wanted) {
                        found.push_back({tid, (ts - top.begin), stack.size()});
                    }
                }
            }
            for (const auto& [tid, stack] : open) {
                EXPECT_TRUE(stack.empty()) << "zones left open on thread " << tid;
            }
            return found;
        }

        const WaitZone* FindOnThread(const std::vector<WaitZone>& zones, uint32_t threadId) {
            for (const WaitZone& zone : zones) {
                if (zone.tid == static_cast<int64_t>(threadId)) {
                    return &zone;
                }
            }
            return nullptr;
        }

        size_t CountRingZones(const FrameRingBackend& ring, const char* name, uint32_t threadId, uint32_t* depth = nullptr, uint64_t* ticks = nullptr) {
            size_t count = 0;
            for (const RingThread& thread : ring.Snapshot().threads) {
                for (const ZoneRecord& zone : thread.zones) {
                    if (thread.threadId == threadId && std::string(zone.site->name) == name) {
                        ++count;
                        if (depth != nullptr) {
                            *depth = zone.depth;
                        }
                        if (ticks != nullptr) {
                            *ticks = zone.endTicks - zone.beginTicks;
                        }
                    }
                }
            }
            return count;
        }

    }  // namespace

    TEST(LockProfileTest, TheDefaultThresholdIsFiftyMicroseconds) {
        EXPECT_EQ(Profiler::GetLockWaitThresholdUs(), 50u);
        {
            const ScopedThreshold threshold(7);
            EXPECT_EQ(Profiler::GetLockWaitThresholdUs(), 7u);
        }
        EXPECT_EQ(Profiler::GetLockWaitThresholdUs(), 50u);
    }

    TEST(LockProfileTest, AContendedMutexRecordsALockWaitZoneOnTheWaitingThread) {
        const ProfilerStateGuard guard;
        const ScopedThreshold threshold(kTestThresholdUs);
        ChromeTraceBackend recorder;
        ASSERT_TRUE(Profiler::SetBackend(&recorder));

        Mutex mutex{BE_LOCK_SITE("Test lock")};
        const uint32_t waiterId = RunContended([&] { return std::unique_lock<Mutex>(mutex); }, [&] { std::lock_guard<Mutex> lock(mutex); });

        const std::vector<WaitZone> zones = FindZones(recorder.WriteJson(), kWaitZoneName);
        ASSERT_EQ(zones.size(), 1u);
        const WaitZone* zone = FindOnThread(zones, waiterId);
        ASSERT_NE(zone, nullptr);
        EXPECT_GE(zone->durationUs, kMinWaitUs);
        EXPECT_EQ(zone->depth, 0u);
        EXPECT_EQ(recorder.GetDroppedCount(), 0u);
    }

    TEST(LockProfileTest, TheWaitZoneNestsInsideTheZonesTheWaiterHasOpen) {
        const ProfilerStateGuard guard;
        const ScopedThreshold threshold(kTestThresholdUs);
        ChromeTraceBackend recorder;
        ASSERT_TRUE(Profiler::SetBackend(&recorder));

        Mutex mutex{BE_LOCK_SITE("Test lock")};
        const uint32_t waiterId = RunContended([&] { return std::unique_lock<Mutex>(mutex); },
                                               [&] {
                                                   BE_PROFILE_SCOPE("Outer work");
                                                   std::lock_guard<Mutex> lock(mutex);
                                               });

        const std::vector<WaitZone> zones = FindZones(recorder.WriteJson(), kWaitZoneName);
        ASSERT_EQ(zones.size(), 1u);
        EXPECT_EQ(zones[0].tid, static_cast<int64_t>(waiterId));
        EXPECT_EQ(zones[0].depth, 1u);
        const std::vector<WaitZone> outer = FindZones(recorder.WriteJson(), "Outer work");
        ASSERT_EQ(outer.size(), 1u);
        EXPECT_GE(outer[0].durationUs, zones[0].durationUs);
    }

    TEST(LockProfileTest, AnUncontendedLockRecordsNothing) {
        const ProfilerStateGuard guard;
        const ScopedThreshold threshold(0);  // even a zero threshold: nothing waited, nothing recorded
        ChromeTraceBackend recorder;
        ASSERT_TRUE(Profiler::SetBackend(&recorder));

        Mutex mutex{BE_LOCK_SITE("Test lock")};
        SharedMutex shared{BE_LOCK_SITE("Test lock")};
        for (int i = 0; i < 100; ++i) {
            const std::lock_guard<Mutex> lock(mutex);
            const std::shared_lock<SharedMutex> reader(shared);
        }
        {
            const std::lock_guard<SharedMutex> writer(shared);
        }

        EXPECT_TRUE(FindZones(recorder.WriteJson(), kWaitZoneName).empty());
        EXPECT_EQ(recorder.WriteJson().find("lock wait"), eastl::string::npos);
    }

    TEST(LockProfileTest, AWaitBelowTheThresholdRecordsNothing) {
        const ProfilerStateGuard guard;
        const ScopedThreshold threshold(60U * 1000U * 1000U);  // a minute
        ChromeTraceBackend recorder;
        ASSERT_TRUE(Profiler::SetBackend(&recorder));

        Mutex mutex{BE_LOCK_SITE("Test lock")};
        RunContended([&] { return std::unique_lock<Mutex>(mutex); }, [&] { std::lock_guard<Mutex> lock(mutex); });

        EXPECT_TRUE(FindZones(recorder.WriteJson(), kWaitZoneName).empty());
    }

    TEST(LockProfileTest, WithNoBackendAContendedLockJustWorks) {
        const ProfilerStateGuard guard;
        const ScopedThreshold threshold(0);
        ASSERT_EQ(Profiler::GetBackend(), nullptr);

        Mutex mutex{BE_LOCK_SITE("Test lock")};
        int counter = 0;
        RunContended([&] { return std::unique_lock<Mutex>(mutex); },
                     [&] {
                         std::lock_guard<Mutex> lock(mutex);
                         ++counter;
                     });
        EXPECT_EQ(counter, 1);
    }

    TEST(LockProfileTest, AWriterWaitingForReadersAndAReaderWaitingForAWriterAreRecorded) {
        const ProfilerStateGuard guard;
        const ScopedThreshold threshold(kTestThresholdUs);
        ChromeTraceBackend recorder;
        ASSERT_TRUE(Profiler::SetBackend(&recorder));

        SharedMutex mutex{BE_LOCK_SITE("Test lock")};
        RunContended([&] { return std::shared_lock<SharedMutex>(mutex); }, [&] { std::lock_guard<SharedMutex> lock(mutex); });
        RunContended([&] { return std::unique_lock<SharedMutex>(mutex); }, [&] { std::shared_lock<SharedMutex> lock(mutex); });

        // Not matched to thread ids: the recorder finds a thread's buffer by std::thread::id, which a later thread may reuse
        const std::vector<WaitZone> zones = FindZones(recorder.WriteJson(), kWaitZoneName);
        ASSERT_EQ(zones.size(), 2u);
        for (const WaitZone& zone : zones) {
            EXPECT_GE(zone.durationUs, kMinWaitUs);
        }
    }

    TEST(LockProfileTest, TwoReadersDoNotWaitForEachOther) {
        const ProfilerStateGuard guard;
        const ScopedThreshold threshold(0);
        ChromeTraceBackend recorder;
        ASSERT_TRUE(Profiler::SetBackend(&recorder));

        SharedMutex mutex{BE_LOCK_SITE("Test lock")};
        RunContended([&] { return std::shared_lock<SharedMutex>(mutex); }, [&] { std::shared_lock<SharedMutex> lock(mutex); });

        EXPECT_TRUE(FindZones(recorder.WriteJson(), kWaitZoneName).empty());
    }

    TEST(LockProfileTest, TheRingHoldsTheWaitZone) {
        const ProfilerStateGuard guard;
        const ScopedThreshold threshold(kTestThresholdUs);
        FrameRingBackend ring;
        ASSERT_TRUE(Profiler::SetBackend(&ring));

        Mutex mutex{BE_LOCK_SITE("Test lock")};
        uint32_t depth = 99;
        uint64_t ticks = 0;
        const uint32_t waiterId = RunContended([&] { return std::unique_lock<Mutex>(mutex); }, [&] { std::lock_guard<Mutex> lock(mutex); });

        ASSERT_EQ(CountRingZones(ring, kWaitZoneName, waiterId, &depth, &ticks), 1u);
        EXPECT_EQ(depth, 0u);
        EXPECT_GE(ProfilerClock::ToNanoseconds(ticks) / 1000.0, kMinWaitUs);
    }

    TEST(LockProfileTest, TheRingKeepsTheOpenZonesOfTheWaiterIntact) {
        const ProfilerStateGuard guard;
        const ScopedThreshold threshold(kTestThresholdUs);
        FrameRingBackend ring;
        ASSERT_TRUE(Profiler::SetBackend(&ring));

        Mutex mutex{BE_LOCK_SITE("Test lock")};
        const uint32_t waiterId = RunContended([&] { return std::unique_lock<Mutex>(mutex); },
                                               [&] {
                                                   BE_PROFILE_SCOPE("Outer work");
                                                   std::lock_guard<Mutex> lock(mutex);
                                               });

        uint32_t depth = 99;
        EXPECT_EQ(CountRingZones(ring, kWaitZoneName, waiterId, &depth), 1u);
        EXPECT_EQ(depth, 1u);
        uint32_t outerDepth = 99;
        EXPECT_EQ(CountRingZones(ring, "Outer work", waiterId, &outerDepth), 1u);
        EXPECT_EQ(outerDepth, 0u);
    }

    TEST(LockProfileTest, TheZoneReachesBothSidesOfAFanOut) {
        const ProfilerStateGuard guard;
        const ScopedThreshold threshold(kTestThresholdUs);
        ChromeTraceBackend recorder;
        ASSERT_TRUE(Profiler::SetBackend(&recorder));
        Profiler::SetRingEnabled(true);
        ASSERT_NE(Profiler::GetBackend(), &recorder);

        Mutex mutex{BE_LOCK_SITE("Test lock")};
        const uint32_t waiterId = RunContended([&] { return std::unique_lock<Mutex>(mutex); }, [&] { std::lock_guard<Mutex> lock(mutex); });

        EXPECT_NE(FindOnThread(FindZones(recorder.WriteJson(), kWaitZoneName), waiterId), nullptr);
        EXPECT_EQ(CountRingZones(Profiler::GetFrameRing(), kWaitZoneName, waiterId), 1u);
    }

    TEST(LockProfileTest, TheTraceStaysBalancedWithFourThreadsFightingOverALock) {
        const ProfilerStateGuard guard;
        const ScopedThreshold threshold(0);
        ChromeTraceBackend recorder;
        ASSERT_TRUE(Profiler::SetBackend(&recorder));

        Mutex mutex{BE_LOCK_SITE("Test lock")};
        int counter = 0;
        constexpr int kThreads = 4;
        constexpr int kRounds = 2000;
        std::vector<std::thread> threads;
        for (int i = 0; i < kThreads; ++i) {
            threads.emplace_back([&] {
                for (int n = 0; n < kRounds; ++n) {
                    BE_PROFILE_SCOPE("Round");
                    std::lock_guard<Mutex> lock(mutex);
                    ++counter;
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        EXPECT_EQ(counter, kThreads * kRounds);
        const eastl::string trace = recorder.WriteJson();
        FindZones(trace, kWaitZoneName);  // checks the nesting of every thread
    }

#endif

}  // namespace BECore::Tests
