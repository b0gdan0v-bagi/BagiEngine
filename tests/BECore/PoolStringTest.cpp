#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/XmlDeserializer.h>
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

        constexpr size_t kEntryHeaderBytes = offsetof(Details::PoolStringEntry, data);

        /// A text of `length` characters that differs from the texts of other seeds from its first character on
        String BlockText(size_t length, char seed) {
            String text(length, seed);
            for (size_t i = 1; i < length; i += 7) {
                text[i] = static_cast<char>('a' + (i + static_cast<size_t>(seed)) % 26);
            }
            return text;
        }

        void ExpectInternedWholeAndTerminated(const String& text) {
            const PoolString interned = PoolString::Intern(StringView{text});
            ASSERT_EQ(interned.Length(), text.size());
            EXPECT_EQ(interned.CStr()[interned.Length()], '\0');
            EXPECT_EQ(std::memcmp(interned.CStr(), text.data(), text.size()), 0);
            const auto found = PoolString::Find(StringView{text});
            ASSERT_TRUE(found.has_value());
            EXPECT_EQ(*found, interned);
            EXPECT_EQ(PoolString::Intern(StringView{text}), interned);
        }

    }  // namespace

    TEST(PoolStringBlocksTest, StringsAroundTheBlockSizeAreStoredWhole) {
        constexpr size_t kLengths[] = {
            PoolString::kBlockBytes - kEntryHeaderBytes - 1,  // the entry fills a block exactly
            PoolString::kBlockBytes - kEntryHeaderBytes,      // one character more: a block of its own
            PoolString::kBlockBytes,
            3 * PoolString::kBlockBytes + 17,
        };
        char seed = 'A';
        for (const size_t length : kLengths) {
            SCOPED_TRACE(length);
            ExpectInternedWholeAndTerminated(BlockText(length, seed++));
        }
    }

    TEST(PoolStringBlocksTest, AnEntryThatFitsABlockButNotTheRestOfTheCurrentOneIsStoredWhole) {
        const PoolString before = PoolString::Intern("pool_block_before_5d02");
        ExpectInternedWholeAndTerminated(BlockText(PoolString::kBlockBytes - kEntryHeaderBytes - 1, 'q'));
        EXPECT_EQ(PoolString::Intern("pool_block_before_5d02"), before);
        EXPECT_EQ(before.ToStringView(), "pool_block_before_5d02");
    }

    TEST(PoolStringBlocksTest, SmallStringsAroundAHugeOneStayIntact) {
        const String hugeText = BlockText(2 * PoolString::kBlockBytes + 5, 'h');
        const PoolString small1 = PoolString::Intern("pool_block_small_first_7e31");
        const PoolString huge = PoolString::Intern(StringView{hugeText});
        const PoolString small2 = PoolString::Intern("pool_block_small_second_7e31");

        EXPECT_EQ(small1, PoolString::Intern("pool_block_small_first_7e31"));
        EXPECT_EQ(small2, PoolString::Intern("pool_block_small_second_7e31"));
        EXPECT_EQ(small1.ToStringView(), "pool_block_small_first_7e31");
        EXPECT_EQ(small2.ToStringView(), "pool_block_small_second_7e31");
        EXPECT_NE(small1, small2);
        EXPECT_EQ(huge.ToStringView(), StringView{hugeText});
    }

    TEST(PoolStringBlocksTest, AnXmlAttributeLongerThanABlockLoadsIntoAPoolString) {
        const String longText = BlockText(PoolString::kBlockBytes + 100, 'x');
        String document = "<root name=\"";
        document += longText;
        document += "\"><child>";
        document += longText;
        document += "</child></root>";

        XmlDeserializer reader;
        ASSERT_TRUE(reader.LoadFromString(document));
        PoolString attribute;
        ASSERT_TRUE(reader.ReadAttribute("name", attribute));
        EXPECT_EQ(attribute.ToStringView(), StringView{longText});
        EXPECT_EQ(attribute.CStr()[attribute.Length()], '\0');
        PoolString element;
        ASSERT_TRUE(reader.Read("child", element));
        EXPECT_EQ(element, attribute);
        EXPECT_FALSE(reader.HasErrors());
    }

    namespace {

        // The initial table of the pool has 16384 slots and grows at half load; the names below are well past both
        constexpr size_t kInitialTableSlots = 16384;
        constexpr size_t kGrowthNames = 20000;
        constexpr int kTableThreads = 4;
        constexpr auto kTableThreadsLimit = std::chrono::seconds(60);

        String CounterName(StringView prefix, size_t index) {
            String name(prefix);
            name += ToString(index);
            return name;
        }

        size_t InitialSlotOf(StringView text) {
            return static_cast<size_t>(StringUtils::GetHash(text)) & (kInitialTableSlots - 1);
        }

    }  // namespace

    TEST(PoolStringTableTest, EveryStringIsFoundAfterTheTableGrows) {
        Vector<PoolString> interned;
        interned.reserve(kGrowthNames);
        for (size_t i = 0; i < kGrowthNames; ++i) {
            interned.push_back(PoolString::Intern(StringView{CounterName("pool_growth_", i)}));
        }
        for (size_t i = 0; i < kGrowthNames; ++i) {
            const String name = CounterName("pool_growth_", i);
            const auto found = PoolString::Find(StringView{name});
            ASSERT_TRUE(found.has_value()) << name.c_str();
            ASSERT_EQ(*found, interned[i]) << name.c_str();
            ASSERT_EQ(PoolString::Intern(StringView{name}), interned[i]) << name.c_str();
            ASSERT_EQ(interned[i].ToStringView(), StringView{name});
        }
        EXPECT_FALSE(PoolString::Find("pool_growth_never_interned_61bd").has_value());
        EXPECT_FALSE(PoolString::Find(StringView{CounterName("pool_growth_", kGrowthNames)}).has_value());
    }

    TEST(PoolStringTableTest, StringsOfOneSlotAreAllFound) {
        constexpr size_t kClusterSize = 8;
        HashMap<size_t, Vector<String>> bySlot;
        const Vector<String>* cluster = nullptr;
        for (size_t i = 0; cluster == nullptr; ++i) {
            Vector<String>& names = bySlot[InitialSlotOf(CounterName("pool_collide_", i))];
            names.push_back(CounterName("pool_collide_", i));
            if (names.size() > kClusterSize) {
                cluster = &names;
            }
            ASSERT_LT(i, size_t{2'000'000}) << "no slot got " << kClusterSize + 1 << " names";
        }

        Vector<PoolString> interned;
        for (size_t i = 0; i < kClusterSize; ++i) {
            interned.push_back(PoolString::Intern(StringView{(*cluster)[i]}));
        }
        for (size_t i = 0; i < kClusterSize; ++i) {
            const auto found = PoolString::Find(StringView{(*cluster)[i]});
            ASSERT_TRUE(found.has_value()) << (*cluster)[i].c_str();
            EXPECT_EQ(*found, interned[i]);
            EXPECT_EQ(interned[i].ToStringView(), StringView{(*cluster)[i]});
            for (size_t other = 0; other < i; ++other) {
                EXPECT_NE(interned[i], interned[other]);
            }
        }
        // The probe of a text that is not in the pool walks the cluster and ends at an empty slot
        EXPECT_FALSE(PoolString::Find(StringView{(*cluster)[kClusterSize]}).has_value());
    }

    TEST(PoolStringTableTest, ThreadsInterningOverlappingNamesWhileTheTableGrowsAgree) {
        Vector<String> names;
        names.reserve(kGrowthNames);
        for (size_t i = 0; i < kGrowthNames; ++i) {
            names.push_back(CounterName("pool_concurrent_", i));
        }

        Vector<Vector<PoolString>> results(kTableThreads, Vector<PoolString>(kGrowthNames));
        std::atomic<int> ready{0};
        std::atomic<int> finished{0};
        Vector<std::thread> threads;
        for (int t = 0; t < kTableThreads; ++t) {
            threads.emplace_back([&, t] {
                ready.fetch_add(1);
                while (ready.load() < kTableThreads) {
                    std::this_thread::yield();
                }
                // Each thread starts at another name, so the threads insert different names at the same time
                const size_t start = kGrowthNames / kTableThreads * static_cast<size_t>(t);
                for (size_t step = 0; step < kGrowthNames; ++step) {
                    const size_t i = (start + step) % kGrowthNames;
                    results[static_cast<size_t>(t)][i] = PoolString::Intern(StringView{names[i]});
                }
                finished.fetch_add(1);
            });
        }
        const auto deadline = std::chrono::steady_clock::now() + kTableThreadsLimit;
        while (finished.load() < kTableThreads && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        ASSERT_EQ(finished.load(), kTableThreads) << "the threads did not finish in time";
        for (auto& thread : threads) {
            thread.join();
        }

        for (size_t i = 0; i < kGrowthNames; ++i) {
            for (int t = 1; t < kTableThreads; ++t) {
                ASSERT_EQ(results[static_cast<size_t>(t)][i], results[0][i]) << names[i].c_str();
            }
            ASSERT_EQ(results[0][i].ToStringView(), StringView{names[i]});
            ASSERT_EQ(PoolString::Find(StringView{names[i]}).value_or(PoolString{}), results[0][i]);
        }
    }

    namespace {

        constexpr int kStatsThreads = 4;
        constexpr size_t kStatsNamesPerThread = 3000;
        constexpr size_t kFillGuard = 4'000'000;

        /// The size an entry takes in a block: header, text and NUL, aligned to 8
        constexpr size_t EntryBytes(size_t length) {
            return (kEntryHeaderBytes + length + 1 + 7) & ~size_t{7};
        }

        /// Interns names until the pool takes a new standard block, so the current block is nearly empty afterwards
        void StartFreshBlock() {
            const size_t blocksBefore = PoolString::GetStats().blockCount;
            for (size_t i = 0; i < kFillGuard && PoolString::GetStats().blockCount == blocksBefore; ++i) {
                static_cast<void>(PoolString::Intern(StringView{CounterName("pool_stats_fill_", i)}));
            }
            ASSERT_GT(PoolString::GetStats().blockCount, blocksBefore);
        }

    }  // namespace

    TEST(PoolStringStatsTest, ANewTextIsAMissAndARepeatIsAHit) {
        const PoolStringStats before = PoolString::GetStats();
        static_cast<void>(PoolString::Intern("pool_stats_new_text_4b7e"));
        const PoolStringStats afterNew = PoolString::GetStats();
        EXPECT_EQ(afterNew.stringCount, before.stringCount + 1);
        EXPECT_EQ(afterNew.tableUsed, before.tableUsed + 1);
        EXPECT_EQ(afterNew.misses, before.misses + 1);
        EXPECT_EQ(afterNew.hits, before.hits);
        EXPECT_EQ(afterNew.stringBytes, before.stringBytes + EntryBytes(StringView{"pool_stats_new_text_4b7e"}.size()));

        static_cast<void>(PoolString::Intern("pool_stats_new_text_4b7e"));
        const PoolStringStats afterRepeat = PoolString::GetStats();
        EXPECT_EQ(afterRepeat.stringCount, afterNew.stringCount);
        EXPECT_EQ(afterRepeat.misses, afterNew.misses);
        EXPECT_EQ(afterRepeat.hits, afterNew.hits + 1);
        EXPECT_EQ(afterRepeat.internCalls, afterRepeat.hits + afterRepeat.misses);
    }

    TEST(PoolStringStatsTest, TheEmptyTextIsAHitAndNotAString) {
        const PoolStringStats before = PoolString::GetStats();
        EXPECT_TRUE(PoolString::Intern("").Empty());
        const PoolStringStats after = PoolString::GetStats();
        EXPECT_EQ(after.hits, before.hits + 1);
        EXPECT_EQ(after.misses, before.misses);
        EXPECT_EQ(after.stringCount, before.stringCount);
        EXPECT_EQ(after.stringBytes, before.stringBytes);
    }

    TEST(PoolStringStatsTest, FindIsNotAnInternCall) {
        static_cast<void>(PoolString::Intern("pool_stats_find_target_c2a9"));
        const PoolStringStats before = PoolString::GetStats();
        EXPECT_TRUE(PoolString::Find("pool_stats_find_target_c2a9").has_value());
        EXPECT_FALSE(PoolString::Find("pool_stats_find_missing_c2a9").has_value());
        const PoolStringStats after = PoolString::GetStats();
        EXPECT_EQ(after.internCalls, before.internCalls);
        EXPECT_EQ(after.stringCount, before.stringCount);
    }

    TEST(PoolStringStatsTest, AStringOfItsOwnBlockAddsABlockAndLeavesTheCurrentOne) {
        ASSERT_NO_FATAL_FAILURE(StartFreshBlock());
        static_cast<void>(PoolString::Intern("pool_stats_before_huge_91d4"));
        const PoolStringStats before = PoolString::GetStats();

        const String hugeText = BlockText(2 * PoolString::kBlockBytes + 5, 'g');
        static_cast<void>(PoolString::Intern(StringView{hugeText}));
        const PoolStringStats afterHuge = PoolString::GetStats();
        EXPECT_EQ(afterHuge.blockCount, before.blockCount + 1);
        EXPECT_EQ(afterHuge.blockBytes, before.blockBytes + EntryBytes(hugeText.size()));
        EXPECT_EQ(afterHuge.stringBytes, before.stringBytes + EntryBytes(hugeText.size()));

        // The small string after the huge one goes into the block that was current before it
        static_cast<void>(PoolString::Intern("pool_stats_after_huge_91d4"));
        const PoolStringStats afterSmall = PoolString::GetStats();
        EXPECT_EQ(afterSmall.blockCount, afterHuge.blockCount);
        EXPECT_EQ(afterSmall.blockBytes, afterHuge.blockBytes);
    }

    TEST(PoolStringStatsTest, AnEntryThatDoesNotFitTheRestOfTheBlockStartsAStandardBlock) {
        ASSERT_NO_FATAL_FAILURE(StartFreshBlock());
        const PoolStringStats before = PoolString::GetStats();
        // Fits a standard block but not what is left of the current one: a new standard block, not one of its own
        static_cast<void>(PoolString::Intern(StringView{BlockText(PoolString::kBlockBytes - kEntryHeaderBytes - 1 - 32, 'k')}));
        const PoolStringStats afterBig = PoolString::GetStats();
        EXPECT_EQ(afterBig.blockCount, before.blockCount + 1);
        EXPECT_EQ(afterBig.blockBytes, before.blockBytes + PoolString::kBlockBytes);
        // Fewer than 100 bytes are left in that block, so the next small string starts another one
        static_cast<void>(PoolString::Intern(StringView{BlockText(100, 'm')}));
        const PoolStringStats afterSmall = PoolString::GetStats();
        EXPECT_EQ(afterSmall.blockCount, before.blockCount + 2);
        EXPECT_EQ(afterSmall.blockBytes, before.blockBytes + 2 * PoolString::kBlockBytes);
    }

    TEST(PoolStringStatsTest, TheTableGrowsWhenItsLoadPassesHalf) {
        const PoolStringStats before = PoolString::GetStats();
        size_t interned = 0;
        PoolStringStats current = before;
        while (current.tableCapacity == before.tableCapacity && interned < kFillGuard) {
            static_cast<void>(PoolString::Intern(StringView{CounterName("pool_stats_grow_", interned++)}));
            current = PoolString::GetStats();
        }
        EXPECT_EQ(current.tableCapacity, before.tableCapacity * 2);
        EXPECT_EQ(current.tableUsed, before.tableUsed + interned);
        EXPECT_EQ(current.stringCount, before.stringCount + interned);
        EXPECT_LE(current.tableUsed * 2, current.tableCapacity);
        EXPECT_LT(current.longestProbe, current.tableCapacity);
        EXPECT_EQ(current.misses, before.misses + interned);
    }

    TEST(PoolStringStatsTest, CallsOfSeveralThreadsAreAllCounted) {
        Vector<String> names;
        for (size_t i = 0; i < kStatsNamesPerThread; ++i) {
            names.push_back(CounterName("pool_stats_threads_", i));
        }
        const PoolStringStats before = PoolString::GetStats();

        std::atomic<int> ready{0};
        std::atomic<int> finished{0};
        Vector<std::thread> threads;
        for (int t = 0; t < kStatsThreads; ++t) {
            threads.emplace_back([&] {
                ready.fetch_add(1);
                while (ready.load() < kStatsThreads) {
                    std::this_thread::yield();
                }
                for (const String& name : names) {
                    static_cast<void>(PoolString::Intern(StringView{name}));
                }
                finished.fetch_add(1);
            });
        }
        const auto deadline = std::chrono::steady_clock::now() + kTableThreadsLimit;
        while (finished.load() < kStatsThreads && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        ASSERT_EQ(finished.load(), kStatsThreads) << "the threads did not finish in time";
        for (auto& thread : threads) {
            thread.join();
        }

        // The threads are gone, their counts stay. Every name is one miss and the other calls are hits, whoever won the race
        const PoolStringStats after = PoolString::GetStats();
        EXPECT_EQ(after.misses, before.misses + kStatsNamesPerThread);
        EXPECT_EQ(after.hits, before.hits + (kStatsThreads - 1) * kStatsNamesPerThread);
        EXPECT_EQ(after.internCalls - before.internCalls, kStatsThreads * kStatsNamesPerThread);
        EXPECT_EQ(after.stringCount, before.stringCount + kStatsNamesPerThread);
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
