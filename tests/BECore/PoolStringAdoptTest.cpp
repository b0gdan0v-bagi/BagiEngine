#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/PoolStringStorage.h>
#include <BECore/PoolStringTestSupport.h>
#include <Common/Printers.h>
#include <cstring>

namespace BECore::Tests {

    namespace {

        using Details::ConstructPoolEntry;
        using Details::PoolEntryBytes;
        using Details::PoolStringEntry;

        constexpr size_t kBlock = PoolString::kBlockBytes;
        constexpr size_t kEntryHeader = offsetof(PoolStringEntry, data);

        /// A block of `AllocateBlock` filled the way a cook would lay it out: entries one after another
        struct BuiltBlock {
            PoolBlock block;
            size_t used = 0;
            Vector<size_t> offsets;
        };

        size_t BytesOf(const Vector<String>& texts) {
            size_t bytes = 0;
            for (const String& text : texts) {
                bytes += PoolEntryBytes(text.size());
            }
            return bytes;
        }

        /// The block is exactly as large as the entries, so a read past `used` is a read past the allocation (ASan)
        BuiltBlock Build(Details::PoolStringStorage& storage, const Vector<String>& texts) {
            BuiltBlock built;
            built.used = BytesOf(texts);
            built.block = storage.AllocateBlock(built.used);
            size_t offset = 0;
            for (const String& text : texts) {
                built.offsets.push_back(offset);
                ConstructPoolEntry(built.block.Data() + offset, StringUtils::GetHash(text), text);
                offset += PoolEntryBytes(text.size());
            }
            return built;
        }

        std::expected<PoolAdoptResult, PoolAdoptError> Adopt(Details::PoolStringStorage& storage, BuiltBlock& built, Vector<PoolString>* out = nullptr, bool verify = true) {
            if (out != nullptr) {
                out->assign(built.offsets.size(), PoolString());
            }
            const Span<PoolString> span = out != nullptr ? Span<PoolString>(out->data(), out->size()) : Span<PoolString>();
            return storage.AdoptBlock(built.block, built.used, span, PoolAdoptOptions{verify});
        }

        Vector<String> Names(StringView prefix, size_t count) {
            Vector<String> names;
            for (size_t i = 0; i < count; ++i) {
                names.push_back(CounterName(prefix, i));
            }
            return names;
        }

        /// The numbers a refused call must leave as they were
        void ExpectUnchanged(const PoolStringStats& before, const PoolStringStats& after) {
            EXPECT_EQ(after.stringCount, before.stringCount);
            EXPECT_EQ(after.tableCapacity, before.tableCapacity);
            EXPECT_EQ(after.blockCount, before.blockCount);
            EXPECT_EQ(after.blockBytes, before.blockBytes);
            EXPECT_EQ(after.adopted.strings, before.adopted.strings);
            EXPECT_EQ(after.adopted.blocks, before.adopted.blocks);
            EXPECT_EQ(after.deadStrings, before.deadStrings);
            EXPECT_EQ(after.deadBytes, before.deadBytes);
            EXPECT_EQ(after.stringBytes, before.stringBytes);
        }

        bool InsideBlock(const PoolBlock& block, size_t used, const char* pointer) {
            return pointer >= block.Data() && pointer < block.Data() + used;
        }

    }  // namespace

    TEST(PoolStringAdoptTest, DistinctEntriesAreLinkedWithoutACopy) {
        IsolatedPool pool(SIZE_MAX, 16384);
        const Vector<String> texts = Names("adopt_distinct_", 100);
        BuiltBlock built = Build(pool.storage, texts);

        Vector<PoolString> out;
        const auto result = Adopt(pool.storage, built, &out);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->linkedStrings, 100u);
        EXPECT_EQ(result->deadStrings, 0u);
        EXPECT_EQ(result->deadBytes, 0u);

