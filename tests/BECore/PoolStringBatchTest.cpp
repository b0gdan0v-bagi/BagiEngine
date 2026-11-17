#include <BECore/Assert/AssertMacros.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/PoolStringStorage.h>
#include <BECore/PoolStringTestSupport.h>
#include <Common/AssertBridge.h>
#include <Common/Printers.h>
#include <chrono>
#include <cstdio>

namespace BECore::Tests {

    namespace {

        constexpr size_t kBlock = PoolString::kBlockBytes;
        constexpr auto kThreadsLimit = std::chrono::seconds(60);

        Vector<StringView> ViewsOf(const Vector<String>& names) {
            Vector<StringView> views;
            views.reserve(names.size());
            for (const String& name : names) {
                views.push_back(StringView{name});
            }
            return views;
        }

        Vector<uint64_t> HashesOf(const Vector<StringView>& texts) {
            Vector<uint64_t> hashes;
            hashes.reserve(texts.size());
            for (const StringView text : texts) {
                hashes.push_back(StringUtils::GetHash(text));
            }
            return hashes;
        }

        Vector<PoolString> Batch(Details::PoolStringStorage& storage, const Vector<StringView>& texts) {
            Vector<PoolString> out(texts.size());
            storage.InternBatch(Span<const StringView>(texts.data(), texts.size()), Span<PoolString>(out.data(), out.size()));
            return out;
        }

        Vector<PoolString> BatchWithHashes(Details::PoolStringStorage& storage, const Vector<StringView>& texts) {
            const Vector<uint64_t> hashes = HashesOf(texts);
            Vector<PoolString> out(texts.size());
            storage.InternBatch(Span<const StringView>(texts.data(), texts.size()), Span<const uint64_t>(hashes.data(), hashes.size()), Span<PoolString>(out.data(), out.size()));
            return out;
        }

        size_t BytesOf(std::initializer_list<StringView> texts) {
            size_t bytes = 0;
            for (const StringView text : texts) {
                bytes += Details::PoolEntryBytes(text.size());
            }
            return bytes;
        }

    }  // namespace

    TEST(PoolStringBatchTest, TheResultsEqualInterningOneByOne) {
        Vector<String> names;
        for (size_t i = 0; i < 300; ++i) {
            names.push_back(CounterName("batch_same_", i % 120));
            if (i % 17 == 0) {
                names.push_back(String());
            }
        }
        const Vector<StringView> texts = ViewsOf(names);

        IsolatedPool single;
        Vector<PoolString> expected;
        for (const StringView text : texts) {
            expected.push_back(single.storage.Intern(text));
        }

        for (const bool withHashes : {false, true}) {
            SCOPED_TRACE(withHashes);
            IsolatedPool batched;
            const Vector<PoolString> out = withHashes ? BatchWithHashes(batched.storage, texts) : Batch(batched.storage, texts);
            ASSERT_EQ(out.size(), texts.size());
            for (size_t i = 0; i < texts.size(); ++i) {
                ASSERT_EQ(out[i].ToStringView(), expected[i].ToStringView()) << i;
                ASSERT_EQ(out[i].CStr()[out[i].Length()], '\0');
                const auto found = batched.storage.Find(texts[i]);
                ASSERT_TRUE(found.has_value());
                ASSERT_EQ(*found, out[i]);
            }
            const PoolStringStats a = batched.storage.GetStats();
            const PoolStringStats b = single.storage.GetStats();
            EXPECT_EQ(a.stringCount, b.stringCount);
            EXPECT_EQ(a.stringBytes, b.stringBytes);
            EXPECT_EQ(a.misses, b.misses);
            EXPECT_EQ(a.hits, b.hits);
            EXPECT_EQ(a.tableCapacity, b.tableCapacity);
        }
    }

