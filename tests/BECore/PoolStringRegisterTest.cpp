#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/PoolStringStorage.h>
#include <BECore/PoolStringTestSupport.h>
#include <Common/Printers.h>

namespace BECore::Tests {

    namespace {

        using Details::PoolStringEntry;

        /// Ready entries as a cook or a `constinit` literal would leave them: built before the pool sees them, never written
        /// afterwards. A test keeps them alive longer than the storage they are registered in (declare them first).
        class ReadyEntries {
            struct Buffer {
                static constexpr size_t kBytes = 128;
                alignas(Details::kPoolEntryAlignment) char bytes[kBytes];
            };

            Vector<UniquePtr<Buffer>> _buffers;

        public:
            const PoolStringEntry& Make(StringView text) {
                EXPECT_LT(Details::PoolEntryBytes(text.size()), Buffer::kBytes);
                _buffers.push_back(MakeUnique<Buffer>());
                return *Details::ConstructPoolEntry(_buffers.back()->bytes, StringUtils::GetHash(text), text);
            }
        };

        /// True when `value` is the very entry `entry`
        bool IsEntry(const PoolString& value, const PoolStringEntry& entry) {
            return value.CStr() == entry.data;
        }

        Vector<PoolStaticRegistration> Registrations(const Vector<const PoolStringEntry*>& entries, Vector<PoolString>& values) {
            values.assign(entries.size(), PoolString());
            Vector<PoolStaticRegistration> registrations;
            for (size_t i = 0; i < entries.size(); ++i) {
                registrations.push_back({entries[i], &values[i]});
            }
            return registrations;
        }

        void RegisterAll(Details::PoolStringStorage& storage, const Vector<PoolStaticRegistration>& registrations) {
            storage.RegisterStatic(Span<const PoolStaticRegistration>(registrations.data(), registrations.size()));
        }

    }  // namespace

    TEST(PoolStringRegisterTest, ATextNeverInternedBecomesTheEntryItself) {
        ReadyEntries ready;
        const PoolStringEntry& entry = ready.Make("register_fresh_text");
        IsolatedPool pool;

        PoolString value;
        const PoolStaticRegistration registration{&entry, &value};
        pool.storage.RegisterStatic(Span<const PoolStaticRegistration>(&registration, 1));
        EXPECT_TRUE(IsEntry(value, entry));
        EXPECT_EQ(value.ToStringView(), "register_fresh_text");

        PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(stats.stringCount, 1u);
        EXPECT_EQ(stats.staticEntries.strings, 1u);
        EXPECT_EQ(stats.staticEntries.bytes, Details::PoolEntryBytes(entry.size));
        EXPECT_EQ(stats.staticEntries.blocks, 0u);
        EXPECT_EQ(stats.stringBytes, Details::PoolEntryBytes(entry.size));
        EXPECT_EQ(stats.blockCount, 0u);
        EXPECT_EQ(stats.blockBytes, 0u);
        EXPECT_EQ(stats.hits, 0u);
        EXPECT_EQ(stats.misses, 0u);
        EXPECT_EQ(stats.runtime.strings, 0u);
        EXPECT_EQ(stats.staticDuplicates, 0u);
        EXPECT_EQ(stats.writeLocks, 1u);

        EXPECT_EQ(pool.storage.Find("register_fresh_text").value_or(PoolString()), value);
        EXPECT_EQ(pool.storage.Intern("register_fresh_text"), value);
        stats = pool.storage.GetStats();
        EXPECT_EQ(stats.hits, 1u);
        EXPECT_EQ(stats.misses, 0u);
        EXPECT_EQ(stats.blockCount, 0u);
    }

    TEST(PoolStringRegisterTest, ATextInternedBeforeKeepsItsRuntimeEntry) {
        ReadyEntries ready;
        const PoolStringEntry& entry = ready.Make("register_runtime_first");
        IsolatedPool pool;
        const PoolString runtime = pool.storage.Intern("register_runtime_first");

        PoolString value;
        const PoolStaticRegistration registration{&entry, &value};
        pool.storage.RegisterStatic(Span<const PoolStaticRegistration>(&registration, 1));
        EXPECT_EQ(value, runtime);
        EXPECT_FALSE(IsEntry(value, entry));

        const PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(stats.stringCount, 1u);
        EXPECT_EQ(stats.staticDuplicates, 1u);
        EXPECT_EQ(stats.staticEntries.strings, 0u);
        EXPECT_EQ(stats.staticEntries.bytes, 0u);
        // The text was in the pool: nothing to insert, so the unique lock was not taken for the registration
        EXPECT_EQ(stats.writeLocks, 1u);
    }