        const PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(stats.stringCount, 100u);
        EXPECT_EQ(stats.adopted.strings, 100u);
        EXPECT_EQ(stats.adopted.bytes, built.used);
        EXPECT_EQ(stats.stringBytes, built.used);
        EXPECT_EQ(stats.adopted.blocks, 1u);
        EXPECT_EQ(stats.adopted.blockBytes, built.block.Capacity());
        EXPECT_EQ(stats.blockCount, 1u);
        EXPECT_EQ(stats.runtime.strings, 0u);
        EXPECT_EQ(stats.deadStrings, 0u);

        for (size_t i = 0; i < texts.size(); ++i) {
            const Optional<PoolString> found = pool.storage.Find(texts[i]);
            ASSERT_TRUE(found.has_value());
            EXPECT_EQ(out[i], *found);
            EXPECT_EQ(out[i].ToStringView(), texts[i]);
            EXPECT_EQ(found->CStr(), built.block.Data() + built.offsets[i] + kEntryHeader);
        }
        // The pool answers `Intern` with the adopted entry: a hit, no copy
        EXPECT_EQ(pool.storage.Intern(texts[7]).CStr(), built.block.Data() + built.offsets[7] + kEntryHeader);
        EXPECT_EQ(pool.storage.GetStats().misses, 0u);
    }

    TEST(PoolStringAdoptTest, AnEmptyOutIsAllowed) {
        IsolatedPool pool;
        BuiltBlock built = Build(pool.storage, Names("adopt_no_out_", 3));
        const auto result = Adopt(pool.storage, built);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->linkedStrings, 3u);
        EXPECT_EQ(pool.storage.GetStats().stringCount, 3u);
    }

    TEST(PoolStringAdoptTest, AnEmptyBlockAdoptsNothing) {
        IsolatedPool pool;
        PoolBlock block = pool.storage.AllocateBlock(64);
        const auto result = pool.storage.AdoptBlock(block, 0, {}, {});
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->linkedStrings, 0u);
        const PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(stats.stringCount, 0u);
        EXPECT_EQ(stats.adopted.blocks, 1u);
    }

    TEST(PoolStringAdoptTest, DuplicatesOfTheContentOfThePoolAreDeadWeight) {
        IsolatedPool pool;
        const PoolString runtime = pool.storage.Intern("adopt_dup_runtime");

        alignas(Details::kPoolEntryAlignment) char staticBuffer[64];
        const PoolStringEntry* staticEntry = ConstructPoolEntry(staticBuffer, StringUtils::GetHash("adopt_dup_static"), "adopt_dup_static");
        PoolString staticValue;
        const PoolStaticRegistration registration{staticEntry, &staticValue};
        pool.storage.RegisterStatic(Span<const PoolStaticRegistration>(&registration, 1));
        ASSERT_EQ(staticValue.CStr(), staticEntry->data);
        const size_t before = pool.storage.GetStats().stringCount;

        // adopt_dup_runtime, new_a, adopt_dup_static, new_a (inside the block), new_b, adopt_dup_runtime
        const Vector<String> texts = {"adopt_dup_runtime", "adopt_dup_new_a", "adopt_dup_static", "adopt_dup_new_a", "adopt_dup_new_b", "adopt_dup_runtime"};
        BuiltBlock built = Build(pool.storage, texts);
        Vector<PoolString> out;
        const auto result = Adopt(pool.storage, built, &out);
        ASSERT_TRUE(result.has_value());

        const size_t deadBytes = PoolEntryBytes(texts[0].size()) + PoolEntryBytes(texts[2].size()) + PoolEntryBytes(texts[3].size()) + PoolEntryBytes(texts[5].size());
        EXPECT_EQ(result->linkedStrings, 2u);
        EXPECT_EQ(result->deadStrings, 4u);
        EXPECT_EQ(result->deadBytes, deadBytes);

        EXPECT_EQ(out[0], runtime);
        EXPECT_EQ(out[5], runtime);
        EXPECT_EQ(out[2], staticValue);
        EXPECT_EQ(out[1].CStr(), built.block.Data() + built.offsets[1] + kEntryHeader);
        EXPECT_EQ(out[3], out[1]);
        EXPECT_EQ(out[4].CStr(), built.block.Data() + built.offsets[4] + kEntryHeader);

        const PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(stats.stringCount, before + 2);
        EXPECT_EQ(stats.adopted.strings, 2u);
        EXPECT_EQ(stats.adopted.bytes, PoolEntryBytes(texts[1].size()) + PoolEntryBytes(texts[4].size()));
        EXPECT_EQ(stats.deadStrings, 4u);
        EXPECT_EQ(stats.deadBytes, deadBytes);
        EXPECT_EQ(pool.storage.Intern("adopt_dup_runtime"), runtime);
    }

    TEST(PoolStringAdoptTest, AnEntryOfTheEmptyTextIsDead) {
        IsolatedPool pool;
        const Vector<String> texts = {"", "adopt_empty_a", ""};
        BuiltBlock built = Build(pool.storage, texts);
        Vector<PoolString> out;
        const auto result = Adopt(pool.storage, built, &out);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->linkedStrings, 1u);
        EXPECT_EQ(result->deadStrings, 2u);
        EXPECT_EQ(result->deadBytes, 2 * PoolEntryBytes(0));
        EXPECT_EQ(out[0], PoolString());
        EXPECT_TRUE(out[0].Empty());
        EXPECT_EQ(out[2], PoolString());
        EXPECT_EQ(out[1].ToStringView(), "adopt_empty_a");
        EXPECT_EQ(pool.storage.GetStats().stringCount, 1u);
    }

    TEST(PoolStringAdoptTest, ABlockIsAdoptedOnce) {
        IsolatedPool pool;
        BuiltBlock built = Build(pool.storage, Names("adopt_twice_", 5));
        ASSERT_TRUE(Adopt(pool.storage, built).has_value());
        const PoolStringStats before = pool.storage.GetStats();

        const auto second = Adopt(pool.storage, built);
        ASSERT_FALSE(second.has_value());
        EXPECT_EQ(second.error(), PoolAdoptError::AlreadyAdopted);
        const PoolStringStats after = pool.storage.GetStats();
        ExpectUnchanged(before, after);
        EXPECT_EQ(after.writeLocks, before.writeLocks);
    }

    TEST(PoolStringAdoptTest, AMovedFromTokenAndATokenOfAnotherStorageAreRefused) {
        IsolatedPool pool;
        IsolatedPool other;
        BuiltBlock built = Build(pool.storage, Names("adopt_token_", 3));
        const PoolStringStats before = pool.storage.GetStats();

        // The same index and size in the other storage must not make a foreign token pass
        BuiltBlock twin = Build(other.storage, Names("adopt_token_", 3));
        const PoolStringStats otherBefore = other.storage.GetStats();
        const auto foreign = pool.storage.AdoptBlock(twin.block, twin.used, {}, {});
        ASSERT_FALSE(foreign.has_value());
        EXPECT_EQ(foreign.error(), PoolAdoptError::NotOwned);

        PoolBlock moved = eastl::move(built.block);
        EXPECT_TRUE(built.block.Data() == nullptr);
        EXPECT_EQ(built.block.Capacity(), 0u);
        const auto empty = pool.storage.AdoptBlock(built.block, built.used, {}, {});
        ASSERT_FALSE(empty.has_value());
        EXPECT_EQ(empty.error(), PoolAdoptError::NotOwned);

        PoolBlock neverGiven;
        const auto defaulted = pool.storage.AdoptBlock(neverGiven, 0, {}, {});
        ASSERT_FALSE(defaulted.has_value());
        EXPECT_EQ(defaulted.error(), PoolAdoptError::NotOwned);

        ExpectUnchanged(before, pool.storage.GetStats());
        ExpectUnchanged(otherBefore, other.storage.GetStats());

        // The token that was moved into is the live one
        const auto result = pool.storage.AdoptBlock(moved, built.used, {}, {});
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->linkedStrings, 3u);
    }

    namespace {

        /// One block of `capacity` bytes whose first entry is written by `ConstructPoolEntry` and then spoilt by `spoil`
        struct MalformedCase {
            const char* name;
            size_t capacity;
            size_t usedBytes;
            PoolAdoptError error;
            void (*write)(char* data);
        };

        void WriteGoodEntry(char* data, StringView text) {
            ConstructPoolEntry(data, StringUtils::GetHash(text), text);
        }

        void SetSize(char* data, uint32_t size) {
            std::memcpy(data + offsetof(PoolStringEntry, size), &size, sizeof(size));
        }

    }  // namespace

    TEST(PoolStringAdoptTest, MalformedBlocksAreRefusedAndLinkNothing) {
        const MalformedCase cases[] = {
            {"UsedBytesBeyondCapacity", 32, 40, PoolAdoptError::UsedBytesTooLarge, [](char* data) { WriteGoodEntry(data, "abc"); }},
            {"UsedBytesNotAMultipleOf8", 32, 20, PoolAdoptError::UsedBytesNotAligned, [](char* data) { WriteGoodEntry(data, "abc"); }},
            {"SizeRunsPastUsedBytes", 32, 32, PoolAdoptError::TruncatedEntry,
             [](char* data) {
                 WriteGoodEntry(data, "abc");
                 SetSize(data, 100);
             }},
            {"SizeOfFFFFFFFF", 32, 32, PoolAdoptError::TruncatedEntry,
             [](char* data) {
                 WriteGoodEntry(data, "abc");
                 SetSize(data, 0xFFFFFFFFu);
             }},
            {"SizeFillsTheBlockExactlyButHasNoRoomForTheNul", 16, 16, PoolAdoptError::TruncatedEntry,
             [](char* data) {
                 WriteGoodEntry(data, "abc");
                 SetSize(data, 4);
             }},
            {"MissingNul", 16, 16, PoolAdoptError::MissingNul,
             [](char* data) {
                 WriteGoodEntry(data, "abc");
                 data[kEntryHeader + 3] = 'x';
             }},
            {"HeaderCutByTheEnd", 24, 24, PoolAdoptError::TruncatedEntry,
             [](char* data) {
                 WriteGoodEntry(data, "abc");
                 std::memset(data + 16, 0, 8);
             }},
            {"SecondEntryIsBadAfterAGoodFirst", 40, 40, PoolAdoptError::TruncatedEntry,
             [](char* data) {
                 WriteGoodEntry(data, "abc");
                 WriteGoodEntry(data + 16, "abcdefghij");
                 SetSize(data + 16, 0xFFFFFFF0u);
             }},
            {"WrongHash", 16, 16, PoolAdoptError::WrongHash,
             [](char* data) {
                 const uint64_t hash = StringUtils::GetHash("abc") + 1;
                 ConstructPoolEntry(data, hash, "abc");
             }},
        };

        for (const MalformedCase& malformed : cases) {
            SCOPED_TRACE(malformed.name);
            IsolatedPool pool;
            PoolBlock block = pool.storage.AllocateBlock(malformed.capacity);
            ASSERT_EQ(block.Capacity(), malformed.capacity);
            std::memset(block.Data(), 0, block.Capacity());
            malformed.write(block.Data());
            const PoolStringStats before = pool.storage.GetStats();

            const auto result = pool.storage.AdoptBlock(block, malformed.usedBytes, {}, {});
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error(), malformed.error);
            ExpectUnchanged(before, pool.storage.GetStats());
            EXPECT_FALSE(pool.storage.Find("abc").has_value());
        }
    }

    TEST(PoolStringAdoptTest, AWrongHashIsAcceptedWhenTheCallerSaysItWasChecked) {
        IsolatedPool pool;
        PoolBlock block = pool.storage.AllocateBlock(16);
        ConstructPoolEntry(block.Data(), StringUtils::GetHash("abc") + 1, "abc");

        const auto result = pool.storage.AdoptBlock(block, 16, {}, PoolAdoptOptions{false});
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->linkedStrings, 1u);
        EXPECT_EQ(pool.storage.GetStats().stringCount, 1u);
    }

    TEST(PoolStringAdoptTest, ARefusedBlockStaysAdoptableOnceItIsFixed) {
        IsolatedPool pool;
        constexpr size_t kSecond = PoolEntryBytes(11);
        constexpr size_t kBytes = 2 * kSecond;
        PoolBlock block = pool.storage.AllocateBlock(kBytes);
        std::memset(block.Data(), 0, block.Capacity());
        WriteGoodEntry(block.Data(), "adopt_fix_a");
        WriteGoodEntry(block.Data() + kSecond, "adopt_fix_b");
        SetSize(block.Data() + kSecond, 0xFFFFFFFFu);
        ASSERT_FALSE(pool.storage.AdoptBlock(block, kBytes, {}, {}).has_value());

        WriteGoodEntry(block.Data() + kSecond, "adopt_fix_b");
        const auto result = pool.storage.AdoptBlock(block, kBytes, {}, {});
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->linkedStrings, 2u);
    }

    TEST(PoolStringAdoptTest, AnOutOfTheWrongSizeIsRefused) {
        IsolatedPool pool;
        BuiltBlock built = Build(pool.storage, Names("adopt_out_", 4));
        const PoolStringStats before = pool.storage.GetStats();

        Vector<PoolString> tooSmall(3);
        auto result = pool.storage.AdoptBlock(built.block, built.used, Span<PoolString>(tooSmall.data(), tooSmall.size()), {});
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), PoolAdoptError::OutSizeMismatch);
        Vector<PoolString> tooBig(5);
        result = pool.storage.AdoptBlock(built.block, built.used, Span<PoolString>(tooBig.data(), tooBig.size()), {});
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), PoolAdoptError::OutSizeMismatch);
        ExpectUnchanged(before, pool.storage.GetStats());

        Vector<PoolString> exact(4);
        EXPECT_TRUE(pool.storage.AdoptBlock(built.block, built.used, Span<PoolString>(exact.data(), exact.size()), {}).has_value());
    }

    TEST(PoolStringAdoptTest, TheTableGrowsOnceForABigBlock) {
        IsolatedPool pool;  // a table of 8
        const Vector<String> texts = Names("adopt_growth_", 300);
        BuiltBlock built = Build(pool.storage, texts);
        const PoolStringStats before = pool.storage.GetStats();
        ASSERT_EQ(before.tableCapacity, IsolatedPool::kSmallTable);

        ASSERT_TRUE(Adopt(pool.storage, built).has_value());
        const PoolStringStats after = pool.storage.GetStats();
        EXPECT_EQ(after.tableCapacity, TableCapacityFor(IsolatedPool::kSmallTable, 300));
        EXPECT_EQ(after.stringCount, 300u);
        EXPECT_EQ(after.writeLocks, before.writeLocks + 1);
        for (const String& text : texts) {
            EXPECT_TRUE(pool.storage.Find(text).has_value());
        }
    }

    TEST(PoolStringAdoptTest, ABlockWithEntriesOfManySizesIsWalkedToItsEnd) {
        IsolatedPool pool;
        Vector<String> texts;
        for (size_t length = 1; length < 40; ++length) {
            texts.push_back(BlockText(length, static_cast<char>('A' + length % 20)));
        }
        BuiltBlock built = Build(pool.storage, texts);
        Vector<PoolString> out;
        const auto result = Adopt(pool.storage, built, &out);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->linkedStrings + result->deadStrings, texts.size());
        for (size_t i = 0; i < texts.size(); ++i) {
            EXPECT_EQ(out[i].ToStringView(), texts[i]);
        }
    }

    TEST(PoolStringAdoptTest, AllocateBlockGivesAlignedWritableMemory) {
        IsolatedPool pool;
        PoolBlock big = pool.storage.AllocateBlock(2 * kBlock + 8);
        PoolBlock tiny = pool.storage.AllocateBlock(1);
        PoolBlock zero = pool.storage.AllocateBlock(0);

        EXPECT_EQ(big.Capacity(), 2 * kBlock + 8);
        EXPECT_EQ(tiny.Capacity(), 8u);
        EXPECT_EQ(zero.Capacity(), 8u);
        PoolBlock* blocks[] = {&big, &tiny, &zero};
        unsigned char pattern = 1;
        for (PoolBlock* block : blocks) {
            ASSERT_TRUE(block->Data() != nullptr);
            EXPECT_EQ(reinterpret_cast<uintptr_t>(block->Data()) % Details::kPoolEntryAlignment, 0u);
            std::memset(block->Data(), pattern++, block->Capacity());
        }
        pattern = 1;
        for (PoolBlock* block : blocks) {
            for (size_t i = 0; i < block->Capacity(); ++i) {
                ASSERT_EQ(static_cast<unsigned char>(block->Data()[i]), pattern) << "byte " << i;
            }
            ++pattern;
        }

        // Held by the pool, shown in the totals, not yet adopted
        const PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(stats.blockCount, 3u);
        EXPECT_EQ(stats.blockBytes, big.Capacity() + 16);
        EXPECT_EQ(stats.adopted.blocks, 0u);
        EXPECT_EQ(stats.adopted.blockBytes, 0u);
        EXPECT_EQ(stats.runtime.blocks, 0u);
        EXPECT_EQ(pool.source.served, 3u);
        EXPECT_EQ(stats.stringCount, 0u);
    }

    TEST(PoolStringAdoptTest, ABlockFromASourceThatHasNoMoreComesFromTheHeap) {
        IsolatedPool pool(0);
        PoolBlock block = pool.storage.AllocateBlock(64);
        ASSERT_TRUE(block.Data() != nullptr);
        EXPECT_EQ(pool.storage.GetStats().heapFallbackBlocks, 1u);
        ConstructPoolEntry(block.Data(), StringUtils::GetHash("adopt_fallback"), "adopt_fallback");
        ASSERT_TRUE(pool.storage.AdoptBlock(block, PoolEntryBytes(14), {}, {}).has_value());
        EXPECT_TRUE(pool.storage.Find("adopt_fallback").has_value());
    }

    TEST(PoolStringAdoptTest, AllocatingBlocksDoesNotDisturbTheRuntimeBlocks) {
        IsolatedPool pool;
        const PoolString first = pool.storage.Intern("adopt_runtime_first");
        PoolBlock block = pool.storage.AllocateBlock(kBlock * 2);
        const PoolString second = pool.storage.Intern("adopt_runtime_second");
        const PoolStringStats stats = pool.storage.GetStats();
        // Both strings share the one standard block: the allocated block is not the current one
        EXPECT_EQ(stats.runtime.blocks, 1u);
        EXPECT_EQ(stats.blockCount, 2u);
        EXPECT_EQ(second.CStr() - first.CStr(), static_cast<ptrdiff_t>(PoolEntryBytes(first.Length())));
        EXPECT_TRUE(block.Data() != nullptr);
    }

    TEST(PoolStringAdoptTest, ACookedBlockAdoptedIntoTheProcessPoolAnswersIntern) {
        const Vector<String> texts = Names("adopt_process_pool_name_", 50);
        const PoolStringStats before = PoolString::GetStats();

        PoolBlock block = PoolString::AllocateBlock(BytesOf(texts));
        size_t offset = 0;
        for (const String& text : texts) {
            ConstructPoolEntry(block.Data() + offset, StringUtils::GetHash(text), text);
            offset += PoolEntryBytes(text.size());
        }
        Vector<PoolString> out(texts.size());
        const auto result = PoolString::AdoptBlock(block, offset, Span<PoolString>(out.data(), out.size()));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->linkedStrings, texts.size());

        for (size_t i = 0; i < texts.size(); ++i) {
            const PoolString interned = PoolString::Intern(texts[i]);
            EXPECT_EQ(interned, out[i]);
            EXPECT_TRUE(InsideBlock(block, offset, interned.CStr()));
            ASSERT_TRUE(PoolString::Find(texts[i]).has_value());
            EXPECT_EQ(*PoolString::Find(texts[i]), out[i]);
        }
        const PoolStringStats after = PoolString::GetStats();
        EXPECT_EQ(after.stringCount, before.stringCount + texts.size());
        EXPECT_EQ(after.adopted.strings, before.adopted.strings + texts.size());
        EXPECT_EQ(after.misses, before.misses);

        const auto again = PoolString::AdoptBlock(block, offset);
        ASSERT_FALSE(again.has_value());
        EXPECT_EQ(again.error(), PoolAdoptError::AlreadyAdopted);
    }

}  // namespace BECore::Tests
