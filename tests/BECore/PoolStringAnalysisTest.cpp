#include <BECore/PoolString/PoolBlock.h>
#include <BECore/PoolString/PoolStringAnalysis.h>
#include <BECore/PoolString/PoolStringStorage.h>
#include <BECore/PoolStringTestSupport.h>
#include <Common/Printers.h>

namespace BECore::Tests {

    namespace {

        namespace Analysis = PoolStringAnalysis;

        struct Hashed {
            StringView text;
            uint64_t hash;
        };

        /// Adopts entries with the given hashes, unverified: how a pool comes to hold what `Intern` never would
        void Adopt(Details::PoolStringStorage& storage, const Vector<Hashed>& entries, StringView source) {
            size_t bytes = 0;
            for (const Hashed& entry : entries) {
                bytes += Details::PoolEntryBytes(entry.text.size());
            }
            PoolBlock block = storage.AllocateBlock(bytes);
            size_t offset = 0;
            for (const Hashed& entry : entries) {
                Details::ConstructPoolEntry(block.Data() + offset, entry.hash, entry.text);
                offset += Details::PoolEntryBytes(entry.text.size());
            }
            PoolAdoptOptions options;
            options.verifyHashes = false;
            options.sourceName = source;
            ASSERT_TRUE(storage.AdoptBlock(block, bytes, {}, options).has_value());
        }

        Hashed Right(StringView text) {
            return {text, StringUtils::GetHash(text)};
        }

        PoolStringSnapshot SnapshotOf(Details::PoolStringStorage& storage) {
            PoolStringSnapshot snapshot;
            storage.TakeSnapshot(snapshot);
            return snapshot;
        }

        /// The pairs as sorted texts, `first=second;`
        String Texts(const PoolStringSnapshot& snapshot, const Vector<Analysis::RowPair>& pairs) {
            String texts;
            for (const Analysis::RowPair& pair : pairs) {
                EXPECT_NE(pair.first, pair.second);
                texts += Format("{}={};", snapshot.strings[pair.first].text.ToStringView(), snapshot.strings[pair.second].text.ToStringView());
            }
            return texts;
        }

        String Found(const PoolStringSnapshot& snapshot, const PoolStringQuery& query) {
            Vector<uint32_t> rows = {999};  // refilled, not appended to
            Analysis::Search(snapshot, query, rows);
            String texts;
            for (const uint32_t row : rows) {
                texts += Format("{};", snapshot.strings[row].text.ToStringView());
            }
            return texts;
        }

    }  // namespace

    TEST(PoolStringAnalysisTest, DifferentTextsWithOneHashAreACollision) {
        IsolatedPool pool;
        constexpr uint64_t kShared = 42;
        ASSERT_NO_FATAL_FAILURE(Adopt(pool.storage, {{"collide_b", kShared}, {"collide_a", kShared}, Right("plain"), {"collide_c", kShared}}, {}));
        static_cast<void>(pool.storage.Intern("other"));
        const PoolStringSnapshot snapshot = SnapshotOf(pool.storage);

        Vector<Analysis::RowPair> pairs;
        Analysis::FindHashCollisions(snapshot, pairs);
        EXPECT_EQ(Texts(snapshot, pairs), "collide_a=collide_b;collide_a=collide_c;");
        Analysis::FindDuplicateTexts(snapshot, pairs);
        EXPECT_TRUE(pairs.empty());
    }

    TEST(PoolStringAnalysisTest, OneTextInTwoEntriesIsADuplicateAndNoCollision) {
        IsolatedPool pool;
        ASSERT_NO_FATAL_FAILURE(Adopt(pool.storage, {{"twice", 1}, {"once", 3}, {"twice", 2}}, {}));
        const PoolStringSnapshot snapshot = SnapshotOf(pool.storage);

        Vector<Analysis::RowPair> pairs;
        Analysis::FindDuplicateTexts(snapshot, pairs);
        EXPECT_EQ(Texts(snapshot, pairs), "twice=twice;");
        Analysis::FindHashCollisions(snapshot, pairs);
        EXPECT_TRUE(pairs.empty()) << "the earlier pairs are dropped and one text is no collision";
    }

    TEST(PoolStringAnalysisTest, SearchFiltersByTextOriginAndBlockSource) {
        IsolatedPool pool;
        static_cast<void>(pool.storage.Intern("Player_Health"));
        static_cast<void>(pool.storage.Intern("enemy"));
        ASSERT_NO_FATAL_FAILURE(Adopt(pool.storage, {Right("player_name"), Right("door")}, "config/A.bin"));
        ASSERT_NO_FATAL_FAILURE(Adopt(pool.storage, {Right("PLAYER_speed")}, "config/B.bin"));
        const PoolStringSnapshot snapshot = SnapshotOf(pool.storage);

        EXPECT_EQ(Found(snapshot, {}), "PLAYER_speed;Player_Health;door;enemy;player_name;");
        EXPECT_EQ(Found(snapshot, {.text = "pLaYeR"}), "PLAYER_speed;Player_Health;player_name;");
        EXPECT_EQ(Found(snapshot, {.text = "player", .origin = PoolStringOrigin::Adopted}), "PLAYER_speed;player_name;");
        EXPECT_EQ(Found(snapshot, {.origin = PoolStringOrigin::Runtime}), "Player_Health;enemy;");
        EXPECT_EQ(Found(snapshot, {.blockSource = "config/A.bin"}), "door;player_name;");
        EXPECT_EQ(Found(snapshot, {.origin = PoolStringOrigin::Static}), "");
        EXPECT_EQ(Found(snapshot, {.text = "a text longer than every string of the pool"}), "");
    }

    TEST(PoolStringAnalysisTest, SearchSortsByTheKeyInEitherDirection) {
        IsolatedPool pool;
        static_cast<void>(pool.storage.Intern("ccc"));
        ASSERT_NO_FATAL_FAILURE(Adopt(pool.storage, {{"a", 30}, {"dddd", 10}}, "z.bin"));
        ASSERT_NO_FATAL_FAILURE(Adopt(pool.storage, {{"bb", 20}}, "y.bin"));
        const PoolStringSnapshot snapshot = SnapshotOf(pool.storage);

        EXPECT_EQ(Found(snapshot, {.descending = true}), "dddd;ccc;bb;a;");
        EXPECT_EQ(Found(snapshot, {.sortBy = PoolStringSortKey::Length}), "a;bb;ccc;dddd;");
        EXPECT_EQ(Found(snapshot, {.sortBy = PoolStringSortKey::Length, .descending = true}), "dddd;ccc;bb;a;");
        EXPECT_EQ(Found(snapshot, {.origin = PoolStringOrigin::Adopted, .sortBy = PoolStringSortKey::Hash}), "dddd;bb;a;");
        EXPECT_EQ(Found(snapshot, {.sortBy = PoolStringSortKey::Source}), "ccc;bb;a;dddd;");
        EXPECT_EQ(Found(snapshot, {.sortBy = PoolStringSortKey::Source, .descending = true}), "a;dddd;bb;ccc;") << "one source keeps the order of the texts";
    }

}  // namespace BECore::Tests
