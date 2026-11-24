#include <BECore/Resource/TextureLibrary.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/TextureEditor/SpriteAtlasPreview.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 0.0001f;
        constexpr uint32_t kChessSheetWidth = 700;
        constexpr uint32_t kChessSheetHeight = 381;
        constexpr float kCell = 116.67f;
        constexpr float kRow = 190.5f;
        constexpr uint8_t kOpaque = 255;

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

        /// A member of group @p group already packed: its own texture and rect are on @p atlasPath, the pixels came from the source
        SpriteEntry MakePacked(const char* name, const char* group, const char* atlasPath, Rect placed, const char* sourcePath, Rect sourceRect) {
            SpriteEntry entry = MakeEntry(name, atlasPath, placed, group);
            SpriteSource source;
            source.texturePath = PoolString::Intern(sourcePath);
            source.rect = sourceRect;
            entry.atlas->source = source;
            return entry;
        }

        RgbaImage MakeSolid(uint32_t width, uint32_t height) {
            RgbaImage image;
            image.width = width;
            image.height = height;
            image.pixels.assign(static_cast<size_t>(width) * height * kBytesPerPixel, kOpaque);
            return image;
        }

        bool SameRect(const Rect& a, const Rect& b) {
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }

        /// The sizes of virtual paths, asked through a lookup that counts its calls
        class FakeSizes {
        public:
            void Put(const char* path, uint32_t width, uint32_t height) {
                _sizes[PoolString::Intern(path)] = ImageSize{width, height};
            }

            std::expected<ImageSize, String> Look(PoolString path) {
                ++calls;
                const auto found = _sizes.find(path);
                if (found == _sizes.end()) {
                    return std::unexpected(String("no such file"));
                }
                return found->second;
            }

            int calls = 0;

        private:
            UnorderedPoolMap<ImageSize> _sizes;
        };

        /// Three fractional cells of one sheet and a sprite of its own file, all in group "pieces"
        void FillChess(TextureLibrary& library, FakeSizes& sizes) {
            sizes.Put("assets/chess_test.png", kChessSheetWidth, kChessSheetHeight);
            sizes.Put("assets/pawn.png", 20, 30);
            library.AddOrUpdate(MakeEntry("WhiteKing", "assets/chess_test.png", Rect(0, 0, kCell, kRow), "pieces"));
            library.AddOrUpdate(MakeEntry("WhiteQueen", "assets/chess_test.png", Rect(kCell, 0, kCell, kRow), "pieces"));
            library.AddOrUpdate(MakeEntry("BlackKing", "assets/chess_test.png", Rect(0, kRow, kCell, kRow), "pieces"));
            library.AddOrUpdate(MakeEntry("Pawn", "assets/pawn.png", Rect(0, 0, 20, 30), "pieces"));
        }

        /// A folder of the test with `assets/` in it, removed with the object
        class TempGame {
        public:
            TempGame() : _root(Path(TestTempDirectory("SpriteAtlasPreview"))), _resolver{_root} {
                RemoveDirectory(_root);
                MakeDirectory(_root / "assets");
            }
            ~TempGame() {
                RemoveDirectory(_root);
            }
            TempGame(const TempGame&) = delete;
            TempGame& operator=(const TempGame&) = delete;

            /// The `resolve` of the result is a view of this object, which must outlive the call it is given to
            [[nodiscard]] AtlasIo Io() const {
                return AtlasIo{_root / "assets", _resolver};
            }

            [[nodiscard]] Path Resolve(StringView virtualPath) const {
                return _resolver(virtualPath);
            }

            void SavePng(const char* virtualPath, const RgbaImage& image) const {
                const Path file = Resolve(virtualPath);
                MakeDirectory(file.Parent());
                ASSERT_TRUE(SaveRgbaPng(file, image).has_value());
            }

        private:
            struct Resolver {
                Path root;

                Path operator()(StringView virtualPath) const {
                    return root / virtualPath;
                }
            };

            Path _root;
            Resolver _resolver;
        };

        /// The chess fixture on disk, so `WriteAtlas` can run on it
        void SaveChessSources(const TempGame& game) {
            game.SavePng("assets/chess_test.png", MakeSolid(kChessSheetWidth, kChessSheetHeight));
            game.SavePng("assets/pawn.png", MakeSolid(20, 30));
        }

    }  // namespace

    TEST(SpriteAtlasPreview, ATightPlanOfTwoSpritesIsFull) {
        TextureLibrary library;
        FakeSizes sizes;
        sizes.Put("assets/s.png", 8, 16);
        library.AddOrUpdate(MakeEntry("Top", "assets/s.png", Rect(0, 0, 8, 8), "g"));
        library.AddOrUpdate(MakeEntry("Bottom", "assets/s.png", Rect(0, 8, 8, 8), "g"));
        PackSettings settings;
        settings.padding = 0;
        settings.sizeMode = AtlasSizeMode::Tight;

        const auto preview = PreviewAtlasPlan(library, "g", settings, [&sizes](PoolString path) { return sizes.Look(path); });
        ASSERT_TRUE(preview.has_value()) << preview.error().c_str();
        EXPECT_EQ(preview->usedPixels, 128u);
        EXPECT_EQ(static_cast<uint64_t>(preview->width) * preview->height, 128u);
        EXPECT_NEAR(preview->fillRatio, 1.0f, kTolerance);
        EXPECT_EQ(preview->outside, 0u);
        EXPECT_EQ(preview->hidden, 0u);
        EXPECT_EQ(preview->texturePath.ToStringView(), "assets/atlases/g.png");
    }

    TEST(SpriteAtlasPreview, PaddingAndPowerOfTwoLeaveRoom) {
        TextureLibrary library;
        FakeSizes sizes;
        sizes.Put("assets/s.png", 8, 16);
        library.AddOrUpdate(MakeEntry("Top", "assets/s.png", Rect(0, 0, 8, 8), "g"));
        library.AddOrUpdate(MakeEntry("Bottom", "assets/s.png", Rect(0, 8, 8, 8), "g"));
        PackSettings settings;
        settings.padding = 2;
        settings.sizeMode = AtlasSizeMode::PowerOfTwo;

        // Either shape the packer may pick is 256 pixels: 8 by (8 + 2 + 8 rounded up to 32), or 32 by 8
        const auto preview = PreviewAtlasPlan(library, "g", settings, [&sizes](PoolString path) { return sizes.Look(path); });
        ASSERT_TRUE(preview.has_value()) << preview.error().c_str();
        EXPECT_EQ(preview->usedPixels, 128u);
        EXPECT_EQ(static_cast<uint64_t>(preview->width) * preview->height, 256u);
        EXPECT_NEAR(preview->fillRatio, 0.5f, kTolerance);
    }

    TEST(SpriteAtlasPreview, ThePlanPreviewIsWhatWriteAtlasMakes) {
        TextureLibrary library;
        FakeSizes sizes;
        FillChess(library, sizes);
        TempGame game;
        SaveChessSources(game);
        const PackSettings settings;

        const auto planned = PreviewAtlasPlan(library, "pieces", settings, [&sizes](PoolString path) { return sizes.Look(path); });
        ASSERT_TRUE(planned.has_value()) << planned.error().c_str();
        ASSERT_EQ(planned->rects.size(), 4u);
        // The plan changes nothing
        EXPECT_FALSE(library.GetSprite("Pawn"_intern)->atlas->source.has_value());

        const auto written = WriteAtlas(library, "pieces", settings, game.Io());
        ASSERT_TRUE(written.has_value()) << written.error().c_str();
        EXPECT_EQ(written->width, planned->width);
        EXPECT_EQ(written->height, planned->height);
        const auto file = LoadRgbaImage(game.Resolve(AtlasTexturePath("pieces").ToStringView()));
        ASSERT_TRUE(file.has_value());
        EXPECT_EQ(file->width, planned->width);
        EXPECT_EQ(file->height, planned->height);

        for (const AtlasPreviewRect& item : planned->rects) {
            const SpriteEntry& entry = *library.GetSprite(item.name);
            EXPECT_TRUE(SameRect(item.rect, entry.srcRect)) << String(item.name.ToStringView()).c_str();
            EXPECT_EQ(item.imagePath, entry.atlas->source->texturePath);
            EXPECT_TRUE(SameRect(item.imageRect, entry.atlas->source->rect));
        }
    }

    TEST(SpriteAtlasPreview, ThePackedPreviewAfterWriteEqualsThePlan) {
        TextureLibrary library;
        FakeSizes sizes;
        FillChess(library, sizes);
        TempGame game;
        SaveChessSources(game);
        const PackSettings settings;
        const auto planned = PreviewAtlasPlan(library, "pieces", settings, [&sizes](PoolString path) { return sizes.Look(path); });
        ASSERT_TRUE(planned.has_value());
        ASSERT_TRUE(WriteAtlas(library, "pieces", settings, game.Io()).has_value());

        const AtlasPreview current = PreviewPackedAtlas(library, "pieces", static_cast<float>(planned->width), static_cast<float>(planned->height));
        EXPECT_EQ(current.texturePath, planned->texturePath);
        EXPECT_EQ(current.width, planned->width);
        EXPECT_EQ(current.height, planned->height);
        ASSERT_EQ(current.rects.size(), planned->rects.size());
        for (size_t i = 0; i < current.rects.size(); ++i) {
            EXPECT_EQ(current.rects[i].name, planned->rects[i].name);
            EXPECT_TRUE(SameRect(current.rects[i].rect, planned->rects[i].rect));
            // Current draws the atlas file itself where Plan draws the sources
            EXPECT_EQ(current.rects[i].imagePath, current.texturePath);
            EXPECT_TRUE(SameRect(current.rects[i].imageRect, current.rects[i].rect));
        }
        EXPECT_EQ(current.usedPixels, planned->usedPixels);
        EXPECT_NEAR(current.fillRatio, planned->fillRatio, kTolerance);
        EXPECT_EQ(current.outside, 0u);
        EXPECT_EQ(current.hidden, 0u);
    }

    TEST(SpriteAtlasPreview, HiddenCountsMembersNotShownOnTheFile) {
        TextureLibrary library;
        library.AddOrUpdate(MakePacked("OnFile", "g", "assets/atlases/g.png", Rect(0, 0, 8, 8), "assets/a.png", Rect(0, 0, 8, 8)));
        library.AddOrUpdate(MakeEntry("NotPacked", "assets/b.png", Rect(0, 0, 8, 8), "g"));
        library.AddOrUpdate(MakePacked("OtherFile", "g", "assets/atlases/other.png", Rect(0, 0, 8, 8), "assets/c.png", Rect(0, 0, 8, 8)));
        library.AddOrUpdate(MakePacked("OtherGroup", "h", "assets/atlases/h.png", Rect(8, 0, 8, 8), "assets/d.png", Rect(0, 0, 8, 8)));

        const AtlasPreview preview = PreviewPackedAtlas(library, "g", 16.0f, 8.0f);
        ASSERT_EQ(preview.rects.size(), 1u);
        EXPECT_EQ(preview.rects[0].name.ToStringView(), "OnFile");
        EXPECT_EQ(preview.hidden, 2u);
        EXPECT_EQ(preview.usedPixels, 64u);
        EXPECT_NEAR(preview.fillRatio, 0.5f, kTolerance);
    }

    TEST(SpriteAtlasPreview, OutsideCountsRectsThatLeaveThePictureAndFillIsClamped) {
        TextureLibrary library;
        library.AddOrUpdate(MakePacked("Inside", "g", "assets/atlases/g.png", Rect(0, 0, 4, 4), "assets/a.png", Rect(0, 0, 4, 4)));
        library.AddOrUpdate(MakePacked("Beyond", "g", "assets/atlases/g.png", Rect(2, 0, 8, 4), "assets/b.png", Rect(0, 0, 8, 4)));

        const AtlasPreview preview = PreviewPackedAtlas(library, "g", 4.0f, 4.0f);
        EXPECT_EQ(preview.rects.size(), 2u);
        EXPECT_EQ(preview.outside, 1u);
        EXPECT_EQ(preview.usedPixels, 48u);
        EXPECT_NEAR(preview.fillRatio, 1.0f, kTolerance);

        const AtlasPreview empty = PreviewPackedAtlas(library, "g", 0.0f, 0.0f);
        EXPECT_NEAR(empty.fillRatio, 0.0f, kTolerance);
    }

    TEST(SpriteAtlasPreview, HitTestTakesTheLeftAndTopEdgesAndTheLastRect) {
        AtlasPreview preview;
        preview.rects.push_back(AtlasPreviewRect{"A"_intern, Rect(0, 0, 10, 10), {}, {}});
        preview.rects.push_back(AtlasPreviewRect{"B"_intern, Rect(20, 0, 10, 10), {}, {}});
        preview.rects.push_back(AtlasPreviewRect{"Over"_intern, Rect(5, 5, 10, 10), {}, {}});

        EXPECT_EQ(HitTestPreview(preview, 1.0f, 1.0f), 0);
        EXPECT_EQ(HitTestPreview(preview, 0.0f, 0.0f), 0);
        EXPECT_EQ(HitTestPreview(preview, 20.0f, 3.0f), 1);
        EXPECT_EQ(HitTestPreview(preview, 30.0f, 3.0f), -1);
        EXPECT_EQ(HitTestPreview(preview, 10.0f, 0.0f), -1);
        EXPECT_EQ(HitTestPreview(preview, 15.0f, 3.0f), -1);
        EXPECT_EQ(HitTestPreview(preview, 7.0f, 7.0f), 2);
        EXPECT_EQ(HitTestPreview(preview, 15.0f, 15.0f), -1);
        EXPECT_EQ(HitTestPreview(AtlasPreview{}, 0.0f, 0.0f), -1);
    }

    TEST(SpriteAtlasPreview, TheKeyMovesWithWhatThePreviewShowsOnly) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("A", "assets/a.png", Rect(0, 0, 8, 8), "g"));
        library.AddOrUpdate(MakeEntry("B", "assets/b.png", Rect(0, 0, 8, 8), "g"));
        library.AddOrUpdate(MakeEntry("Other", "assets/o.png", Rect(0, 0, 8, 8), "h"));
        const uint64_t before = AtlasPreviewKey(library, "g");
        EXPECT_EQ(AtlasPreviewKey(library, "g"), before);

        library.AddOrUpdate(MakeEntry("Other", "assets/o2.png", Rect(1, 2, 3, 4), "h"));
        EXPECT_EQ(AtlasPreviewKey(library, "g"), before) << "another group's member";

        library.AddOrUpdate(MakeEntry("A", "assets/a.png", Rect(1, 0, 8, 8), "g"));
        const uint64_t moved = AtlasPreviewKey(library, "g");
        EXPECT_NE(moved, before) << "a rect";

        library.AddOrUpdate(MakeEntry("A", "assets/a2.png", Rect(1, 0, 8, 8), "g"));
        const uint64_t otherTexture = AtlasPreviewKey(library, "g");
        EXPECT_NE(otherTexture, moved) << "a texture";

        library.AddOrUpdate(MakePacked("A", "g", "assets/a2.png", Rect(1, 0, 8, 8), "assets/src.png", Rect(0, 0, 8, 8)));
        const uint64_t packed = AtlasPreviewKey(library, "g");
        EXPECT_NE(packed, otherTexture) << "the packed flag and a source";

        library.AddOrUpdate(MakePacked("A", "g", "assets/a2.png", Rect(1, 0, 8, 8), "assets/src2.png", Rect(0, 0, 8, 8)));
        const uint64_t otherSource = AtlasPreviewKey(library, "g");
        EXPECT_NE(otherSource, packed) << "a source path";

        library.AddOrUpdate(MakePacked("A", "g", "assets/a2.png", Rect(1, 0, 8, 8), "assets/src2.png", Rect(0, 1, 8, 8)));
        EXPECT_NE(AtlasPreviewKey(library, "g"), otherSource) << "a source rect";
    }

    TEST(SpriteAtlasPreview, PlanErrorsAreThoseOfBuildAtlas) {
        FakeSizes sizes;
        sizes.Put("assets/small.png", 4, 4);
        const PackSettings settings;

        TextureLibrary none;
        const auto noSprite = PreviewAtlasPlan(none, "ui", settings, [&sizes](PoolString path) { return sizes.Look(path); });
        ASSERT_FALSE(noSprite.has_value());
        const auto noSpriteBuilt = BuildAtlas(none, "ui", settings, [](PoolString) -> std::expected<RgbaImage, String> { return std::unexpected(String("no such file")); });
        ASSERT_FALSE(noSpriteBuilt.has_value());
        EXPECT_EQ(noSprite.error(), noSpriteBuilt.error());

        TextureLibrary gone;
        gone.AddOrUpdate(MakeEntry("Culprit", "assets/missing.png", Rect(0, 0, 2, 2), "ui"));
        const auto missing = PreviewAtlasPlan(gone, "ui", settings, [&sizes](PoolString path) { return sizes.Look(path); });
        ASSERT_FALSE(missing.has_value());
        const auto built = BuildAtlas(gone, "ui", settings, [](PoolString) -> std::expected<RgbaImage, String> { return std::unexpected(String("no such file")); });
        ASSERT_FALSE(built.has_value());
        EXPECT_EQ(missing.error(), built.error());
        EXPECT_NE(missing.error().find("Culprit"), String::npos);

        TextureLibrary outside;
        outside.AddOrUpdate(MakeEntry("Culprit", "assets/small.png", Rect(3, 3, 2, 2), "ui"));
        const auto beyond = PreviewAtlasPlan(outside, "ui", settings, [&sizes](PoolString path) { return sizes.Look(path); });
        ASSERT_FALSE(beyond.has_value());
        EXPECT_NE(beyond.error().find("not inside"), String::npos) << beyond.error().c_str();
    }

    TEST(SpriteAtlasPreview, TheLookupIsAskedOncePerDistinctFile) {
        TextureLibrary library;
        FakeSizes sizes;
        FillChess(library, sizes);
        const auto preview = PreviewAtlasPlan(library, "pieces", PackSettings{}, [&sizes](PoolString path) { return sizes.Look(path); });
        ASSERT_TRUE(preview.has_value()) << preview.error().c_str();
        EXPECT_EQ(preview->rects.size(), 4u);
        EXPECT_EQ(sizes.calls, 2);
    }

}  // namespace BECore::Tests
