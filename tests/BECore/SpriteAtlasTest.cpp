#include <BECore/Resource/TextureLibrary.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr uint8_t kOpaque = 255;
        constexpr uint32_t kChessSheetWidth = 700;
        constexpr uint32_t kChessSheetHeight = 381;
        constexpr uint32_t kByteMask = 0xFF;

        /// One colour for the whole image: a crop of it is told by its colour
        RgbaImage MakeSolid(uint32_t width, uint32_t height, uint8_t red, uint8_t green, uint8_t blue) {
            RgbaImage image;
            image.width = width;
            image.height = height;
            image.pixels.resize(static_cast<size_t>(width) * height * kBytesPerPixel);
            for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
                image.pixels[i * kBytesPerPixel + 0] = red;
                image.pixels[i * kBytesPerPixel + 1] = green;
                image.pixels[i * kBytesPerPixel + 2] = blue;
                image.pixels[i * kBytesPerPixel + 3] = kOpaque;
            }
            return image;
        }

        /// Every pixel tells its place, so a crop that took in a neighbour or is off by one shows
        RgbaImage MakeGradient(uint32_t width, uint32_t height) {
            RgbaImage image;
            image.width = width;
            image.height = height;
            image.pixels.resize(static_cast<size_t>(width) * height * kBytesPerPixel);
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    uint8_t* pixel = image.pixels.data() + (static_cast<size_t>(y) * width + x) * kBytesPerPixel;
                    pixel[0] = static_cast<uint8_t>(x & kByteMask);
                    pixel[1] = static_cast<uint8_t>((x >> 8) | ((y >> 8) << 4));
                    pixel[2] = static_cast<uint8_t>(y & kByteMask);
                    pixel[3] = kOpaque;
                }
            }
            return image;
        }

        const uint8_t* PixelAt(const RgbaImage& image, uint32_t x, uint32_t y) {
            return image.pixels.data() + (static_cast<size_t>(y) * image.width + x) * kBytesPerPixel;
        }

        bool SamePixel(const RgbaImage& a, uint32_t ax, uint32_t ay, const RgbaImage& b, uint32_t bx, uint32_t by) {
            return std::equal(PixelAt(a, ax, ay), PixelAt(a, ax, ay) + kBytesPerPixel, PixelAt(b, bx, by));
        }

        bool IsTransparent(const RgbaImage& image, uint32_t x, uint32_t y) {
            const uint8_t* pixel = PixelAt(image, x, y);
            return pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 0;
        }

        /// True when the rect of @p atlas at @p placed holds the pixels of @p source from @p fromX, @p fromY
        bool CropMatches(const RgbaImage& atlas, const Rect& placed, const RgbaImage& source, uint32_t fromX, uint32_t fromY) {
            for (uint32_t y = 0; y < static_cast<uint32_t>(placed.h); ++y) {
                for (uint32_t x = 0; x < static_cast<uint32_t>(placed.w); ++x) {
                    if (!SamePixel(atlas, static_cast<uint32_t>(placed.x) + x, static_cast<uint32_t>(placed.y) + y, source, fromX + x, fromY + y)) {
                        return false;
                    }
                }
            }
            return true;
        }

        /// True when no pixel of @p atlas outside the rects of @p rects is anything but transparent black
        bool EverythingElseIsTransparent(const RgbaImage& atlas, const Vector<Rect>& rects) {
            for (uint32_t y = 0; y < atlas.height; ++y) {
                for (uint32_t x = 0; x < atlas.width; ++x) {
                    bool inside = false;
                    for (const Rect& rect : rects) {
                        const float fx = static_cast<float>(x);
                        const float fy = static_cast<float>(y);
                        inside = inside || (fx >= rect.x && fx < rect.x + rect.w && fy >= rect.y && fy < rect.y + rect.h);
                    }
                    if (!inside && !IsTransparent(atlas, x, y)) {
                        return false;
                    }
                }
            }
            return true;
        }

        SpriteEntry MakeEntry(const char* name, const char* path, Rect rect, const char* group = nullptr) {
            SpriteEntry entry;
            entry.name = PoolString::Intern(name);
            entry.texturePath = PoolString::Intern(path);
            entry.srcRect = rect;
            if (group != nullptr) {
                SpriteAtlasLink link;
                link.name = PoolString::Intern(group);
                entry.atlas = link;
            }
            return entry;
        }

        bool SameEntry(const SpriteEntry& a, const SpriteEntry& b) {
            const bool sameRect = a.srcRect.x == b.srcRect.x && a.srcRect.y == b.srcRect.y && a.srcRect.w == b.srcRect.w && a.srcRect.h == b.srcRect.h;
            if (a.name != b.name || a.texturePath != b.texturePath || !sameRect || a.atlas.has_value() != b.atlas.has_value()) {
                return false;
            }
            if (!a.atlas.has_value()) {
                return true;
            }
            if (a.atlas->name != b.atlas->name || a.atlas->source.has_value() != b.atlas->source.has_value()) {
                return false;
            }
            if (!a.atlas->source.has_value()) {
                return true;
            }
            const Rect& ra = a.atlas->source->rect;
            const Rect& rb = b.atlas->source->rect;
            return a.atlas->source->texturePath == b.atlas->source->texturePath && ra.x == rb.x && ra.y == rb.y && ra.w == rb.w && ra.h == rb.h;
        }

        bool SameLibrary(const Vector<SpriteEntry>& before, const TextureLibrary& library) {
            if (before.size() != library.GetAll().size()) {
                return false;
            }
            for (size_t i = 0; i < before.size(); ++i) {
                if (!SameEntry(before[i], library.GetAll()[i])) {
                    return false;
                }
            }
            return true;
        }

        /// Images by virtual path, read through a loader that counts its calls
        class FakeSources {
        public:
            void Put(const char* path, RgbaImage image) {
                _images[PoolString::Intern(path)] = eastl::move(image);
            }

            std::expected<RgbaImage, String> Load(PoolString path) {
                ++calls;
                const auto found = _images.find(path);
                if (found == _images.end()) {
                    return std::unexpected(String("no such file"));
                }
                return found->second;
            }

            int calls = 0;

        private:
            UnorderedPoolMap<RgbaImage> _images;
        };

        Vector<uint8_t> ReadBytes(const std::filesystem::path& file) {
            return ReadFileBytes(file);
        }

        /// A folder of the test with `assets/` in it, removed with the object; virtual paths resolve into it
        class TempGame {
        public:
            TempGame() : _root(TestTempDirectory("SpriteAtlas")), _resolver{_root} {
                std::error_code ignored;
                std::filesystem::remove_all(_root, ignored);
                std::filesystem::create_directories(_root / "assets", ignored);
            }
            ~TempGame() {
                std::error_code ignored;
                std::filesystem::remove_all(_root, ignored);
            }
            TempGame(const TempGame&) = delete;
            TempGame& operator=(const TempGame&) = delete;

            [[nodiscard]] std::filesystem::path Resolve(StringView virtualPath) const {
                return _resolver(virtualPath);
            }

            /// The `resolve` of the result is a view of this object, which must outlive the call it is given to
            [[nodiscard]] AtlasIo Io() const {
                return AtlasIo{_root / "assets", _resolver};
            }

            void SavePng(const char* virtualPath, const RgbaImage& image) const {
                const std::filesystem::path file = Resolve(virtualPath);
                std::error_code ignored;
                std::filesystem::create_directories(file.parent_path(), ignored);
                ASSERT_TRUE(SaveRgbaPng(file, image).has_value());
            }

            [[nodiscard]] bool Exists(PoolString virtualPath) const {
                return std::filesystem::exists(Resolve(virtualPath.ToStringView()));
            }

        private:
            struct Resolver {
                std::filesystem::path root;

                std::filesystem::path operator()(StringView virtualPath) const {
                    return root / ToStd(virtualPath);
                }
            };

            std::filesystem::path _root;
            Resolver _resolver;
        };

        /// A group "ui" of three sprites of three colours on three files, plus one sprite that is in no group
        void FillLibrary(TextureLibrary& library) {
            library.AddOrUpdate(MakeEntry("Charlie", "assets/c.png", Rect(0, 0, 1, 4), "ui"));
            library.AddOrUpdate(MakeEntry("Loner", "assets/loner.png", Rect(0, 0, 5, 5)));
            library.AddOrUpdate(MakeEntry("Alpha", "assets/a.png", Rect(0, 0, 2, 2), "ui"));
            library.AddOrUpdate(MakeEntry("Bravo", "assets/b.png", Rect(0, 0, 3, 1), "ui"));
        }

        void SaveSources(const TempGame& game) {
            game.SavePng("assets/a.png", MakeSolid(2, 2, 200, 0, 0));
            game.SavePng("assets/b.png", MakeSolid(3, 1, 0, 200, 0));
            game.SavePng("assets/c.png", MakeSolid(1, 4, 0, 0, 200));
        }

        void LoadSources(FakeSources& sources) {
            sources.Put("assets/a.png", MakeSolid(2, 2, 200, 0, 0));
            sources.Put("assets/b.png", MakeSolid(3, 1, 0, 200, 0));
            sources.Put("assets/c.png", MakeSolid(1, 4, 0, 0, 200));
        }

        std::expected<AtlasWritten, String> Pack(TextureLibrary& library, const TempGame& game, const PackSettings& settings = PackSettings{}) {
            return WriteAtlas(library, "ui", settings, game.Io());
        }

    }  // namespace

    TEST(SpriteAtlas, TheAtlasFileIsUnderAssetsAtlases) {
        EXPECT_EQ(AtlasTexturePath("pieces").ToStringView(), "assets/atlases/pieces.png");
    }

    TEST(SpriteAtlas, SetChangeAndClearAGroup) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("Sprite", "assets/a.png", Rect(0, 0, 2, 2)));
        const PoolString sprite = "Sprite"_intern;

        auto set = SetSpriteAtlas(library, sprite, "ui");
        ASSERT_TRUE(set.has_value());
        EXPECT_TRUE(*set);
        ASSERT_TRUE(library.GetSprite(sprite)->atlas.has_value());
        EXPECT_EQ(library.GetSprite(sprite)->atlas->name.ToStringView(), "ui");
        EXPECT_FALSE(library.GetSprite(sprite)->atlas->source.has_value());

        set = SetSpriteAtlas(library, sprite, "ui");
        ASSERT_TRUE(set.has_value());
        EXPECT_FALSE(*set);

        set = SetSpriteAtlas(library, sprite, "icons");
        ASSERT_TRUE(set.has_value());
        EXPECT_TRUE(*set);
        EXPECT_EQ(library.GetSprite(sprite)->atlas->name.ToStringView(), "icons");

        set = SetSpriteAtlas(library, sprite, "");
        ASSERT_TRUE(set.has_value());
        EXPECT_TRUE(*set);
        EXPECT_FALSE(library.GetSprite(sprite)->atlas.has_value());

        set = SetSpriteAtlas(library, sprite, "");
        ASSERT_TRUE(set.has_value());
        EXPECT_FALSE(*set);
    }

    TEST(SpriteAtlas, SetRefusesAPackedSpriteABadNameAndAnUnknownSprite) {
        TextureLibrary library;
        FillLibrary(library);
        FakeSources sources;
        LoadSources(sources);
        auto loader = [&sources](PoolString path) { return sources.Load(path); };
        auto built = BuildAtlas(library, "ui", PackSettings{}, loader);
        ASSERT_TRUE(built.has_value()) << built.error().c_str();
        for (SpriteEntry& entry : built->entries) {
            library.AddOrUpdate(entry);
        }
        const Vector<SpriteEntry> before = library.GetAll();

        const PoolString alpha = "Alpha"_intern;
        auto packed = SetSpriteAtlas(library, alpha, "other");
        ASSERT_FALSE(packed.has_value());
        EXPECT_NE(packed.error().find("unpack first"), String::npos) << packed.error().c_str();
        EXPECT_TRUE(SameLibrary(before, library));

        auto badName = SetSpriteAtlas(library, "Loner"_intern, "no/slash");
        ASSERT_FALSE(badName.has_value());
        EXPECT_FALSE(SetSpriteAtlas(library, "Loner"_intern, "has space").has_value());

        auto unknown = SetSpriteAtlas(library, "Nobody"_intern, "ui");
        ASSERT_FALSE(unknown.has_value());
        EXPECT_NE(unknown.error().find("Nobody"), String::npos) << unknown.error().c_str();
        EXPECT_TRUE(SameLibrary(before, library));
    }

    TEST(SpriteAtlas, MembersAreSortedByNameAndLeaveOtherGroupsOut) {
        TextureLibrary library;
        FillLibrary(library);
        library.AddOrUpdate(MakeEntry("Elsewhere", "assets/e.png", Rect(0, 0, 1, 1), "icons"));

        const Vector<PoolString> members = AtlasMembers(library, "ui");
        ASSERT_EQ(members.size(), 3u);
        EXPECT_EQ(members[0].ToStringView(), "Alpha");
        EXPECT_EQ(members[1].ToStringView(), "Bravo");
        EXPECT_EQ(members[2].ToStringView(), "Charlie");
        EXPECT_TRUE(AtlasMembers(library, "").empty());
        EXPECT_TRUE(AtlasMembers(library, "none").empty());
    }

    TEST(SpriteAtlas, SummaryCountsMembersAndPackedOnes) {
        TextureLibrary library;
        FillLibrary(library);
        AtlasCounts counts = AtlasSummary(library, "ui");
        EXPECT_EQ(counts.members, 3u);
        EXPECT_EQ(counts.packed, 0u);

        SpriteEntry packed = *library.GetSprite("Alpha"_intern);
        SpriteSource source;
        source.texturePath = packed.texturePath;
        source.rect = packed.srcRect;
        packed.atlas->source = source;
        library.AddOrUpdate(packed);
        counts = AtlasSummary(library, "ui");
        EXPECT_EQ(counts.members, 3u);
        EXPECT_EQ(counts.packed, 1u);
        EXPECT_EQ(AtlasSummary(library, "none").members, 0u);
    }

    TEST(SpriteAtlas, BuildPlacesEverySpriteWithItsPixelsAndLeavesTheRestTransparent) {
        TextureLibrary library;
        FillLibrary(library);
        FakeSources sources;
        LoadSources(sources);
        auto loader = [&sources](PoolString path) { return sources.Load(path); };

        const PackSettings settings;
        const auto built = BuildAtlas(library, "ui", settings, loader);
        ASSERT_TRUE(built.has_value()) << built.error().c_str();

        const auto expected = PackSizes({PackSize{2, 2}, PackSize{3, 1}, PackSize{1, 4}}, settings);
        ASSERT_TRUE(expected.has_value());
        EXPECT_EQ(built->image.width, expected->width);
        EXPECT_EQ(built->image.height, expected->height);
        ASSERT_EQ(built->entries.size(), 3u);

        const char* names[] = {"Alpha", "Bravo", "Charlie"};
        const char* paths[] = {"assets/a.png", "assets/b.png", "assets/c.png"};
        const uint8_t colours[][3] = {{200, 0, 0}, {0, 200, 0}, {0, 0, 200}};
        const Rect originals[] = {Rect(0, 0, 2, 2), Rect(0, 0, 3, 1), Rect(0, 0, 1, 4)};
        Vector<Rect> placed;
        for (size_t i = 0; i < 3; ++i) {
            const SpriteEntry& entry = built->entries[i];
            EXPECT_EQ(entry.name.ToStringView(), names[i]);
            EXPECT_EQ(entry.texturePath.ToStringView(), "assets/atlases/ui.png");
            EXPECT_EQ(entry.srcRect.x, static_cast<float>(expected->placements[i].x));
            EXPECT_EQ(entry.srcRect.y, static_cast<float>(expected->placements[i].y));
            EXPECT_EQ(entry.srcRect.w, originals[i].w);
            EXPECT_EQ(entry.srcRect.h, originals[i].h);
            ASSERT_TRUE(entry.atlas.has_value());
            EXPECT_EQ(entry.atlas->name.ToStringView(), "ui");
            ASSERT_TRUE(entry.atlas->source.has_value());
            EXPECT_EQ(entry.atlas->source->texturePath.ToStringView(), paths[i]);
            EXPECT_EQ(entry.atlas->source->rect.w, originals[i].w);

            const RgbaImage solid = MakeSolid(static_cast<uint32_t>(originals[i].w), static_cast<uint32_t>(originals[i].h), colours[i][0], colours[i][1], colours[i][2]);
            EXPECT_TRUE(CropMatches(built->image, entry.srcRect, solid, 0, 0)) << names[i];
            placed.push_back(entry.srcRect);
        }
        EXPECT_TRUE(EverythingElseIsTransparent(built->image, placed));
        // Nothing is changed by a build
        EXPECT_FALSE(library.GetSprite("Alpha"_intern)->atlas->source.has_value());
    }

    TEST(SpriteAtlas, AFractionalRectIsCutAtTheRoundedEdgesAndKeepsItsFloatRectInSource) {
        constexpr float kCell = 116.67f;
        constexpr float kRow = 190.5f;
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("WhiteKing", "assets/chess_test.png", Rect(0, 0, kCell, kRow), "pieces"));
        library.AddOrUpdate(MakeEntry("WhiteQueen", "assets/chess_test.png", Rect(kCell, 0, kCell, kRow), "pieces"));
        library.AddOrUpdate(MakeEntry("BlackKing", "assets/chess_test.png", Rect(0, kRow, kCell, kRow), "pieces"));
        const RgbaImage sheet = MakeGradient(kChessSheetWidth, kChessSheetHeight);
        FakeSources sources;
        sources.Put("assets/chess_test.png", sheet);
        auto loader = [&sources](PoolString path) { return sources.Load(path); };

        const auto built = BuildAtlas(library, "pieces", PackSettings{}, loader);
        ASSERT_TRUE(built.has_value()) << built.error().c_str();
        ASSERT_EQ(built->entries.size(), 3u);
        EXPECT_EQ(sources.calls, 1);

        // Members are by name: BlackKing, WhiteKing, WhiteQueen; the edges are 0, 117, 233 and 0, 191, 381
        const SpriteEntry& black = built->entries[0];
        const SpriteEntry& king = built->entries[1];
        const SpriteEntry& queen = built->entries[2];
        EXPECT_EQ(king.srcRect.w, 117.0f);
        EXPECT_EQ(king.srcRect.h, 191.0f);
        EXPECT_EQ(queen.srcRect.w, 116.0f);
        EXPECT_EQ(black.srcRect.w, 117.0f);
        EXPECT_EQ(black.srcRect.h, 190.0f);
        EXPECT_TRUE(CropMatches(built->image, king.srcRect, sheet, 0, 0));
        EXPECT_TRUE(CropMatches(built->image, queen.srcRect, sheet, 117, 0));
        EXPECT_TRUE(CropMatches(built->image, black.srcRect, sheet, 0, 191));

        ASSERT_TRUE(queen.atlas->source.has_value());
        EXPECT_EQ(queen.atlas->source->rect.x, kCell);
        EXPECT_EQ(queen.atlas->source->rect.w, kCell);
        EXPECT_EQ(black.atlas->source->rect.y, kRow);
    }

    TEST(SpriteAtlas, SpritesOfOneSheetReadItOnce) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("One", "assets/sheet.png", Rect(0, 0, 4, 4), "ui"));
        library.AddOrUpdate(MakeEntry("Two", "assets/sheet.png", Rect(4, 0, 4, 4), "ui"));
        library.AddOrUpdate(MakeEntry("Three", "assets/other.png", Rect(0, 0, 2, 2), "ui"));
        FakeSources sources;
        sources.Put("assets/sheet.png", MakeGradient(8, 4));
        sources.Put("assets/other.png", MakeGradient(2, 2));
        auto loader = [&sources](PoolString path) { return sources.Load(path); };

        ASSERT_TRUE(BuildAtlas(library, "ui", PackSettings{}, loader).has_value());
        EXPECT_EQ(sources.calls, 2);
    }

    TEST(SpriteAtlas, BuildErrorsNameTheSprite) {
        FakeSources sources;
        sources.Put("assets/small.png", MakeSolid(4, 4, 1, 2, 3));
        auto loader = [&sources](PoolString path) { return sources.Load(path); };
        const PackSettings settings;

        {
            TextureLibrary library;
            const auto built = BuildAtlas(library, "ui", settings, loader);
            ASSERT_FALSE(built.has_value());
            EXPECT_NE(built.error().find("ui"), String::npos);
        }
        {
            TextureLibrary library;
            library.AddOrUpdate(MakeEntry("Sprite", "assets/small.png", Rect(0, 0, 2, 2), "bad name"));
            EXPECT_FALSE(BuildAtlas(library, "bad name", settings, loader).has_value());
        }

        struct Case {
            const char* path;
            Rect rect;
            const char* reason;
        };
        const Case cases[] = {
            {"", Rect(0, 0, 2, 2), "no texture"},
            {"assets/small.png", Rect(0, 0, 0, 2), "empty rect"},
            {"assets/small.png", Rect(3, 3, 2, 2), "outside the image"},
            {"assets/small.png", Rect(-1, 0, 2, 2), "outside on the left"},
            {"assets/missing.png", Rect(0, 0, 2, 2), "the loader fails"},
        };
        for (const Case& test : cases) {
            TextureLibrary library;
            library.AddOrUpdate(MakeEntry("Fine", "assets/small.png", Rect(0, 0, 2, 2), "ui"));
            library.AddOrUpdate(MakeEntry("Culprit", test.path, test.rect, "ui"));
            const auto built = BuildAtlas(library, "ui", settings, loader);
            ASSERT_FALSE(built.has_value()) << test.reason;
            EXPECT_NE(built.error().find("Culprit"), String::npos) << test.reason << ": " << built.error().c_str();
        }
    }

    TEST(SpriteAtlas, ARectWithinTheToleranceOfTheEdgeIsAccepted) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("Edge", "assets/small.png", Rect(0, 0, 4.0005f, 4), "ui"));
        FakeSources sources;
        sources.Put("assets/small.png", MakeSolid(4, 4, 1, 2, 3));
        auto loader = [&sources](PoolString path) { return sources.Load(path); };
        const auto built = BuildAtlas(library, "ui", PackSettings{}, loader);
        ASSERT_TRUE(built.has_value()) << built.error().c_str();
        EXPECT_EQ(built->entries[0].srcRect.w, 4.0f);
    }

    TEST(SpriteAtlas, WriteMakesTheFileAndRewritesTheEntriesInPlace) {
        TempGame game;
        SaveSources(game);
        TextureLibrary library;
        FillLibrary(library);

        const auto written = Pack(library, game);
        ASSERT_TRUE(written.has_value()) << written.error().c_str();
        EXPECT_EQ(written->texturePath.ToStringView(), "assets/atlases/ui.png");
        ASSERT_EQ(written->changed.size(), 3u);
        EXPECT_EQ(written->changed[0].ToStringView(), "Alpha");

        const auto atlas = LoadRgbaImage(game.Resolve("assets/atlases/ui.png"));
        ASSERT_TRUE(atlas.has_value()) << atlas.error().c_str();
        EXPECT_EQ(atlas->width, written->width);
        EXPECT_EQ(atlas->height, written->height);

        // The library keeps its order and the loner is untouched
        ASSERT_EQ(library.GetAll().size(), 4u);
        EXPECT_EQ(library.GetAll()[0].name.ToStringView(), "Charlie");
        EXPECT_EQ(library.GetAll()[1].name.ToStringView(), "Loner");
        EXPECT_EQ(library.GetAll()[1].texturePath.ToStringView(), "assets/loner.png");
        EXPECT_FALSE(library.GetAll()[1].atlas.has_value());

        const uint8_t colours[][3] = {{200, 0, 0}, {0, 200, 0}, {0, 0, 200}};
        const char* names[] = {"Alpha", "Bravo", "Charlie"};
        Vector<Rect> placed;
        for (size_t i = 0; i < 3; ++i) {
            const SpriteEntry* entry = library.GetSprite(PoolString::Intern(names[i]));
            ASSERT_NE(entry, nullptr);
            EXPECT_EQ(entry->texturePath.ToStringView(), "assets/atlases/ui.png");
            ASSERT_TRUE(entry->atlas.has_value());
            EXPECT_TRUE(entry->atlas->source.has_value());
            const RgbaImage solid = MakeSolid(static_cast<uint32_t>(entry->srcRect.w), static_cast<uint32_t>(entry->srcRect.h), colours[i][0], colours[i][1], colours[i][2]);
            EXPECT_TRUE(CropMatches(*atlas, entry->srcRect, solid, 0, 0)) << names[i];
            placed.push_back(entry->srcRect);
        }
        EXPECT_TRUE(EverythingElseIsTransparent(*atlas, placed));
    }

    TEST(SpriteAtlas, TheSameSpritesGiveTheSameFileWhateverTheLibraryOrder) {
        TempGame game;
        SaveSources(game);
        TextureLibrary first;
        FillLibrary(first);
        ASSERT_TRUE(Pack(first, game).has_value());
        const Vector<uint8_t> bytes = ReadBytes(game.Resolve("assets/atlases/ui.png"));
        ASSERT_FALSE(bytes.empty());

        TextureLibrary reversed;
        reversed.AddOrUpdate(MakeEntry("Bravo", "assets/b.png", Rect(0, 0, 3, 1), "ui"));
        reversed.AddOrUpdate(MakeEntry("Alpha", "assets/a.png", Rect(0, 0, 2, 2), "ui"));
        reversed.AddOrUpdate(MakeEntry("Charlie", "assets/c.png", Rect(0, 0, 1, 4), "ui"));
        ASSERT_TRUE(Pack(reversed, game).has_value());
        EXPECT_TRUE(ReadBytes(game.Resolve("assets/atlases/ui.png")) == bytes);
    }

    TEST(SpriteAtlas, ARepackReadsTheSourcesAndNotTheOldAtlas) {
        TempGame game;
        SaveSources(game);
        TextureLibrary library;
        FillLibrary(library);
        ASSERT_TRUE(Pack(library, game).has_value());

        game.SavePng("assets/a.png", MakeSolid(2, 2, 10, 20, 30));
        const auto repacked = Pack(library, game);
        ASSERT_TRUE(repacked.has_value()) << repacked.error().c_str();

        const auto atlas = LoadRgbaImage(game.Resolve("assets/atlases/ui.png"));
        ASSERT_TRUE(atlas.has_value());
        const SpriteEntry* alpha = library.GetSprite("Alpha"_intern);
        const SpriteEntry* bravo = library.GetSprite("Bravo"_intern);
        EXPECT_TRUE(CropMatches(*atlas, alpha->srcRect, MakeSolid(2, 2, 10, 20, 30), 0, 0));
        EXPECT_TRUE(CropMatches(*atlas, bravo->srcRect, MakeSolid(3, 1, 0, 200, 0), 0, 0));
        EXPECT_EQ(alpha->atlas->source->texturePath.ToStringView(), "assets/a.png");
    }

    TEST(SpriteAtlas, AFailingPackLeavesTheLibraryAndTheOldFileAsTheyWere) {
        TempGame game;
        SaveSources(game);
        TextureLibrary library;
        FillLibrary(library);
        ASSERT_TRUE(Pack(library, game).has_value());
        const Vector<uint8_t> bytes = ReadBytes(game.Resolve("assets/atlases/ui.png"));

        library.AddOrUpdate(MakeEntry("Delta", "assets/gone.png", Rect(0, 0, 2, 2), "ui"));
        const Vector<SpriteEntry> before = library.GetAll();
        const auto failed = Pack(library, game);
        ASSERT_FALSE(failed.has_value());
        EXPECT_NE(failed.error().find("Delta"), String::npos) << failed.error().c_str();
        EXPECT_TRUE(SameLibrary(before, library));
        EXPECT_TRUE(ReadBytes(game.Resolve("assets/atlases/ui.png")) == bytes);
    }

    TEST(SpriteAtlas, UnpackIsTheExactInverseOfPack) {
        TempGame game;
        SaveSources(game);
        game.SavePng("assets/loner.png", MakeSolid(5, 5, 9, 9, 9));
        TextureLibrary library;
        FillLibrary(library);
        // A fractional original rect must come back exactly
        library.AddOrUpdate(MakeEntry("Bravo", "assets/b.png", Rect(0.25f, 0, 2.5f, 1), "ui"));
        const Vector<SpriteEntry> original = library.GetAll();

        ASSERT_TRUE(Pack(library, game).has_value());
        auto exists = [&game](PoolString path) { return game.Exists(path); };
        const auto unpacked = UnpackAtlas(library, "ui", exists);
        ASSERT_TRUE(unpacked.has_value()) << unpacked.error().c_str();
        ASSERT_EQ(unpacked->changed.size(), 3u);
        ASSERT_EQ(unpacked->oldTextures.size(), 1u);
        EXPECT_EQ(unpacked->oldTextures[0].ToStringView(), "assets/atlases/ui.png");

        EXPECT_TRUE(SameLibrary(original, library));
        for (const SpriteEntry& entry : library.GetAll()) {
            if (entry.atlas.has_value()) {
                EXPECT_EQ(entry.atlas->name.ToStringView(), "ui");
                EXPECT_FALSE(entry.atlas->source.has_value());
            }
        }
        EXPECT_TRUE(game.Exists(AtlasTexturePath("ui")));
    }

    TEST(SpriteAtlas, UnpackRefusesWhatItCannotRestoreAndChangesNothing) {
        TempGame game;
        SaveSources(game);
        TextureLibrary library;
        FillLibrary(library);
        auto exists = [&game](PoolString path) { return game.Exists(path); };

        Vector<SpriteEntry> before = library.GetAll();
        const auto nothingPacked = UnpackAtlas(library, "ui", exists);
        ASSERT_FALSE(nothingPacked.has_value());
        EXPECT_TRUE(SameLibrary(before, library));

        ASSERT_TRUE(Pack(library, game).has_value());
        before = library.GetAll();
        std::error_code ignored;
        std::filesystem::remove(game.Resolve("assets/b.png"), ignored);
        const auto missing = UnpackAtlas(library, "ui", exists);
        ASSERT_FALSE(missing.has_value());
        EXPECT_NE(missing.error().find("Bravo"), String::npos) << missing.error().c_str();
        EXPECT_TRUE(SameLibrary(before, library));
    }

    TEST(SpriteAtlas, PackUnpackPackGivesTheFirstFile) {
        TempGame game;
        SaveSources(game);
        TextureLibrary library;
        FillLibrary(library);
        ASSERT_TRUE(Pack(library, game).has_value());
        const Vector<uint8_t> first = ReadBytes(game.Resolve("assets/atlases/ui.png"));

        auto exists = [&game](PoolString path) { return game.Exists(path); };
        ASSERT_TRUE(UnpackAtlas(library, "ui", exists).has_value());
        ASSERT_TRUE(Pack(library, game).has_value());
        EXPECT_TRUE(ReadBytes(game.Resolve("assets/atlases/ui.png")) == first);
    }

    namespace {

        PackSettings CustomSettings() {
            PackSettings settings;
            settings.padding = 3;
            settings.sizeMode = AtlasSizeMode::Tight;
            settings.maxSide = 2048;
            return settings;
        }

        bool SameSettings(const PackSettings& a, const PackSettings& b) {
            return a.padding == b.padding && a.sizeMode == b.sizeMode && a.maxSide == b.maxSide;
        }

        /// Gives every member of "ui" the record, so a case can bend one field of it
        void SetRecord(TextureLibrary& library, const char* member, int padding, const char* sizeMode, uint32_t maxSide) {
            SpriteEntry entry = *library.GetSprite(PoolString::Intern(member));
            AtlasPackRecord record;
            record.padding = padding;
            record.sizeMode = PoolString::Intern(sizeMode);
            record.maxSide = maxSide;
            entry.atlas->pack = record;
            library.AddOrUpdate(eastl::move(entry));
        }

    }  // namespace

    TEST(SpriteAtlas, PackRecordsItsSettingsInEveryMemberAndPackSettingsOfGivesThemBack) {
        TempGame game;
        SaveSources(game);
        TextureLibrary library;
        FillLibrary(library);
        EXPECT_TRUE(SameSettings(PackSettingsOf(library, "ui"), PackSettings{}));
        EXPECT_FALSE(HasPackRecord(library, "ui"));

        const PackSettings settings = CustomSettings();
        ASSERT_TRUE(Pack(library, game, settings).has_value());

        for (const SpriteEntry& entry : library.GetAll()) {
            if (!entry.atlas.has_value()) {
                continue;
            }
            ASSERT_TRUE(entry.atlas->pack.has_value()) << entry.name.ToStringView().data();
            EXPECT_EQ(entry.atlas->pack->padding, 3);
            EXPECT_EQ(entry.atlas->pack->sizeMode.ToStringView(), "Tight");
            EXPECT_EQ(entry.atlas->pack->maxSide, 2048u);
        }
        EXPECT_TRUE(SameSettings(PackSettingsOf(library, "ui"), settings));
        EXPECT_TRUE(HasPackRecord(library, "ui"));
        EXPECT_FALSE(HasPackRecord(library, "other"));
        EXPECT_TRUE(SameSettings(PackSettingsOf(library, "other"), PackSettings{}));
        EXPECT_TRUE(SameSettings(PackSettingsOf(library, ""), PackSettings{}));
    }

    TEST(SpriteAtlas, APackWithTheSettingsItFoundGivesTheSameFileAndEntries) {
        TempGame game;
        SaveSources(game);
        TextureLibrary library;
        FillLibrary(library);
        ASSERT_TRUE(Pack(library, game, CustomSettings()).has_value());
        const Vector<uint8_t> first = ReadBytes(game.Resolve("assets/atlases/ui.png"));
        const Vector<SpriteEntry> entries = library.GetAll();

        ASSERT_TRUE(Pack(library, game, PackSettingsOf(library, "ui")).has_value());
        EXPECT_TRUE(ReadBytes(game.Resolve("assets/atlases/ui.png")) == first);
        EXPECT_TRUE(SameLibrary(entries, library));
        EXPECT_TRUE(SameSettings(PackSettingsOf(library, "ui"), CustomSettings()));
    }

    TEST(SpriteAtlas, UnpackKeepsTheRecordSoPackUnpackPackWithItIsTheSameFile) {
        TempGame game;
        SaveSources(game);
        TextureLibrary library;
        FillLibrary(library);
        ASSERT_TRUE(Pack(library, game, CustomSettings()).has_value());
        const Vector<uint8_t> first = ReadBytes(game.Resolve("assets/atlases/ui.png"));

        auto exists = [&game](PoolString path) { return game.Exists(path); };
        ASSERT_TRUE(UnpackAtlas(library, "ui", exists).has_value());
        EXPECT_TRUE(library.GetSprite("Alpha"_intern)->atlas->pack.has_value());
        EXPECT_TRUE(SameSettings(PackSettingsOf(library, "ui"), CustomSettings()));

        ASSERT_TRUE(Pack(library, game, PackSettingsOf(library, "ui")).has_value());
        EXPECT_TRUE(ReadBytes(game.Resolve("assets/atlases/ui.png")) == first);
    }

    TEST(SpriteAtlas, PackSettingsOfTakesTheFirstMemberByNameWithARecord) {
        TextureLibrary library;
        FillLibrary(library);
        SetRecord(library, "Charlie", 5, "Tight", 1024);
        EXPECT_EQ(PackSettingsOf(library, "ui").padding, 5);

        SetRecord(library, "Bravo", 2, "PowerOfTwo", 512);
        const PackSettings settings = PackSettingsOf(library, "ui");
        EXPECT_EQ(settings.padding, 2);
        EXPECT_EQ(settings.sizeMode, AtlasSizeMode::PowerOfTwo);
        EXPECT_EQ(settings.maxSide, 512u);
    }

    TEST(SpriteAtlas, ARecordOutOfRangeGivesTheDefaultOfThatField) {
        const PackSettings defaults;
        struct Case {
            int padding;
            const char* sizeMode;
            uint32_t maxSide;
        };
        const Case cases[] = {
            {-1, "Tight", 1024}, {kMaxAtlasPadding + 1, "Tight", 1024}, {2, "Hexagonal", 1024}, {2, "Tight", 0}, {2, "Tight", kMaxSpriteImageSide + 1},
        };
        for (const Case& test : cases) {
            TextureLibrary library;
            FillLibrary(library);
            SetRecord(library, "Alpha", test.padding, test.sizeMode, test.maxSide);
            const PackSettings settings = PackSettingsOf(library, "ui");
            const bool paddingOk = test.padding >= 0 && test.padding <= kMaxAtlasPadding;
            const bool modeOk = StringView(test.sizeMode) == "Tight";
            const bool sideOk = test.maxSide >= 1 && test.maxSide <= kMaxSpriteImageSide;
            EXPECT_EQ(settings.padding, paddingOk ? test.padding : defaults.padding) << test.padding;
            EXPECT_EQ(settings.sizeMode, modeOk ? AtlasSizeMode::Tight : defaults.sizeMode) << test.sizeMode;
            EXPECT_EQ(settings.maxSide, sideOk ? test.maxSide : defaults.maxSide) << test.maxSide;
        }
    }

    TEST(SpriteAtlas, AGroupChangeDropsTheRecordAndAFirstGroupLeavesItEmpty) {
        TempGame game;
        SaveSources(game);
        TextureLibrary library;
        FillLibrary(library);
        ASSERT_TRUE(Pack(library, game, CustomSettings()).has_value());
        auto exists = [&game](PoolString path) { return game.Exists(path); };
        ASSERT_TRUE(UnpackAtlas(library, "ui", exists).has_value());

        const PoolString alpha = "Alpha"_intern;
        const auto moved = SetSpriteAtlas(library, alpha, "icons");
        ASSERT_TRUE(moved.has_value());
        EXPECT_TRUE(*moved);
        ASSERT_TRUE(library.GetSprite(alpha)->atlas.has_value());
        EXPECT_FALSE(library.GetSprite(alpha)->atlas->pack.has_value());
        EXPECT_TRUE(library.GetSprite("Bravo"_intern)->atlas->pack.has_value());

        const PoolString loner = "Loner"_intern;
        ASSERT_TRUE(SetSpriteAtlas(library, loner, "ui").has_value());
        EXPECT_FALSE(library.GetSprite(loner)->atlas->pack.has_value());
        EXPECT_TRUE(SameSettings(PackSettingsOf(library, "ui"), CustomSettings()));

        ASSERT_TRUE(SetSpriteAtlas(library, "Bravo"_intern, "").has_value());
        EXPECT_FALSE(library.GetSprite("Bravo"_intern)->atlas.has_value());
    }

}  // namespace BECore::Tests
