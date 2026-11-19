#include <BECore/Control/Json.h>
#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/ProfilerClock.h>
#include <BECore/Profiler/Ring/FrameRingBackend.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Threading/SharedMutex.h>
#include <Common/Wait.h>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <pthread.h>
#endif

namespace BECore::Tests {

#if BE_PROFILER

    namespace {

        /// How long the holder keeps the lock once the waiter is blocked on it: above kTestThresholdUs, so the wait is long enough to record
        constexpr auto kHoldWhileBlocked = std::chrono::milliseconds(20);
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

        /// Leaves the process with no backend and the ring off, whatever the test did. It stops the
        /// primary backend, so declare it after every backend the test installs (locals die in reverse)
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

        /**
         * Tells whether a thread is blocked in the system, so that a test can release a lock only once the waiter sits in it. The waiter
         * calls Register() on itself first. Without a probe of the platform (only macOS has one) IsBlocked() is true kFallbackPause after
         * Register(): there a waiter descheduled for longer than that before its lock() is not contended, and the test fails.
         */
        class BlockedThreadProbe {
        public:
            void Register() {
#if defined(__APPLE__)
                _thread = pthread_mach_thread_np(pthread_self());
#endif
                _registeredAt = std::chrono::steady_clock::now();
                _registered = true;
            }

            [[nodiscard]] bool IsBlocked() const {
                if (!_registered) {
                    return false;
                }
#if defined(__APPLE__)
                thread_basic_info_data_t info{};
                mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
                if (thread_info(_thread, THREAD_BASIC_INFO, reinterpret_cast<thread_info_t>(&info), &count) != KERN_SUCCESS) {
                    return false;
                }
                return info.run_state == TH_STATE_WAITING;
#else
                return std::chrono::steady_clock::now() - _registeredAt >= kFallbackPause;
#endif
            }

        private:
            static constexpr auto kFallbackPause = std::chrono::milliseconds(200);

            std::atomic<bool> _registered{false};
            std::chrono::steady_clock::time_point _registeredAt;
#if defined(__APPLE__)
            mach_port_t _thread = MACH_PORT_NULL;
#endif
        };

