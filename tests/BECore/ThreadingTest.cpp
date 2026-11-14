#include <BECore/Profiler/Profiler.h>
#include <BECore/Threading/LockOrder.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Threading/SharedMutex.h>
#include <chrono>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace BECore::Tests {

    namespace {

        constexpr int kContendingThreads = 4;
        constexpr int kIncrementsPerThread = 20000;
        constexpr auto kPollLimit = std::chrono::seconds(20);
        constexpr auto kShortWait = std::chrono::milliseconds(20);

        template <typename TCondition>
        bool WaitUntil(TCondition condition) {
            const auto deadline = std::chrono::steady_clock::now() + kPollLimit;
            while (!condition()) {
                if (std::chrono::steady_clock::now() > deadline) {
                    return false;
                }
                std::this_thread::yield();
            }
            return true;
        }

    }  // namespace

    // ---------------------------------------------------------------- Mutex

    TEST(ThreadingTest, MutexExcludesFourThreads) {
        Mutex mutex{BE_LOCK_SITE("Test counter")};
        int counter = 0;  // not atomic: only the mutex keeps it right

        Vector<std::thread> threads;
        for (int i = 0; i < kContendingThreads; ++i) {
            threads.emplace_back([&] {
                for (int n = 0; n < kIncrementsPerThread; ++n) {
                    std::lock_guard<Mutex> lock(mutex);
                    ++counter;
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        EXPECT_EQ(counter, kContendingThreads * kIncrementsPerThread);
    }

    TEST(ThreadingTest, MutexTryLockFailsWhileHeldByAnotherThread) {
        Mutex mutex{BE_LOCK_SITE("Test try")};
        ASSERT_TRUE(mutex.try_lock());

        bool lockedByOther = true;
        std::thread other([&] {
            lockedByOther = mutex.try_lock();
            if (lockedByOther) {
                mutex.unlock();
            }
        });
        other.join();
        EXPECT_FALSE(lockedByOther);

        mutex.unlock();
        EXPECT_TRUE(mutex.try_lock());
        mutex.unlock();
    }

    TEST(ThreadingTest, GuardsWorkOverTheEngineTypes) {
        Mutex mutex{BE_LOCK_SITE("Test guards")};
        Mutex second{BE_LOCK_SITE("Test guards second")};

        {
            std::lock_guard guard(mutex);  // CTAD
            EXPECT_FALSE(mutex.try_lock());
        }
        {
            LockGuard<Mutex> guard(mutex);
            EXPECT_FALSE(mutex.try_lock());
        }
        {
            std::unique_lock deferred(mutex, std::defer_lock);
            EXPECT_FALSE(deferred.owns_lock());
            deferred.lock();
            EXPECT_TRUE(deferred.owns_lock());
        }
        {
            UniqueLock<Mutex> held(mutex);
            std::unique_lock tried(mutex, std::try_to_lock);
            EXPECT_FALSE(tried.owns_lock());
        }
        {
            std::scoped_lock both(mutex, second);
            EXPECT_FALSE(mutex.try_lock());
            EXPECT_FALSE(second.try_lock());
        }
        EXPECT_TRUE(mutex.try_lock());
        mutex.unlock();
        EXPECT_TRUE(second.try_lock());
        second.unlock();
    }

    // ---------------------------------------------------------------- SharedMutex

    TEST(ThreadingTest, SharedMutexReadersTogetherWriterExcluded) {
        SharedMutex mutex{BE_LOCK_SITE("Test shared")};
        std::atomic<int> readersInside{0};
        std::atomic<bool> release{false};

        Vector<std::thread> readers;
        for (int i = 0; i < kContendingThreads; ++i) {
            readers.emplace_back([&] {
                std::shared_lock<SharedMutex> lock(mutex);
                readersInside.fetch_add(1);
                while (!release.load()) {
                    std::this_thread::yield();
                }
            });
        }
        // All four hold the shared side at once: they could not if readers excluded each other
        ASSERT_TRUE(WaitUntil([&] { return readersInside.load() == kContendingThreads; }));
        EXPECT_FALSE(mutex.try_lock());
        EXPECT_TRUE(mutex.try_lock_shared());
        mutex.unlock_shared();

        release.store(true);
        for (std::thread& reader : readers) {
            reader.join();
        }
        EXPECT_TRUE(mutex.try_lock());
        EXPECT_FALSE(mutex.try_lock_shared());
        mutex.unlock();
    }

    TEST(ThreadingTest, SharedMutexWriterExcludesWritersAndReaders) {
        SharedMutex mutex{BE_LOCK_SITE("Test writer")};
        int counter = 0;

        Vector<std::thread> threads;
        for (int i = 0; i < kContendingThreads; ++i) {
            threads.emplace_back([&] {
                for (int n = 0; n < kIncrementsPerThread; ++n) {
                    std::lock_guard<SharedMutex> lock(mutex);
                    ++counter;
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        EXPECT_EQ(counter, kContendingThreads * kIncrementsPerThread);

        std::lock_guard<SharedMutex> writer(mutex);
        bool readerGotIn = true;
        std::thread reader([&] {
            readerGotIn = mutex.try_lock_shared();
            if (readerGotIn) {
                mutex.unlock_shared();
            }
        });
        reader.join();
        EXPECT_FALSE(readerGotIn);
    }

    TEST(ThreadingTest, SharedLockAliasWorks) {
        SharedMutex mutex{BE_LOCK_SITE("Test shared alias")};
        SharedLock lock(mutex);  // CTAD through the alias
        EXPECT_TRUE(lock.owns_lock());
        EXPECT_FALSE(mutex.try_lock());
    }

    // ---------------------------------------------------------------- ConditionVariable

    TEST(ThreadingTest, ConditionVariableWaitWithPredicateWakesOnNotify) {
        Mutex mutex{BE_LOCK_SITE("Test cv")};
        ConditionVariable changed;
        bool ready = false;
        bool seen = false;

        std::thread waiter([&] {
            std::unique_lock<Mutex> lock(mutex);
            changed.wait(lock, [&] { return ready; });
            seen = ready;
        });

        std::this_thread::sleep_for(kShortWait);
        {
            std::lock_guard<Mutex> lock(mutex);
            ready = true;
        }
        changed.notify_one();
        waiter.join();
        EXPECT_TRUE(seen);
    }

    TEST(ThreadingTest, ConditionVariablePlainWaitReturnsHoldingTheLock) {
        Mutex mutex{BE_LOCK_SITE("Test cv plain")};
        ConditionVariable changed;
        std::atomic<bool> waiting{false};
        std::atomic<bool> woke{false};
        bool ready = false;

        std::thread waiter([&] {
            std::unique_lock<Mutex> lock(mutex);
            waiting.store(true);
            while (!ready) {
                changed.wait(lock);
            }
            EXPECT_TRUE(lock.owns_lock());
            woke.store(true);
        });

        ASSERT_TRUE(WaitUntil([&] { return waiting.load(); }));
        {
            // The waiter released the mutex inside wait(): this lock is only possible then
            std::lock_guard<Mutex> lock(mutex);
            ready = true;
        }
        changed.notify_all();
        waiter.join();
        EXPECT_TRUE(woke.load());
        EXPECT_TRUE(mutex.try_lock());  // and the guard of the waiter gave it back at the end
        mutex.unlock();
    }

    TEST(ThreadingTest, ConditionVariableWaitForTimesOutAndKeepsTheLock) {
        Mutex mutex{BE_LOCK_SITE("Test cv timeout")};
        ConditionVariable changed;

        std::unique_lock<Mutex> lock(mutex);
        EXPECT_EQ(changed.wait_for(lock, std::chrono::milliseconds(5)), std::cv_status::timeout);
        EXPECT_TRUE(lock.owns_lock());
        EXPECT_FALSE(changed.wait_for(lock, std::chrono::milliseconds(5), [] { return false; }));
        EXPECT_TRUE(lock.owns_lock());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5);
        EXPECT_EQ(changed.wait_until(lock, deadline), std::cv_status::timeout);
        EXPECT_FALSE(changed.wait_until(lock, std::chrono::steady_clock::now() + std::chrono::milliseconds(5), [] { return false; }));
        EXPECT_TRUE(lock.owns_lock());
    }

    TEST(ThreadingTest, ConditionVariableWaitForReturnsTrueWhenPredicateHolds) {
        Mutex mutex{BE_LOCK_SITE("Test cv wait_for")};
        ConditionVariable changed;
        bool ready = false;

        std::thread notifier([&] {
            std::this_thread::sleep_for(kShortWait);
            {
                std::lock_guard<Mutex> lock(mutex);
                ready = true;
            }
            changed.notify_one();
        });

        std::unique_lock<Mutex> lock(mutex);
        EXPECT_TRUE(changed.wait_for(lock, kPollLimit, [&] { return ready; }));
        lock.unlock();
        notifier.join();
    }

    // ---------------------------------------------------------------- lock order

#if BE_LOCK_ORDER_CHECK

    namespace {

        constexpr uint32_t kLowLevel = 10;
        constexpr uint32_t kMidLevel = 20;
        constexpr uint32_t kHighLevel = 30;

        /// A failed engine ASSERT is reported by AssertBridge as a non-fatal gtest failure: the lock order message names both locks
        constexpr const char* kLowAfterHigh = "taking 'Order low' (level 10) while holding 'Order high' (level 30)";

    }  // namespace

    TEST(ThreadingTest, LockOrderAscendingPasses) {
        Mutex low{BE_LOCK_SITE_LEVEL("Order low", kLowLevel)};
        Mutex mid{BE_LOCK_SITE_LEVEL("Order mid", kMidLevel)};
        SharedMutex high{BE_LOCK_SITE_LEVEL("Order high", kHighLevel)};

        ASSERT_EQ(LockOrder::HeldCount(), 0u);
        {
            std::lock_guard<Mutex> first(low);
            std::lock_guard<Mutex> second(mid);
            std::lock_guard<SharedMutex> third(high);
            EXPECT_EQ(LockOrder::HeldCount(), 3u);
        }
        EXPECT_EQ(LockOrder::HeldCount(), 0u);
    }

    TEST(ThreadingTest, LockOrderDescendingAsserts) {
        Mutex low{BE_LOCK_SITE_LEVEL("Order low", kLowLevel)};
        Mutex high{BE_LOCK_SITE_LEVEL("Order high", kHighLevel)};

        high.lock();
        EXPECT_NONFATAL_FAILURE(low.lock(), kLowAfterHigh);
        // The lock is taken after the report, like any assert that lets the code go on: release both
        low.unlock();
        high.unlock();
        EXPECT_EQ(LockOrder::HeldCount(), 0u);
    }

    TEST(ThreadingTest, LockOrderSameLevelAsserts) {
        Mutex first{BE_LOCK_SITE_LEVEL("Order first", kMidLevel)};
        Mutex second{BE_LOCK_SITE_LEVEL("Order second", kMidLevel)};

        first.lock();
        EXPECT_NONFATAL_FAILURE(second.lock(), "taking 'Order second' (level 20) while holding 'Order first' (level 20)");
        second.unlock();
        first.unlock();
    }

    TEST(ThreadingTest, LockOrderLevelZeroNeverAsserts) {
        Mutex high{BE_LOCK_SITE_LEVEL("Order high", kHighLevel)};
        Mutex unordered{BE_LOCK_SITE("Order none")};
        Mutex low{BE_LOCK_SITE_LEVEL("Order low", kLowLevel)};

        {
            std::lock_guard<Mutex> highGuard(high);
            std::lock_guard<Mutex> unorderedGuard(unordered);
            EXPECT_EQ(LockOrder::HeldCount(), 1u);  // a level 0 lock is not recorded
        }
        {
            std::lock_guard<Mutex> unorderedGuard(unordered);
            std::lock_guard<Mutex> lowGuard(low);
            std::lock_guard<Mutex> highGuard(high);
        }
        EXPECT_EQ(LockOrder::HeldCount(), 0u);
    }

    TEST(ThreadingTest, LockOrderTryLockIsRecordedButNeverAsserts) {
        Mutex low{BE_LOCK_SITE_LEVEL("Order low", kLowLevel)};
        Mutex high{BE_LOCK_SITE_LEVEL("Order high", kHighLevel)};

        high.lock();
        EXPECT_TRUE(low.try_lock());  // descending, but it cannot wait, so it cannot deadlock
        EXPECT_EQ(LockOrder::HeldCount(), 2u);
        low.unlock();
        high.unlock();
        EXPECT_EQ(LockOrder::HeldCount(), 0u);
    }

    TEST(ThreadingTest, LockOrderFailedTryLockRecordsNothing) {
        Mutex low{BE_LOCK_SITE_LEVEL("Order low", kLowLevel)};
        ASSERT_TRUE(low.try_lock());

        size_t heldByOther = 99;
        std::thread other([&] {
            const bool locked = low.try_lock();
            heldByOther = LockOrder::HeldCount() + (locked ? 100u : 0u);
        });
        other.join();
        EXPECT_EQ(heldByOther, 0u);
        low.unlock();
    }

    TEST(ThreadingTest, LockOrderReleaseOutOfOrderKeepsTheStackRight) {
        Mutex low{BE_LOCK_SITE_LEVEL("Order low", kLowLevel)};
        Mutex mid{BE_LOCK_SITE_LEVEL("Order mid", kMidLevel)};
        Mutex high{BE_LOCK_SITE_LEVEL("Order high", kHighLevel)};

        low.lock();
        mid.lock();
        high.lock();
        mid.unlock();  // the middle one, not the last
        low.unlock();
        EXPECT_EQ(LockOrder::HeldCount(), 1u);

        // Only `high` is held now: a lower lock still asserts, and the released ones are really gone
        EXPECT_NONFATAL_FAILURE(mid.lock(), "taking 'Order mid' (level 20) while holding 'Order high' (level 30)");
        mid.unlock();
        high.unlock();
        EXPECT_EQ(LockOrder::HeldCount(), 0u);

        // Nothing stale is left behind: the ascending order passes again
        low.lock();
        mid.lock();
        high.lock();
        high.unlock();
        mid.unlock();
        low.unlock();
        EXPECT_EQ(LockOrder::HeldCount(), 0u);
    }

    TEST(ThreadingTest, LockOrderEveryThreadHasItsOwnStack) {
        Mutex low{BE_LOCK_SITE_LEVEL("Order low", kLowLevel)};
        Mutex high{BE_LOCK_SITE_LEVEL("Order high", kHighLevel)};
        std::atomic<bool> holding{false};
        std::atomic<bool> release{false};

        std::thread holder([&] {
            std::lock_guard<Mutex> lock(high);
            holding.store(true);
            while (!release.load()) {
                std::this_thread::yield();
            }
        });
        ASSERT_TRUE(WaitUntil([&] { return holding.load(); }));

        // Another thread holds the high lock; this one holds nothing, so the low lock is in order here
        {
            std::lock_guard<Mutex> lock(low);
            EXPECT_EQ(LockOrder::HeldCount(), 1u);
        }
        release.store(true);
        holder.join();
        EXPECT_EQ(LockOrder::HeldCount(), 0u);
    }

    TEST(ThreadingTest, LockOrderCountsBothSidesOfASharedMutex) {
        SharedMutex high{BE_LOCK_SITE_LEVEL("Order high", kHighLevel)};
        Mutex low{BE_LOCK_SITE_LEVEL("Order low", kLowLevel)};
        SharedMutex lowShared{BE_LOCK_SITE_LEVEL("Order low shared", kLowLevel)};

        high.lock_shared();
        EXPECT_NONFATAL_FAILURE(low.lock(), kLowAfterHigh);
        low.unlock();
        EXPECT_NONFATAL_FAILURE(lowShared.lock_shared(), "taking 'Order low shared' (level 10) while holding 'Order high' (level 30)");
        lowShared.unlock_shared();
        high.unlock_shared();
        EXPECT_EQ(LockOrder::HeldCount(), 0u);

        high.lock();
        EXPECT_NONFATAL_FAILURE(low.lock(), kLowAfterHigh);
        low.unlock();
        high.unlock();
        EXPECT_EQ(LockOrder::HeldCount(), 0u);

        // And a shared lock below a held lock is in order as well as an exclusive one
        low.lock();
        high.lock_shared();
        high.unlock_shared();
        low.unlock();
    }

    TEST(ThreadingTest, LockOrderConditionVariableWaitKeepsTheLockHeld) {
        Mutex mutex{BE_LOCK_SITE_LEVEL("Order cv", kMidLevel)};
        ConditionVariable changed;
        bool ready = false;
        size_t heldAfterWait = 0;
        size_t heldAfterUnlock = 99;

        std::thread waiter([&] {
            std::unique_lock<Mutex> lock(mutex);
            changed.wait(lock, [&] { return ready; });
            heldAfterWait = LockOrder::HeldCount();
            lock.unlock();
            heldAfterUnlock = LockOrder::HeldCount();
        });
        std::this_thread::sleep_for(kShortWait);
        {
            std::lock_guard<Mutex> lock(mutex);
            ready = true;
        }
        changed.notify_one();
        waiter.join();
        EXPECT_EQ(heldAfterWait, 1u);
        EXPECT_EQ(heldAfterUnlock, 0u);
    }

#endif

    // ---------------------------------------------------------------- Thread

#if !BE_SINGLE_THREAD

    namespace {

        constexpr uint32_t kTestSortIndex = 321;

        /// The name the profiler has for the calling thread, "" when it has none (and always "" with the profiler off)
        std::string CurrentThreadNameInProfiler() {
#if BE_PROFILER
            const char* name = Profiler::GetThreadName(Profiler::CurrentThreadId());
            return name != nullptr ? name : "";
#else
            return {};
#endif
        }

    }  // namespace

    TEST(ThreadingTest, ThreadRunsTheBodyOnAnotherThreadAndJoins) {
        Thread::Id bodyThread;
        Thread thread("Test body", [&] { bodyThread = Thread::CurrentId(); });
        EXPECT_TRUE(thread.joinable());
        const Thread::Id id = thread.get_id();
        thread.join();
        EXPECT_FALSE(thread.joinable());
        EXPECT_NE(bodyThread, Thread::CurrentId());
        EXPECT_EQ(bodyThread, id);
    }

    TEST(ThreadingTest, ThreadIsNamedBeforeItsBodyRuns) {
        std::string seen;
        uint32_t seenSortIndex = 0;
        Thread thread("Test named", [&] {
            seen = CurrentThreadNameInProfiler();
#if BE_PROFILER
            seenSortIndex = Profiler::GetThreadSortIndex(Profiler::CurrentThreadId());
#endif
        });
        thread.join();
#if BE_PROFILER
        EXPECT_EQ(seen, "Test named");
        EXPECT_EQ(seenSortIndex, Profiler::kNoSortIndex);
#else
        EXPECT_TRUE(seen.empty());
        static_cast<void>(seenSortIndex);
#endif
    }

    TEST(ThreadingTest, ThreadKeepsItsSortIndex) {
        std::string seen;
        uint32_t seenSortIndex = 0;
        Thread thread("Test sorted", kTestSortIndex, [&] {
            seen = CurrentThreadNameInProfiler();
#if BE_PROFILER
            seenSortIndex = Profiler::GetThreadSortIndex(Profiler::CurrentThreadId());
#endif
        });
        thread.join();
#if BE_PROFILER
        EXPECT_EQ(seen, "Test sorted");
        EXPECT_EQ(seenSortIndex, kTestSortIndex);
#else
        static_cast<void>(seenSortIndex);
        EXPECT_TRUE(seen.empty());
#endif
    }

    TEST(ThreadingTest, ThreadCopiesARunTimeName) {
        std::atomic<bool> nameGone{false};
        std::string seen;
        auto name = MakeUnique<std::string>("Test run-time name 7");
        Thread thread(name->c_str(), [&] {
            while (!nameGone.load()) {
                std::this_thread::yield();
            }
            seen = CurrentThreadNameInProfiler();
        });
        // The thread may not have started yet; the name must not be needed after the constructor returned
        name.reset();
        nameGone.store(true);
        thread.join();
#if BE_PROFILER
        EXPECT_EQ(seen, "Test run-time name 7");
#else
        EXPECT_TRUE(seen.empty());
#endif
    }

    TEST(ThreadingTest, ThreadDefaultConstructedIsNotJoinable) {
        Thread thread;
        EXPECT_FALSE(thread.joinable());
        EXPECT_EQ(thread.get_id(), Thread::Id{});
    }

    TEST(ThreadingTest, ThreadMovesByConstructionAndAssignment) {
        std::atomic<int> runs{0};
        Thread first("Test move", [&] { runs.fetch_add(1); });
        const Thread::Id id = first.get_id();

        Thread second(std::move(first));
        EXPECT_FALSE(first.joinable());
        EXPECT_TRUE(second.joinable());
        EXPECT_EQ(second.get_id(), id);

        Thread third;
        third = std::move(second);
        EXPECT_FALSE(second.joinable());
        EXPECT_TRUE(third.joinable());
        third.join();
        EXPECT_EQ(runs.load(), 1);

        // Assigning over a finished (joined) Thread is fine
        third = Thread("Test move again", [&] { runs.fetch_add(1); });
        third.join();
        EXPECT_EQ(runs.load(), 2);
    }

    TEST(ThreadingTest, ThreadDetachedBodyStillRuns) {
        // The state outlives the test body: a detached thread may touch it after the Thread object is gone
        struct State {
            std::atomic<bool> ran{false};
        };
        auto state = MakeShared<State>();
        Thread thread("Test detach", [state] { state->ran.store(true); });
        thread.detach();
        EXPECT_FALSE(thread.joinable());
        EXPECT_TRUE(WaitUntil([&] { return state->ran.load(); }));
    }

    TEST(ThreadingTest, ThreadTakesAMoveOnlyBody) {
        auto payload = MakeUnique<int>(42);
        int seen = 0;
        Thread thread("Test move-only body", [owned = std::move(payload), &seen] { seen = *owned; });
        thread.join();
        EXPECT_EQ(seen, 42);
    }

    TEST(ThreadingTest, ThreadHardwareConcurrencyIsTheStdValue) {
        EXPECT_EQ(Thread::HardwareConcurrency(), std::thread::hardware_concurrency());
        EXPECT_TRUE(Thread::kAvailable);
    }

#else

    // The single-thread build (the browser's single-thread page): a Thread starts nothing
    TEST(ThreadingTest, ThreadStartsNothingInASingleThreadBuild) {
        EXPECT_FALSE(Thread::kAvailable);
        bool ran = false;
#if BE_LOCK_ORDER_CHECK
        EXPECT_NONFATAL_FAILURE(Thread("Test single", [&] { ran = true; }), "no thread can start");
#else
        Thread thread("Test single", [&] { ran = true; });
        EXPECT_FALSE(thread.joinable());
#endif
        EXPECT_FALSE(ran);
    }

#endif

    // ---------------------------------------------------------------- sizes

    TEST(ThreadingTest, ShippedLocksAreTheStdLocksInSize) {
#if !BE_LOCK_SITES
        static_assert(sizeof(Mutex) == sizeof(std::mutex));
        static_assert(sizeof(SharedMutex) == sizeof(std::shared_mutex));
        SUCCEED();
#else
        GTEST_SKIP() << "the locks keep their site in this build (BE_LOCK_SITES)";
#endif
    }

}  // namespace BECore::Tests
