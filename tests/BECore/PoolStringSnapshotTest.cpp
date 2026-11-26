#include <BECore/PoolString/PoolBlock.h>
#include <BECore/PoolString/PoolStringSnapshot.h>
#include <BECore/PoolString/PoolStringStorage.h>
#include <BECore/PoolStringTestSupport.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/Wait.h>

namespace BECore::Tests {

    namespace {

        using Details::ConstructPoolEntry;
        using Details::PoolEntryBytes;

        constexpr size_t kBlock = PoolString::kBlockBytes;
        constexpr StringView kSource = "config/Snapshot.bin";

        /// A ready entry outside every block of the pool; it outlives the storage that registers it
        struct ReadyEntry {
            alignas(Details::kPoolEntryAlignment) char memory[64];

            const Details::PoolStringEntry& Build(StringView text) {
                return *ConstructPoolEntry(memory, StringUtils::GetHash(text), text);
            }
        };

        /// An adopted block holding `texts`, named `source`
        void AdoptTexts(Details::PoolStringStorage& storage, const Vector<String>& texts, StringView source) {
            size_t bytes = 0;
            for (const String& text : texts) {
                bytes += PoolEntryBytes(text.size());
            }
            PoolBlock block = storage.AllocateBlock(bytes);
            size_t offset = 0;
            for (const String& text : texts) {
                ConstructPoolEntry(block.Data() + offset, StringUtils::GetHash(text), text);
                offset += PoolEntryBytes(text.size());
            }
            PoolAdoptOptions options;
            options.sourceName = source;
            ASSERT_TRUE(storage.AdoptBlock(block, bytes, {}, options).has_value());
        }

        const PoolStringRow* RowOf(const PoolStringSnapshot& snapshot, StringView text) {
            for (const PoolStringRow& row : snapshot.strings) {
                if (row.text.ToStringView() == text) {
                    return &row;
                }
            }
            return nullptr;
        }

        void ExpectRow(const PoolStringSnapshot& snapshot, StringView text, PoolStringOrigin origin, uint32_t block) {
            const PoolStringRow* row = RowOf(snapshot, text);
            ASSERT_NE(row, nullptr) << String(text).c_str();
            EXPECT_EQ(row->origin, origin) << String(text).c_str();
            EXPECT_EQ(row->block, block) << String(text).c_str();
        }

        void ExpectBlock(const PoolBlockRow& row, PoolBlockKind kind, size_t capacity, size_t used) {
            EXPECT_EQ(row.kind, kind);
            EXPECT_EQ(row.capacity, capacity);
            EXPECT_EQ(row.used, used);
        }

        class RecordingObserver final : public IPoolStringObserver {
        public:
            Vector<PoolString> heard;

            void OnRuntimeString(PoolString text) override {
                heard.push_back(text);
            }
        };

    }  // namespace

    TEST(PoolStringSnapshotTest, EveryStringHasItsOriginAndBlockAndEveryBlockItsNumbers) {
        ReadyEntry ready;
        IsolatedPool pool;
        const PoolString constant = [&] {
            PoolString value;
            const PoolStaticRegistration registration{&ready.Build("snapshot_static"), &value};
            pool.storage.RegisterStatic(Span<const PoolStaticRegistration>(&registration, 1));
            return value;
        }();

        const String first = BlockText(2000, 'f');
        const String large = BlockText(kBlock - 1000, 'l');  // fits a standard block, not the rest of the first one
        const String small = "snapshot_small";
        const String own = BlockText(kBlock, 'o');  // its entry fits no standard block
        const String afterOwn = "snapshot_after_own";
        const PoolString firstString = pool.storage.Intern(first);
        static_cast<void>(pool.storage.Intern(large));
        static_cast<void>(pool.storage.Intern(small));
        static_cast<void>(pool.storage.Intern(own));
        static_cast<void>(pool.storage.Intern(afterOwn));
        const PoolBlock held = pool.storage.AllocateBlock(64);
        String source(kSource);
        ASSERT_NO_FATAL_FAILURE(AdoptTexts(pool.storage, {String("snapshot_adopted"), first}, source));
        source.assign(source.size(), 'x');  // the pool keeps its own copy

        PoolStringSnapshot snapshot;
        pool.storage.TakeSnapshot(snapshot);

        EXPECT_EQ(snapshot.strings.size(), 7u) << "the dead duplicate of the adopted block is no row";
        ASSERT_EQ(snapshot.blocks.size(), 5u);
        ExpectBlock(snapshot.blocks[0], PoolBlockKind::Runtime, kBlock, PoolEntryBytes(first.size()));
        ExpectBlock(snapshot.blocks[1], PoolBlockKind::Runtime, kBlock, PoolEntryBytes(large.size()) + PoolEntryBytes(small.size()) + PoolEntryBytes(afterOwn.size()));
        ExpectBlock(snapshot.blocks[2], PoolBlockKind::Runtime, PoolEntryBytes(own.size()), PoolEntryBytes(own.size()));
        ExpectBlock(snapshot.blocks[3], PoolBlockKind::Allocated, held.Capacity(), 0);
        const size_t adoptedBytes = PoolEntryBytes(StringView("snapshot_adopted").size()) + PoolEntryBytes(first.size());
        ExpectBlock(snapshot.blocks[4], PoolBlockKind::Adopted, adoptedBytes, adoptedBytes);
        EXPECT_EQ(snapshot.blocks[4].deadStrings, 1u);
        EXPECT_EQ(snapshot.blocks[4].deadBytes, PoolEntryBytes(first.size()));
        EXPECT_EQ(snapshot.blocks[4].source, kSource);
        EXPECT_EQ(snapshot.blocks[3].data, held.Data());

        ExpectRow(snapshot, constant.ToStringView(), PoolStringOrigin::Static, kNoPoolBlock);
        ExpectRow(snapshot, first, PoolStringOrigin::Runtime, 0);
        ExpectRow(snapshot, large, PoolStringOrigin::Runtime, 1);
        ExpectRow(snapshot, small, PoolStringOrigin::Runtime, 1);
        ExpectRow(snapshot, own, PoolStringOrigin::Runtime, 2);
        ExpectRow(snapshot, afterOwn, PoolStringOrigin::Runtime, 1);
        ExpectRow(snapshot, "snapshot_adopted", PoolStringOrigin::Adopted, 4);
        EXPECT_EQ(Require(RowOf(snapshot, first)).text, firstString);

        const PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(snapshot.stats.stringCount, stats.stringCount);
        EXPECT_EQ(snapshot.stats.blockCount, snapshot.blocks.size());
        EXPECT_EQ(snapshot.stats.internCalls, stats.internCalls);

        const Details::PoolStringTotals totals = pool.storage.GetTotals();
        EXPECT_EQ(totals.runtimeStrings, stats.runtime.strings);
        EXPECT_EQ(totals.runtimeBytes, stats.runtime.bytes);
        EXPECT_EQ(totals.otherStrings, stats.staticEntries.strings + stats.adopted.strings);
        EXPECT_EQ(totals.otherBytes, stats.staticEntries.bytes + stats.adopted.bytes);

        // A second call replaces the contents
        pool.storage.TakeSnapshot(snapshot);
        EXPECT_EQ(snapshot.strings.size(), 7u);
        EXPECT_EQ(snapshot.blocks.size(), 5u);
        EXPECT_EQ(snapshot.stats.internCalls, stats.internCalls);
    }