        /**
         * A thread takes a lock with @p acquire (it returns the guard that keeps it) and holds it until the waiting thread is blocked in
         * @p wait, then kHoldWhileBlocked more, so a wait of the waiter is at least that long whatever the load of the machine. A @p wait
         * that does not block (a second reader) ends on its own and the holder lets go after it. Returns the profiler id of the waiter.
         */
        template <typename TAcquire, typename TWait>
        uint32_t RunContended(TAcquire acquire, TWait wait) {
            std::atomic<bool> held{false};
            std::atomic<bool> release{false};
            std::thread holder([&] {
                auto guard = acquire();
                held = true;
                WaitUntil([&] { return release.load(); });
                std::this_thread::sleep_for(kHoldWhileBlocked);
            });
            EXPECT_TRUE(WaitUntil([&] { return held.load(); }));
            uint32_t waiterId = 0;
            BlockedThreadProbe probe;
            std::atomic<bool> waited{false};
            std::thread waiter([&] {
                waiterId = Profiler::CurrentThreadId();
                probe.Register();
                wait();
                waited = true;
            });
            EXPECT_TRUE(WaitUntil([&] { return waited.load() || probe.IsBlocked(); })) << "the waiter neither blocked nor finished";
            release = true;
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
        Vector<WaitZone> FindZones(const String& trace, const char* name) {
            Vector<WaitZone> found;
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
            Map<int64_t, Vector<Open>> open;
            for (const Json& event : *events) {
                const auto phase = GetJsonString(event, "ph");
                if (!phase || (*phase != "B" && *phase != "E")) {
                    continue;
                }
                const int64_t tid = GetJsonInt(event, "tid").value_or(-1);
                const double ts = GetJsonNumber(event, "ts").value_or(-1.0);
                Vector<Open>& stack = open[tid];
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

        const WaitZone* FindOnThread(const Vector<WaitZone>& zones, uint32_t threadId) {
            for (const WaitZone& zone : zones) {
                if (zone.tid == static_cast<int64_t>(threadId)) {
                    return &zone;
                }
            }
            return nullptr;
        }

        /// Any thread, for CountRingZones on the process's ring
        constexpr uint32_t kAnyThread = 0;

        size_t CountRingZones(const FrameRingBackend& ring, const char* name, uint32_t threadId, uint32_t* depth = nullptr, uint64_t* ticks = nullptr) {
            size_t count = 0;
            for (const RingThread& thread : ring.Snapshot().threads) {
                for (const ZoneRecord& zone : thread.zones) {
                    if ((threadId == kAnyThread || thread.threadId == threadId) && String(zone.site->name) == name) {
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

        /// A backend that wants the events of every lock operation and counts them. Never destroyed: a lock may still reach it (Profiler keeps
        /// the backend that made the contexts alive to end them)
        class CountingLockBackend : public IProfilerBackend {
        public:
            uint64_t BeginZone(const ProfileSite& /*site*/) override {
                return 0;
            }
            void EndZone(uint64_t /*token*/, const ProfileSite& /*site*/, const ZoneAllocs& /*allocs*/) override {}
            void FrameMark() override {}
            void Counter(const char* /*name*/, double /*value*/) override {}
            void SetThreadName(const char* /*name*/) override {}

            [[nodiscard]] bool WantsLockEvents() const override {
                return true;
            }
            void* AnnounceLock(const ProfileSite& site, LockKind kind) override {
                announced.fetch_add(1);
                lastName = site.function;
                lastAnnounceKind = kind;
                return new int(0);
            }
            void TerminateLock(void* context, LockKind /*kind*/) override {
                terminated.fetch_add(1);
                delete static_cast<int*>(context);
            }
            bool BeforeLock(void* /*context*/, LockKind kind) override {
                (kind == LockKind::SharedReader ? readerBefore : writerBefore).fetch_add(1);
                return true;
            }
            void AfterLock(void* /*context*/, LockKind kind) override {
                (kind == LockKind::SharedReader ? readerAfter : writerAfter).fetch_add(1);
            }
            void BeforeUnlock(void* /*context*/, LockKind kind) override {
                (kind == LockKind::SharedReader ? readerUnlock : writerUnlock).fetch_add(1);
            }
            void AfterTryLock(void* /*context*/, LockKind /*kind*/, bool acquired) override {
                (acquired ? tryAcquired : tryFailed).fetch_add(1);
            }

            void Reset() {
                for (std::atomic<int>* counter : {&announced, &terminated, &writerBefore, &writerAfter, &writerUnlock, &readerBefore, &readerAfter, &readerUnlock, &tryAcquired, &tryFailed}) {
                    counter->store(0);
                }
            }
            [[nodiscard]] int Live() const {
                return announced.load() - terminated.load();
            }

            std::atomic<int> announced{0};
            std::atomic<int> terminated{0};
            std::atomic<int> writerBefore{0};
            std::atomic<int> writerAfter{0};
            std::atomic<int> writerUnlock{0};
            std::atomic<int> readerBefore{0};
            std::atomic<int> readerAfter{0};
            std::atomic<int> readerUnlock{0};
            std::atomic<int> tryAcquired{0};
            std::atomic<int> tryFailed{0};
            String lastName;
            LockKind lastAnnounceKind = LockKind::Mutex;
        };

        CountingLockBackend& GetCountingBackend() {
            static CountingLockBackend* const backend = new CountingLockBackend();
            backend->Reset();
            return *backend;
        }

    }  // namespace

    TEST(LockProfileTest, AScopedThresholdRestoresTheDefaultOnExit) {
        EXPECT_EQ(Profiler::GetLockWaitThresholdUs(), Profiler::kDefaultLockWaitThresholdUs);
        {
            const ScopedThreshold threshold(7);
            EXPECT_EQ(Profiler::GetLockWaitThresholdUs(), 7u);
        }
        EXPECT_EQ(Profiler::GetLockWaitThresholdUs(), Profiler::kDefaultLockWaitThresholdUs);
    }

    TEST(LockProfileTest, AContendedMutexRecordsALockWaitZoneOnTheWaitingThread) {
        const ScopedThreshold threshold(kTestThresholdUs);
        ChromeTraceBackend recorder;
        const ProfilerStateGuard guard;
        ASSERT_TRUE(Profiler::SetBackend(&recorder));

        Mutex mutex{BE_LOCK_SITE("Test lock")};
        const uint32_t waiterId = RunContended([&] { return std::unique_lock<Mutex>(mutex); }, [&] { std::lock_guard<Mutex> lock(mutex); });

        const Vector<WaitZone> zones = FindZones(recorder.WriteJson(), kWaitZoneName);
        ASSERT_EQ(zones.size(), 1u);
        const WaitZone* zone = FindOnThread(zones, waiterId);
        ASSERT_NE(zone, nullptr);
        EXPECT_EQ(zone->depth, 0u);
        EXPECT_EQ(recorder.GetDroppedCount(), 0u);
    }

    TEST(LockProfileTest, TheWaitZoneNestsInsideTheZonesTheWaiterHasOpen) {
        const ScopedThreshold threshold(kTestThresholdUs);
        ChromeTraceBackend recorder;
        const ProfilerStateGuard guard;
        ASSERT_TRUE(Profiler::SetBackend(&recorder));

        Mutex mutex{BE_LOCK_SITE("Test lock")};
        const uint32_t waiterId = RunContended([&] { return std::unique_lock<Mutex>(mutex); },
                                               [&] {
                                                   BE_PROFILE_SCOPE("Outer work");
                                                   std::lock_guard<Mutex> lock(mutex);
                                               });

        const Vector<WaitZone> zones = FindZones(recorder.WriteJson(), kWaitZoneName);
        ASSERT_EQ(zones.size(), 1u);
        EXPECT_EQ(zones[0].tid, static_cast<int64_t>(waiterId));
        EXPECT_EQ(zones[0].depth, 1u);
        const Vector<WaitZone> outer = FindZones(recorder.WriteJson(), "Outer work");
        ASSERT_EQ(outer.size(), 1u);
        EXPECT_GE(outer[0].durationUs, zones[0].durationUs);
    }

    TEST(LockProfileTest, AnUncontendedLockRecordsNothing) {
        const ScopedThreshold threshold(0);  // even a zero threshold: nothing waited, nothing recorded
        ChromeTraceBackend recorder;
        const ProfilerStateGuard guard;
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
        EXPECT_EQ(recorder.WriteJson().find("lock wait"), String::npos);
    }

    TEST(LockProfileTest, AWaitBelowTheThresholdRecordsNothing) {
        const ScopedThreshold threshold(60U * 1000U * 1000U);  // a minute
        ChromeTraceBackend recorder;
        const ProfilerStateGuard guard;
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
        const ScopedThreshold threshold(kTestThresholdUs);
        ChromeTraceBackend recorder;
        const ProfilerStateGuard guard;
        ASSERT_TRUE(Profiler::SetBackend(&recorder));

        SharedMutex mutex{BE_LOCK_SITE("Test lock")};
        RunContended([&] { return std::shared_lock<SharedMutex>(mutex); }, [&] { std::lock_guard<SharedMutex> lock(mutex); });
        RunContended([&] { return std::unique_lock<SharedMutex>(mutex); }, [&] { std::shared_lock<SharedMutex> lock(mutex); });

        // Not matched to thread ids: the recorder finds a thread's buffer by std::thread::id, which a later thread may reuse
        const Vector<WaitZone> zones = FindZones(recorder.WriteJson(), kWaitZoneName);
        EXPECT_EQ(zones.size(), 2u);
    }

    TEST(LockProfileTest, TwoReadersDoNotWaitForEachOther) {
        const ScopedThreshold threshold(0);
        ChromeTraceBackend recorder;
        const ProfilerStateGuard guard;
        ASSERT_TRUE(Profiler::SetBackend(&recorder));

        SharedMutex mutex{BE_LOCK_SITE("Test lock")};
        RunContended([&] { return std::shared_lock<SharedMutex>(mutex); }, [&] { std::shared_lock<SharedMutex> lock(mutex); });

        EXPECT_TRUE(FindZones(recorder.WriteJson(), kWaitZoneName).empty());
    }

    TEST(LockProfileTest, TheRingHoldsTheWaitZone) {
        const ScopedThreshold threshold(kTestThresholdUs);
        FrameRingBackend ring;
        const ProfilerStateGuard guard;
        ASSERT_TRUE(Profiler::SetBackend(&ring));

        Mutex mutex{BE_LOCK_SITE("Test lock")};
        uint32_t depth = 99;
        const uint32_t waiterId = RunContended([&] { return std::unique_lock<Mutex>(mutex); }, [&] { std::lock_guard<Mutex> lock(mutex); });

        ASSERT_EQ(CountRingZones(ring, kWaitZoneName, waiterId, &depth), 1u);
        EXPECT_EQ(depth, 0u);
    }

    TEST(LockProfileTest, TheRingKeepsTheOpenZonesOfTheWaiterIntact) {
        const ScopedThreshold threshold(kTestThresholdUs);
        FrameRingBackend ring;
        const ProfilerStateGuard guard;
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
        const ScopedThreshold threshold(kTestThresholdUs);
        ChromeTraceBackend recorder;
        const ProfilerStateGuard guard;
        ASSERT_TRUE(Profiler::SetBackend(&recorder));
        Profiler::SetRingEnabled(true);
        ASSERT_NE(Profiler::GetBackend(), &recorder);

        Mutex mutex{BE_LOCK_SITE("Test lock")};
        const uint32_t waiterId = RunContended([&] { return std::unique_lock<Mutex>(mutex); }, [&] { std::lock_guard<Mutex> lock(mutex); });

        EXPECT_NE(FindOnThread(FindZones(recorder.WriteJson(), kWaitZoneName), waiterId), nullptr);
        // The process's ring outlives the case: a waiter whose std::thread::id an earlier thread had writes into that thread's buffer, under its id.
        // The ring holds only what came after SetRingEnabled(true), so the one wait zone of this case is the one on any thread
        EXPECT_EQ(CountRingZones(Profiler::GetFrameRing(), kWaitZoneName, kAnyThread), 1u);
    }

    TEST(LockProfileTest, TheTraceStaysBalancedWithFourThreadsFightingOverALock) {
        const ScopedThreshold threshold(0);
        ChromeTraceBackend recorder;
        const ProfilerStateGuard guard;
        ASSERT_TRUE(Profiler::SetBackend(&recorder));

        Mutex mutex{BE_LOCK_SITE("Test lock")};
        int counter = 0;
        constexpr int kThreads = 4;
        constexpr int kRounds = 2000;
        Vector<std::thread> threads;
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
        const String trace = recorder.WriteJson();
        FindZones(trace, kWaitZoneName);  // checks the nesting of every thread
    }

    // ---- The events of every lock operation (Tracy's lockables), through a counting backend ----

    TEST(LockProfileTest, OnlyABackendThatAsksGetsTheEventsOfEveryLockOperation) {
        ChromeTraceBackend recorder;
        const ProfilerStateGuard guard;
        ASSERT_TRUE(Profiler::SetBackend(&recorder));
        EXPECT_FALSE(LockHooks::EventsActive());
        Profiler::SetRingEnabled(true);
        EXPECT_FALSE(LockHooks::EventsActive());

        CountingLockBackend& counting = GetCountingBackend();
        ASSERT_TRUE(Profiler::SetBackend(&counting));
        EXPECT_TRUE(LockHooks::EventsActive());
        Profiler::SetBackend(nullptr);
        Profiler::SetRingEnabled(false);
        EXPECT_FALSE(LockHooks::EventsActive());
    }

    TEST(LockProfileTest, EveryOperationOfBothLocksReachesTheBackendWithItsKind) {
        const ProfilerStateGuard guard;
        CountingLockBackend& counting = GetCountingBackend();
        ASSERT_TRUE(Profiler::SetBackend(&counting));
        {
            Mutex mutex{BE_LOCK_SITE("Test lock")};
            {
                const std::lock_guard<Mutex> lock(mutex);
            }
            EXPECT_EQ(counting.announced.load(), 1);
            EXPECT_EQ(counting.lastName, "Test lock");
            EXPECT_EQ(counting.lastAnnounceKind, LockKind::Mutex);
            EXPECT_EQ(counting.writerBefore.load(), 1);
            EXPECT_EQ(counting.writerAfter.load(), 1);
            EXPECT_EQ(counting.writerUnlock.load(), 1);

            ASSERT_TRUE(mutex.try_lock());
            bool otherGotIn = true;
            std::thread other([&] { otherGotIn = mutex.try_lock(); });
            other.join();
            EXPECT_FALSE(otherGotIn);
            mutex.unlock();
            EXPECT_EQ(counting.tryAcquired.load(), 1);
            EXPECT_EQ(counting.tryFailed.load(), 1);
            EXPECT_EQ(counting.writerUnlock.load(), 2);
        }
        EXPECT_EQ(counting.Live(), 0);
    }

    TEST(LockProfileTest, ASharedMutexReportsItsWriterAndReaderSidesAndItsTryLocks) {
        const ProfilerStateGuard guard;
        CountingLockBackend& counting = GetCountingBackend();
        ASSERT_TRUE(Profiler::SetBackend(&counting));
        {
            SharedMutex mutex{BE_LOCK_SITE("Test lock")};
            {
                const std::lock_guard<SharedMutex> writer(mutex);
            }
            {
                const std::shared_lock<SharedMutex> reader(mutex);
            }
            EXPECT_EQ(counting.lastAnnounceKind, LockKind::SharedWriter);
            EXPECT_EQ(counting.writerBefore.load(), 1);
            EXPECT_EQ(counting.writerAfter.load(), 1);
            EXPECT_EQ(counting.writerUnlock.load(), 1);
            EXPECT_EQ(counting.readerBefore.load(), 1);
            EXPECT_EQ(counting.readerAfter.load(), 1);
            EXPECT_EQ(counting.readerUnlock.load(), 1);

            ASSERT_TRUE(mutex.try_lock_shared());
            bool writerGotIn = true;
            std::thread other([&] { writerGotIn = mutex.try_lock(); });
            other.join();
            EXPECT_FALSE(writerGotIn);
            mutex.unlock_shared();
            EXPECT_EQ(counting.tryAcquired.load(), 1);
            EXPECT_EQ(counting.tryFailed.load(), 1);
            EXPECT_EQ(counting.announced.load(), 1);
        }
        EXPECT_EQ(counting.Live(), 0);
    }

    TEST(LockProfileTest, TheContextIsMadeLazilyAndExactlyOnceUnderARace) {
        const ProfilerStateGuard guard;
        CountingLockBackend& counting = GetCountingBackend();
        constexpr int kThreads = 8;
        constexpr int kRounds = 200;
        for (int round = 0; round < kRounds; ++round) {
            counting.Reset();
            {
                Mutex early{BE_LOCK_SITE("Test lock")};
                ASSERT_EQ(Profiler::GetBackend(), nullptr);
                {
                    const std::lock_guard<Mutex> lock(early);  // used while no backend wants events: no context
                }
                ASSERT_EQ(counting.announced.load(), 0);
                ASSERT_TRUE(Profiler::SetBackend(&counting));

                Mutex raced{BE_LOCK_SITE("Test lock")};
                std::atomic<bool> go{false};
                Vector<std::thread> threads;
                for (int i = 0; i < kThreads; ++i) {
                    threads.emplace_back([&] {
                        while (!go.load()) {
                            std::this_thread::yield();
                        }
                        const std::lock_guard<Mutex> lock(raced);
                    });
                }
                go = true;
                for (std::thread& thread : threads) {
                    thread.join();
                }
                EXPECT_EQ(counting.Live(), 1) << "round " << round;  // the threads that lost the race ended their own context
                {
                    const std::lock_guard<Mutex> lock(early);  // its first use under the backend
                }
                EXPECT_EQ(counting.Live(), 2) << "round " << round;
                Profiler::SetBackend(nullptr);
            }
            EXPECT_EQ(counting.Live(), 0) << "round " << round;  // both locks ended their contexts after the backend was removed
        }
    }

    TEST(LockProfileTest, ALockThatOutlivesItsBackendEndsItsContextAndStaysUsable) {
        const ProfilerStateGuard guard;
        CountingLockBackend& counting = GetCountingBackend();
        ASSERT_TRUE(Profiler::SetBackend(&counting));
        int value = 0;
        {
            Mutex mutex{BE_LOCK_SITE("Test lock")};
            {
                const std::lock_guard<Mutex> lock(mutex);
            }
            ASSERT_EQ(counting.Live(), 1);
            Profiler::SetBackend(nullptr);
            {
                const std::lock_guard<Mutex> lock(mutex);  // no events now
                ++value;
            }
            EXPECT_EQ(counting.writerBefore.load(), 1);
        }
        EXPECT_EQ(value, 1);
        EXPECT_EQ(counting.Live(), 0);
    }

    TEST(LockProfileTest, AMutexHeldWhileTheBackendChangesIsReleasedCleanly) {
        const ProfilerStateGuard guard;
        CountingLockBackend& counting = GetCountingBackend();
        Mutex mutex{BE_LOCK_SITE("Test lock")};
        mutex.lock();  // taken before any backend wants events
        ASSERT_TRUE(Profiler::SetBackend(&counting));
        mutex.unlock();  // no context yet: the backend sees nothing of it
        EXPECT_EQ(counting.writerUnlock.load(), 0);
        {
            const std::lock_guard<Mutex> lock(mutex);
        }
        EXPECT_EQ(counting.writerBefore.load(), 1);
        EXPECT_EQ(counting.writerUnlock.load(), 1);
    }

#endif

}  // namespace BECore::Tests