    TEST(PoolStringBatchTest, RepeatsInsideTheBatchAreOneStringAndHits) {
        IsolatedPool pool;
        const StringView a = "batch_dup_a";
        const StringView b = "batch_dup_bb";
        const StringView c = "batch_dup_ccc";
        const Vector<StringView> texts{a, b, a, a, c, b};
        const Vector<PoolString> out = Batch(pool.storage, texts);

        EXPECT_EQ(out[0], out[2]);
        EXPECT_EQ(out[0], out[3]);
        EXPECT_EQ(out[1], out[5]);
        EXPECT_NE(out[0], out[1]);
        EXPECT_NE(out[0], out[4]);
        EXPECT_NE(out[1], out[4]);
        for (size_t i = 0; i < texts.size(); ++i) {
            EXPECT_EQ(out[i].ToStringView(), texts[i]);
        }

        const PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(stats.stringCount, 3u);
        EXPECT_EQ(stats.misses, 3u);
        EXPECT_EQ(stats.hits, 3u);
        EXPECT_EQ(stats.stringBytes, BytesOf({a, b, c}));
        EXPECT_EQ(stats.runtime.strings, 3u);
        // Three entries that do not fit the (absent) current block: one block of exactly their size, not current
        EXPECT_EQ(stats.blockCount, 1u);
        EXPECT_EQ(stats.blockBytes, BytesOf({a, b, c}));
        EXPECT_EQ(pool.source.servedBytes, BytesOf({a, b, c}));
        EXPECT_EQ(stats.writeLocks, 1u);
        EXPECT_EQ(stats.batchCalls, 1u);
        EXPECT_EQ(stats.batchStrings, 6u);
    }

    TEST(PoolStringBatchTest, ABatchThatFitsTheCurrentBlockTakesNoBlock) {
        IsolatedPool pool;
        static_cast<void>(pool.storage.Intern("batch_current_first"));
        const PoolStringStats before = pool.storage.GetStats();
        ASSERT_EQ(before.blockCount, 1u);

        const Vector<StringView> texts{"batch_current_x", "batch_current_y", "batch_current_x"};
        const Vector<PoolString> out = Batch(pool.storage, texts);
        const PoolStringStats after = pool.storage.GetStats();
        EXPECT_EQ(after.blockCount, 1u);
        EXPECT_EQ(after.blockBytes, before.blockBytes);
        EXPECT_EQ(after.stringBytes, before.stringBytes + BytesOf({texts[0], texts[1]}));
        EXPECT_EQ(out[0], out[2]);
    }

    TEST(PoolStringBatchTest, ABatchOfSeveralEntriesThatDoesNotFitTheRestGetsABlockOfItsOwnAndLeavesTheCurrentOne) {
        IsolatedPool pool;
        // Leaves 40 bytes in the current block
        static_cast<void>(pool.storage.Intern(StringView{BlockText(kBlock - 53, 'f')}));
        ASSERT_EQ(pool.storage.GetStats().blockCount, 1u);

        const String x = BlockText(100, 'x');
        const String y = BlockText(101, 'y');
        const Vector<StringView> texts{StringView{x}, StringView{y}};
        static_cast<void>(Batch(pool.storage, texts));
        PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(stats.blockCount, 2u);
        EXPECT_EQ(stats.blockBytes, kBlock + BytesOf({texts[0], texts[1]}));

        // A small string still goes into the old block
        static_cast<void>(pool.storage.Intern("batch_small_after"));
        stats = pool.storage.GetStats();
        EXPECT_EQ(stats.blockCount, 2u);
    }

    TEST(PoolStringBatchTest, ABatchOfOnlyHitsTakesNoLockAndNoMemory) {
        for (const bool withHashes : {false, true}) {
            SCOPED_TRACE(withHashes);
            IsolatedPool pool;
            Vector<String> names;
            for (size_t i = 0; i < 40; ++i) {
                names.push_back(CounterName("batch_hits_", i));
            }
            const Vector<StringView> texts = ViewsOf(names);
            const Vector<PoolString> first = Batch(pool.storage, texts);
            const PoolStringStats before = pool.storage.GetStats();

            const Vector<PoolString> again = withHashes ? BatchWithHashes(pool.storage, texts) : Batch(pool.storage, texts);
            const PoolStringStats after = pool.storage.GetStats();
            EXPECT_EQ(after.writeLocks, before.writeLocks);
            EXPECT_EQ(after.blockCount, before.blockCount);
            EXPECT_EQ(after.stringCount, before.stringCount);
            EXPECT_EQ(after.misses, before.misses);
            EXPECT_EQ(after.hits, before.hits + texts.size());
            EXPECT_EQ(after.batchCalls, before.batchCalls + 1);
            EXPECT_EQ(after.batchStrings, before.batchStrings + texts.size());
            for (size_t i = 0; i < texts.size(); ++i) {
                EXPECT_EQ(again[i], first[i]);
            }
        }
    }

