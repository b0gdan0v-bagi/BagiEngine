#include <BECore/PoolString/PoolBlock.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/PoolStringMap.h>
#include <BECore/PoolString/StaticPoolString.h>
#include <BECore/PoolStringTestSupport.h>
#include <Common/Printers.h>
#include <chrono>
#include <cstring>

// The pool is the process's and cannot un-intern, so each test uses literals no other test names. Counters are deltas
// of two snapshots of the shared pool: nothing else interns while a test runs.

namespace BECore::Tests {

    namespace {

        constexpr std::chrono::milliseconds kThreadLimit{10000};

        bool IsEntry(const PoolString& value, const Details::PoolStringEntry& entry) {
            return value.CStr() == entry.data;
        }

    }  // namespace

    TEST(PoolStringStaticTest, ARuntimeInternFirstKeepsItsEntryAndTheLiteralLoses) {
        using Literal = StaticPoolString<"static_runtime_first_41c9">;
        const PoolString runtime = PoolString::Intern("static_runtime_first_41c9");
        const PoolStringStats before = PoolString::GetStats();

        const PoolString literal = Literal::ToPoolString();
        const PoolStringStats after = PoolString::GetStats();

        EXPECT_EQ(literal, runtime);
        EXPECT_EQ(literal.CStr(), runtime.CStr());
        EXPECT_FALSE(IsEntry(literal, Literal::Entry()));
        EXPECT_EQ(after.staticDuplicates, before.staticDuplicates + 1);
        EXPECT_EQ(after.staticEntries.strings, before.staticEntries.strings);
        EXPECT_EQ(after.stringCount, before.stringCount);
    }

    TEST(PoolStringStaticTest, ALiteralFirstIsTheEntryOfEveryLaterRuntimeCall) {
        using Literal = StaticPoolString<"static_literal_first_7be2">;
        const PoolStringStats before = PoolString::GetStats();

        const PoolString literal = Literal::ToPoolString();
        const PoolStringStats registered = PoolString::GetStats();
        EXPECT_TRUE(IsEntry(literal, Literal::Entry()));
        EXPECT_EQ(registered.staticEntries.strings, before.staticEntries.strings + 1);
        EXPECT_EQ(registered.staticEntries.bytes, before.staticEntries.bytes + Details::PoolEntryBytes(literal.Length()));
        EXPECT_EQ(registered.staticEntries.blocks, before.staticEntries.blocks);
        EXPECT_EQ(registered.runtime.strings, before.runtime.strings);
        EXPECT_EQ(registered.runtime.bytes, before.runtime.bytes);
        EXPECT_EQ(registered.blockBytes, before.blockBytes);
        EXPECT_EQ(registered.blockCount, before.blockCount);
        EXPECT_EQ(registered.hits, before.hits);
        EXPECT_EQ(registered.misses, before.misses);
        EXPECT_EQ(registered.staticDuplicates, before.staticDuplicates);

        const PoolString interned = PoolString::Intern("static_literal_first_7be2");
        const StringView texts[] = {"static_literal_first_7be2"};
        PoolString batched[1];
        PoolString::InternBatch(Span<const StringView>(texts, 1), Span<PoolString>(batched, 1));
        const auto found = PoolString::Find("static_literal_first_7be2");
        ASSERT_TRUE(found.has_value());

        EXPECT_EQ(interned, literal);
        EXPECT_EQ(batched[0], literal);
        EXPECT_EQ(*found, literal);
        EXPECT_TRUE(IsEntry(interned, Literal::Entry()));
        EXPECT_TRUE(IsEntry(batched[0], Literal::Entry()));

        const PoolStringStats after = PoolString::GetStats();
        EXPECT_EQ(after.runtime.strings, before.runtime.strings);
        EXPECT_EQ(after.runtime.bytes, before.runtime.bytes);
        EXPECT_EQ(after.blockBytes, before.blockBytes);
        EXPECT_EQ(after.blockCount, before.blockCount);
        EXPECT_EQ(after.stringCount, before.stringCount + 1);
        EXPECT_EQ(after.staticDuplicates, before.staticDuplicates);
    }

    TEST(PoolStringStaticTest, ALiteralTakesTheEntryOfAnAdoptedBlock) {
        using Literal = StaticPoolString<"static_adopted_first_d305">;
        constexpr StringView kText = "static_adopted_first_d305";
        PoolBlock block = PoolString::AllocateBlock(Details::PoolEntryBytes(kText.size()));
        const char* const base = block.Data();
        Details::ConstructPoolEntry(block.Data(), StringUtils::GetHash(kText), kText);
        PoolString adopted[1];
        const auto result = PoolString::AdoptBlock(block, Details::PoolEntryBytes(kText.size()), Span<PoolString>(adopted, 1));
        ASSERT_TRUE(result.has_value());
        const PoolStringStats before = PoolString::GetStats();

        const PoolString literal = Literal::ToPoolString();

        EXPECT_EQ(literal, adopted[0]);
        EXPECT_EQ(literal.CStr(), base + offsetof(Details::PoolStringEntry, data));
        EXPECT_FALSE(IsEntry(literal, Literal::Entry()));
        const PoolStringStats after = PoolString::GetStats();
        EXPECT_EQ(after.staticDuplicates, before.staticDuplicates + 1);
        EXPECT_EQ(after.staticEntries.strings, before.staticEntries.strings);
    }

