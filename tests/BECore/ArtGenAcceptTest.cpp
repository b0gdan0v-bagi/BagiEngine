#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        void WriteBytes(const std::filesystem::path& file, const void* data, size_t size) {
            std::FILE* stream = std::fopen(file.string().c_str(), "wb");
            ASSERT_NE(stream, nullptr);
            std::fwrite(data, 1, size, stream);
            std::fclose(stream);
        }

        /// The first bytes of a PNG of @p width x @p height: what ReadPngSize reads (the rest of the file does not matter to it)
        void WritePngHeader(const std::filesystem::path& file, uint32_t width, uint32_t height) {
            Array<uint8_t, 33> bytes{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
            for (int i = 0; i < 4; ++i) {
                bytes[16 + i] = static_cast<uint8_t>(width >> (24 - 8 * i));
                bytes[20 + i] = static_cast<uint8_t>(height >> (24 - 8 * i));
            }
            WriteBytes(file, bytes.data(), bytes.size());
        }

        void WriteText(const std::filesystem::path& file, StringView text) {
            WriteBytes(file, text.data(), text.size());
        }

        /// What the tests about the copy and not about the picture use: the PNGs they write are only a header
        const ArtGenImagePlan kIdentity{ArtGenImageSettings{false, ArtGenImageSettings::kDefaultPadding, 0}, {}};

        /// A real PNG of @p width x @p height with an opaque red box; the rest is transparent
        void WriteRealPng(const std::filesystem::path& file, uint32_t width, uint32_t height, ArtGenIntRect box) {
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

        String ReadText(const std::filesystem::path& file) {
            String text;
            std::FILE* stream = std::fopen(file.string().c_str(), "rb");
            if (stream == nullptr) {
                return text;
            }
            char buffer[256];
            size_t read = 0;
            while ((read = std::fread(buffer, 1, sizeof(buffer), stream)) > 0) {
                text.append(buffer, read);
            }
            std::fclose(stream);
            return text;
        }

    }  // namespace

    class ArtGenAcceptTest : public ::testing::Test {
    protected:
        void SetUp() override {
            folder = TestTempDirectory("artgen_accept");
            std::filesystem::create_directories(folder / "out");
            std::filesystem::create_directories(folder / "assets");
            png = folder / "out" / "queen_7.png";
            provenance = folder / "out" / "queen_7.artgen.json";
            WritePngHeader(png, 1024, 768);
            WriteText(provenance, "{\"seed\": 7}");
        }

        void TearDown() override {
            std::error_code ignored;
            std::filesystem::remove_all(folder, ignored);
        }

        std::filesystem::path folder;
        std::filesystem::path png;
        std::filesystem::path provenance;
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
        const SpriteEntry entry = AcceptedSpriteEntry(library, PoolString::Intern("queen"), PoolString::Intern("assets/artgen/queen.png"), 64, 48);
        EXPECT_EQ(entry.name.ToStringView(), "queen");
        EXPECT_EQ(entry.texturePath.ToStringView(), "assets/artgen/queen.png");
        EXPECT_TRUE(SameRectOf(entry.srcRect, Rect(0, 0, 64, 48)));
        EXPECT_FALSE(entry.atlas.has_value());
    }

    TEST(ArtGenAcceptedEntryTest, AnEntryWithNoLinkIsReplacedWhole) {
        TextureLibrary library;
        library.AddOrUpdate(LibraryEntry("queen", "assets/old.png", Rect(10, 10, 5, 5)));
        const SpriteEntry entry = AcceptedSpriteEntry(library, PoolString::Intern("queen"), PoolString::Intern("assets/artgen/queen.png"), 64, 48);
        EXPECT_EQ(entry.texturePath.ToStringView(), "assets/artgen/queen.png");
        EXPECT_TRUE(SameRectOf(entry.srcRect, Rect(0, 0, 64, 48)));
        EXPECT_FALSE(entry.atlas.has_value());
    }

    TEST(ArtGenAcceptedEntryTest, AnUnpackedMemberKeepsItsGroupAndSettings) {
        TextureLibrary library;
        library.AddOrUpdate(GroupedEntry("queen", "assets/old.png", Rect(10, 10, 5, 5), "pieces", true));
        const SpriteEntry entry = AcceptedSpriteEntry(library, PoolString::Intern("queen"), PoolString::Intern("assets/artgen/queen.png"), 64, 48);
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
        const SpriteEntry entry = AcceptedSpriteEntry(library, PoolString::Intern("queen"), PoolString::Intern("assets/artgen/queen.png"), 64, 48);
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
        const SpriteEntry accepted = AcceptedSpriteEntry(library, PoolString::Intern("queen"), PoolString::Intern("assets/artgen/queen.png"), 64, 48);
        EXPECT_EQ(accepted.atlas->source->texturePath.ToStringView(), "assets/artgen/queen.png");
        EXPECT_EQ(library.GetSprite(PoolString::Intern("king"))->atlas->source->texturePath.ToStringView(), "assets/sheet.png");
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
        EXPECT_NE(library.GetSprite(PoolString::Intern("queen")), nullptr);
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
        EXPECT_EQ(library.GetSprite(PoolString::Intern("queen"))->srcRect.w, 128.0f);
    }

    TEST(ArtGenLibraryEntryTest, AFailedSaveGivesTheLibraryBack) {
        TextureLibrary library;
        library.AddOrUpdate(LibraryEntry("queen", "assets/artgen/queen.png", Rect(0, 0, 64, 64)));

        EXPECT_FALSE(StoreSpriteEntry(library, LibraryEntry("queen", "assets/artgen/queen.png", Rect(0, 0, 128, 64)), [] { return false; }));
        EXPECT_EQ(library.GetSprite(PoolString::Intern("queen"))->srcRect.w, 64.0f);

        EXPECT_FALSE(StoreSpriteEntry(library, LibraryEntry("king", "assets/artgen/king.png", Rect(0, 0, 8, 8)), [] { return false; }));
        EXPECT_EQ(library.GetSprite(PoolString::Intern("king")), nullptr);
    }

    namespace {

        constexpr uint8_t kOpaque = 255;

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

        Vector<uint8_t> ReadAllBytes(const std::filesystem::path& file) {
            std::ifstream stream(file, std::ios::binary | std::ios::ate);  //-NoStd: a test reads the file back
            Vector<uint8_t> bytes(static_cast<size_t>(std::max<std::streamoff>(stream.tellg(), 0)));
            stream.seekg(0);
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return bytes;
        }

        /// True when every pixel of the rect @p at of @p atlas has the colour of a MakeSolid image
        bool RectIsSolid(const RgbaImage& atlas, const Rect& at, uint8_t red, uint8_t green, uint8_t blue) {
            for (uint32_t y = static_cast<uint32_t>(at.y); y < static_cast<uint32_t>(at.y + at.h); ++y) {
                for (uint32_t x = static_cast<uint32_t>(at.x); x < static_cast<uint32_t>(at.x + at.w); ++x) {
                    const uint8_t* pixel = atlas.pixels.data() + (static_cast<size_t>(y) * atlas.width + x) * kBytesPerPixel;
                    if (pixel[0] != red || pixel[1] != green || pixel[2] != blue || pixel[3] != kOpaque) {
                        return false;
                    }
                }
            }
            return true;
        }

        constexpr StringView kQueenPng = "assets/artgen/queen.png";
        constexpr StringView kRookPng = "assets/rook.png";
        constexpr StringView kAtlasPng = "assets/atlases/ui.png";

        /// A game folder with `assets/` in it, removed with the object; virtual paths resolve into it
        class AcceptGame {
        public:
            AcceptGame() : _root(TestTempDirectory("artgen_accept_atlas")), _resolver{_root} {
                std::error_code ignored;
                std::filesystem::remove_all(_root, ignored);
                std::filesystem::create_directories(_root / "assets" / "artgen", ignored);
            }
            ~AcceptGame() {
                std::error_code ignored;
                std::filesystem::remove_all(_root, ignored);
            }
            AcceptGame(const AcceptGame&) = delete;
            AcceptGame& operator=(const AcceptGame&) = delete;

            [[nodiscard]] std::filesystem::path Resolve(StringView virtualPath) const {
                return _resolver(virtualPath);
            }

            /// The `resolve` of the result is a view of this object, which must outlive the call it is given to
            [[nodiscard]] AtlasIo Io() const {
                return AtlasIo{_root / "assets", _resolver};
            }

            void SavePng(StringView virtualPath, const RgbaImage& image) const {
                ASSERT_TRUE(SaveRgbaPng(Resolve(virtualPath), image).has_value());
            }

            /// What CopyVariantIntoAssets would have returned for the picture just saved at the sprite's path
            [[nodiscard]] ArtGenCopied Copied(const RgbaImage& image) const {
                SavePng(kQueenPng, image);
                ArtGenCopied copied;
                copied.texturePath = String(kQueenPng);
                copied.pngFile = Resolve(kQueenPng);
                copied.width = image.width;
                copied.height = image.height;
                return copied;
            }

            [[nodiscard]] bool AtlasExists() const {
                return std::filesystem::exists(Resolve(kAtlasPng));
            }

            [[nodiscard]] RgbaImage Atlas() const {
                auto loaded = LoadRgbaImage(Resolve(kAtlasPng));
                EXPECT_TRUE(loaded.has_value());
                return loaded ? std::move(*loaded) : RgbaImage{};
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

        constexpr uint8_t kLow = 40;
        constexpr uint8_t kHigh = 200;

        /// Group "ui" of the queen (4x4 blue) and the rook (2x2 red), their sources on disk; both packed when @p pack
        void FillGroup(TextureLibrary& library, const AcceptGame& game, bool pack, const PackSettings& settings = PackSettings{}) {
            game.SavePng(kQueenPng, MakeSolid(4, 4, 0, 0, kHigh));
            game.SavePng(kRookPng, MakeSolid(2, 2, kHigh, 0, 0));
            library.AddOrUpdate(GroupedEntry("queen", "assets/artgen/queen.png", Rect(0, 0, 4, 4), "ui", false));
            library.AddOrUpdate(GroupedEntry("rook", "assets/rook.png", Rect(0, 0, 2, 2), "ui", false));
            if (pack) {
                ASSERT_TRUE(WriteAtlas(library, "ui", settings, game.Io()).has_value());
            }
        }

        const SpriteEntry& Queen(const TextureLibrary& library) {
            return *library.GetSprite(PoolString::Intern("queen"));
        }

        const SpriteEntry& Rook(const TextureLibrary& library) {
            return *library.GetSprite(PoolString::Intern("rook"));
        }

    }  // namespace

    TEST(ArtGenAcceptIntoLibraryTest, APackedSpriteIsRepackedWithTheNewPicture) {
        const AcceptGame game;
        TextureLibrary library;
        FillGroup(library, game, true);
        const ArtGenCopied copied = game.Copied(MakeSolid(6, 2, 0, kHigh, 0));
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
        EXPECT_EQ(Queen(library).atlas->source->texturePath.ToStringView(), kQueenPng);
        EXPECT_EQ(Queen(library).atlas->source->rect.w, 6.0f);

        const RgbaImage atlas = game.Atlas();
        EXPECT_TRUE(RectIsSolid(atlas, Queen(library).srcRect, 0, kHigh, 0));
        EXPECT_TRUE(RectIsSolid(atlas, Rook(library).srcRect, kHigh, 0, 0));
    }

    TEST(ArtGenAcceptIntoLibraryTest, TheSamePictureAgainChangesNothingAndSavesNothing) {
        const AcceptGame game;
        TextureLibrary library;
        FillGroup(library, game, true);
        const ArtGenCopied copied = game.Copied(MakeSolid(6, 2, 0, kHigh, 0));
        int saves = 0;
        auto save = [&saves] {
            ++saves;
            return true;
        };
        ASSERT_TRUE(AcceptIntoLibrary(library, copied, "queen", game.Io(), save).has_value());
        const Vector<SpriteEntry> before{Queen(library), Rook(library)};
        const Vector<uint8_t> atlasBefore = ReadAllBytes(game.Resolve(kAtlasPng));
        saves = 0;

        const auto again = AcceptIntoLibrary(library, copied, "queen", game.Io(), save);

        ASSERT_TRUE(again.has_value());
        EXPECT_FALSE(again->saved);
        EXPECT_EQ(saves, 0);
        EXPECT_TRUE(again->repacked.has_value());
        EXPECT_EQ(Queen(library).srcRect.w, before[0].srcRect.w);
        EXPECT_EQ(Queen(library).srcRect.x, before[0].srcRect.x);
        EXPECT_EQ(Rook(library).srcRect.x, before[1].srcRect.x);
        EXPECT_EQ(ReadAllBytes(game.Resolve(kAtlasPng)), atlasBefore);
    }

    TEST(ArtGenAcceptIntoLibraryTest, ThePicturesOfOtherMembersStayAsTheyWere) {
        const AcceptGame game;
        TextureLibrary library;
        FillGroup(library, game, true);
        const ArtGenCopied copied = game.Copied(MakeSolid(3, 3, kLow, kLow, kLow));
        auto save = [] { return true; };

        ASSERT_TRUE(AcceptIntoLibrary(library, copied, "queen", game.Io(), save).has_value());

        EXPECT_EQ(Rook(library).atlas->source->texturePath.ToStringView(), kRookPng);
        const RgbaImage atlas = game.Atlas();
        EXPECT_TRUE(RectIsSolid(atlas, Rook(library).srcRect, kHigh, 0, 0));
        EXPECT_TRUE(RectIsSolid(atlas, Queen(library).srcRect, kLow, kLow, kLow));
    }

    TEST(ArtGenAcceptIntoLibraryTest, ARepackThatFailsKeepsTheNewSourceAndTheOldAtlas) {
        const AcceptGame game;
        TextureLibrary library;
        FillGroup(library, game, true);
        const Vector<uint8_t> atlasBefore = ReadAllBytes(game.Resolve(kAtlasPng));
        const SpriteEntry rookBefore = Rook(library);
        std::error_code ignored;
        std::filesystem::remove(game.Resolve(kRookPng), ignored);
        const ArtGenCopied copied = game.Copied(MakeSolid(6, 2, 0, kHigh, 0));
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
        EXPECT_EQ(Queen(library).atlas->source->texturePath.ToStringView(), kQueenPng);
        EXPECT_EQ(Queen(library).atlas->source->rect.w, 6.0f);
        EXPECT_EQ(Queen(library).texturePath.ToStringView(), kAtlasPng);
        EXPECT_EQ(Rook(library).srcRect.x, rookBefore.srcRect.x);
        EXPECT_EQ(Rook(library).atlas->source->texturePath.ToStringView(), kRookPng);
        EXPECT_EQ(ReadAllBytes(game.Resolve(kAtlasPng)), atlasBefore);
    }

    TEST(ArtGenAcceptIntoLibraryTest, AFailedSecondSaveGivesTheMembersBack) {
        const AcceptGame game;
        TextureLibrary library;
        FillGroup(library, game, true);
        const ArtGenCopied copied = game.Copied(MakeSolid(6, 2, 0, kHigh, 0));
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
        EXPECT_EQ(Queen(library).atlas->source->texturePath.ToStringView(), kQueenPng);
        EXPECT_EQ(Queen(library).atlas->source->rect.w, 6.0f);
    }

    TEST(ArtGenAcceptIntoLibraryTest, AnUnpackedMemberKeepsItsGroupAndNoAtlasIsMade) {
        const AcceptGame game;
        TextureLibrary library;
        FillGroup(library, game, false);
        const ArtGenCopied copied = game.Copied(MakeSolid(6, 2, 0, kHigh, 0));
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
        EXPECT_EQ(Queen(library).atlas->name.ToStringView(), "ui");
        EXPECT_FALSE(Queen(library).atlas->source.has_value());
        EXPECT_FALSE(game.AtlasExists());
    }

    TEST(ArtGenAcceptIntoLibraryTest, ASpriteWithNoLinkIsStoredAsBefore) {
        const AcceptGame game;
        TextureLibrary library;
        library.AddOrUpdate(LibraryEntry("queen", "assets/old.png", Rect(1, 1, 3, 3)));
        const ArtGenCopied copied = game.Copied(MakeSolid(6, 2, 0, kHigh, 0));
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
        EXPECT_FALSE(game.AtlasExists());

        TextureLibrary fresh;
        const auto added = AcceptIntoLibrary(fresh, copied, "queen", game.Io(), save);
        ASSERT_TRUE(added.has_value());
        EXPECT_EQ(Queen(fresh).srcRect.h, 2.0f);
    }

    TEST(ArtGenAcceptIntoLibraryTest, AFailedSaveOfAnUnlinkedSpriteNamesThePng) {
        const AcceptGame game;
        TextureLibrary library;
        const ArtGenCopied copied = game.Copied(MakeSolid(6, 2, 0, kHigh, 0));
        auto save = [] { return false; };

        const auto stored = AcceptIntoLibrary(library, copied, "queen", game.Io(), save);

        ASSERT_FALSE(stored.has_value());
        EXPECT_NE(stored.error().find("the PNG is in"), String::npos);
        EXPECT_EQ(library.GetSprite(PoolString::Intern("queen")), nullptr);
    }

    TEST(ArtGenAcceptIntoLibraryTest, TheRepackUsesTheGroupsSettingsAndMakesTheFileAPlainPackWould) {
        const AcceptGame game;
        TextureLibrary library;
        PackSettings settings;
        settings.padding = 3;
        settings.sizeMode = AtlasSizeMode::Tight;
        FillGroup(library, game, true, settings);
        const ArtGenCopied copied = game.Copied(MakeSolid(6, 2, 0, kHigh, 0));
        auto save = [] { return true; };

        ASSERT_TRUE(AcceptIntoLibrary(library, copied, "queen", game.Io(), save).has_value());

        ASSERT_TRUE(Queen(library).atlas->pack.has_value());
        EXPECT_EQ(Queen(library).atlas->pack->padding, 3);
        const PackSettings remembered = PackSettingsOf(library, "ui");
        EXPECT_EQ(remembered.padding, 3);
        EXPECT_EQ(remembered.sizeMode, AtlasSizeMode::Tight);
        const Vector<uint8_t> afterAccept = ReadAllBytes(game.Resolve(kAtlasPng));
        ASSERT_TRUE(WriteAtlas(library, "ui", remembered, game.Io()).has_value());
        EXPECT_EQ(ReadAllBytes(game.Resolve(kAtlasPng)), afterAccept);
    }

    TEST(ArtGenAtlasReplyTest, NoRepackIsNull) {
        EXPECT_TRUE(AtlasReplyToJson(Optional<AtlasWritten>{}).is_null());
    }

    TEST(ArtGenAtlasReplyTest, ARepackNamesTheAtlasItsFileAndSize) {
        AtlasWritten written;
        written.name = PoolString::Intern("ui");
        written.texturePath = PoolString::Intern("assets/atlases/ui.png");
        written.width = 64;
        written.height = 32;
        written.changed = {PoolString::Intern("queen"), PoolString::Intern("rook")};

        const Json reply = AtlasReplyToJson(written);

        ASSERT_TRUE(reply.is_object());
        EXPECT_EQ(GetJsonString(reply, "name").value_or(String()), "ui");
        EXPECT_EQ(GetJsonString(reply, "texturePath").value_or(String()), "assets/atlases/ui.png");
        EXPECT_EQ(GetJsonInt(reply, "width").value_or(0), 64);
        EXPECT_EQ(GetJsonInt(reply, "height").value_or(0), 32);
        EXPECT_EQ(GetJsonInt(reply, "members").value_or(0), 2);
    }

    TEST(ArtGenAcceptIntoLibraryTest, TheWrittenAtlasNamesItsGroup) {
        const AcceptGame game;
        TextureLibrary library;
        FillGroup(library, game, true);
        const ArtGenCopied copied = game.Copied(MakeSolid(6, 2, 0, kHigh, 0));
        auto save = [] { return true; };

        const auto stored = AcceptIntoLibrary(library, copied, "queen", game.Io(), save);

        ASSERT_TRUE(stored.has_value() && stored->repacked.has_value());
        EXPECT_EQ(stored->repacked->name.ToStringView(), "ui");
        EXPECT_EQ(stored->repacked->changed.size(), 2u);
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
        EXPECT_TRUE(std::filesystem::exists(folder / "assets" / "artgen" / "white_queen.png"));
        EXPECT_EQ(ReadText(folder / "assets" / "artgen" / "white_queen.artgen.json"), "{\"seed\": 7}");
    }

    TEST_F(ArtGenAcceptTest, ASecondAcceptOfTheSameNameReplacesTheFiles) {
        ASSERT_TRUE(CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity).has_value());
        const auto other = folder / "out" / "queen_8.png";
        WritePngHeader(other, 512, 512);
        WriteText(provenance, "{\"seed\": 8}");
        const auto copied = CopyVariantIntoAssets(other, provenance, folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(copied.has_value());
        EXPECT_EQ(copied->width, 512u);
        EXPECT_EQ(ReadPngSize(folder / "assets" / "artgen" / "queen.png")->first, 512u);
        EXPECT_EQ(ReadText(folder / "assets" / "artgen" / "queen.artgen.json"), "{\"seed\": 8}");
    }

    TEST_F(ArtGenAcceptTest, ABadNameOrANonPngIsRefusedAndWritesNothing) {
        EXPECT_FALSE(CopyVariantIntoAssets(png, provenance, folder / "assets", "../escape", kIdentity).has_value());
        EXPECT_FALSE(CopyVariantIntoAssets(provenance, provenance, folder / "assets", "queen", kIdentity).has_value());
        EXPECT_FALSE(std::filesystem::exists(folder / "assets" / "artgen"));
    }

    TEST_F(ArtGenAcceptTest, AVariantWithoutProvenanceIsStillAccepted) {
        std::filesystem::remove(provenance);
        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(copied.has_value());
        EXPECT_FALSE(std::filesystem::exists(copied->provenanceFile));
    }

    TEST_F(ArtGenAcceptTest, TheProvenanceFileIsBesideTheMediaFile) {
        EXPECT_EQ(ProvenanceFileOf("assets/artgen/queen.png").generic_string(), "assets/artgen/queen.artgen.json");
        EXPECT_EQ(ProvenanceFileOf("assets/sounds/move.wav").generic_string(), "assets/sounds/move.artgen.json");
    }

    TEST_F(ArtGenAcceptTest, KeptIsAddedToTheCopiedProvenanceAndItsOwnKeysStay) {
        WriteText(provenance, "{\"seed\": 7, \"preset\": \"gem\"}");
        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(copied.has_value());
        ASSERT_TRUE(MarkProvenanceKept(copied->provenanceFile, 7, "2026-11-17T10:00:00Z").has_value());

        const auto record = ParseJson(StringView(ReadText(copied->provenanceFile)));
        ASSERT_TRUE(record.has_value());
        EXPECT_EQ(GetJsonInt(*record, "seed").value_or(-1), 7);
        EXPECT_EQ(GetJsonString(*record, "preset").value_or(String()), "gem");
        const Json* kept = FindJsonMember(*record, "kept");
        ASSERT_NE(kept, nullptr);
        EXPECT_EQ(GetJsonInt(*kept, "seed").value_or(-1), 7);
        EXPECT_EQ(GetJsonString(*kept, "date").value_or(String()), "2026-11-17T10:00:00Z");
    }

    TEST_F(ArtGenAcceptTest, ASecondKeptReplacesTheFirst) {
        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(copied.has_value());
        ASSERT_TRUE(MarkProvenanceKept(copied->provenanceFile, 7, "2026-11-17T10:00:00Z").has_value());
        ASSERT_TRUE(MarkProvenanceKept(copied->provenanceFile, 9, "2026-11-17T11:00:00Z").has_value());

        const auto record = ParseJson(StringView(ReadText(copied->provenanceFile)));
        ASSERT_TRUE(record.has_value());
        const Json* kept = FindJsonMember(*record, "kept");
        ASSERT_NE(kept, nullptr);
        EXPECT_EQ(GetJsonInt(*kept, "seed").value_or(-1), 9);
        EXPECT_EQ(GetJsonString(*kept, "date").value_or(String()), "2026-11-17T11:00:00Z");
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
        EXPECT_FALSE(std::filesystem::exists(missing));

        WriteText(provenance, "[1, 2]");
        const auto notObject = MarkProvenanceKept(provenance, 7, "2026-11-17T10:00:00Z");
        ASSERT_FALSE(notObject.has_value());
        EXPECT_NE(notObject.error().find("queen_7.artgen.json"), String::npos);
        EXPECT_EQ(ReadText(provenance), "[1, 2]");
    }

    TEST_F(ArtGenAcceptTest, ASwitchRewritesTheProvenanceFromTheVariantWithoutKept) {
        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(copied.has_value());
        ASSERT_TRUE(MarkProvenanceKept(copied->provenanceFile, 7, "2026-11-17T10:00:00Z").has_value());
        WriteText(provenance, "{\"seed\": 8}");
        ASSERT_TRUE(CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity).has_value());
        EXPECT_EQ(ReadText(copied->provenanceFile).find("kept"), String::npos);
    }

    TEST_F(ArtGenAcceptTest, AnIdentityPlanCopiesBothFilesByteForByte) {
        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen", kIdentity);
        ASSERT_TRUE(copied.has_value()) << copied.error().c_str();
        EXPECT_EQ(ReadText(copied->pngFile), ReadText(png));
        EXPECT_EQ(ReadText(copied->provenanceFile), "{\"seed\": 7}");
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
        WriteText(provenance, "{\"seed\": 7, \"preset\": \"gem\"}");
        const auto copied = CopyVariantIntoAssets(source, provenance, folder / "assets", "queen", ArtGenImagePlan{});
        ASSERT_TRUE(copied.has_value()) << copied.error().c_str();

        const auto record = ParseJson(StringView(ReadText(copied->provenanceFile)));
        ASSERT_TRUE(record.has_value());
        EXPECT_EQ(GetJsonString(*record, "preset").value_or(String()), "gem");
        const Json* accepted = FindJsonMember(*record, "accepted");
        ASSERT_NE(accepted, nullptr);
        EXPECT_EQ(GetJsonString(*accepted, "sprite").value_or(String()), "queen");
        EXPECT_EQ(GetJsonString(*accepted, "source").value_or(String()), String(source.generic_string().c_str()));
        EXPECT_EQ(GetJsonInt(*accepted, "seed").value_or(-1), 7);
        EXPECT_EQ(GetJsonString(*accepted, "frame").value_or(String()), "sprite");
        const Json* processing = FindJsonMember(*accepted, "processing");
        ASSERT_NE(processing, nullptr);
        EXPECT_EQ(GetJsonBool(*processing, "trim").value_or(false), true);
        EXPECT_EQ(GetJsonInt(*processing, "maxSize").value_or(-1), 256);
        const Json* crop = FindJsonMember(*accepted, "crop");
        ASSERT_NE(crop, nullptr);
        EXPECT_EQ(GetJsonInt(*crop, "w").value_or(-1), 108);
        EXPECT_EQ(GetJsonNumber(*accepted, "scale").value_or(0.0), copied->scale);
        const Json* size = FindJsonMember(*accepted, "size");
        ASSERT_NE(size, nullptr);
        ASSERT_TRUE(size->is_array());
        EXPECT_EQ(size->size(), 2u);
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
        EXPECT_FALSE(std::filesystem::exists(folder / "assets" / "artgen"));
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
        const auto record = ParseJson(StringView(ReadText(second->provenanceFile)));
        ASSERT_TRUE(record.has_value());
        EXPECT_EQ(GetJsonString(*FindJsonMember(*record, "accepted"), "frame").value_or(String()), "set");
    }

    TEST_F(ArtGenAcceptTest, AnImageWithNothingDrawnIsRefusedAndWritesNothing) {
        const auto source = folder / "out" / "empty.png";
        ASSERT_NO_FATAL_FAILURE(WriteRealPng(source, 64, 64, {0, 0, 0, 0}));
        const auto copied = CopyVariantIntoAssets(source, provenance, folder / "assets", "queen", ArtGenImagePlan{});
        ASSERT_FALSE(copied.has_value());
        EXPECT_NE(copied.error().find("nothing drawn"), String::npos) << copied.error().c_str();
        EXPECT_FALSE(std::filesystem::exists(folder / "assets" / "artgen"));
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
            const std::string text = file.generic_string();  //-NoStd: the text of a std::filesystem::path
            files.push_back(String(text.c_str(), text.size()));
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
        const auto text = [](const std::filesystem::path& path) {
            const std::string generic = path.generic_string();  //-NoStd: the text of a std::filesystem::path
            return String(generic.c_str(), generic.size());
        };
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

}  // namespace BECore::Tests