    TEST(PoolStringBatchTest, HitsMissesAndRepeatsOfEarlierPositionsMix) {
        IsolatedPool pool;
        const PoolString known = pool.storage.Intern("batch_mix_known");
        const PoolString knownTwo = pool.storage.Intern("batch_mix_known_two");
        const PoolStringStats before = pool.storage.GetStats();

        // known (hit), new1, known (hit), new2, new1 (repeat), known_two (hit), new2 (repeat), new3
        const Vector<StringView> texts{"batch_mix_known", "batch_mix_new1", "batch_mix_known", "batch_mix_new2", "batch_mix_new1", "batch_mix_known_two", "batch_mix_new2", "batch_mix_new3"};
        const Vector<PoolString> out = Batch(pool.storage, texts);
        EXPECT_EQ(out[0], known);
        EXPECT_EQ(out[2], known);
        EXPECT_EQ(out[5], knownTwo);
        EXPECT_EQ(out[1], out[4]);
        EXPECT_EQ(out[3], out[6]);
        for (size_t i = 0; i < texts.size(); ++i) {
            EXPECT_EQ(out[i].ToStringView(), texts[i]);
            EXPECT_EQ(pool.storage.Find(texts[i]).value_or(PoolString()), out[i]);
        }
        const PoolStringStats after = pool.storage.GetStats();
        EXPECT_EQ(after.stringCount, before.stringCount + 3);
        EXPECT_EQ(after.misses, before.misses + 3);
        EXPECT_EQ(after.hits, before.hits + 5);
        EXPECT_EQ(after.stringBytes, before.stringBytes + BytesOf({texts[1], texts[3], texts[7]}));
        EXPECT_EQ(after.writeLocks, before.writeLocks + 1);
    }

    TEST(PoolStringBatchTest, AnOversizeStringAmongSmallOnesTakesOneBlockForTheBatch) {
        IsolatedPool pool;
        const String huge = BlockText(kBlock + 1000, 'h');
        const Vector<StringView> texts{"batch_big_before", StringView{huge}, "batch_big_after", "batch_big_before"};
        const Vector<PoolString> out = Batch(pool.storage, texts);
        for (size_t i = 0; i < texts.size(); ++i) {
            EXPECT_EQ(out[i].ToStringView(), texts[i]);
            EXPECT_EQ(pool.storage.Find(texts[i]).value_or(PoolString()), out[i]);
        }
        EXPECT_EQ(out[0], out[3]);
        EXPECT_EQ(out[1].CStr()[huge.size()], '\0');
        const PoolStringStats stats = pool.storage.GetStats();
        const size_t bytes = BytesOf({texts[0], texts[1], texts[2]});
        EXPECT_EQ(stats.blockCount, 1u);
        EXPECT_EQ(stats.blockBytes, bytes);
        EXPECT_EQ(stats.stringBytes, bytes);
        EXPECT_EQ(stats.stringCount, 3u);
    }

    TEST(PoolStringBatchTest, ABatchThatOutgrowsTheTableGrowsItOnce) {
        IsolatedPool pool(SIZE_MAX, 8);
        Vector<String> names;
        for (size_t i = 0; i < 300; ++i) {
            names.push_back(CounterName("batch_growth_", i));
        }
        const Vector<StringView> texts = ViewsOf(names);
        const Vector<PoolString> out = Batch(pool.storage, texts);
        const PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(stats.tableCapacity, TableCapacityFor(8, names.size()));
        EXPECT_EQ(stats.tableCapacity, 1024u);
        EXPECT_EQ(stats.tableUsed, names.size());
        EXPECT_EQ(stats.writeLocks, 1u);
        for (size_t i = 0; i < names.size(); ++i) {
            ASSERT_EQ(pool.storage.Find(texts[i]).value_or(PoolString()), out[i]) << i;
            ASSERT_EQ(out[i].ToStringView(), texts[i]);
        }
    }

