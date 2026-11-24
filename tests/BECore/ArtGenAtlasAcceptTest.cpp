#include <BECore/ArtGenAnswerText.h>
#include <Common/ArtGenTestSupport.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TestFiles.h>
#include <Widgets/ArtGen/ArtGenAtlasAccept.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kTempPrefix = "artgen_atlas_accept";

        constexpr StringView kQueenPng = "assets/artgen/queen.png";
        constexpr StringView kRookPng = "assets/artgen/rook.png";
        constexpr StringView kPiecesAtlas = "assets/atlases/pieces.png";
        constexpr StringView kUiAtlas = "assets/atlases/ui.png";

        /// Counts the saves and can be told to fail from the @p failFrom-th on (0: never)
        struct CountingSave {
            int saves = 0;
            int failFrom = 0;

            bool operator()() {
                ++saves;
                return failFrom == 0 || saves < failFrom;
            }
        };

        SpriteEntry EntryOf(const char* name, StringView path, const Rect& rect) {
            SpriteEntry entry;
            entry.name = PoolString::Intern(name);
            entry.texturePath = PoolString::Intern(path);
            entry.srcRect = rect;
            return entry;
        }

        SpriteEntry EntryInGroup(const char* name, StringView path, const Rect& rect, const char* group) {
            SpriteEntry entry = EntryOf(name, path, rect);
            SpriteAtlasLink link;
            link.name = PoolString::Intern(group);
            entry.atlas = link;
            return entry;
        }

        /// Both sprites accepted into "pieces", packed
        void FillPackedPieces(TextureLibrary& library, const ArtGenGameFolder& game) {
            CountingSave save;
            ASSERT_TRUE(AcceptIntoGroup(library, game.Copied(kQueenPng, MakeSolid(4, 4, 0, 0, kHigh)), "queen", "pieces", false, game.Io(), save).has_value());
            ASSERT_TRUE(AcceptIntoGroup(library, game.Copied(kRookPng, MakeSolid(2, 2, kHigh, 0, 0)), "rook", "pieces", false, game.Io(), save).has_value());
        }

    }  // namespace

    TEST(ArtGenAtlasAcceptTest, ASpriteWithNoEntryJoinsANewGroupAndTheGroupIsPacked) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        CountingSave save;

        const auto stored = AcceptIntoGroup(library, game.Copied(kQueenPng, MakeSolid(4, 4, 0, 0, kHigh)), "queen", "pieces", false, game.Io(), save);

        ASSERT_TRUE(stored.has_value());
        EXPECT_TRUE(stored->saved);
        EXPECT_EQ(save.saves, 2);
        ASSERT_TRUE(stored->repacked.has_value());
        EXPECT_EQ(stored->repacked->name.ToStringView(), "pieces");
        EXPECT_EQ(stored->repacked->texturePath.ToStringView(), kPiecesAtlas);
        ASSERT_TRUE(IsPackedSprite(Queen(library)));
        EXPECT_EQ(Require(Queen(library).atlas).name.ToStringView(), "pieces");
        EXPECT_EQ(Queen(library).texturePath.ToStringView(), kPiecesAtlas);
        EXPECT_EQ(SourceOf(Queen(library)).texturePath.ToStringView(), kQueenPng);
        EXPECT_EQ(SourceOf(Queen(library)).rect.w, 4.0f);
        EXPECT_EQ(SourceOf(Queen(library)).rect.h, 4.0f);
        ASSERT_TRUE(game.Exists(kPiecesAtlas));
        EXPECT_TRUE(RectIsSolid(game.Atlas(kPiecesAtlas), Queen(library).srcRect, 0, 0, kHigh));
    }

    TEST(ArtGenAtlasAcceptTest, ASecondSpriteIntoTheGroupPacksBothTogether) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        ASSERT_NO_FATAL_FAILURE(FillPackedPieces(library, game));

        ASSERT_TRUE(IsPackedSprite(Queen(library)));
        ASSERT_TRUE(IsPackedSprite(Rook(library)));
        EXPECT_EQ(SourceOf(Rook(library)).texturePath.ToStringView(), kRookPng);
        const RgbaImage atlas = game.Atlas(kPiecesAtlas);
        EXPECT_TRUE(RectIsSolid(atlas, Queen(library).srcRect, 0, 0, kHigh));
        EXPECT_TRUE(RectIsSolid(atlas, Rook(library).srcRect, kHigh, 0, 0));
    }

    TEST(ArtGenAtlasAcceptTest, AnUnpackedMemberOfAnotherGroupMovesToTheNewGroup) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        const ArtGenCopied copied = game.Copied(kQueenPng, MakeSolid(4, 4, 0, 0, kHigh));
        SpriteEntry old = EntryInGroup("queen", kQueenPng, Rect(0, 0, 8, 8), "ui");
        old.atlas->pack = AtlasPackRecord{};
        library.AddOrUpdate(old);
        CountingSave save;

        const auto stored = AcceptIntoGroup(library, copied, "queen", "pieces", false, game.Io(), save);

        ASSERT_TRUE(stored.has_value());
        EXPECT_EQ(Require(Queen(library).atlas).name.ToStringView(), "pieces");
        EXPECT_TRUE(IsPackedSprite(Queen(library)));
        EXPECT_TRUE(game.Exists(kPiecesAtlas));
        EXPECT_FALSE(game.Exists(kUiAtlas));
    }

    TEST(ArtGenAtlasAcceptTest, APackedMemberOfAnotherGroupIsRefusedAndNothingChanges) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        library.AddOrUpdate(EntryInGroup("queen", kQueenPng, Rect(0, 0, 4, 4), "ui"));
        library.AddOrUpdate(EntryInGroup("rook", kRookPng, Rect(0, 0, 2, 2), "ui"));
        ASSERT_TRUE(SaveRgbaPng(game.Resolve(kQueenPng), MakeSolid(4, 4, 0, 0, kHigh)).has_value());
        ASSERT_TRUE(SaveRgbaPng(game.Resolve(kRookPng), MakeSolid(2, 2, kHigh, 0, 0)).has_value());
        ASSERT_TRUE(WriteAtlas(library, "ui", PackSettings{}, game.Io()).has_value());
        const SpriteEntry queenBefore = Queen(library);
        const Vector<uint8_t> atlasBefore = ReadFileBytes(game.Resolve(kUiAtlas));
        const ArtGenCopied copied = game.Copied(kQueenPng, MakeSolid(6, 2, 0, kHigh, 0));
        CountingSave save;

        const auto refused = AcceptedSpriteEntryInGroup(library, "queen"_intern, PoolString::Intern(kQueenPng), 6, 2, "pieces");
        const auto stored = AcceptIntoGroup(library, copied, "queen", "pieces", false, game.Io(), save);

        ASSERT_FALSE(refused.has_value());
        EXPECT_NE(refused.error().find("'queen' is packed in atlas 'ui'"), String::npos);
        EXPECT_NE(refused.error().find("Unpack it in the Sprite Library first"), String::npos);
        EXPECT_NE(refused.error().find("'pieces'"), String::npos);
        ASSERT_FALSE(stored.has_value());
        EXPECT_NE(stored.error().find("packed in atlas 'ui'"), String::npos);
        EXPECT_EQ(save.saves, 0);
        EXPECT_EQ(Require(Queen(library).atlas).name.ToStringView(), "ui");
        EXPECT_EQ(Queen(library).texturePath, queenBefore.texturePath);
        EXPECT_EQ(Queen(library).srcRect.x, queenBefore.srcRect.x);
        EXPECT_EQ(SourceOf(Queen(library)).texturePath, SourceOf(queenBefore).texturePath);
        EXPECT_FALSE(game.Exists(kPiecesAtlas));
        EXPECT_EQ(ReadFileBytes(game.Resolve(kUiAtlas)), atlasBefore);
    }

    TEST(ArtGenAtlasAcceptTest, AMemberOfTheSameGroupKeepsItsPackRecordWhileUnpacked) {
        TextureLibrary library;
        SpriteEntry old = EntryInGroup("queen", kQueenPng, Rect(0, 0, 8, 8), "pieces");
        AtlasPackRecord record;
        record.padding = 3;
        old.atlas->pack = record;
        library.AddOrUpdate(old);

        const auto entry = AcceptedSpriteEntryInGroup(library, "queen"_intern, PoolString::Intern(kQueenPng), 6, 2, "pieces");

        ASSERT_TRUE(entry.has_value());
        ASSERT_TRUE(entry->atlas.has_value());
        ASSERT_TRUE(entry->atlas->pack.has_value());
        EXPECT_EQ(entry->atlas->pack->padding, 3);
        EXPECT_EQ(entry->srcRect.w, 6.0f);
        EXPECT_FALSE(entry->atlas->source.has_value());
    }

    TEST(ArtGenAtlasAcceptTest, ADeferredPackLeavesLooseMembersUntilTheGroupIsPackedOnce) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        CountingSave save;

        const auto first = AcceptIntoGroup(library, game.Copied(kQueenPng, MakeSolid(4, 4, 0, 0, kHigh)), "queen", "pieces", true, game.Io(), save);
        const auto second = AcceptIntoGroup(library, game.Copied(kRookPng, MakeSolid(2, 2, kHigh, 0, 0)), "rook", "pieces", true, game.Io(), save);

        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        EXPECT_TRUE(first->saved);
        EXPECT_FALSE(first->repacked.has_value());
        EXPECT_FALSE(second->repacked.has_value());
        EXPECT_EQ(save.saves, 2);
        EXPECT_FALSE(IsPackedSprite(Queen(library)));
        EXPECT_FALSE(IsPackedSprite(Rook(library)));
        EXPECT_EQ(Require(Queen(library).atlas).name.ToStringView(), "pieces");
        EXPECT_EQ(Require(Rook(library).atlas).name.ToStringView(), "pieces");
        EXPECT_EQ(Queen(library).texturePath.ToStringView(), kQueenPng);
        EXPECT_FALSE(game.Exists(kPiecesAtlas));

        const auto packed = PackGroupAfterAccept(library, "pieces", game.Io(), save, "");

        ASSERT_TRUE(packed.has_value());
        EXPECT_TRUE(packed->saved);
        EXPECT_EQ(save.saves, 3);
        ASSERT_TRUE(packed->repacked.has_value());
        EXPECT_EQ(packed->repacked->changed.size(), 2u);
        EXPECT_TRUE(IsPackedSprite(Queen(library)));
        EXPECT_TRUE(IsPackedSprite(Rook(library)));
        const RgbaImage atlas = game.Atlas(kPiecesAtlas);
        EXPECT_TRUE(RectIsSolid(atlas, Queen(library).srcRect, 0, 0, kHigh));
        EXPECT_TRUE(RectIsSolid(atlas, Rook(library).srcRect, kHigh, 0, 0));
    }

    TEST(ArtGenAtlasAcceptTest, ARowsAndThePackInsideABatchOweOneSaveAndAFailedCommitGivesTheLibraryBack) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        ASSERT_NO_FATAL_FAILURE(FillPackedPieces(library, game));
        const Vector<uint8_t> atlasBefore = ReadFileBytes(game.Resolve(kPiecesAtlas));
        const SpriteEntry queenBefore = Queen(library);
        const SpriteEntry rookBefore = Rook(library);
        const size_t entriesBefore = library.GetAll().size();
        ArtGenLibraryBatch batch(library);
        int saves = 0;
        const auto defer = [&batch, &saves] {
            ++saves;
            return batch.Defer();
        };

        ASSERT_TRUE(AcceptIntoGroup(library, game.Copied(kQueenPng, MakeSolid(6, 2, 0, kHigh, 0)), "queen", "pieces", true, game.Io(), defer).has_value());
        ASSERT_TRUE(AcceptIntoGroup(library, game.Copied("assets/artgen/bishop.png", MakeSolid(3, 5, kHigh, kHigh, 0)), "bishop", "pieces", true, game.Io(), defer).has_value());
        const auto packed = PackGroupAfterAccept(library, "pieces", game.Io(), defer, "");
        ASSERT_TRUE(packed.has_value());
        ASSERT_TRUE(packed->repacked.has_value());
        batch.NoteRepacked();
        EXPECT_NE(ReadFileBytes(game.Resolve(kPiecesAtlas)), atlasBefore) << "the pack rewrote the atlas file";

        int committedSaves = 0;
        const auto committed = batch.Commit([&committedSaves] {
            ++committedSaves;
            return false;
        });

        ASSERT_FALSE(committed.has_value());
        EXPECT_NE(committed.error().find("press Pack in the Sprite Library"), String::npos) << committed.error().c_str();
        EXPECT_EQ(committedSaves, 1) << "the batch's save is the only one that would reach the file";
        EXPECT_EQ(library.GetAll().size(), entriesBefore);
        EXPECT_EQ(library.GetSprite("bishop"_intern), nullptr);
        EXPECT_TRUE(IsSameSpriteEntry(Queen(library), queenBefore));
        EXPECT_TRUE(IsSameSpriteEntry(Rook(library), rookBefore));
    }

    TEST(ArtGenAtlasAcceptTest, ANewPictureOfAPackedMemberPacksTheGroupAgain) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        ASSERT_NO_FATAL_FAILURE(FillPackedPieces(library, game));
        CountingSave save;

        const auto stored = AcceptIntoGroup(library, game.Copied(kQueenPng, MakeSolid(6, 2, 0, kHigh, 0)), "queen", "pieces", false, game.Io(), save);

        ASSERT_TRUE(stored.has_value());
        EXPECT_TRUE(stored->saved);
        EXPECT_EQ(save.saves, 2);
        ASSERT_TRUE(stored->repacked.has_value());
        EXPECT_EQ(Queen(library).srcRect.w, 6.0f);
        EXPECT_EQ(Queen(library).srcRect.h, 2.0f);
        EXPECT_EQ(SourceOf(Queen(library)).rect.w, 6.0f);
        const RgbaImage atlas = game.Atlas(kPiecesAtlas);
        EXPECT_TRUE(RectIsSolid(atlas, Queen(library).srcRect, 0, kHigh, 0));
        EXPECT_TRUE(RectIsSolid(atlas, Rook(library).srcRect, kHigh, 0, 0));
    }

    TEST(ArtGenAtlasAcceptTest, ThePictureOfAPackedMemberAcceptedAgainChangesNothingAndSavesNothingMore) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        ASSERT_NO_FATAL_FAILURE(FillPackedPieces(library, game));
        const Vector<uint8_t> atlasBefore = ReadFileBytes(game.Resolve(kPiecesAtlas));
        CountingSave save;

        const auto stored = AcceptIntoGroup(library, game.Copied(kQueenPng, MakeSolid(4, 4, 0, 0, kHigh)), "queen", "pieces", false, game.Io(), save);

        ASSERT_TRUE(stored.has_value());
        EXPECT_FALSE(stored->saved);
        EXPECT_EQ(save.saves, 0);
        EXPECT_TRUE(stored->repacked.has_value());
        EXPECT_EQ(ReadFileBytes(game.Resolve(kPiecesAtlas)), atlasBefore);
    }

    TEST(ArtGenAtlasAcceptTest, AFailedPackKeepsTheNewEntryAndTheOldAtlasFile) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        ASSERT_NO_FATAL_FAILURE(FillPackedPieces(library, game));
        const Vector<uint8_t> atlasBefore = ReadFileBytes(game.Resolve(kPiecesAtlas));
        const Rect rookPlace = Rook(library).srcRect;
        RemoveFileIfPresent(game.Resolve(kRookPng));
        CountingSave save;

        const auto stored = AcceptIntoGroup(library, game.Copied(kQueenPng, MakeSolid(6, 2, 0, kHigh, 0)), "queen", "pieces", false, game.Io(), save);

        ASSERT_FALSE(stored.has_value());
        EXPECT_NE(stored.error().find("atlas 'pieces' could not be packed"), String::npos);
        EXPECT_NE(stored.error().find("press Pack in the Sprite Library"), String::npos);
        EXPECT_NE(stored.error().find("rook"), String::npos);
        EXPECT_EQ(save.saves, 1);
        ASSERT_TRUE(IsPackedSprite(Queen(library)));
        EXPECT_EQ(SourceOf(Queen(library)).rect.w, 6.0f);
        EXPECT_EQ(Rook(library).srcRect.x, rookPlace.x);
        EXPECT_EQ(ReadFileBytes(game.Resolve(kPiecesAtlas)), atlasBefore);
    }

    TEST(ArtGenAtlasAcceptTest, AFailedSecondSaveGivesTheMembersBack) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        CountingSave save;
        save.failFrom = 2;

        const auto stored = AcceptIntoGroup(library, game.Copied(kQueenPng, MakeSolid(4, 4, 0, 0, kHigh)), "queen", "pieces", false, game.Io(), save);

        ASSERT_FALSE(stored.has_value());
        EXPECT_NE(stored.error().find("was rewritten"), String::npos);
        EXPECT_NE(stored.error().find("press Pack in the Sprite Library"), String::npos);
        EXPECT_EQ(save.saves, 2);
        ASSERT_NE(library.GetSprite("queen"_intern), nullptr);
        EXPECT_FALSE(IsPackedSprite(Queen(library)));
        EXPECT_EQ(Queen(library).texturePath.ToStringView(), kQueenPng);
        EXPECT_EQ(Queen(library).srcRect.w, 4.0f);
    }

    TEST(ArtGenAtlasAcceptTest, AFailedFirstSaveStoresNothingAndPacksNothing) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        CountingSave save;
        save.failFrom = 1;

        const auto stored = AcceptIntoGroup(library, game.Copied(kQueenPng, MakeSolid(4, 4, 0, 0, kHigh)), "queen", "pieces", false, game.Io(), save);

        ASSERT_FALSE(stored.has_value());
        EXPECT_NE(stored.error().find("the PNG is in"), String::npos);
        EXPECT_EQ(library.GetSprite("queen"_intern), nullptr);
        EXPECT_FALSE(game.Exists(kPiecesAtlas));
    }

    TEST(ArtGenAtlasAcceptTest, AGroupWithNoMemberIsAnError) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        CountingSave save;

        const auto packed = PackGroupAfterAccept(library, "pieces", game.Io(), save, "");

        ASSERT_FALSE(packed.has_value());
        EXPECT_NE(packed.error().find("no sprite to pack"), String::npos);
        EXPECT_EQ(save.saves, 0);
        EXPECT_FALSE(game.Exists(kPiecesAtlas));
    }

    TEST(ArtGenAtlasAcceptTest, ABadGroupNameIsRefusedAndNothingIsStored) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        CountingSave save;

        for (const char* bad : {"", "a b", "../up", "a/b"}) {
            const auto stored = AcceptIntoGroup(library, game.Copied(kQueenPng, MakeSolid(4, 4, 0, 0, kHigh)), "queen", bad, false, game.Io(), save);

            ASSERT_FALSE(stored.has_value()) << bad;
            EXPECT_NE(stored.error().find("is not an atlas name"), String::npos) << bad;
        }
        EXPECT_EQ(library.GetSprite("queen"_intern), nullptr);
        EXPECT_EQ(save.saves, 0);
    }

    TEST(ArtGenAtlasAcceptTest, TheRowAnswerNamesTheGroupAndTheAtlas) {
        AtlasWritten written;
        written.name = "pieces"_intern;
        written.texturePath = PoolString::Intern(kPiecesAtlas);
        written.width = 64;
        written.height = 32;
        written.changed = {"queen"_intern, "rook"_intern};
        ArtGenAcceptAnswer reply;

        AddAtlasKeysToReply(reply, "pieces", written);

        EXPECT_EQ(reply.atlasGroup.value_or(String()), "pieces");
        ASSERT_TRUE(reply.atlas.has_value());
        EXPECT_EQ(reply.atlas->texturePath, kPiecesAtlas);
        EXPECT_EQ(reply.atlas->members, 2u);
    }

    TEST(ArtGenAtlasAcceptTest, TheRowAnswerLeavesOutWhereNothingWasPacked) {
        ArtGenAcceptAnswer deferred;
        AddAtlasKeysToReply(deferred, "pieces", Optional<AtlasWritten>{});

        EXPECT_EQ(deferred.atlasGroup.value_or(String()), "pieces");
        EXPECT_FALSE(deferred.atlas.has_value()) << "a deferred pack has no atlas yet";

        ArtGenAcceptAnswer ungrouped;
        AddAtlasKeysToReply(ungrouped, "", Optional<AtlasWritten>{});

        const ControlReply text = AnswerReply(ungrouped);
        EXPECT_FALSE(text.View().Find("atlasGroup").has_value());
        EXPECT_FALSE(text.View().Find("atlas").has_value());
    }

}  // namespace BECore::Tests
