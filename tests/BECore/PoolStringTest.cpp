#include <BECore/PoolString/PoolString.h>
#include <Common/Printers.h>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace BECore::Tests {

    TEST(PoolStringTest, FindDoesNotAddAString) {
        EXPECT_FALSE(PoolString::Find("pool_find_never_interned_3c81").has_value());
        EXPECT_FALSE(PoolString::Find("pool_find_never_interned_3c81").has_value());
    }

    TEST(PoolStringTest, FindReturnsTheInternedString) {
        const PoolString interned = PoolString::Intern("pool_find_interned_9a47");
        const auto found = PoolString::Find("pool_find_interned_9a47");
        ASSERT_TRUE(found.has_value());
        EXPECT_EQ(*found, interned);
    }

    TEST(PoolStringTest, FindOfAnEmptyStringIsTheEmptyPoolString) {
        const auto found = PoolString::Find("");
        ASSERT_TRUE(found.has_value());
        EXPECT_EQ(*found, PoolString());
    }

    TEST(PoolStringTest, FindDistinguishesStringsWithTheSamePrefix) {
        const PoolString interned = PoolString::Intern("pool_find_prefix_b6d5");
        EXPECT_FALSE(PoolString::Find("pool_find_prefix_b6d5x").has_value());
        EXPECT_FALSE(PoolString::Find("pool_find_prefix_b6d").has_value());
        EXPECT_TRUE(PoolString::Find("pool_find_prefix_b6d5").has_value());
    }

    namespace {

        StringView View(const String& str) {
            return str;
        }

        constexpr int kLockCostNames = 1000;
        constexpr int kLockCostRounds = 2000;
        constexpr int kLockCostNewEvery = 100;
        constexpr int kLockCostRepeats = 5;
        constexpr int kLockCostThreads = 4;

        /// One thread's work: kLockCostRounds passes over the names; with newEvery > 0 every newEvery-th call interns a string not seen before
        double InternNanosecondsPerCall(const Vector<String>& names, const Vector<String>& fresh, int newEvery) {
            size_t nextFresh = 0;
            size_t sink = 0;
            const auto start = std::chrono::steady_clock::now();
            for (int round = 0; round < kLockCostRounds; ++round) {
                for (int i = 0; i < kLockCostNames; ++i) {
                    if (newEvery > 0 && i % newEvery == 0) {
                        sink += PoolString::Intern(View(fresh[nextFresh++])).Length();
                    } else {
                        sink += PoolString::Intern(View(names[static_cast<size_t>(i)])).Length();
                    }
                }
            }
            const auto elapsed = std::chrono::steady_clock::now() - start;
            EXPECT_NE(sink, 0u);
            return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()) / (kLockCostRounds * kLockCostNames);
        }

        /// The best of kLockCostRepeats runs of `threads` threads doing the same work; nanoseconds per call of one thread
        double BestInternNanoseconds(int threads, int newEvery) {
            static std::atomic<uint32_t> s_serial{0};
            Vector<String> names;
            for (int i = 0; i < kLockCostNames; ++i) {
                names.push_back("pool_lock_cost_" + ToString(i));
                static_cast<void>(PoolString::Intern(View(names.back())));
            }
            double best = 1e30;
            for (int repeat = 0; repeat < kLockCostRepeats; ++repeat) {
                Vector<Vector<String>> fresh(static_cast<size_t>(threads));
                for (auto& list : fresh) {
                    const size_t count = newEvery > 0 ? static_cast<size_t>(kLockCostRounds) * kLockCostNames / newEvery : 0;
                    for (size_t i = 0; i < count; ++i) {
                        list.push_back("pool_lock_cost_new_" + ToString(s_serial.fetch_add(1)));
                    }
                }
                Vector<double> results(static_cast<size_t>(threads), 0.0);
                std::atomic<int> ready{0};
                Vector<std::thread> pool;
                for (int t = 0; t < threads; ++t) {
                    pool.emplace_back([&, t] {
                        ready.fetch_add(1);
                        while (ready.load() < threads) {
                        }
                        results[static_cast<size_t>(t)] = InternNanosecondsPerCall(names, fresh[static_cast<size_t>(t)], newEvery);
                    });
                }
                for (auto& thread : pool) {
                    thread.join();
                }
                double worst = 0.0;
                for (const double value : results) {
                    worst = std::max(worst, value);
                }
                best = std::min(best, worst);
            }
            return best;
        }

    }  // namespace

    /// The cost of the shared lock on the hit path of PoolString::Intern. Run in macos-release:
    /// BagiEngineTests --gtest_also_run_disabled_tests --gtest_filter=*PoolStringLockCost*
    TEST(PoolStringLockCostTest, DISABLED_InternHitAndMix) {
        const double hit1 = BestInternNanoseconds(1, 0);
        const double mix1 = BestInternNanoseconds(1, kLockCostNewEvery);
        const double hit4 = BestInternNanoseconds(kLockCostThreads, 0);
        const double mix4 = BestInternNanoseconds(kLockCostThreads, kLockCostNewEvery);
        std::printf("Intern ns per call, best of %d: 1 thread hit %.1f, 1 thread mix %.1f, %d threads hit %.1f, %d threads mix %.1f\n", kLockCostRepeats, hit1, mix1, kLockCostThreads, hit4,
                    kLockCostThreads, mix4);
    }

}  // namespace BECore::Tests