    TEST(PoolStringSnapshotTest, AReservedBlockCountsWhatGoesIntoIt) {
        IsolatedPool pool;
        pool.storage.Reserve(4, 256);
        PoolStringSnapshot snapshot;
        pool.storage.TakeSnapshot(snapshot);
        ASSERT_EQ(snapshot.blocks.size(), 1u);
        ExpectBlock(snapshot.blocks[0], PoolBlockKind::Runtime, 256, 0);

        static_cast<void>(pool.storage.Intern("snapshot_reserved"));
        pool.storage.TakeSnapshot(snapshot);
        ASSERT_EQ(snapshot.blocks.size(), 1u);
        EXPECT_EQ(snapshot.blocks[0].used, PoolEntryBytes(StringView("snapshot_reserved").size()));
        ExpectRow(snapshot, "snapshot_reserved", PoolStringOrigin::Runtime, 0);
    }

    TEST(PoolStringSnapshotTest, TheLockIsNotHeldAfterTheCall) {
        IsolatedPool pool;
        static_cast<void>(pool.storage.Intern("snapshot_before"));
        PoolStringSnapshot snapshot;
        pool.storage.TakeSnapshot(snapshot);

        EXPECT_TRUE(RunThreadsTogether(1, kWaitLimit, [&](int) { static_cast<void>(pool.storage.Intern("snapshot_meanwhile")); }));

        EXPECT_EQ(snapshot.strings.size(), 1u);
        EXPECT_EQ(Back(snapshot.strings).text.ToStringView(), "snapshot_before");
    }

    TEST(PoolStringSnapshotTest, AnObserverHearsTheNewRuntimeStringsOnlyUntilItIsRemoved) {
        ReadyEntry ready;
        IsolatedPool pool;
        const PoolString old = pool.storage.Intern("observer_old");
        RecordingObserver observer;
        RecordingObserver other;
        pool.storage.SetObserver(observer);

        const PoolString single = pool.storage.Intern("observer_single");
        static_cast<void>(pool.storage.Intern("observer_single"));
        static_cast<void>(pool.storage.Intern(StringView{}));
        const Array<StringView, 4> texts = {"observer_batch", old.ToStringView(), "observer_batch", ""};
        Array<PoolString, 4> out;
        pool.storage.InternBatch(Span<const StringView>(texts.data(), texts.size()), Span<PoolString>(out.data(), out.size()));
        PoolString constant;
        const PoolStaticRegistration registration{&ready.Build("observer_static"), &constant};
        pool.storage.RegisterStatic(Span<const PoolStaticRegistration>(&registration, 1));
        ASSERT_NO_FATAL_FAILURE(AdoptTexts(pool.storage, {String("observer_adopted")}, {}));

        ASSERT_EQ(observer.heard.size(), 2u);
        EXPECT_EQ(observer.heard[0], single);
        EXPECT_EQ(observer.heard[1], out[0]);

        pool.storage.RemoveObserver(other);  // not the one set
        static_cast<void>(pool.storage.Intern("observer_still_heard"));
        EXPECT_EQ(observer.heard.size(), 3u);

        pool.storage.RemoveObserver(observer);
        static_cast<void>(pool.storage.Intern("observer_after"));
        EXPECT_EQ(observer.heard.size(), 3u);
        EXPECT_TRUE(other.heard.empty());
    }

}  // namespace BECore::Tests
