#include <BECore/Resource/TextureLibrary.h>
#include <Common/Printers.h>
#include <Widgets/TextureEditor/SpriteLibraryView.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTextureSize = 100.0f;
        constexpr float kTolerance = 0.0001f;

        SpriteEntry MakeEntry(const char* name, const char* path, Rect rect = Rect(0.0f, 0.0f, 10.0f, 10.0f)) {
            SpriteEntry entry;
            entry.name = PoolString::Intern(name);
            entry.texturePath = PoolString::Intern(path);
            entry.srcRect = rect;
            return entry;
        }

        SpriteEntry Grouped(const char* name, const char* path, const char* group) {
            SpriteEntry entry = MakeEntry(name, path);
            SpriteAtlasLink link;
            link.name = PoolString::Intern(group);
            entry.atlas = link;
            return entry;
        }

        SpriteEntry Packed(const char* name, const char* group, const char* sourcePath) {
            SpriteEntry entry = Grouped(name, "assets/atlases/pack.png", group);
            SpriteSource source;
            source.texturePath = PoolString::Intern(sourcePath);
            source.rect = Rect(1.0f, 2.0f, 3.0f, 4.0f);
            entry.atlas->source = source;
            return entry;
        }

        /// No group, a group "pieces" of two (one packed), another group "ui"
        void FillMixed(TextureLibrary& library) {
            library.AddOrUpdate(MakeEntry("Plain", "assets/plain.png"));
            library.AddOrUpdate(Grouped("Pawn", "assets/sheet.png", "pieces"));
            library.AddOrUpdate(Packed("King", "pieces", "assets/sheet.png"));
            library.AddOrUpdate(Grouped("Button", "assets/ui.png", "ui"));
        }

        PoolString Name(const char* text) {
            return PoolString::Intern(text);
        }

        AtlasFilter GroupFilter(const char* group) {
            AtlasFilter filter;
            filter.kind = AtlasFilterKind::Group;
            filter.group = Name(group);
            return filter;
        }

        AtlasFilter NoAtlasFilter() {
            AtlasFilter filter;
            filter.kind = AtlasFilterKind::NoAtlas;
            return filter;
        }

    }  // namespace

    TEST(SpriteLibraryViewTest, StateIsSingleGroupedOrPacked) {
        EXPECT_EQ(AtlasStateOf(MakeEntry("a", "t.png")), SpriteAtlasState::Single);
        EXPECT_EQ(AtlasStateOf(Grouped("a", "t.png", "g")), SpriteAtlasState::Grouped);
        EXPECT_EQ(AtlasStateOf(Packed("a", "g", "s.png")), SpriteAtlasState::Packed);
    }

    TEST(SpriteLibraryViewTest, LinkWithoutNameIsSingleUnlessItHasASource) {
        SpriteEntry noName = MakeEntry("a", "t.png");
        noName.atlas = SpriteAtlasLink{};
        EXPECT_EQ(AtlasStateOf(noName), SpriteAtlasState::Single);

        // A hand-made <atlas> with a source and no name still is a packed sprite
        SpriteEntry packedNoName = noName;
        SpriteSource source;
        source.texturePath = Name("s.png");
        packedNoName.atlas->source = source;
        EXPECT_EQ(AtlasStateOf(packedNoName), SpriteAtlasState::Packed);
    }

    TEST(SpriteLibraryViewTest, GroupsAreSortedCountedAndNonEmpty) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("Plain", "p.png"));
        library.AddOrUpdate(Grouped("B1", "s.png", "zeta"));
        library.AddOrUpdate(Packed("A1", "alpha", "s.png"));
        library.AddOrUpdate(Packed("A2", "alpha", "s.png"));
        library.AddOrUpdate(Grouped("A3", "s.png", "alpha"));
        SpriteEntry noName = MakeEntry("Empty", "p.png");
        noName.atlas = SpriteAtlasLink{};
        library.AddOrUpdate(noName);

        const Vector<AtlasGroupInfo> groups = ListAtlasGroups(library);
        ASSERT_EQ(groups.size(), 2u);
        EXPECT_EQ(groups[0].name, Name("alpha"));
        EXPECT_EQ(groups[0].members, 3u);
        EXPECT_EQ(groups[0].packed, 2u);
        EXPECT_EQ(groups[1].name, Name("zeta"));
        EXPECT_EQ(groups[1].members, 1u);
        EXPECT_EQ(groups[1].packed, 0u);
    }

    TEST(SpriteLibraryViewTest, NoGroupsInALibraryWithoutAtlases) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("a", "t.png"));
        EXPECT_TRUE(ListAtlasGroups(library).empty());
    }

    TEST(SpriteLibraryViewTest, FilterKindsMatchTheGroupName) {
        TextureLibrary library;
        FillMixed(library);
        const SpriteEntry& plain = library.GetAll()[0];
        const SpriteEntry& pawn = library.GetAll()[1];
        const SpriteEntry& king = library.GetAll()[2];
        const SpriteEntry& button = library.GetAll()[3];

        const AtlasFilter all;
        for (const SpriteEntry& entry : library.GetAll()) {
            EXPECT_TRUE(MatchesFilter(entry, all));
        }

        EXPECT_TRUE(MatchesFilter(plain, NoAtlasFilter()));
        EXPECT_FALSE(MatchesFilter(pawn, NoAtlasFilter()));
        EXPECT_FALSE(MatchesFilter(king, NoAtlasFilter()));
        EXPECT_FALSE(MatchesFilter(button, NoAtlasFilter()));

        const AtlasFilter pieces = GroupFilter("pieces");
        EXPECT_FALSE(MatchesFilter(plain, pieces));
        EXPECT_TRUE(MatchesFilter(pawn, pieces));
        EXPECT_TRUE(MatchesFilter(king, pieces));
        EXPECT_FALSE(MatchesFilter(button, pieces));
    }

    TEST(SpriteLibraryViewTest, NoAtlasIgnoresTheStateOfAnEntryWithAnEmptyGroupName) {
        SpriteEntry packedNoName = MakeEntry("a", "t.png");
        packedNoName.atlas = SpriteAtlasLink{};
        SpriteSource source;
        source.texturePath = Name("s.png");
        packedNoName.atlas->source = source;
        EXPECT_TRUE(MatchesFilter(packedNoName, NoAtlasFilter()));
    }

    TEST(SpriteLibraryViewTest, FilterSpritesGivesLibraryIndicesInOrder) {
        TextureLibrary library;
        FillMixed(library);

        EXPECT_EQ(FilterSprites(library, AtlasFilter{}), (Vector<uint32_t>{0, 1, 2, 3}));
        EXPECT_EQ(FilterSprites(library, NoAtlasFilter()), (Vector<uint32_t>{0}));
        EXPECT_EQ(FilterSprites(library, GroupFilter("pieces")), (Vector<uint32_t>{1, 2}));
        EXPECT_EQ(FilterSprites(library, GroupFilter("ui")), (Vector<uint32_t>{3}));
        EXPECT_TRUE(FilterSprites(library, GroupFilter("gone")).empty());
    }

    TEST(SpriteLibraryViewTest, FilterOverALibraryWithNoGroupKeepsEverythingUnderNoAtlas) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("a", "t.png"));
        library.AddOrUpdate(MakeEntry("b", "t.png"));
        EXPECT_EQ(FilterSprites(library, NoAtlasFilter()), (Vector<uint32_t>{0, 1}));
    }

    TEST(SpriteLibraryViewTest, NormalizeFilterDropsAGoneGroup) {
        TextureLibrary library;
        FillMixed(library);
        const Vector<AtlasGroupInfo> groups = ListAtlasGroups(library);

        EXPECT_EQ(NormalizeFilter(groups, GroupFilter("ui")), GroupFilter("ui"));
        EXPECT_EQ(NormalizeFilter(groups, GroupFilter("gone")), AtlasFilter{});
        EXPECT_EQ(NormalizeFilter(groups, NoAtlasFilter()), NoAtlasFilter());
        EXPECT_EQ(NormalizeFilter(groups, AtlasFilter{}), AtlasFilter{});
        EXPECT_EQ(NormalizeFilter({}, GroupFilter("ui")), AtlasFilter{});
    }

    TEST(SpriteLibraryViewTest, ListKeyIsStableAndPinned) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("a", "t.png"));
        library.AddOrUpdate(Packed("b", "grp", "s.png"));
        // Pinned: the fixed hash must not drift with a compiler or a standard library
        constexpr uint64_t kExpectedKey = 0xbc0c150cc11d13b6ULL;
        EXPECT_EQ(LibraryListKey(library), LibraryListKey(library));
        EXPECT_EQ(LibraryListKey(library), kExpectedKey);
    }

    TEST(SpriteLibraryViewTest, ListKeyMovesWithWhatTheListShows) {
        TextureLibrary base;
        base.AddOrUpdate(MakeEntry("a", "t.png"));
        base.AddOrUpdate(Grouped("b", "s.png", "grp"));
        const uint64_t baseKey = LibraryListKey(base);

        {
            TextureLibrary other;
            other.AddOrUpdate(MakeEntry("a2", "t.png"));
            other.AddOrUpdate(Grouped("b", "s.png", "grp"));
            EXPECT_NE(LibraryListKey(other), baseKey) << "name";
        }
        {
            TextureLibrary other;
            other.AddOrUpdate(MakeEntry("a", "t2.png"));
            other.AddOrUpdate(Grouped("b", "s.png", "grp"));
            EXPECT_NE(LibraryListKey(other), baseKey) << "texture path";
        }
        {
            TextureLibrary other;
            other.AddOrUpdate(MakeEntry("a", "t.png"));
            other.AddOrUpdate(Grouped("b", "s.png", "grp2"));
            EXPECT_NE(LibraryListKey(other), baseKey) << "group name";
        }
        {
            TextureLibrary other;
            other.AddOrUpdate(MakeEntry("a", "t.png"));
            SpriteEntry packed = Grouped("b", "s.png", "grp");
            packed.atlas->source = SpriteSource{};
            other.AddOrUpdate(packed);
            EXPECT_NE(LibraryListKey(other), baseKey) << "packed flag";
        }
        {
            TextureLibrary other;
            other.AddOrUpdate(Grouped("b", "s.png", "grp"));
            other.AddOrUpdate(MakeEntry("a", "t.png"));
            EXPECT_NE(LibraryListKey(other), baseKey) << "order";
        }
    }

    TEST(SpriteLibraryViewTest, ListKeyIgnoresRects) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("a", "t.png", Rect(0.0f, 0.0f, 10.0f, 10.0f)));
        const uint64_t before = LibraryListKey(library);
        library.AddOrUpdate(MakeEntry("a", "t.png", Rect(5.0f, 6.0f, 7.0f, 8.0f)));
        EXPECT_EQ(LibraryListKey(library), before);
    }

    TEST(SpriteLibraryViewTest, ListKeySeparatesNeighbouringStrings) {
        TextureLibrary one;
        one.AddOrUpdate(MakeEntry("ab", "c"));
        TextureLibrary two;
        two.AddOrUpdate(MakeEntry("a", "bc"));
        EXPECT_NE(LibraryListKey(one), LibraryListKey(two));
    }

    TEST(SpriteLibraryViewTest, TextureFileNameIsTheLastPathPart) {
        EXPECT_EQ(TextureFileName(Name("assets/atlases/pack.png")), "pack.png");
        EXPECT_EQ(TextureFileName(Name("pack.png")), "pack.png");
        EXPECT_EQ(TextureFileName(Name("assets/")), "");
        EXPECT_EQ(TextureFileName(PoolString()), "");
    }

    TEST(SpriteLibraryViewTest, ThumbnailUvIsTheRectOverTheTexture) {
        const ThumbnailUv uv = ComputeThumbnailUv(Rect(10.0f, 20.0f, 30.0f, 40.0f), kTextureSize, kTextureSize);
        EXPECT_NEAR(uv.u0, 0.1f, kTolerance);
        EXPECT_NEAR(uv.v0, 0.2f, kTolerance);
        EXPECT_NEAR(uv.u1, 0.4f, kTolerance);
        EXPECT_NEAR(uv.v1, 0.6f, kTolerance);
    }

    TEST(SpriteLibraryViewTest, EmptyRectIsTheWholeTexture) {
        const ThumbnailUv uv = ComputeThumbnailUv(Rect(), kTextureSize, kTextureSize);
        EXPECT_EQ(uv.u0, 0.0f);
        EXPECT_EQ(uv.v0, 0.0f);
        EXPECT_EQ(uv.u1, 1.0f);
        EXPECT_EQ(uv.v1, 1.0f);
    }

    TEST(SpriteLibraryViewTest, RectPastTheTextureIsClamped) {
        const ThumbnailUv uv = ComputeThumbnailUv(Rect(80.0f, 90.0f, 50.0f, 50.0f), kTextureSize, kTextureSize);
        EXPECT_NEAR(uv.u0, 0.8f, kTolerance);
        EXPECT_NEAR(uv.v0, 0.9f, kTolerance);
        EXPECT_EQ(uv.u1, 1.0f);
        EXPECT_EQ(uv.v1, 1.0f);

        const ThumbnailUv before = ComputeThumbnailUv(Rect(-10.0f, -20.0f, 30.0f, 30.0f), kTextureSize, kTextureSize);
        EXPECT_EQ(before.u0, 0.0f);
        EXPECT_EQ(before.v0, 0.0f);
    }

    TEST(SpriteLibraryViewTest, ZeroTextureSizeIsTheWholeTexture) {
        const ThumbnailUv uv = ComputeThumbnailUv(Rect(10.0f, 10.0f, 5.0f, 5.0f), 0.0f, kTextureSize);
        EXPECT_EQ(uv.u0, 0.0f);
        EXPECT_EQ(uv.u1, 1.0f);
        EXPECT_EQ(uv.v1, 1.0f);
    }

    TEST(SpriteLibraryViewTest, FitInsideKeepsTheAspect) {
        const FitSize wide = FitInside(200.0f, 100.0f, 50.0f, 50.0f, false);
        EXPECT_NEAR(wide.w, 50.0f, kTolerance);
        EXPECT_NEAR(wide.h, 25.0f, kTolerance);

        const FitSize tall = FitInside(100.0f, 200.0f, 50.0f, 50.0f, false);
        EXPECT_NEAR(tall.w, 25.0f, kTolerance);
        EXPECT_NEAR(tall.h, 50.0f, kTolerance);
    }

    TEST(SpriteLibraryViewTest, FitInsideUpscalesOnlyWhenAsked) {
        const FitSize kept = FitInside(10.0f, 20.0f, 100.0f, 100.0f, false);
        EXPECT_NEAR(kept.w, 10.0f, kTolerance);
        EXPECT_NEAR(kept.h, 20.0f, kTolerance);

        const FitSize grown = FitInside(10.0f, 20.0f, 100.0f, 100.0f, true);
        EXPECT_NEAR(grown.w, 50.0f, kTolerance);
        EXPECT_NEAR(grown.h, 100.0f, kTolerance);
    }

    TEST(SpriteLibraryViewTest, FitInsideOfNothingIsNothing) {
        for (const FitSize size :
             {FitInside(0.0f, 10.0f, 50.0f, 50.0f, true), FitInside(10.0f, -1.0f, 50.0f, 50.0f, true), FitInside(10.0f, 10.0f, 0.0f, 50.0f, true), FitInside(10.0f, 10.0f, 50.0f, -5.0f, false)}) {
            EXPECT_EQ(size.w, 0.0f);
            EXPECT_EQ(size.h, 0.0f);
        }
    }

}  // namespace BECore::Tests