    TEST(PoolStringRegisterTest, ATextInternedAfterGetsTheStaticEntryAndEqualityIsAPointerCompare) {
        ReadyEntries ready;
        const PoolStringEntry& entry = ready.Make("register_static_first");
        IsolatedPool pool;

        const PoolString registered = pool.storage.Intern("register_static_other");  // an unrelated string in the table
        static_cast<void>(registered);
        PoolString value;
        const PoolStaticRegistration registration{&entry, &value};
        pool.storage.RegisterStatic(Span<const PoolStaticRegistration>(&registration, 1));

        const PoolString runtime = pool.storage.Intern("register_static_first");
        EXPECT_EQ(runtime, value);
        EXPECT_TRUE(IsEntry(runtime, entry));
        EXPECT_FALSE(runtime != value);
        EXPECT_EQ(runtime.CStr(), value.CStr());
        EXPECT_NE(runtime, registered);
    }

    TEST(PoolStringRegisterTest, TheSameEntryRegisteredTwiceChangesNothing) {
        ReadyEntries ready;
        const PoolStringEntry& entry = ready.Make("register_twice");
        IsolatedPool pool;

        PoolString first;
        PoolString second;
        const PoolStaticRegistration one{&entry, &first};
        const PoolStaticRegistration two{&entry, &second};
        pool.storage.RegisterStatic(Span<const PoolStaticRegistration>(&one, 1));
        const PoolStringStats afterFirst = pool.storage.GetStats();
        pool.storage.RegisterStatic(Span<const PoolStaticRegistration>(&two, 1));
        const PoolStringStats afterSecond = pool.storage.GetStats();

        EXPECT_EQ(first, second);
        EXPECT_TRUE(IsEntry(second, entry));
        EXPECT_EQ(afterSecond.stringCount, 1u);
        EXPECT_EQ(afterSecond.staticEntries.strings, 1u);
        EXPECT_EQ(afterSecond.staticEntries.bytes, afterFirst.staticEntries.bytes);
        EXPECT_EQ(afterSecond.staticDuplicates, 0u);
        EXPECT_EQ(afterSecond.writeLocks, afterFirst.writeLocks);
    }

    TEST(PoolStringRegisterTest, AnotherEntryOfTheSameTextLosesToTheFirstAndIsCounted) {
        ReadyEntries ready;
        const PoolStringEntry& winner = ready.Make("register_two_entries");
        const PoolStringEntry& loser = ready.Make("register_two_entries");
        IsolatedPool pool;

        const PoolString first = [&] {
            PoolString value;
            const PoolStaticRegistration registration{&winner, &value};
            pool.storage.RegisterStatic(Span<const PoolStaticRegistration>(&registration, 1));
            return value;
        }();
        PoolString second;
        const PoolStaticRegistration registration{&loser, &second};
        pool.storage.RegisterStatic(Span<const PoolStaticRegistration>(&registration, 1));
        EXPECT_TRUE(IsEntry(first, winner));
        EXPECT_TRUE(IsEntry(second, winner));
        EXPECT_FALSE(IsEntry(second, loser));
        EXPECT_EQ(pool.storage.GetStats().staticDuplicates, 1u);
        EXPECT_EQ(pool.storage.GetStats().staticEntries.strings, 1u);
    }

    TEST(PoolStringRegisterTest, AnEntryOfAnEmptyTextIsTheEmptyPoolString) {
        ReadyEntries ready;
        const PoolStringEntry& entry = ready.Make("");
        IsolatedPool pool;
        PoolString value = pool.storage.Intern("something");
        const PoolStaticRegistration registration{&entry, &value};
        pool.storage.RegisterStatic(Span<const PoolStaticRegistration>(&registration, 1));
        EXPECT_EQ(value, PoolString());
        const PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(stats.stringCount, 1u);
        EXPECT_EQ(stats.staticEntries.strings, 0u);
        EXPECT_EQ(stats.staticDuplicates, 0u);
    }