    TEST(PoolStringStaticTest, ToPoolStringTwiceIsOneRegistration) {
        using Literal = StaticPoolString<"static_twice_a4f8">;
        const PoolStringStats before = PoolString::GetStats();

        const PoolString first = Literal::ToPoolString();
        const PoolString second = Literal::ToPoolString();
        const PoolString viaConversion = PoolString(Literal{});

        EXPECT_EQ(first, second);
        EXPECT_EQ(first, viaConversion);
        const PoolStringStats after = PoolString::GetStats();
        EXPECT_EQ(after.staticEntries.strings, before.staticEntries.strings + 1);
        EXPECT_EQ(after.writeLocks, before.writeLocks + 1);
        EXPECT_EQ(after.staticDuplicates, before.staticDuplicates);
    }

    TEST(PoolStringStaticTest, ThreadsCallingAFreshLiteralAtOnceShareOneEntry) {
        using Literal = StaticPoolString<"static_threads_9d12">;
        constexpr int kThreads = 8;
        const PoolStringStats before = PoolString::GetStats();
        PoolString results[kThreads];

        EXPECT_TRUE(RunThreadsTogether(kThreads, kThreadLimit, [&](int t) { results[t] = Literal::ToPoolString(); }));

        for (const PoolString& result : results) {
            EXPECT_EQ(result, results[0]);
            EXPECT_TRUE(IsEntry(result, Literal::Entry()));
        }
        const PoolStringStats after = PoolString::GetStats();
        EXPECT_EQ(after.staticEntries.strings, before.staticEntries.strings + 1);
        EXPECT_EQ(after.staticDuplicates, before.staticDuplicates);
    }

    TEST(PoolStringStaticTest, TheEmptyLiteralIsTheEmptyPoolString) {
        static_assert(""_intern == PoolString());
        EXPECT_EQ(StaticPoolString<"">::ToPoolString(), PoolString());
        EXPECT_TRUE(StaticPoolString<"">::ToPoolString().Empty());
        const PoolStringStats before = PoolString::GetStats();
        static_cast<void>(StaticPoolString<"">::ToPoolString());
        EXPECT_EQ(PoolString::GetStats().stringCount, before.stringCount);
    }

    TEST(PoolStringStaticTest, ALiteralComparesWithRuntimeStrings) {
        constexpr auto literal = "static_compare_b"_intern;
        const PoolString same = PoolString::Intern("static_compare_b");
        const PoolString lower = PoolString::Intern("static_compare_a");
        const PoolString higher = PoolString::Intern("static_compare_c");

        EXPECT_TRUE(literal == same);
        EXPECT_TRUE(same == literal);
        EXPECT_FALSE(literal != same);
        EXPECT_TRUE(literal != lower);
        EXPECT_TRUE(lower < literal);
        EXPECT_TRUE(literal < higher);
        EXPECT_TRUE(literal > lower);
        EXPECT_TRUE(higher > literal);
        EXPECT_TRUE(literal <= same);
        EXPECT_TRUE(literal >= same);
        EXPECT_FALSE(literal < same);
        EXPECT_EQ(static_cast<PoolString>(literal), same);
        EXPECT_EQ(literal.View(), "static_compare_b");
        EXPECT_TRUE(literal == "static_compare_b"_intern);

        constexpr uint64_t hash = "static_compare_b"_intern.HashValue();
        EXPECT_EQ(hash, StringUtils::GetHash("static_compare_b"));
        EXPECT_EQ(hash, same.HashValue());
        EXPECT_EQ(hash, StaticPoolString<"static_compare_b">::ToPoolString().HashValue());
    }

    TEST(PoolStringStaticTest, TheEntryHasTheLayoutOfAPoolEntry) {
        using Literal = StaticPoolString<"static_layout_5e70">;
        constexpr StringView kText = "static_layout_5e70";
        const Details::PoolStringEntry& entry = Literal::Entry();

        EXPECT_EQ(entry.hash, StringUtils::GetHash(kText));
        EXPECT_EQ(entry.size, kText.size());
        EXPECT_EQ(entry.ToStringView(), kText);
        EXPECT_EQ(entry.data[entry.size], '\0');
        EXPECT_EQ(std::memcmp(entry.data, kText.data(), kText.size() + 1), 0);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(&entry) % Details::kPoolEntryAlignment, 0u);
        EXPECT_EQ(Literal::ToPoolString().HashValue(), entry.hash);
    }

    TEST(PoolStringStaticTest, TwoLiteralsOfOneTextShareOneStore) {
        using First = StaticPoolString<"static_shared_store_2a6b">;
        using Second = StaticPoolString<"static_shared_store_2a6b">;
        static_assert(eastl::is_same_v<First, Second>);
        EXPECT_EQ(&First::Entry(), &Second::Entry());
        EXPECT_EQ(First::ToPoolString(), Second::ToPoolString());
    }

}  // namespace BECore::Tests
