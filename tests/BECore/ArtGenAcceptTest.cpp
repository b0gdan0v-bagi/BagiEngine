#include <BECore/ArtGenAnswerText.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Resource/IResource.h>
#include <BECore/Resource/ResourceCache.h>
#include <Common/ArtGenTestSupport.h>
#include <Common/JsonDoc.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenDrag.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>
#include <Widgets/TextureEditor/SpriteTextureCache.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// The first bytes of a PNG of @p width x @p height: what ReadPngSize reads (the rest of the file does not matter to it)
        void WritePngHeader(const Path& file, uint32_t width, uint32_t height) {
            Array<uint8_t, 33> bytes{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
            for (int i = 0; i < 4; ++i) {
                bytes[16 + i] = static_cast<uint8_t>(width >> (24 - 8 * i));
                bytes[20 + i] = static_cast<uint8_t>(height >> (24 - 8 * i));
            }
            WriteFile(file, StringView(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        }

        /// What the tests about the copy and not about the picture use: the PNGs they write are only a header
        const ArtGenImagePlan kIdentity{ArtGenImageSettings{false, ArtGenImageSettings::kDefaultPadding, 0}, {}};

        /// A real PNG of @p width x @p height with an opaque red box; the rest is transparent
        void WriteRealPng(const Path& file, uint32_t width, uint32_t height, ArtGenIntRect box) {
            ArtGenPixels pixels;
            pixels.width = width;
            pixels.height = height;
            pixels.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
            for (int32_t y = box.y; y < box.y + box.h; ++y) {
                for (int32_t x = box.x; x < box.x + box.w; ++x) {
                    uint8_t* at = pixels.rgba.data() + (static_cast<size_t>(y) * width + static_cast<uint32_t>(x)) * 4;
                    at[0] = 255;
                    at[3] = 255;
                }
            }
            ASSERT_TRUE(WriteArtGenPng(file, pixels).has_value());
        }

    }  // namespace

    class ArtGenAcceptTest : public ::testing::Test {
    protected:
        void SetUp() override {
            folder = TestTempDirectory("artgen_accept");
            MakeEmptyDirectory(folder);
            MakeDirectory(folder / "out");
            MakeDirectory(folder / "assets");
            png = folder / "out" / "queen_7.png";
            provenance = folder / "out" / "queen_7.artgen.json";
            WritePngHeader(png, 1024, 768);
            WriteFile(provenance, "{\"seed\": 7}");
        }

        void TearDown() override {
            RemoveDirectory(folder);
        }

        Path folder;
        Path png;
        Path provenance;
    };

    namespace {

        SpriteEntry LibraryEntry(const char* name, const char* path, Rect rect) {
            SpriteEntry entry;
            entry.name = PoolString::Intern(name);
            entry.texturePath = PoolString::Intern(path);
            entry.srcRect = rect;
            return entry;
        }

        constexpr int kTestPadding = 3;
        constexpr uint32_t kTestMaxSide = 2048;

        AtlasPackRecord PackRecordOf(const char* sizeMode) {
            AtlasPackRecord record;
            record.padding = kTestPadding;
            record.sizeMode = PoolString::Intern(sizeMode);
            record.maxSide = kTestMaxSide;
            return record;
        }

        /// An entry in group @p group, with the pack record `Tight` when @p packRecord; packed, it has the source @p source
        SpriteEntry GroupedEntry(const char* name, const char* path, Rect rect, const char* group, bool packRecord, const char* source = nullptr, Rect sourceRect = Rect(0, 0, 0, 0)) {
            SpriteEntry entry = LibraryEntry(name, path, rect);
            SpriteAtlasLink link;
            link.name = PoolString::Intern(group);
            if (packRecord) {
                link.pack = PackRecordOf("Tight");
            }
            if (source != nullptr) {
                SpriteSource from;
                from.texturePath = PoolString::Intern(source);
                from.rect = sourceRect;
                link.source = from;
            }
            entry.atlas = link;
            return entry;
        }

        bool SameRectOf(const Rect& a, const Rect& b) {
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }

    }  // namespace

    TEST(ArtGenAcceptedEntryTest, ASpriteWithNoEntryGetsTheWholeImage) {
        const TextureLibrary library;
        const SpriteEntry entry = AcceptedSpriteEntry(library, "queen"_intern, "assets/artgen/queen.png"_intern, 64, 48);
        EXPECT_EQ(entry.name.ToStringView(), "queen");
        EXPECT_EQ(entry.texturePath.ToStringView(), "assets/artgen/queen.png");
        EXPECT_TRUE(SameRectOf(entry.srcRect, Rect(0, 0, 64, 48)));
        EXPECT_FALSE(entry.atlas.has_value());
    }

    TEST(ArtGenAcceptedEntryTest, AnEntryWithNoLinkIsReplacedWhole) {
        TextureLibrary library;
        library.AddOrUpdate(LibraryEntry("queen", "assets/old.png", Rect(10, 10, 5, 5)));
        const SpriteEntry entry = AcceptedSpriteEntry(library, "queen"_intern, "assets/artgen/queen.png"_intern, 64, 48);
        EXPECT_EQ(entry.texturePath.ToStringView(), "assets/artgen/queen.png");
        EXPECT_TRUE(SameRectOf(entry.srcRect, Rect(0, 0, 64, 48)));
        EXPECT_FALSE(entry.atlas.has_value());
    }

    TEST(ArtGenAcceptedEntryTest, AnUnpackedMemberKeepsItsGroupAndSettings) {
        TextureLibrary library;
        library.AddOrUpdate(GroupedEntry("queen", "assets/old.png", Rect(10, 10, 5, 5), "pieces", true));
        const SpriteEntry entry = AcceptedSpriteEntry(library, "queen"_intern, "assets/artgen/queen.png"_intern, 64, 48);
        EXPECT_EQ(entry.texturePath.ToStringView(), "assets/artgen/queen.png");
        EXPECT_TRUE(SameRectOf(entry.srcRect, Rect(0, 0, 64, 48)));
        ASSERT_TRUE(entry.atlas.has_value());
        EXPECT_EQ(entry.atlas->name.ToStringView(), "pieces");
        EXPECT_FALSE(entry.atlas->source.has_value());
        ASSERT_TRUE(entry.atlas->pack.has_value());
        EXPECT_EQ(entry.atlas->pack->padding, kTestPadding);
        EXPECT_EQ(entry.atlas->pack->sizeMode.ToStringView(), "Tight");
        EXPECT_EQ(entry.atlas->pack->maxSide, kTestMaxSide);
    }

    TEST(ArtGenAcceptedEntryTest, APackedMemberKeepsItsPlaceAndTakesTheNewSource) {
        TextureLibrary library;
        library.AddOrUpdate(GroupedEntry("queen", "assets/atlases/pieces.png", Rect(8, 16, 20, 30), "pieces", true, "assets/sheet.png", Rect(100, 50, 20, 30)));
        const SpriteEntry entry = AcceptedSpriteEntry(library, "queen"_intern, "assets/artgen/queen.png"_intern, 64, 48);
        EXPECT_EQ(entry.texturePath.ToStringView(), "assets/atlases/pieces.png");
        EXPECT_TRUE(SameRectOf(entry.srcRect, Rect(8, 16, 20, 30)));
        ASSERT_TRUE(entry.atlas.has_value());
        EXPECT_EQ(entry.atlas->name.ToStringView(), "pieces");
        ASSERT_TRUE(entry.atlas->source.has_value());
        EXPECT_EQ(entry.atlas->source->texturePath.ToStringView(), "assets/artgen/queen.png");
        EXPECT_TRUE(SameRectOf(entry.atlas->source->rect, Rect(0, 0, 64, 48)));
        ASSERT_TRUE(entry.atlas->pack.has_value());
        EXPECT_EQ(entry.atlas->pack->sizeMode.ToStringView(), "Tight");
    }

    TEST(ArtGenAcceptedEntryTest, AnotherMembersOfTheSameSheetKeepTheirSource) {
        TextureLibrary library;
        library.AddOrUpdate(GroupedEntry("queen", "assets/atlases/pieces.png", Rect(8, 16, 20, 30), "pieces", true, "assets/sheet.png", Rect(100, 50, 20, 30)));
        library.AddOrUpdate(GroupedEntry("king", "assets/atlases/pieces.png", Rect(40, 16, 20, 30), "pieces", true, "assets/sheet.png", Rect(0, 0, 20, 30)));
        const SpriteEntry accepted = AcceptedSpriteEntry(library, "queen"_intern, "assets/artgen/queen.png"_intern, 64, 48);
        EXPECT_EQ(accepted.atlas->source->texturePath.ToStringView(), "assets/artgen/queen.png");
        EXPECT_EQ(library.GetSprite("king"_intern)->atlas->source->texturePath.ToStringView(), "assets/sheet.png");
    }

    TEST(ArtGenLibraryEntryTest, AnotherLinkOfTheSameTextureAndRectIsStored) {
        const SpriteEntry base = GroupedEntry("queen", "assets/atlases/pieces.png", Rect(8, 16, 20, 30), "pieces", true, "assets/a.png", Rect(0, 0, 20, 30));
        const Vector<SpriteEntry> others{
            GroupedEntry("queen", "assets/atlases/pieces.png", Rect(8, 16, 20, 30), "other", true, "assets/a.png", Rect(0, 0, 20, 30)),
            GroupedEntry("queen", "assets/atlases/pieces.png", Rect(8, 16, 20, 30), "pieces", true, "assets/b.png", Rect(0, 0, 20, 30)),
            GroupedEntry("queen", "assets/atlases/pieces.png", Rect(8, 16, 20, 30), "pieces", true, "assets/a.png", Rect(0, 0, 21, 30)),
            GroupedEntry("queen", "assets/atlases/pieces.png", Rect(8, 16, 20, 30), "pieces", false, "assets/a.png", Rect(0, 0, 20, 30)),
            GroupedEntry("queen", "assets/atlases/pieces.png", Rect(8, 16, 20, 30), "pieces", true),
            LibraryEntry("queen", "assets/atlases/pieces.png", Rect(8, 16, 20, 30)),
        };
        for (const SpriteEntry& other : others) {
            TextureLibrary library;
            library.AddOrUpdate(base);
            int saves = 0;
            const auto stored = StoreSpriteEntry(library, other, [&saves] {
                ++saves;
                return true;
            });
            ASSERT_TRUE(stored);
            EXPECT_TRUE(*stored);
            EXPECT_EQ(saves, 1);
        }

        TextureLibrary same;
        same.AddOrUpdate(base);
        int saves = 0;
        const auto stored = StoreSpriteEntry(same, base, [&saves] {
            ++saves;
            return true;
        });
        ASSERT_TRUE(stored);
        EXPECT_FALSE(*stored);
        EXPECT_EQ(saves, 0);
    }

    TEST(ArtGenLibraryEntryTest, ANewEntryIsAddedAndSavedOnce) {
        TextureLibrary library;
        int saves = 0;
        const auto stored = StoreSpriteEntry(library, LibraryEntry("queen", "assets/artgen/queen.png", Rect(0, 0, 64, 64)), [&saves] {
            ++saves;
            return true;
        });
        ASSERT_TRUE(stored);
        EXPECT_TRUE(*stored);
        EXPECT_EQ(saves, 1);
        EXPECT_NE(library.GetSprite("queen"_intern), nullptr);
    }

    TEST(ArtGenLibraryEntryTest, AnUnchangedEntryIsNotSavedAgain) {
        TextureLibrary library;
        const SpriteEntry entry = LibraryEntry("queen", "assets/artgen/queen.png", Rect(0, 0, 64, 64));
        library.AddOrUpdate(entry);
        int saves = 0;
        const auto stored = StoreSpriteEntry(library, entry, [&saves] {
            ++saves;
            return true;
        });
        ASSERT_TRUE(stored);
        EXPECT_FALSE(*stored);
        EXPECT_EQ(saves, 0);
    }

    TEST(ArtGenLibraryEntryTest, AnEntryWithAnotherSizeIsSavedAndReplacesTheOld) {
        TextureLibrary library;
        library.AddOrUpdate(LibraryEntry("queen", "assets/artgen/queen.png", Rect(0, 0, 64, 64)));
        int saves = 0;
        const auto stored = StoreSpriteEntry(library, LibraryEntry("queen", "assets/artgen/queen.png", Rect(0, 0, 128, 64)), [&saves] {
            ++saves;
            return true;
        });
        ASSERT_TRUE(stored);
        EXPECT_TRUE(*stored);
        EXPECT_EQ(saves, 1);
        EXPECT_EQ(library.GetSprite("queen"_intern)->srcRect.w, 128.0f);
    }

    TEST(ArtGenLibraryEntryTest, AFailedSaveGivesTheLibraryBack) {
        TextureLibrary library;
        library.AddOrUpdate(LibraryEntry("queen", "assets/artgen/queen.png", Rect(0, 0, 64, 64)));

        EXPECT_FALSE(StoreSpriteEntry(library, LibraryEntry("queen", "assets/artgen/queen.png", Rect(0, 0, 128, 64)), [] { return false; }));
        EXPECT_EQ(library.GetSprite("queen"_intern)->srcRect.w, 64.0f);

        EXPECT_FALSE(StoreSpriteEntry(library, LibraryEntry("king", "assets/artgen/king.png", Rect(0, 0, 8, 8)), [] { return false; }));
        EXPECT_EQ(library.GetSprite("king"_intern), nullptr);
    }

    namespace {

        constexpr const char* kTempPrefix = "artgen_accept_atlas";

        constexpr StringView kQueenPng = "assets/artgen/queen.png";
        constexpr StringView kRookPng = "assets/rook.png";
        constexpr StringView kAtlasPng = "assets/atlases/ui.png";

        /// Group "ui" of the queen (4x4 blue) and the rook (2x2 red), their sources on disk; both packed when @p pack
        void FillGroup(TextureLibrary& library, const ArtGenGameFolder& game, bool pack, const PackSettings& settings = PackSettings{}) {
            game.SavePng(kQueenPng, MakeSolid(4, 4, 0, 0, kHigh));
            game.SavePng(kRookPng, MakeSolid(2, 2, kHigh, 0, 0));
            library.AddOrUpdate(GroupedEntry("queen", "assets/artgen/queen.png", Rect(0, 0, 4, 4), "ui", false));
            library.AddOrUpdate(GroupedEntry("rook", "assets/rook.png", Rect(0, 0, 2, 2), "ui", false));
            if (pack) {
                ASSERT_TRUE(WriteAtlas(library, "ui", settings, game.Io()).has_value());
            }
        }

    }  // namespace

    TEST(ArtGenAcceptIntoLibraryTest, APackedSpriteIsRepackedWithTheNewPicture) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        FillGroup(library, game, true);
        const ArtGenCopied copied = game.Copied(kQueenPng, MakeSolid(6, 2, 0, kHigh, 0));
        int saves = 0;
        auto save = [&saves] {
            ++saves;
            return true;
        };

        const auto stored = AcceptIntoLibrary(library, copied, "queen", game.Io(), save);

        ASSERT_TRUE(stored.has_value());
        EXPECT_TRUE(stored->saved);
        EXPECT_EQ(saves, 2);
        ASSERT_TRUE(stored->repacked.has_value());
        EXPECT_EQ(stored->repacked->texturePath.ToStringView(), kAtlasPng);
        EXPECT_EQ(Queen(library).texturePath.ToStringView(), kAtlasPng);
        EXPECT_EQ(Queen(library).srcRect.w, 6.0f);
        EXPECT_EQ(Queen(library).srcRect.h, 2.0f);
        ASSERT_TRUE(IsPackedSprite(Queen(library)));
        EXPECT_EQ(SourceOf(Queen(library)).texturePath.ToStringView(), kQueenPng);
        EXPECT_EQ(SourceOf(Queen(library)).rect.w, 6.0f);

        const RgbaImage atlas = game.Atlas(kAtlasPng);
        EXPECT_TRUE(RectIsSolid(atlas, Queen(library).srcRect, 0, kHigh, 0));
        EXPECT_TRUE(RectIsSolid(atlas, Rook(library).srcRect, kHigh, 0, 0));
    }

    TEST(ArtGenAcceptIntoLibraryTest, TheSamePictureAgainChangesNothingAndSavesNothing) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        FillGroup(library, game, true);
        const ArtGenCopied copied = game.Copied(kQueenPng, MakeSolid(6, 2, 0, kHigh, 0));
        int saves = 0;
        auto save = [&saves] {
            ++saves;
            return true;
        };
        ASSERT_TRUE(AcceptIntoLibrary(library, copied, "queen", game.Io(), save).has_value());
        const Vector<SpriteEntry> before{Queen(library), Rook(library)};
        const Vector<uint8_t> atlasBefore = ReadFileBytes(game.Resolve(kAtlasPng));
        saves = 0;

        const auto again = AcceptIntoLibrary(library, copied, "queen", game.Io(), save);

        ASSERT_TRUE(again.has_value());
        EXPECT_FALSE(again->saved);
        EXPECT_EQ(saves, 0);
        EXPECT_TRUE(again->repacked.has_value());
        EXPECT_EQ(Queen(library).srcRect.w, before[0].srcRect.w);
        EXPECT_EQ(Queen(library).srcRect.x, before[0].srcRect.x);
        EXPECT_EQ(Rook(library).srcRect.x, before[1].srcRect.x);
        EXPECT_EQ(ReadFileBytes(game.Resolve(kAtlasPng)), atlasBefore);
    }

    TEST(ArtGenAcceptIntoLibraryTest, ThePicturesOfOtherMembersStayAsTheyWere) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        FillGroup(library, game, true);
        const ArtGenCopied copied = game.Copied(kQueenPng, MakeSolid(3, 3, kLow, kLow, kLow));
        auto save = [] { return true; };

        ASSERT_TRUE(AcceptIntoLibrary(library, copied, "queen", game.Io(), save).has_value());

        EXPECT_EQ(SourceOf(Rook(library)).texturePath.ToStringView(), kRookPng);
        const RgbaImage atlas = game.Atlas(kAtlasPng);
        EXPECT_TRUE(RectIsSolid(atlas, Rook(library).srcRect, kHigh, 0, 0));
        EXPECT_TRUE(RectIsSolid(atlas, Queen(library).srcRect, kLow, kLow, kLow));
    }

    TEST(ArtGenAcceptIntoLibraryTest, ARepackThatFailsKeepsTheNewSourceAndTheOldAtlas) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        FillGroup(library, game, true);
        const Vector<uint8_t> atlasBefore = ReadFileBytes(game.Resolve(kAtlasPng));
        const SpriteEntry rookBefore = Rook(library);
        RemoveFileIfPresent(game.Resolve(kRookPng));
        const ArtGenCopied copied = game.Copied(kQueenPng, MakeSolid(6, 2, 0, kHigh, 0));
        int saves = 0;
        auto save = [&saves] {
            ++saves;
            return true;
        };

        const auto stored = AcceptIntoLibrary(library, copied, "queen", game.Io(), save);

        ASSERT_FALSE(stored.has_value());
        EXPECT_NE(stored.error().find("could not be repacked"), String::npos);
        EXPECT_NE(stored.error().find("atlas 'ui'"), String::npos);
        EXPECT_NE(stored.error().find("press Pack in the Sprite Library"), String::npos);
        EXPECT_NE(stored.error().find("rook"), String::npos);
        EXPECT_EQ(saves, 1);
        ASSERT_TRUE(IsPackedSprite(Queen(library)));
        EXPECT_EQ(SourceOf(Queen(library)).texturePath.ToStringView(), kQueenPng);
        EXPECT_EQ(SourceOf(Queen(library)).rect.w, 6.0f);
        EXPECT_EQ(Queen(library).texturePath.ToStringView(), kAtlasPng);
        EXPECT_EQ(Rook(library).srcRect.x, rookBefore.srcRect.x);
        EXPECT_EQ(SourceOf(Rook(library)).texturePath.ToStringView(), kRookPng);
        EXPECT_EQ(ReadFileBytes(game.Resolve(kAtlasPng)), atlasBefore);
    }

    TEST(ArtGenAcceptIntoLibraryTest, AFailedSecondSaveGivesTheMembersBack) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        FillGroup(library, game, true);
        const ArtGenCopied copied = game.Copied(kQueenPng, MakeSolid(6, 2, 0, kHigh, 0));
        const Rect queenPlace = Queen(library).srcRect;
        const Rect rookPlace = Rook(library).srcRect;
        int saves = 0;
        auto save = [&saves] { return ++saves == 1; };

        const auto stored = AcceptIntoLibrary(library, copied, "queen", game.Io(), save);

        ASSERT_FALSE(stored.has_value());
        EXPECT_NE(stored.error().find("was rewritten"), String::npos);
        EXPECT_NE(stored.error().find("press Pack in the Sprite Library"), String::npos);
        EXPECT_EQ(Queen(library).srcRect.x, queenPlace.x);
        EXPECT_EQ(Queen(library).srcRect.w, queenPlace.w);
        EXPECT_EQ(Rook(library).srcRect.x, rookPlace.x);
        EXPECT_EQ(Rook(library).srcRect.y, rookPlace.y);
        EXPECT_EQ(SourceOf(Queen(library)).texturePath.ToStringView(), kQueenPng);
        EXPECT_EQ(SourceOf(Queen(library)).rect.w, 6.0f);
    }

    TEST(ArtGenAcceptIntoLibraryTest, AnUnpackedMemberKeepsItsGroupAndNoAtlasIsMade) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        FillGroup(library, game, false);
        const ArtGenCopied copied = game.Copied(kQueenPng, MakeSolid(6, 2, 0, kHigh, 0));
        int saves = 0;
        auto save = [&saves] {
            ++saves;
            return true;
        };

        const auto stored = AcceptIntoLibrary(library, copied, "queen", game.Io(), save);

        ASSERT_TRUE(stored.has_value());
        EXPECT_TRUE(stored->saved);
        EXPECT_FALSE(stored->repacked.has_value());
        EXPECT_EQ(saves, 1);
        EXPECT_EQ(Queen(library).texturePath.ToStringView(), kQueenPng);
        EXPECT_EQ(Queen(library).srcRect.w, 6.0f);
        ASSERT_TRUE(Queen(library).atlas.has_value());
        EXPECT_EQ(Require(Queen(library).atlas).name.ToStringView(), "ui");
        EXPECT_FALSE(Require(Queen(library).atlas).source.has_value());
        EXPECT_FALSE(game.Exists(kAtlasPng));
    }

    TEST(ArtGenAcceptIntoLibraryTest, ASpriteWithNoLinkIsStoredAsBefore) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        library.AddOrUpdate(LibraryEntry("queen", "assets/old.png", Rect(1, 1, 3, 3)));
        const ArtGenCopied copied = game.Copied(kQueenPng, MakeSolid(6, 2, 0, kHigh, 0));
        int saves = 0;
        auto save = [&saves] {
            ++saves;
            return true;
        };

        const auto stored = AcceptIntoLibrary(library, copied, "queen", game.Io(), save);

        ASSERT_TRUE(stored.has_value());
        EXPECT_TRUE(stored->saved);
        EXPECT_FALSE(stored->repacked.has_value());
        EXPECT_EQ(saves, 1);
        EXPECT_EQ(Queen(library).texturePath.ToStringView(), kQueenPng);
        EXPECT_FALSE(Queen(library).atlas.has_value());
        EXPECT_FALSE(game.Exists(kAtlasPng));

        TextureLibrary fresh;
        const auto added = AcceptIntoLibrary(fresh, copied, "queen", game.Io(), save);
        ASSERT_TRUE(added.has_value());
        EXPECT_EQ(Queen(fresh).srcRect.h, 2.0f);
    }

    TEST(ArtGenAcceptIntoLibraryTest, AFailedSaveOfAnUnlinkedSpriteNamesThePng) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        const ArtGenCopied copied = game.Copied(kQueenPng, MakeSolid(6, 2, 0, kHigh, 0));
        auto save = [] { return false; };

        const auto stored = AcceptIntoLibrary(library, copied, "queen", game.Io(), save);

        ASSERT_FALSE(stored.has_value());
        EXPECT_NE(stored.error().find("already written over the game's file"), String::npos);
        EXPECT_EQ(library.GetSprite("queen"_intern), nullptr);
    }

    TEST(ArtGenAcceptIntoLibraryTest, TheRepackUsesTheGroupsSettingsAndMakesTheFileAPlainPackWould) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        PackSettings settings;
        settings.padding = 3;
        settings.sizeMode = AtlasSizeMode::Tight;
        FillGroup(library, game, true, settings);
        const ArtGenCopied copied = game.Copied(kQueenPng, MakeSolid(6, 2, 0, kHigh, 0));
        auto save = [] { return true; };

        ASSERT_TRUE(AcceptIntoLibrary(library, copied, "queen", game.Io(), save).has_value());

        ASSERT_TRUE(Require(Queen(library).atlas).pack.has_value());
        EXPECT_EQ(Require(Require(Queen(library).atlas).pack).padding, 3);
        const PackSettings remembered = PackSettingsOf(library, "ui");
        EXPECT_EQ(remembered.padding, 3);
        EXPECT_EQ(remembered.sizeMode, AtlasSizeMode::Tight);
        const Vector<uint8_t> afterAccept = ReadFileBytes(game.Resolve(kAtlasPng));
        ASSERT_TRUE(WriteAtlas(library, "ui", remembered, game.Io()).has_value());
        EXPECT_EQ(ReadFileBytes(game.Resolve(kAtlasPng)), afterAccept);
    }

    TEST(ArtGenAtlasReplyTest, ARepackNamesTheAtlasItsFileAndSize) {
        AtlasWritten written;
        written.name = "ui"_intern;
        written.texturePath = "assets/atlases/ui.png"_intern;
        written.width = 64;
        written.height = 32;
        written.changed = {"queen"_intern, "rook"_intern};

        const ControlReply reply = AnswerReply(MakeArtGenAtlasAnswer(written));

        ASSERT_TRUE(reply.View().IsObject());
        EXPECT_EQ(StringOf(reply.View(), "name"), "ui");
        EXPECT_EQ(StringOf(reply.View(), "texturePath"), "assets/atlases/ui.png");
        EXPECT_EQ(IntOf(reply.View(), "width").value_or(0), 64);
        EXPECT_EQ(IntOf(reply.View(), "height").value_or(0), 32);
        EXPECT_EQ(IntOf(reply.View(), "members").value_or(0), 2);
    }

    TEST(ArtGenAcceptIntoLibraryTest, TheWrittenAtlasNamesItsGroup) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        FillGroup(library, game, true);
        const ArtGenCopied copied = game.Copied(kQueenPng, MakeSolid(6, 2, 0, kHigh, 0));
        auto save = [] { return true; };

        const auto stored = AcceptIntoLibrary(library, copied, "queen", game.Io(), save);

        ASSERT_TRUE(stored.has_value() && stored->repacked.has_value());
        EXPECT_EQ(stored->repacked->name.ToStringView(), "ui");
        EXPECT_EQ(stored->repacked->changed.size(), 2u);
    }

    namespace {

        /// What CopyVariantIntoAssets would have returned for a sprite of the unpacked kind (no file is read for it)
        ArtGenCopied CopiedOf(StringView sprite, uint32_t width, uint32_t height) {
            ArtGenCopied copied;
            copied.texturePath = Format("assets/artgen/{}.png", sprite);
            copied.width = width;
            copied.height = height;
            return copied;
        }

        /// The library as the batch tests start it: a queen and a rook, no atlas
        void FillPlain(TextureLibrary& library) {
            library.AddOrUpdate(LibraryEntry("queen", "assets/artgen/queen.png", Rect(0, 0, 64, 64)));
            library.AddOrUpdate(LibraryEntry("rook", "assets/artgen/rook.png", Rect(0, 0, 32, 32)));
        }

    }  // namespace

    TEST(ArtGenLibraryBatchTest, NewRowsAreOnlyOwedUntilTheCommitSavesOnce) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        ArtGenLibraryBatch batch(library);
        int saves = 0;
        auto save = [&saves] {
            ++saves;
            return true;
        };

        for (const char* name : {"knight", "bishop", "pawn"}) {
            const auto stored = AcceptIntoLibrary(library, CopiedOf(name, 16, 16), name, game.Io(), [&batch] { return batch.Defer(); });
            ASSERT_TRUE(stored.has_value());
            EXPECT_TRUE(stored->saved);
        }
        EXPECT_EQ(saves, 0);
        EXPECT_EQ(library.GetAll().size(), 3u);

        const auto committed = batch.Commit(save);

        ASSERT_TRUE(committed.has_value());
        EXPECT_TRUE(*committed);
        EXPECT_EQ(saves, 1);
        EXPECT_EQ(library.GetAll().size(), 3u);
    }

    TEST(ArtGenLibraryBatchTest, RowsThatChangeNothingOweNoSave) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        FillPlain(library);
        ArtGenLibraryBatch batch(library);
        ASSERT_TRUE(AcceptIntoLibrary(library, CopiedOf("queen", 64, 64), "queen", game.Io(), [&batch] { return batch.Defer(); }).has_value());
        int saves = 0;

        const auto committed = batch.Commit([&saves] {
            ++saves;
            return true;
        });

        ASSERT_TRUE(committed.has_value());
        EXPECT_FALSE(*committed);
        EXPECT_EQ(saves, 0);
    }

    TEST(ArtGenLibraryBatchTest, AFailedCommitGivesTheLibraryBack) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        FillPlain(library);
        ArtGenLibraryBatch batch(library);
        auto defer = [&batch] { return batch.Defer(); };
        ASSERT_TRUE(AcceptIntoLibrary(library, CopiedOf("queen", 128, 64), "queen", game.Io(), defer).has_value());
        ASSERT_TRUE(AcceptIntoLibrary(library, CopiedOf("king", 8, 8), "king", game.Io(), defer).has_value());
        ASSERT_EQ(Queen(library).srcRect.w, 128.0f);
        ASSERT_NE(library.GetSprite("king"_intern), nullptr);

        const auto committed = batch.Commit([] { return false; });

        ASSERT_FALSE(committed.has_value());
        EXPECT_NE(committed.error().find("TextureLibrary.xml could not be saved"), String::npos);
        EXPECT_EQ(library.GetAll().size(), 2u);
        EXPECT_EQ(library.GetSprite("king"_intern), nullptr);
        EXPECT_EQ(Queen(library).srcRect.w, 64.0f);
        EXPECT_EQ(Rook(library).srcRect.w, 32.0f);
        EXPECT_EQ(At(library.GetAll(), 0).name.ToStringView(), "queen");
    }

    TEST(ArtGenLibraryBatchTest, AFailedCommitAfterARepackTellsThePersonToPack) {
        TextureLibrary library;
        FillPlain(library);
        ArtGenLibraryBatch plain(library);
        library.AddOrUpdate(LibraryEntry("queen", "assets/artgen/queen.png", Rect(0, 0, 128, 64)));
        ASSERT_TRUE(plain.Defer());
        const auto plainFailure = plain.Commit([] { return false; });
        ASSERT_FALSE(plainFailure.has_value());
        EXPECT_EQ(plainFailure.error().find("Pack"), String::npos) << "no atlas was touched";

        ArtGenLibraryBatch repacked(library);
        library.AddOrUpdate(LibraryEntry("queen", "assets/artgen/queen.png", Rect(0, 0, 128, 64)));
        ASSERT_TRUE(repacked.Defer());
        repacked.NoteRepacked();
        const auto repackedFailure = repacked.Commit([] { return false; });
        ASSERT_FALSE(repackedFailure.has_value());
        EXPECT_NE(repackedFailure.error().find("press Pack in the Sprite Library"), String::npos) << repackedFailure.error().c_str();
    }

    TEST(ArtGenLibraryBatchTest, APackedRowIsSavedOnceAndItsMembersComeBackWhenTheSaveFails) {
        const ArtGenGameFolder game(kTempPrefix);
        TextureLibrary library;
        FillGroup(library, game, true);
        ArtGenLibraryBatch batch(library);
        const ArtGenCopied copied = game.Copied(kQueenPng, MakeSolid(6, 2, 0, kHigh, 0));

        const auto stored = AcceptIntoLibrary(library, copied, "queen", game.Io(), [&batch] { return batch.Defer(); });
        ASSERT_TRUE(stored.has_value());
        ASSERT_TRUE(stored->repacked.has_value());
        ASSERT_EQ(SourceOf(Queen(library)).rect.w, 6.0f);

        int saves = 0;
        const auto committed = batch.Commit([&saves] {
            ++saves;
            return true;
        });
        ASSERT_TRUE(committed.has_value());
        EXPECT_TRUE(*committed);
        EXPECT_EQ(saves, 1);

        ArtGenLibraryBatch failing(library);
        const ArtGenCopied again = game.Copied(kQueenPng, MakeSolid(3, 3, kLow, kLow, kLow));
        const SpriteEntry queenSaved = Queen(library);
        const SpriteEntry rookSaved = Rook(library);
        ASSERT_TRUE(AcceptIntoLibrary(library, again, "queen", game.Io(), [&failing] { return failing.Defer(); }).has_value());
        ASSERT_NE(SourceOf(Queen(library)).rect.w, SourceOf(queenSaved).rect.w);

        EXPECT_FALSE(failing.Commit([] { return false; }).has_value());

        EXPECT_EQ(SourceOf(Queen(library)).rect.w, SourceOf(queenSaved).rect.w);
        EXPECT_EQ(Queen(library).srcRect.x, queenSaved.srcRect.x);
        EXPECT_EQ(Queen(library).srcRect.w, queenSaved.srcRect.w);
        EXPECT_EQ(Rook(library).srcRect.x, rookSaved.srcRect.x);
        EXPECT_EQ(Rook(library).srcRect.y, rookSaved.srcRect.y);
    }

    TEST(ArtGenSpriteNameTest, OnlyPlainNamesAreSpriteNames) {
        EXPECT_TRUE(IsValidSpriteName("white_knight"));
        EXPECT_TRUE(IsValidSpriteName("Gem-Red-2"));
        EXPECT_FALSE(IsValidSpriteName(""));
        EXPECT_FALSE(IsValidSpriteName("../config/x"));
        EXPECT_FALSE(IsValidSpriteName("a b"));
        EXPECT_FALSE(IsValidSpriteName("knight.png"));
        EXPECT_FALSE(IsValidSpriteName(String(kMaxSpriteNameLength + 1, 'a')));
        EXPECT_TRUE(IsValidSpriteName(String(kMaxSpriteNameLength, 'a')));
    }

    TEST_F(ArtGenAcceptTest, ThePngSizeComesFromItsHeader) {
        const auto size = ReadPngSize(png);
        ASSERT_TRUE(size.has_value());
        EXPECT_EQ(size->first, 1024u);
        EXPECT_EQ(size->second, 768u);
        EXPECT_FALSE(ReadPngSize(provenance).has_value());
        EXPECT_FALSE(ReadPngSize(folder / "missing.png").has_value());
    }

    TEST_F(ArtGenAcceptTest, TheVariantGoesIntoAssetsUnderTheSpriteName) {
        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "white_queen", kIdentity);
        ASSERT_TRUE(copied.has_value()) << copied.error().c_str();
        EXPECT_EQ(copied->texturePath, "assets/artgen/white_queen.png");
        EXPECT_EQ(copied->width, 1024u);
        EXPECT_EQ(copied->height, 768u);
        EXPECT_TRUE(PathExists(folder / "assets" / "artgen" / "white_queen.png"));
        EXPECT_EQ(ReadFile(folder / "assets" / "artgen" / "white_queen.artgen.json"), "{\"seed\": 7}");
    }

    TEST_F(ArtGenAcceptTest, ASecondAcceptOfTheSameNameReplacesTheFiles) {
        ASSERT_TRUE(CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity).has_value());
        const auto other = folder / "out" / "queen_8.png";
        WritePngHeader(other, 512, 512);
        WriteFile(provenance, "{\"seed\": 8}");
        const auto copied = CopyVariantIntoAssets(other, provenance, folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(copied.has_value());
        EXPECT_EQ(copied->width, 512u);
        EXPECT_EQ(Require(ReadPngSize(folder / "assets" / "artgen" / "queen.png")).first, 512u);
        EXPECT_EQ(ReadFile(folder / "assets" / "artgen" / "queen.artgen.json"), "{\"seed\": 8}");
    }

    TEST_F(ArtGenAcceptTest, ABadNameOrANonPngIsRefusedAndWritesNothing) {
        EXPECT_FALSE(CopyVariantIntoAssets(png, provenance, folder / "assets", "../escape", kIdentity).has_value());
        EXPECT_FALSE(CopyVariantIntoAssets(provenance, provenance, folder / "assets", "queen", kIdentity).has_value());
        EXPECT_FALSE(PathExists(folder / "assets" / "artgen"));
    }

    TEST_F(ArtGenAcceptTest, AVariantWithoutProvenanceIsStillAccepted) {
        RemoveFileIfPresent(provenance);
        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(copied.has_value());
        EXPECT_FALSE(PathExists(copied->provenanceFile));
    }

    TEST_F(ArtGenAcceptTest, TheProvenanceFileIsBesideTheMediaFile) {
        EXPECT_EQ(ProvenanceFileOf("assets/artgen/queen.png"), Path("assets/artgen/queen.artgen.json"));
        EXPECT_EQ(ProvenanceFileOf("assets/sounds/move.wav"), Path("assets/sounds/move.artgen.json"));
    }

    TEST_F(ArtGenAcceptTest, OnlyTheLastExtensionOfTheMediaFileIsReplacedByTheProvenanceOne) {
        EXPECT_EQ(ProvenanceFileOf("x/a.b.png"), Path("x/a.b.artgen.json"));
        EXPECT_EQ(ProvenanceFileOf("a.png"), Path("a.artgen.json"));
    }

    TEST_F(ArtGenAcceptTest, AFileShorterThanThePngHeaderHasNoSize) {
        const auto shortFile = folder / "out" / "short.png";
        WriteFile(shortFile, "\x89PNG");
        EXPECT_FALSE(ReadPngSize(shortFile).has_value());
        EXPECT_FALSE(ReadPngSize(folder / "out" / "nothing.png").has_value());
    }

    TEST_F(ArtGenAcceptTest, AnAcceptIntoAGameWhoseAssetsFolderIsNonAsciiWritesTheFiles) {
        const Path assets(folder / "caf\xC3\xA9" / "assets");
        const auto identity = CopyVariantIntoAssets(png, provenance, assets, "queen", kIdentity);
        ASSERT_TRUE(identity.has_value()) << identity.error().c_str();
        EXPECT_EQ(Require(ReadPngSize(identity->pngFile)).first, 1024u);
        EXPECT_EQ(ReadFile(identity->provenanceFile), "{\"seed\": 7}");

        const auto source = folder / "out" / "real.png";
        ASSERT_NO_FATAL_FAILURE(WriteRealPng(source, 256, 256, {40, 40, 100, 100}));
        ArtGenImagePlan own;
        own.settings = ArtGenImageSettings{true, 5, 64};
        const auto processed = CopyVariantIntoAssets(source, provenance, assets, "king", own);
        ASSERT_TRUE(processed.has_value()) << processed.error().c_str();
        EXPECT_EQ(processed->pngFile, assets / "artgen" / "king.png");
        EXPECT_TRUE(ReadPngSize(processed->pngFile).has_value());
        EXPECT_FALSE(PathExists(Path(Format("{}.tmp", processed->pngFile.View()))));
        EXPECT_TRUE(JsonDoc(StringView(ReadFile(processed->provenanceFile))).IsParsed());
    }

    TEST_F(ArtGenAcceptTest, AnIdentityAcceptWithNoProvenanceRemovesTheOldRecordOfTheName) {
        ASSERT_TRUE(CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity).has_value());
        const auto old = folder / "assets" / "artgen" / "queen.artgen.json";
        ASSERT_TRUE(PathExists(old));

        const auto copied = CopyVariantIntoAssets(png, folder / "out" / "none.artgen.json", folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(copied.has_value()) << copied.error().c_str();
        EXPECT_FALSE(PathExists(old));

        const auto again = CopyVariantIntoAssets(png, Path(), folder / "assets", "queen", kIdentity);
        EXPECT_TRUE(again.has_value()) << "a name with no old record is no error";
    }

    TEST_F(ArtGenAcceptTest, KeptIsAddedToTheCopiedProvenanceAndItsOwnKeysStay) {
        WriteFile(provenance, "{\"seed\": 7, \"preset\": \"gem\"}");
        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(copied.has_value());
        ASSERT_TRUE(MarkProvenanceKept(copied->provenanceFile, 7, "2026-11-17T10:00:00Z").has_value());

        const JsonDoc record(StringView(ReadFile(copied->provenanceFile)));
        ASSERT_TRUE(record.IsParsed());
        EXPECT_EQ(IntOf(record.Root(), "seed").value_or(-1), 7);
        EXPECT_EQ(StringOf(record.Root(), "preset"), "gem");
        const JsonView kept = MemberOf(record.Root(), "kept");
        ASSERT_TRUE(kept.IsObject());
        EXPECT_EQ(IntOf(kept, "seed").value_or(-1), 7);
        EXPECT_EQ(StringOf(kept, "date"), "2026-11-17T10:00:00Z");
    }

    TEST_F(ArtGenAcceptTest, AnAcceptAndAKeepWriteTheRecordByteForByteAndLeaveTheServicesKeysUntouched) {
        // The service's keys are in no order and one holds a nested object and an exponent: they come back sorted at every level, the `accepted` and `kept` records are
        // new, and `seed` is read back from the record
        WriteFile(provenance, R"({"seed": 7, "zeta": {"b": [1, 2.5], "a": "x"}, "checks": {"passed": true}, "alpha": 1e2})");
        const auto source = folder / "out" / "real.png";
        ASSERT_NO_FATAL_FAILURE(WriteRealPng(source, 512, 512, {10, 20, 100, 200}));
        const auto copied = CopyVariantIntoAssets(source, provenance, folder / "assets", "queen", ArtGenImagePlan{});
        ASSERT_TRUE(copied.has_value()) << copied.error().c_str();
        ASSERT_TRUE(MarkProvenanceKept(copied->provenanceFile, 7, "2026-11-17T10:00:00Z").has_value());

        const String expected = Format(R"({{
  "accepted": {{
    "crop": {{
      "h": 208,
      "w": 108,
      "x": 6,
      "y": 16
    }},
    "frame": "sprite",
    "processing": {{
      "maxSize": 256,
      "padding": 2,
      "trim": true
    }},
    "scale": 1.0,
    "seed": 7,
    "size": [
      108,
      208
    ],
    "source": "{}",
    "sprite": "queen"
  }},
  "alpha": 100.0,
  "checks": {{
    "passed": true
  }},
  "kept": {{
    "date": "2026-11-17T10:00:00Z",
    "seed": 7
  }},
  "seed": 7,
  "zeta": {{
    "a": "x",
    "b": [
      1,
      2.5
    ]
  }}
}}
)",
                                       source.View());
        EXPECT_EQ(ReadFile(copied->provenanceFile), expected);
    }

    TEST_F(ArtGenAcceptTest, AnAcceptWithoutASeedInTheRecordWritesANull) {
        const auto source = folder / "out" / "real.png";
        ASSERT_NO_FATAL_FAILURE(WriteRealPng(source, 512, 512, {10, 20, 100, 200}));
        WriteFile(provenance, R"({"seed": "seven"})");
        const auto copied = CopyVariantIntoAssets(source, provenance, folder / "assets", "queen", ArtGenImagePlan{});
        ASSERT_TRUE(copied.has_value()) << copied.error().c_str();
        const String text = ReadFile(copied->provenanceFile);
        EXPECT_NE(text.find("\"seed\": null,"), String::npos) << text.c_str();
        EXPECT_NE(text.find("\"seed\": \"seven\""), String::npos) << "the service's own seed stays as it was";
    }

    TEST_F(ArtGenAcceptTest, ARecordSavedWithAByteOrderMarkIsMergedInto) {
        const auto source = folder / "out" / "real.png";
        ASSERT_NO_FATAL_FAILURE(WriteRealPng(source, 512, 512, {10, 20, 100, 200}));
        WriteFile(provenance, "\xEF\xBB\xBF{\"preset\": \"gem\"}");
        const auto copied = CopyVariantIntoAssets(source, provenance, folder / "assets", "queen", ArtGenImagePlan{});
        ASSERT_TRUE(copied.has_value()) << copied.error().c_str();
        EXPECT_NE(ReadFile(copied->provenanceFile).find("\"preset\": \"gem\""), String::npos);

        WriteFile(copied->provenanceFile, "\xEF\xBB\xBF{\"preset\": \"gem\"}");
        ASSERT_TRUE(MarkProvenanceKept(copied->provenanceFile, 7, "2026-11-17T10:00:00Z").has_value());
        EXPECT_EQ(ReadFile(copied->provenanceFile), "{\n  \"kept\": {\n    \"date\": \"2026-11-17T10:00:00Z\",\n    \"seed\": 7\n  },\n  \"preset\": \"gem\"\n}\n");
    }

    TEST_F(ArtGenAcceptTest, ASecondKeptReplacesTheFirst) {
        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(copied.has_value());
        ASSERT_TRUE(MarkProvenanceKept(copied->provenanceFile, 7, "2026-11-17T10:00:00Z").has_value());
        ASSERT_TRUE(MarkProvenanceKept(copied->provenanceFile, 9, "2026-11-17T11:00:00Z").has_value());

        const JsonDoc record(StringView(ReadFile(copied->provenanceFile)));
        ASSERT_TRUE(record.IsParsed());
        const JsonView kept = MemberOf(record.Root(), "kept");
        ASSERT_TRUE(kept.IsObject());
        EXPECT_EQ(IntOf(kept, "seed").value_or(-1), 9);
        EXPECT_EQ(StringOf(kept, "date"), "2026-11-17T11:00:00Z");
    }

    TEST_F(ArtGenAcceptTest, TheGameFileIsWhereAcceptWritesIt) {
        EXPECT_EQ(ArtGenGameFile("queen", "image"), "assets/artgen/queen.png");
        EXPECT_EQ(ArtGenGameFile("click", "audio"), "assets/artgen/click.wav");

        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(copied.has_value());
        EXPECT_EQ(copied->texturePath, ArtGenGameFile("queen", "image"));
    }

    TEST_F(ArtGenAcceptTest, KeepingAMissingOrBrokenProvenanceIsAnErrorThatNamesTheFile) {
        const auto missing = folder / "assets" / "nothing.artgen.json";
        const auto result = MarkProvenanceKept(missing, 7, "2026-11-17T10:00:00Z");
        ASSERT_FALSE(result.has_value());
        EXPECT_NE(result.error().find("nothing.artgen.json"), String::npos);
        EXPECT_FALSE(PathExists(missing));

        WriteFile(provenance, "[1, 2]");
        const auto notObject = MarkProvenanceKept(provenance, 7, "2026-11-17T10:00:00Z");
        ASSERT_FALSE(notObject.has_value());
        EXPECT_NE(notObject.error().find("queen_7.artgen.json"), String::npos);
        EXPECT_EQ(ReadFile(provenance), "[1, 2]");
    }

    TEST_F(ArtGenAcceptTest, ASwitchRewritesTheProvenanceFromTheVariantWithoutKept) {
        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(copied.has_value());
        ASSERT_TRUE(MarkProvenanceKept(copied->provenanceFile, 7, "2026-11-17T10:00:00Z").has_value());
        WriteFile(provenance, "{\"seed\": 8}");
        ASSERT_TRUE(CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity).has_value());
        EXPECT_EQ(ReadFile(copied->provenanceFile).find("kept"), String::npos);
    }

    TEST_F(ArtGenAcceptTest, AnIdentityAcceptOfAVariantWithNoProvenanceRemovesTheOldRecord) {
        const auto first = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(MarkProvenanceKept(first->provenanceFile, 7, "2026-11-17T10:00:00Z").has_value());

        RemoveFileIfPresent(provenance);
        const auto second = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(second.has_value()) << second.error().c_str();
        EXPECT_FALSE(PathExists(second->provenanceFile)) << "a record with the earlier seed and its `kept` is not this variant's";

        const auto withEmptyPath = CopyVariantIntoAssets(png, Path(), folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(withEmptyPath.has_value());
        EXPECT_FALSE(PathExists(withEmptyPath->provenanceFile));
    }

    TEST_F(ArtGenAcceptTest, ADefaultAcceptOfAVariantWithNoProvenanceReplacesTheOldRecordWithAnAcceptedOnlyOne) {
        const auto source = folder / "out" / "real.png";
        ASSERT_NO_FATAL_FAILURE(WriteRealPng(source, 256, 256, {40, 40, 100, 100}));
        const auto first = CopyVariantIntoAssets(source, provenance, folder / "assets", "queen", ArtGenImagePlan{});
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(MarkProvenanceKept(first->provenanceFile, 7, "2026-11-17T10:00:00Z").has_value());

        RemoveFileIfPresent(provenance);
        const auto second = CopyVariantIntoAssets(source, provenance, folder / "assets", "queen", ArtGenImagePlan{});
        ASSERT_TRUE(second.has_value()) << second.error().c_str();
        const JsonDoc record(StringView(ReadFile(second->provenanceFile)));
        ASSERT_TRUE(record.IsParsed());
        EXPECT_TRUE(record.Root().Find("accepted").has_value());
        EXPECT_FALSE(record.Root().Find("kept").has_value());
        EXPECT_FALSE(record.Root().Find("seed").has_value()) << "the earlier variant's seed is not carried over";
    }

    namespace {

        class FakeTexture : public IResource {
        public:
            explicit FakeTexture(PoolString path) : _path(path) {}

            ResourceState GetState() const override {
                return ResourceState::Loaded;
            }
            PoolString GetPath() const override {
                return _path;
            }
            uint64_t GetMemoryUsage() const override {
                return 1;
            }
            PoolString GetTypeName() const override {
                return "FakeTexture"_intern;
            }

        private:
            PoolString _path;
        };

        /// What a holder that keeps a texture of the cache sees of an accept: the handle it kept and the cache after the accept's evict
        struct EvictScene {
            ResourceCache cache;
            Vector<String> evicted;

            IntrusivePtr<IResource> Put(PoolString path) {
                cache.Put(path, New<FakeTexture>(path));
                return cache.Get(path);
            }

            void Evict(const ArtGenCopied& copied, const Optional<AtlasWritten>& repacked) {
                EvictAcceptedTextures(copied, repacked, [this](StringView path) {
                    evicted.push_back(String(path));
                    return cache.Remove(PoolString::Intern(path));
                });
            }
        };

        ArtGenCopied CopiedAt(const char* texturePath) {
            ArtGenCopied copied;
            copied.texturePath = texturePath;
            return copied;
        }

    }  // namespace

    TEST(ArtGenEvictTest, AReAcceptAndASwitchOfTheSameNameFlipTheHoldersPredicate) {
        EvictScene scene;
        const auto queen = "assets/artgen/queen.png"_intern;
        const auto rook = "assets/artgen/rook.png"_intern;
        const IntrusivePtr<IResource> queenHandle = scene.Put(queen);
        const IntrusivePtr<IResource> rookHandle = scene.Put(rook);
        EXPECT_FALSE(IsEvictedHandle(scene.cache, queen, queenHandle.Get()));

        // A re-accept of the name writes the same path
        scene.Evict(CopiedAt("assets/artgen/queen.png"), {});
        EXPECT_TRUE(IsEvictedHandle(scene.cache, queen, queenHandle.Get()));
        EXPECT_FALSE(IsEvictedHandle(scene.cache, rook, rookHandle.Get())) << "a path that was not accepted stays held";

        // The holder loads the path again; a switch to another variant of the same name is the same path again
        const IntrusivePtr<IResource> reloaded = scene.Put(queen);
        EXPECT_FALSE(IsEvictedHandle(scene.cache, queen, reloaded.Get())) << "the fresh resource is the held one";
        EXPECT_TRUE(IsEvictedHandle(scene.cache, queen, queenHandle.Get())) << "the old handle stays evicted";
        scene.Evict(CopiedAt("assets/artgen/queen.png"), {});
        EXPECT_TRUE(IsEvictedHandle(scene.cache, queen, reloaded.Get()));
        EXPECT_FALSE(IsEvictedHandle(scene.cache, rook, rookHandle.Get()));
    }

    TEST(ArtGenEvictTest, ARepackEvictsTheAtlasPathToo) {
        EvictScene scene;
        const auto sprite = "assets/artgen/queen.png"_intern;
        const auto atlas = "assets/atlases/pieces.png"_intern;
        const IntrusivePtr<IResource> spriteHandle = scene.Put(sprite);
        const IntrusivePtr<IResource> atlasHandle = scene.Put(atlas);
        const IntrusivePtr<IResource> otherHandle = scene.Put("assets/atlases/other.png"_intern);

        AtlasWritten repacked;
        repacked.texturePath = atlas;
        scene.Evict(CopiedAt("assets/artgen/queen.png"), repacked);

        EXPECT_TRUE(IsEvictedHandle(scene.cache, sprite, spriteHandle.Get()));
        EXPECT_TRUE(IsEvictedHandle(scene.cache, atlas, atlasHandle.Get()));
        EXPECT_FALSE(IsEvictedHandle(scene.cache, "assets/atlases/other.png"_intern, otherHandle.Get()));
        ASSERT_EQ(scene.evicted.size(), 2u);
        EXPECT_EQ(scene.evicted[0], "assets/artgen/queen.png");
        EXPECT_EQ(scene.evicted[1], "assets/atlases/pieces.png");
    }

    TEST(ArtGenEvictTest, AnAcceptWithNoRepackEvictsOnlyThePicture) {
        EvictScene scene;
        scene.Evict(CopiedAt("assets/artgen/queen.png"), {});
        ASSERT_EQ(scene.evicted.size(), 1u);
        EXPECT_EQ(scene.evicted[0], "assets/artgen/queen.png");
    }

    TEST(ArtGenEvictTest, ANullHandleIsNeverEvicted) {
        ResourceCache cache;
        EXPECT_FALSE(IsEvictedHandle(cache, "assets/artgen/queen.png"_intern, nullptr));
    }

    TEST_F(ArtGenAcceptTest, AnIdentityPlanCopiesBothFilesByteForByte) {
        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(copied.has_value()) << copied.error().c_str();
        EXPECT_EQ(ReadFile(copied->pngFile), ReadFile(png));
        EXPECT_EQ(ReadFile(copied->provenanceFile), "{\"seed\": 7}");
        EXPECT_EQ(copied->width, 1024u);
        EXPECT_EQ(copied->height, 768u);
        EXPECT_EQ(copied->crop, (ArtGenIntRect{0, 0, 1024, 768}));
        EXPECT_EQ(copied->scale, 1.0);
    }

    TEST_F(ArtGenAcceptTest, ADefaultPlanWritesTheTrimmedAndDownscaledPicture) {
        const auto source = folder / "out" / "real.png";
        ASSERT_NO_FATAL_FAILURE(WriteRealPng(source, 1024, 768, {100, 50, 400, 300}));
        const auto copied = CopyVariantIntoAssets(source, provenance, folder / "assets", "queen", ArtGenImagePlan{});
        ASSERT_TRUE(copied.has_value()) << copied.error().c_str();

        // 2 percent of 400 is 8 on every side: a 416 x 316 crop, scaled to 256 on the longer side
        EXPECT_EQ(copied->crop, (ArtGenIntRect{92, 42, 416, 316}));
        EXPECT_EQ(copied->width, 256u);
        EXPECT_EQ(copied->height, 194u);
        EXPECT_NEAR(copied->scale, 256.0 / 416.0, 1e-9);
        EXPECT_FALSE(copied->fromSet);
        const auto written = ReadPngSize(copied->pngFile);
        ASSERT_TRUE(written.has_value());
        EXPECT_EQ(written->first, 256u);
        EXPECT_EQ(written->second, 194u);
    }

    TEST_F(ArtGenAcceptTest, TheProvenanceHasAcceptedAndKeepsTheGenerationsOwnKeys) {
        const auto source = folder / "out" / "real.png";
        ASSERT_NO_FATAL_FAILURE(WriteRealPng(source, 512, 512, {10, 20, 100, 200}));
        WriteFile(provenance, "{\"seed\": 7, \"preset\": \"gem\"}");
        const auto copied = CopyVariantIntoAssets(source, provenance, folder / "assets", "queen", ArtGenImagePlan{});
        ASSERT_TRUE(copied.has_value()) << copied.error().c_str();

        const JsonDoc record(StringView(ReadFile(copied->provenanceFile)));
        ASSERT_TRUE(record.IsParsed());
        EXPECT_EQ(StringOf(record.Root(), "preset"), "gem");
        const JsonView accepted = MemberOf(record.Root(), "accepted");
        ASSERT_TRUE(accepted.IsObject());
        EXPECT_EQ(StringOf(accepted, "sprite"), "queen");
        EXPECT_EQ(StringOf(accepted, "source"), String(source.View()));
        EXPECT_EQ(IntOf(accepted, "seed").value_or(-1), 7);
        EXPECT_EQ(StringOf(accepted, "frame"), "sprite");
        const JsonView processing = MemberOf(accepted, "processing");
        ASSERT_TRUE(processing.IsObject());
        EXPECT_EQ(BoolOf(processing, "trim").value_or(false), true);
        EXPECT_EQ(IntOf(processing, "maxSize").value_or(-1), 256);
        const JsonView crop = MemberOf(accepted, "crop");
        ASSERT_TRUE(crop.IsObject());
        EXPECT_EQ(IntOf(crop, "w").value_or(-1), 108);
        EXPECT_EQ(MemberOf(accepted, "scale").AsDouble().value_or(0.0), copied->scale);
        const JsonView size = MemberOf(accepted, "size");
        ASSERT_TRUE(size.IsArray());
        EXPECT_EQ(size.Size(), 2u);
    }

    TEST_F(ArtGenAcceptTest, TheAnswersProcessingSaysWhatTheProvenanceRecords) {
        const auto source = folder / "out" / "real.png";
        ASSERT_NO_FATAL_FAILURE(WriteRealPng(source, 512, 512, {10, 20, 100, 200}));
        const Array<ArtGenIntRect, 1> bounds{ArtGenIntRect{10, 20, 100, 200}};
        ArtGenImagePlan own;
        own.settings = ArtGenImageSettings{true, 5, 64};
        ArtGenImagePlan framed;
        framed.frame = MakeArtGenFrame(bounds, 512, 512, ArtGenImageSettings{}).value();

        for (const ArtGenImagePlan& plan : {own, framed}) {
            const auto copied = CopyVariantIntoAssets(source, provenance, folder / "assets", "queen", plan);
            ASSERT_TRUE(copied.has_value()) << copied.error().c_str();
            const JsonDoc record(StringView(ReadFile(copied->provenanceFile)));
            ASSERT_TRUE(record.IsParsed());
            const JsonView accepted = MemberOf(record.Root(), "accepted");
            ASSERT_TRUE(accepted.IsObject());

            const ControlReply answer = AnswerReply(MakeArtGenProcessingAnswer(*copied));

            for (const StringView key : {"crop", "scale", "size", "frame"}) {
                const Optional<JsonView> fromAnswer = answer.View().Find(key);
                const Optional<JsonView> fromFile = accepted.Find(key);
                ASSERT_TRUE(fromAnswer.has_value() && fromFile.has_value()) << key.data();
                EXPECT_TRUE(JsonEquals(*fromAnswer, *fromFile)) << key.data();
            }
            // The settings are what the file writer says, so the two cannot drift
            String settingsText;
            JsonWriter settingsWriter(settingsText);
            WriteArtGenImageSettings(settingsWriter, copied->settings);
            const JsonDoc settings{StringView(settingsText)};
            ASSERT_TRUE(settings.IsParsed());
            for (const JsonMember member : settings.Root().Members()) {
                const Optional<JsonView> fromAnswer = answer.View().Find(member.key.Text().value_or(StringView()));
                ASSERT_TRUE(fromAnswer.has_value()) << settingsText.c_str();
                EXPECT_TRUE(JsonEquals(*fromAnswer, member.value)) << settingsText.c_str();
            }
            EXPECT_EQ(answer.View().Size(), 7u) << "the settings and the placement, nothing else";
        }
    }

    TEST_F(ArtGenAcceptTest, APlanFrameTheImageDoesNotFitIsRefusedAndWritesNothing) {
        const auto source = folder / "out" / "real.png";
        ASSERT_NO_FATAL_FAILURE(WriteRealPng(source, 256, 256, {100, 100, 100, 100}));
        ArtGenImagePlan plan;
        plan.frame = ArtGenImageFrame{0, 0, 120, 120, 60, 60};
        const auto copied = CopyVariantIntoAssets(source, provenance, folder / "assets", "queen", plan);
        ASSERT_FALSE(copied.has_value());
        EXPECT_NE(copied.error().find("does not fit the frame"), String::npos) << copied.error().c_str();
        EXPECT_NE(copied.error().find("real.png"), String::npos);
        EXPECT_FALSE(PathExists(folder / "assets" / "artgen"));
    }

    TEST_F(ArtGenAcceptTest, TwoImagesInOnePlanFrameGiveFilesOfOneSize) {
        const auto tall = folder / "out" / "tall.png";
        const auto small = folder / "out" / "small.png";
        ASSERT_NO_FATAL_FAILURE(WriteRealPng(tall, 512, 512, {200, 100, 100, 300}));
        ASSERT_NO_FATAL_FAILURE(WriteRealPng(small, 512, 512, {200, 300, 100, 100}));
        const Array<ArtGenIntRect, 2> bounds{ArtGenIntRect{200, 100, 100, 300}, ArtGenIntRect{200, 300, 100, 100}};
        const auto frame = MakeArtGenFrame(bounds, 512, 512, ArtGenImageSettings{});
        ASSERT_TRUE(frame.has_value());
        ArtGenImagePlan plan;
        plan.frame = *frame;

        const auto first = CopyVariantIntoAssets(tall, provenance, folder / "assets", "king", plan);
        const auto second = CopyVariantIntoAssets(small, provenance, folder / "assets", "pawn", plan);
        ASSERT_TRUE(first.has_value()) << first.error().c_str();
        ASSERT_TRUE(second.has_value()) << second.error().c_str();
        EXPECT_EQ(first->width, second->width);
        EXPECT_EQ(first->height, second->height);
        EXPECT_EQ(first->crop, second->crop);
        EXPECT_TRUE(first->fromSet);
        const auto firstSize = ReadPngSize(first->pngFile);
        const auto secondSize = ReadPngSize(second->pngFile);
        ASSERT_TRUE(firstSize.has_value());
        ASSERT_TRUE(secondSize.has_value());
        EXPECT_EQ(*firstSize, *secondSize);
        const JsonDoc record(StringView(ReadFile(second->provenanceFile)));
        ASSERT_TRUE(record.IsParsed());
        EXPECT_EQ(StringOf(MemberOf(record.Root(), "accepted"), "frame"), "set");
    }

    TEST_F(ArtGenAcceptTest, AnImageWithNothingDrawnIsRefusedAndWritesNothing) {
        const auto source = folder / "out" / "empty.png";
        ASSERT_NO_FATAL_FAILURE(WriteRealPng(source, 64, 64, {0, 0, 0, 0}));
        const auto copied = CopyVariantIntoAssets(source, provenance, folder / "assets", "queen", ArtGenImagePlan{});
        ASSERT_FALSE(copied.has_value());
        EXPECT_NE(copied.error().find("nothing drawn"), String::npos) << copied.error().c_str();
        EXPECT_FALSE(PathExists(folder / "assets" / "artgen"));
    }

    TEST_F(ArtGenAcceptTest, AMeasuredFrameCutsTheRowsOfASetToOneSizeAndKeepsTheirProportion) {
        // Two rows of different objects, each with an alternative; the second alternative of the king is the biggest thing drawn
        struct Picture {
            const char* name;
            ArtGenIntRect box;
        };
        const Array<Picture, 4> pictures{Picture{"king_1", {200, 100, 100, 300}}, Picture{"king_2", {180, 80, 140, 340}}, Picture{"pawn_1", {200, 300, 100, 100}},
                                         Picture{"pawn_2", {210, 300, 90, 120}}};
        Vector<String> files;
        for (const Picture& picture : pictures) {
            const auto file = folder / "out" / Format("{}.png", picture.name).c_str();
            ASSERT_NO_FATAL_FAILURE(WriteRealPng(file, 512, 512, picture.box));
            files.push_back(String(file.View()));
        }
        const ArtGenImageSettings settings;
        const auto frame = MeasureFrameForControl(files, settings);
        ASSERT_TRUE(frame.has_value()) << frame.error().c_str();
        for (const Picture& picture : pictures) {
            EXPECT_TRUE(FrameContains(*frame, picture.box)) << picture.name << ": the frame is the union of every variant";
        }
        EXPECT_LT(frame->outHeight, frame->h) << "the maxSize of the defaults scales the 512 square's crop down";

        ArtGenImagePlan plan;
        plan.settings = settings;
        plan.frame = *frame;
        Array<ArtGenCopied, 4> written;
        for (size_t index = 0; index < pictures.size(); ++index) {
            const auto source = folder / "out" / Format("{}.png", pictures[index].name).c_str();
            const auto copied = CopyVariantIntoAssets(source, provenance, folder / "assets", pictures[index].name, plan);
            ASSERT_TRUE(copied.has_value()) << copied.error().c_str();
            written[index] = *copied;
            EXPECT_EQ(copied->width, static_cast<uint32_t>(frame->outWidth)) << "an alternative is the same size as the pick";
            EXPECT_EQ(copied->height, static_cast<uint32_t>(frame->outHeight));
        }

        const auto king = ReadArtGenPng(written[0].pngFile);
        const auto pawn = ReadArtGenPng(written[2].pngFile);
        ASSERT_TRUE(king.has_value());
        ASSERT_TRUE(pawn.has_value());
        const Optional<ArtGenIntRect> kingBox = DrawnBounds(*king);
        const Optional<ArtGenIntRect> pawnBox = DrawnBounds(*pawn);
        ASSERT_TRUE(kingBox.has_value());
        ASSERT_TRUE(pawnBox.has_value());
        const double ratio = static_cast<double>(kingBox->h) / pawnBox->h;
        EXPECT_NEAR(ratio, 3.0, 0.15) << "the king is three times the pawn in the sources, and in the files";
    }

    TEST_F(ArtGenAcceptTest, MeasuringSkipsAnEmptyPictureAndRefusesWhatCannotShareAFrame) {
        const auto drawn = folder / "out" / "drawn.png";
        const auto empty = folder / "out" / "empty.png";
        const auto other = folder / "out" / "other.png";
        ASSERT_NO_FATAL_FAILURE(WriteRealPng(drawn, 128, 128, {32, 32, 64, 64}));
        ASSERT_NO_FATAL_FAILURE(WriteRealPng(empty, 128, 128, {0, 0, 0, 0}));
        ASSERT_NO_FATAL_FAILURE(WriteRealPng(other, 64, 64, {0, 0, 32, 32}));
        const auto text = [](const Path& path) { return String(path.View()); };
        const ArtGenImageSettings noPadding{true, 0, 0};

        const auto skipped = MeasureFrameForControl(Vector<String>{text(empty), text(drawn)}, noPadding);
        ASSERT_TRUE(skipped.has_value()) << skipped.error().c_str();
        EXPECT_EQ(skipped->Crop(), (ArtGenIntRect{32, 32, 64, 64}));

        const auto none = MeasureFrameForControl(Vector<String>{text(empty)}, noPadding);
        ASSERT_FALSE(none.has_value());
        EXPECT_NE(none.error().find("nothing drawn"), String::npos);

        const auto mixed = MeasureFrameForControl(Vector<String>{text(drawn), text(other)}, noPadding);
        ASSERT_FALSE(mixed.has_value());
        EXPECT_NE(mixed.error().find("different sizes"), String::npos) << mixed.error().c_str();

        EXPECT_FALSE(MeasureFrameForControl(Vector<String>{text(folder / "out" / "gone.png")}, noPadding).has_value());
    }

    TEST(ArtGenDragTest, ThePayloadIsPlainBytesThatKeepWhatItWasMadeWith) {
        // ImGui copies the payload as bytes and hands the copy to the target
        static_assert(std::is_trivially_copyable_v<ArtGenDragPayload>);
        const ArtGenDragPayload payload = MakeArtGenDragPayload(ArtGenDragKind::Audio, -7, "pieces", "rook", "white_rook");

        EXPECT_EQ(payload.kind, ArtGenDragKind::Audio);
        EXPECT_EQ(payload.seed, -7);
        EXPECT_EQ(StringView(payload.set), "pieces");
        EXPECT_EQ(StringView(payload.job), "rook");
        EXPECT_EQ(StringView(payload.suggestedName), "white_rook");
    }

    TEST(ArtGenDragTest, ATextLongerThanItsBufferIsCutAndStaysTerminated) {
        const String longName(100, 'a');
        const ArtGenDragPayload payload = MakeArtGenDragPayload(ArtGenDragKind::Image, 1, longName, "", longName);
        EXPECT_EQ(StringView(payload.set).size(), kMaxSpriteNameLength);
        EXPECT_EQ(StringView(payload.suggestedName).size(), kMaxSpriteNameLength);
        EXPECT_TRUE(StringView(payload.job).empty());
    }

    TEST(ArtGenDragTest, TheSuggestedNameIsTheJobsOrTheSubjects) {
        EXPECT_EQ(SuggestedArtGenSpriteName("white_rook", "a white rook"), "white_rook") << "a set row keeps its job's name";
        EXPECT_EQ(SuggestedArtGenSpriteName("", "A White Knight!"), "a_white_knight");
        EXPECT_EQ(SuggestedArtGenSpriteName("", "!!!"), "sprite") << "a subject with no letter still gives a valid name";
        EXPECT_TRUE(IsValidSpriteName(StringView(SuggestedArtGenSpriteName("", String(300, 'x')))));
    }

    class ArtGenDropPlanTest : public ::testing::Test {
    protected:
        void SetUp() override {
            library.AddOrUpdate(LibraryEntry("queen", "assets/pieces/queen.png", Rect(0, 0, 64, 64)));
        }

        static ArtGenDragPayload Image(StringView suggested) {
            return MakeArtGenDragPayload(ArtGenDragKind::Image, 5, "", "", suggested);
        }

        TextureLibrary library;
    };

    TEST_F(ArtGenDropPlanTest, ADropForANewSpriteTakesTheSuggestedNameAndATakenOneGetsASuffix) {
        const ArtGenDropPlan fresh = PlanArtGenDrop(Image("king"), "", library);
        EXPECT_EQ(fresh.mode, ArtGenDropMode::New);
        EXPECT_EQ(fresh.sprite, "king");
        EXPECT_TRUE(fresh.reason.empty());

        library.AddOrUpdate(LibraryEntry("queen_2", "assets/pieces/queen2.png", Rect(0, 0, 64, 64)));
        const ArtGenDropPlan taken = PlanArtGenDrop(Image("queen"), "", library);
        EXPECT_EQ(taken.mode, ArtGenDropMode::New);
        EXPECT_EQ(taken.sprite, "queen_3");
    }

    TEST_F(ArtGenDropPlanTest, ATakenNameOfTheLongestLengthStaysAValidName) {
        const String longest(kMaxSpriteNameLength, 'q');
        library.AddOrUpdate(LibraryEntry(longest.c_str(), "assets/pieces/q.png", Rect(0, 0, 64, 64)));
        const ArtGenDropPlan plan = PlanArtGenDrop(Image(longest), "", library);
        ASSERT_EQ(plan.mode, ArtGenDropMode::New);
        EXPECT_TRUE(IsValidSpriteName(StringView(plan.sprite))) << plan.sprite.c_str();
        EXPECT_EQ(library.GetSprite(PoolString::Intern(StringView(plan.sprite))), nullptr) << "the name is free";
    }

    TEST_F(ArtGenDropPlanTest, ADropOntoASpriteReplacesItUnderItsOwnName) {
        const ArtGenDropPlan plan = PlanArtGenDrop(Image("king"), "queen", library);
        EXPECT_EQ(plan.mode, ArtGenDropMode::Replace);
        EXPECT_EQ(plan.sprite, "queen") << "the suggested name does not matter for a replace";
    }

    TEST_F(ArtGenDropPlanTest, ANameAcceptCannotWriteIsRefusedWithTheRule) {
        const ArtGenDropPlan onto = PlanArtGenDrop(Image("king"), "ui/button", library);
        EXPECT_EQ(onto.mode, ArtGenDropMode::Refused);
        EXPECT_NE(onto.reason.find("ui/button"), String::npos) << onto.reason.c_str();
        EXPECT_NE(onto.reason.find("A-Z a-z 0-9 _ -"), String::npos) << onto.reason.c_str();
        EXPECT_TRUE(onto.sprite.empty());

        EXPECT_EQ(PlanArtGenDrop(Image("two words"), "", library).mode, ArtGenDropMode::Refused);
        EXPECT_EQ(PlanArtGenDrop(Image(""), "", library).mode, ArtGenDropMode::Refused);
    }

    TEST_F(ArtGenDropPlanTest, ASoundIsNotASpriteWhereverItIsDropped) {
        const ArtGenDragPayload sound = MakeArtGenDragPayload(ArtGenDragKind::Audio, 5, "", "", "knock");
        for (const StringView target : {StringView(""), StringView("queen")}) {
            const ArtGenDropPlan plan = PlanArtGenDrop(sound, target, library);
            EXPECT_EQ(plan.mode, ArtGenDropMode::Refused);
            EXPECT_EQ(plan.reason, "a sound is not a sprite");
        }
    }

    TEST(ArtGenDragTest, ADropNobodyAnsweredSaysSoAndIsNotOk) {
        ArtGenDropResult result;
        ArtGenVariantDropEvent::Emit(MakeArtGenDragPayload(ArtGenDragKind::Image, 1, "", "", "x"), StringView("x"), &result);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.message.find("not available"), String::npos) << result.message.c_str();
    }

}  // namespace BECore::Tests