    TEST(PoolStringRegisterTest, ABatchWithRepeatsAndInternedTextsGrowsTheTableOnce) {
        constexpr size_t kInterned = 10;
        constexpr size_t kNew = 40;
        constexpr size_t kLosers = 5;
        constexpr size_t kRepeatsOfOneEntry = 3;
        ReadyEntries ready;
        IsolatedPool pool(SIZE_MAX, 4);

        Vector<const PoolStringEntry*> entries;
        Vector<PoolString> runtime;
        for (size_t i = 0; i < kInterned; ++i) {
            const String name = CounterName("register_batch_interned_", i);
            runtime.push_back(pool.storage.Intern(StringView{name}));
            entries.push_back(&ready.Make(StringView{name}));
        }
        Vector<String> newNames;
        for (size_t i = 0; i < kNew; ++i) {
            newNames.push_back(CounterName("register_batch_new_", i));
            entries.push_back(&ready.Make(StringView{newNames.back()}));
        }
        for (size_t i = 0; i < kLosers; ++i) {
            entries.push_back(&ready.Make(StringView{newNames[i * 3]}));  // another entry of a text of this batch
        }
        for (size_t i = 0; i < kRepeatsOfOneEntry; ++i) {
            entries.push_back(entries[kInterned + 7]);  // the same entry again
        }
        const PoolStringStats before = pool.storage.GetStats();

        Vector<PoolString> values;
        RegisterAll(pool.storage, Registrations(entries, values));

        const PoolStringStats after = pool.storage.GetStats();
        EXPECT_EQ(after.stringCount, kInterned + kNew);
        EXPECT_EQ(after.staticEntries.strings, kNew);
        EXPECT_EQ(after.staticDuplicates, kInterned + kLosers);
        EXPECT_EQ(after.writeLocks, before.writeLocks + 1);
        EXPECT_EQ(after.tableCapacity, TableCapacityFor(4, kInterned + kNew));
        EXPECT_EQ(after.blockCount, before.blockCount);
        for (size_t i = 0; i < kInterned; ++i) {
            EXPECT_EQ(values[i], runtime[i]);
        }
        for (size_t i = 0; i < kNew; ++i) {
            EXPECT_TRUE(IsEntry(values[kInterned + i], *entries[kInterned + i]));
            EXPECT_EQ(pool.storage.Find(StringView{newNames[i]}).value_or(PoolString()), values[kInterned + i]);
        }
        for (size_t i = 0; i < kLosers; ++i) {
            EXPECT_EQ(values[kInterned + kNew + i], values[kInterned + i * 3]);
        }
        for (size_t i = 0; i < kRepeatsOfOneEntry; ++i) {
            EXPECT_EQ(values[kInterned + kNew + kLosers + i], values[kInterned + 7]);
        }
    }

    TEST(PoolStringRegisterTest, ABatchOfTextsAlreadyInThePoolTakesNoUniqueLock) {
        ReadyEntries ready;
        IsolatedPool pool;
        Vector<const PoolStringEntry*> entries;
        Vector<PoolString> runtime;
        for (size_t i = 0; i < 20; ++i) {
            const String name = CounterName("register_present_", i);
            runtime.push_back(pool.storage.Intern(StringView{name}));
            entries.push_back(&ready.Make(StringView{name}));
        }
        const PoolStringStats before = pool.storage.GetStats();
        Vector<PoolString> values;
        RegisterAll(pool.storage, Registrations(entries, values));
        const PoolStringStats after = pool.storage.GetStats();
        EXPECT_EQ(after.writeLocks, before.writeLocks);
        EXPECT_EQ(after.staticDuplicates, 20u);
        EXPECT_EQ(after.stringCount, before.stringCount);
        for (size_t i = 0; i < entries.size(); ++i) {
            EXPECT_EQ(values[i], runtime[i]);
        }
    }

    TEST(PoolStringRegisterTest, TheProcessPoolRegistersAnEntry) {
        ReadyEntries ready;
        const PoolStringEntry& entry = ready.Make("register_process_pool_text_5e1c");
        const PoolString value = PoolString::RegisterStatic(entry);
        EXPECT_TRUE(IsEntry(value, entry));
        EXPECT_EQ(PoolString::Intern(StringView{"register_process_pool_text_5e1c"}), value);
        EXPECT_EQ(PoolString::RegisterStatic(entry), value);
        EXPECT_EQ(PoolString::Find(StringView{"register_process_pool_text_5e1c"}).value_or(PoolString()), value);
    }

