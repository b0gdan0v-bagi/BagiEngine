#include <BECore/Threading/ConditionVariable.h>
#include <BECore/Threading/LockGuards.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Threading/SharedMutex.h>
#include <atomic>
#include <chrono>
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

        std::vector<std::thread> threads;
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

        std::vector<std::thread> readers;
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

        std::vector<std::thread> threads;
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