    TEST(PoolStringBatchTest, EmptyTextsGiveTheEmptyPoolStringAndCountAsHits) {
        IsolatedPool pool;
        const Vector<StringView> texts{"", "batch_empty_x", "", ""};
        const Vector<PoolString> out = Batch(pool.storage, texts);
        EXPECT_EQ(out[0], PoolString());
        EXPECT_EQ(out[2], PoolString());
        EXPECT_EQ(out[3], PoolString());
        EXPECT_EQ(out[1].ToStringView(), "batch_empty_x");
        const PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(stats.hits, 3u);
        EXPECT_EQ(stats.misses, 1u);
        EXPECT_EQ(stats.stringCount, 1u);

        IsolatedPool onlyEmpty;
        const Vector<StringView> empties{"", ""};
        const Vector<PoolString> none = BatchWithHashes(onlyEmpty.storage, empties);
        EXPECT_EQ(none[0], PoolString());
        EXPECT_EQ(none[1], PoolString());
        const PoolStringStats emptyStats = onlyEmpty.storage.GetStats();
        EXPECT_EQ(emptyStats.hits, 2u);
        EXPECT_EQ(emptyStats.writeLocks, 0u);
        EXPECT_EQ(emptyStats.blockCount, 0u);
        EXPECT_EQ(emptyStats.stringCount, 0u);
    }

    TEST(PoolStringBatchTest, AnEmptyBatchDoesNothing) {
        IsolatedPool pool;
        const Vector<StringView> none;
        const Vector<PoolString> out = Batch(pool.storage, none);
        EXPECT_TRUE(out.empty());
        const PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(stats.writeLocks, 0u);
        EXPECT_EQ(stats.hits, 0u);
        EXPECT_EQ(stats.batchStrings, 0u);
        EXPECT_EQ(stats.blockCount, 0u);
    }

    TEST(PoolStringBatchTest, TheProcessPoolTakesBatchesToo) {
        const Vector<StringView> texts{"batch_process_one_8d2f", "batch_process_two_8d2f", "batch_process_one_8d2f", ""};
        Vector<PoolString> out(texts.size());
        PoolString::InternBatch(Span<const StringView>(texts.data(), texts.size()), Span<PoolString>(out.data(), out.size()));
        EXPECT_EQ(out[0], PoolString::Intern(StringView{"batch_process_one_8d2f"}));
        EXPECT_EQ(out[1], PoolString::Intern(StringView{"batch_process_two_8d2f"}));
        EXPECT_EQ(out[0], out[2]);
        EXPECT_EQ(out[3], PoolString());

        const Vector<uint64_t> hashes = HashesOf(texts);
        Vector<PoolString> again(texts.size());
        PoolString::InternBatch(Span<const StringView>(texts.data(), texts.size()), Span<const uint64_t>(hashes.data(), hashes.size()), Span<PoolString>(again.data(), again.size()));
        for (size_t i = 0; i < texts.size(); ++i) {
            EXPECT_EQ(again[i], out[i]);
        }
    }

#if ENGINE_ENABLE_ASSERTS
    TEST(PoolStringBatchTest, AWrongHashIsRefusedByAnAssertInDebug) {
        IsolatedPool pool;
        const Vector<StringView> texts{"batch_hash_a", "batch_hash_b"};
        Vector<uint64_t> hashes = HashesOf(texts);
        hashes[1] += 1;
        Vector<PoolString> out(texts.size());
        int asserts = 0;
        {
            AssertBridge::Capture capture([&](const AssertEvent&) { ++asserts; });
            pool.storage.InternBatch(Span<const StringView>(texts.data(), texts.size()), Span<const uint64_t>(hashes.data(), hashes.size()), Span<PoolString>(out.data(), out.size()));
        }
        EXPECT_EQ(asserts, 1);
    }
#endif