    TEST(PoolStringReserveTest, AnExactHintTakesOneBlockAndTheStringsFitIt) {
        constexpr size_t kNames = 60;
        Vector<String> names;
        size_t bytes = 0;
        for (size_t i = 0; i < kNames; ++i) {
            names.push_back(CounterName("reserve_exact_", i));
            bytes += Details::PoolEntryBytes(names.back().size());
        }
        IsolatedPool pool(SIZE_MAX, 8);
        pool.storage.Reserve(kNames, bytes);
        const PoolStringStats reserved = pool.storage.GetStats();
        EXPECT_EQ(reserved.stringCount, 0u);
        EXPECT_EQ(reserved.blockCount, 1u);
        EXPECT_EQ(reserved.blockBytes, bytes);
        EXPECT_EQ(reserved.tableCapacity, TableCapacityFor(8, kNames));
        EXPECT_EQ(reserved.writeLocks, 1u);

        Vector<PoolString> interned;
        for (const String& name : names) {
            interned.push_back(pool.storage.Intern(StringView{name}));
        }
        const PoolStringStats after = pool.storage.GetStats();
        EXPECT_EQ(after.blockCount, 1u);
        EXPECT_EQ(after.blockBytes, bytes);
        EXPECT_EQ(after.tableCapacity, reserved.tableCapacity);
        EXPECT_EQ(after.stringCount, kNames);
        EXPECT_EQ(after.stringBytes, bytes);
        for (size_t i = 0; i < kNames; ++i) {
            EXPECT_EQ(pool.storage.Find(StringView{names[i]}).value_or(PoolString()), interned[i]);
        }
    }

    TEST(PoolStringReserveTest, AShortHintLetsTheRestTakeStandardBlocks) {
        constexpr size_t kNames = 100;
        IsolatedPool pool(SIZE_MAX, 8);
        size_t firstBytes = 0;
        Vector<String> names;
        for (size_t i = 0; i < kNames; ++i) {
            names.push_back(CounterName("reserve_short_name_with_some_length_", i));
            if (i < 10) {
                firstBytes += Details::PoolEntryBytes(names.back().size());
            }
        }
        pool.storage.Reserve(10, firstBytes);
        Vector<PoolString> interned;
        for (const String& name : names) {
            interned.push_back(pool.storage.Intern(StringView{name}));
        }
        const PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(stats.stringCount, kNames);
        EXPECT_EQ(stats.blockCount, 2u);
        EXPECT_EQ(stats.blockBytes, firstBytes + PoolString::kBlockBytes);
        EXPECT_EQ(stats.tableCapacity, TableCapacityFor(8, kNames));
        for (size_t i = 0; i < kNames; ++i) {
            ASSERT_EQ(pool.storage.Find(StringView{names[i]}).value_or(PoolString()), interned[i]);
            ASSERT_EQ(interned[i].ToStringView(), StringView{names[i]});
        }
    }

    TEST(PoolStringReserveTest, MoreStringsThanTheTableHoldsGrowItOnceAtReserve) {
        IsolatedPool pool(SIZE_MAX, 8);
        pool.storage.Reserve(1000, 0);
        PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(stats.tableCapacity, TableCapacityFor(8, 1000));
        EXPECT_EQ(stats.tableCapacity, 2048u);
        EXPECT_EQ(stats.blockCount, 0u);
        EXPECT_EQ(stats.blockBytes, 0u);
        EXPECT_EQ(stats.stringCount, 0u);

        for (size_t i = 0; i < 1000; ++i) {
            static_cast<void>(pool.storage.Intern(StringView{CounterName("reserve_table_", i)}));
        }
        EXPECT_EQ(pool.storage.GetStats().tableCapacity, 2048u);
    }

    TEST(PoolStringReserveTest, AHintForZeroBytesTakesNoBlockAndOneThatFitsTheCurrentBlockNeitherDoes) {
        IsolatedPool pool;
        pool.storage.Reserve(0, 0);
        EXPECT_EQ(pool.storage.GetStats().blockCount, 0u);

        static_cast<void>(pool.storage.Intern("reserve_current"));
        const PoolStringStats before = pool.storage.GetStats();
        ASSERT_EQ(before.blockCount, 1u);
        pool.storage.Reserve(2, 64);
        const PoolStringStats after = pool.storage.GetStats();
        EXPECT_EQ(after.blockCount, 1u);
        EXPECT_EQ(after.blockBytes, before.blockBytes);
        EXPECT_EQ(after.tableCapacity, before.tableCapacity);
    }

    TEST(PoolStringReserveTest, ABlockSizeThatIsNotAMultipleOfEightIsRounded) {
        IsolatedPool pool;
        pool.storage.Reserve(0, 61);
        EXPECT_EQ(pool.storage.GetStats().blockBytes, 64u);
    }

    TEST(PoolStringReserveTest, StringsInternedBeforeAGrowingReserveStayTheSame) {
        IsolatedPool pool(SIZE_MAX, 8);
        Vector<String> names;
        Vector<PoolString> interned;
        for (size_t i = 0; i < 3; ++i) {
            names.push_back(CounterName("reserve_before_", i));
            interned.push_back(pool.storage.Intern(StringView{names.back()}));
        }
        const char* const firstText = interned[0].CStr();
        pool.storage.Reserve(500, 4096);
        EXPECT_EQ(pool.storage.GetStats().tableCapacity, TableCapacityFor(8, 503));
        for (size_t i = 0; i < names.size(); ++i) {
            EXPECT_EQ(pool.storage.Find(StringView{names[i]}).value_or(PoolString()), interned[i]);
            EXPECT_EQ(pool.storage.Intern(StringView{names[i]}), interned[i]);
        }
        EXPECT_EQ(interned[0].CStr(), firstText);
        EXPECT_EQ(pool.storage.GetStats().stringCount, names.size());
    }

    namespace {

        constexpr size_t kMaxReservedStrings = PoolEntryTable<PoolStringEntry>::kMaxReservedEntries;

        /// A pool that holds a few strings, so `Used()` is not 0 when the hint comes
        void InternSome(IsolatedPool& pool, size_t count) {
            for (size_t i = 0; i < count; ++i) {
                static_cast<void>(pool.storage.Intern(StringView{CounterName("reserve_some_", i)}));
            }
        }

    }  // namespace

    TEST(PoolStringReserveTest, AStringCountThatCannotBeRightIsIgnored) {
        IsolatedPool pool(SIZE_MAX, 8);
        InternSome(pool, 3);
        const PoolStringStats before = pool.storage.GetStats();

        const size_t hints[] = {SIZE_MAX, SIZE_MAX / 2 + 1, SIZE_MAX / 2, SIZE_MAX - before.tableUsed + 1, SIZE_MAX - before.tableUsed, kMaxReservedStrings + 1, kMaxReservedStrings};
        for (const size_t strings : hints) {
            SCOPED_TRACE(strings);
            pool.storage.Reserve(strings, 0);
            const PoolStringStats after = pool.storage.GetStats();
            EXPECT_EQ(after.tableCapacity, before.tableCapacity);
            EXPECT_EQ(after.blockCount, before.blockCount);
            EXPECT_EQ(after.stringCount, before.stringCount);
        }
        EXPECT_EQ(pool.source.served, 1u);
    }

    TEST(PoolStringReserveTest, ASumThatPassesTheLimitIsIgnoredEvenWhenEachPartIsSmall) {
        IsolatedPool pool(SIZE_MAX, 8);
        InternSome(pool, 3);
        const size_t capacity = pool.storage.GetStats().tableCapacity;
        pool.storage.Reserve(kMaxReservedStrings - 2, 0);  // 3 are in the table: one more than the limit
        EXPECT_EQ(pool.storage.GetStats().tableCapacity, capacity);
    }

    TEST(PoolStringReserveTest, AByteCountThatCannotBeRightTakesNoBlock) {
        IsolatedPool pool;
        const size_t hints[] = {SIZE_MAX, SIZE_MAX - 3, SIZE_MAX - 7, SIZE_MAX - 8, kDefaultReservedBytes + 1, size_t{1} << 40};
        for (const size_t bytes : hints) {
            SCOPED_TRACE(bytes);
            pool.storage.Reserve(0, bytes);
            const PoolStringStats stats = pool.storage.GetStats();
            EXPECT_EQ(stats.blockCount, 0u);
            EXPECT_EQ(stats.blockBytes, 0u);
            EXPECT_EQ(pool.source.served + pool.source.refused, 0u);
        }
    }

    TEST(PoolStringReserveTest, AHintAtTheLimitsStillWorks) {
        IsolatedPool pool(SIZE_MAX, 8);
        pool.storage.Reserve(kMaxReservedStrings, kDefaultReservedBytes);
        const PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(stats.tableCapacity, TableCapacityFor(8, kMaxReservedStrings));
        EXPECT_EQ(stats.blockCount, 1u);
        EXPECT_EQ(stats.blockBytes, kDefaultReservedBytes);
        EXPECT_EQ(pool.source.served, 1u);
    }

    TEST(PoolStringReserveTest, AllocateBlockOfANormalSizeIsRoundedAndNeverNull) {
        IsolatedPool pool;
        const PoolBlock block = pool.storage.AllocateBlock(4093);
        EXPECT_TRUE(block.Data() != nullptr);
        EXPECT_EQ(block.Capacity(), 4096u);
    }

    TEST(PoolStringReserveTest, TheProcessPoolTakesAHint) {
        const PoolStringStats before = PoolString::GetStats();
        PoolString::Reserve(10, 1024);
        const PoolStringStats after = PoolString::GetStats();
        EXPECT_EQ(after.stringCount, before.stringCount);
        EXPECT_GE(after.tableCapacity, before.tableCapacity);
    }

}  // namespace BECore::Tests