    TEST(PoolStringBatchTest, ThreadsBatchingOverlappingNamesWhileAnotherInternsAgree) {
        constexpr int kBatchThreads = 4;
        constexpr size_t kNames = 3000;
        Vector<String> names;
        for (size_t i = 0; i < kNames; ++i) {
            names.push_back(CounterName("batch_threads_", i));
        }
        const Vector<StringView> texts = ViewsOf(names);

        IsolatedPool pool(SIZE_MAX, 16);
        Vector<Vector<PoolString>> results(kBatchThreads + 1, Vector<PoolString>(kNames));
        const bool inTime = RunThreadsTogether(kBatchThreads + 1, kThreadsLimit, [&](int t) {
            if (t == kBatchThreads) {
                for (size_t i = 0; i < kNames; ++i) {
                    results[static_cast<size_t>(t)][i] = pool.storage.Intern(texts[kNames - 1 - i]);
                }
                return;
            }
            // Each thread batches the same names from another offset in chunks, so the batches overlap and race for the table
            constexpr size_t kChunk = 250;
            const size_t offset = kNames / kBatchThreads * static_cast<size_t>(t);
            for (size_t done = 0; done < kNames; done += kChunk) {
                Vector<StringView> chunk;
                Vector<size_t> positions;
                for (size_t k = done; k < done + kChunk && k < kNames; ++k) {
                    positions.push_back((offset + k) % kNames);
                    chunk.push_back(texts[positions.back()]);
                }
                const Vector<PoolString> out = Batch(pool.storage, chunk);
                for (size_t k = 0; k < chunk.size(); ++k) {
                    results[static_cast<size_t>(t)][positions[k]] = out[k];
                }
            }
        });
        ASSERT_TRUE(inTime) << "the threads did not finish in time";

        for (size_t i = 0; i < kNames; ++i) {
            for (int t = 0; t < kBatchThreads; ++t) {
                ASSERT_EQ(results[static_cast<size_t>(t)][i], results[0][i]) << names[i].c_str();
            }
            ASSERT_EQ(results[kBatchThreads][i], results[0][kNames - 1 - i]);
            ASSERT_EQ(results[0][i].ToStringView(), texts[i]);
            ASSERT_EQ(pool.storage.Find(texts[i]).value_or(PoolString()), results[0][i]);
        }
        const PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(stats.stringCount, kNames);
        EXPECT_EQ(stats.misses, kNames);
        EXPECT_EQ(stats.internCalls, kNames * (kBatchThreads + 1));
    }

    namespace {

        constexpr size_t kBenchNames = 20000;
        constexpr int kBenchRepeats = 5;

        template <typename Work>
        double BestMilliseconds(Work work) {
            double best = 1e30;
            for (int repeat = 0; repeat < kBenchRepeats; ++repeat) {
                const auto start = std::chrono::steady_clock::now();
                work(repeat);
                const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
                best = std::min(best, elapsed.count());
            }
            return best;
        }

    }  // namespace

    /// One batch against single `Intern` calls on 20000 names, new and already present. Run in macos-release, from the build folder:
    /// bin/BagiEngineTests --gtest_also_run_disabled_tests --gtest_filter='*InternBatchVsSingle*'
    TEST(PoolStringBatchBenchTest, DISABLED_InternBatchVsSingle) {
        Vector<String> names;
        for (size_t i = 0; i < kBenchNames; ++i) {
            names.push_back(CounterName("bench_batch_name_", i));
        }
        const Vector<StringView> texts = ViewsOf(names);
        const Vector<uint64_t> hashes = HashesOf(texts);
        constexpr size_t kBenchTable = 16384;

        const double newSingle = BestMilliseconds([&](int) {
            IsolatedPool pool(SIZE_MAX, kBenchTable);
            for (const StringView text : texts) {
                static_cast<void>(pool.storage.Intern(text));
            }
        });
        const double newBatch = BestMilliseconds([&](int) {
            IsolatedPool pool(SIZE_MAX, kBenchTable);
            static_cast<void>(Batch(pool.storage, texts));
        });

        IsolatedPool warm(SIZE_MAX, kBenchTable);
        static_cast<void>(Batch(warm.storage, texts));
        size_t sink = 0;
        const double hitsSingle = BestMilliseconds([&](int) {
            for (const StringView text : texts) {
                sink += warm.storage.Intern(text).Length();
            }
        });
        Vector<PoolString> out(texts.size());
        const double hitsBatch = BestMilliseconds([&](int) {
            warm.storage.InternBatch(Span<const StringView>(texts.data(), texts.size()), Span<const uint64_t>(hashes.data(), hashes.size()), Span<PoolString>(out.data(), out.size()));
            sink += out.back().Length();
        });
        std::printf("%zu names, best of %d, ms: new one by one %.3f, new batch %.3f, hits one by one %.3f, hits batch with hashes %.3f (sink %zu)\n", kBenchNames, kBenchRepeats, newSingle, newBatch,
                    hitsSingle, hitsBatch, sink);
    }

}  // namespace BECore::Tests
