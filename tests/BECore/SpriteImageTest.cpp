#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <SDL3/SDL.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenImage.h>
#include <Widgets/TextureEditor/SpriteImage.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr Array<uint8_t, 8> kPngSignature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

        /// Every pixel differs: alpha runs through 255, 128, 1 and one pixel is fully transparent
        RgbaImage MakeImage(uint32_t width, uint32_t height) {
            RgbaImage image;
            image.width = width;
            image.height = height;
            image.pixels.resize(static_cast<size_t>(width) * height * kBytesPerPixel);
            constexpr Array<uint8_t, 3> alphas = {255, 128, 1};
            for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
                image.pixels[i * kBytesPerPixel + 0] = static_cast<uint8_t>(10 + i * 7);
                image.pixels[i * kBytesPerPixel + 1] = static_cast<uint8_t>(200 - i * 5);
                image.pixels[i * kBytesPerPixel + 2] = static_cast<uint8_t>(i * 13 + 1);
                image.pixels[i * kBytesPerPixel + 3] = alphas[i % alphas.size()];
            }
            if (width * height > 1) {
                image.pixels[3] = 0;  // the first pixel is transparent
            }
            return image;
        }

        Vector<uint8_t> ReadBytes(const std::filesystem::path& file) {
            return ReadFileBytes(file);
        }

        /// A test file under the system temp folder that is removed with the object
        class TempFile {
        public:
            explicit TempFile(const char* extension = ".png") : _path(TestTempDirectory("SpriteImage")) {
                _path += ToStd(Format("_{}{}", ++_counter, extension));
            }
            ~TempFile() {
                std::error_code ignored;
                std::filesystem::remove(_path, ignored);
            }
            TempFile(const TempFile&) = delete;
            TempFile& operator=(const TempFile&) = delete;

            [[nodiscard]] const std::filesystem::path& Path() const {
                return _path;
            }

        private:
            // One test makes several files, and TestTempDirectory names them alike
            static inline int _counter = 0;
            std::filesystem::path _path;
        };

        void ExpectRoundTrip(const RgbaImage& image) {
            TempFile file;
            const auto saved = SaveRgbaPng(file.Path(), image);
            ASSERT_TRUE(saved.has_value()) << saved.error().c_str();
            const auto loaded = LoadRgbaImage(file.Path());
            ASSERT_TRUE(loaded.has_value()) << loaded.error().c_str();
            EXPECT_EQ(loaded->width, image.width);
            EXPECT_EQ(loaded->height, image.height);
            EXPECT_TRUE(loaded->pixels == image.pixels);
        }

    }  // namespace

    TEST(SpriteImage, RoundTripKeepsEveryPixel) {
        ExpectRoundTrip(MakeImage(5, 3));
    }

    TEST(SpriteImage, ASideThatIsNotAMultipleOfFourAndOnePixelRoundTrip) {
        ExpectRoundTrip(MakeImage(7, 5));
        ExpectRoundTrip(MakeImage(1, 1));
        ExpectRoundTrip(MakeImage(1, 9));
    }

    TEST(SpriteImage, TheSameImageGivesTheSameBytes) {
        const RgbaImage image = MakeImage(5, 3);
        TempFile first;
        TempFile second;
        ASSERT_TRUE(SaveRgbaPng(first.Path(), image).has_value());
        ASSERT_TRUE(SaveRgbaPng(second.Path(), image).has_value());
        const auto a = ReadBytes(first.Path());
        const auto b = ReadBytes(second.Path());
        EXPECT_FALSE(a.empty());
        EXPECT_TRUE(a == b);
    }

    TEST(SpriteImage, TheFileIsAPngOfTheImageSize) {
        TempFile file;
        ASSERT_TRUE(SaveRgbaPng(file.Path(), MakeImage(5, 3)).has_value());
        const auto bytes = ReadBytes(file.Path());
        ASSERT_GE(bytes.size(), kPngSignature.size());
        EXPECT_TRUE(std::equal(kPngSignature.begin(), kPngSignature.end(), bytes.begin()));
        const auto size = ReadPngSize(file.Path());
        ASSERT_TRUE(size.has_value());
        EXPECT_EQ(size->first, 5u);
        EXPECT_EQ(size->second, 3u);
    }

    TEST(SpriteImage, AMissingFileIsAnError) {
        TempFile file;
        const auto loaded = LoadRgbaImage(file.Path());
        ASSERT_FALSE(loaded.has_value());
        EXPECT_FALSE(loaded.error().empty());
    }

    TEST(SpriteImage, AFileOfTextIsNotAnImage) {
        TempFile file(".txt");
        WriteFile(file.Path(), "this is not a picture");
        const auto loaded = LoadRgbaImage(file.Path());
        ASSERT_FALSE(loaded.has_value());
        EXPECT_FALSE(loaded.error().empty());
    }

    TEST(SpriteImage, AnEmptyImageIsRefused) {
        TempFile file;
        EXPECT_FALSE(SaveRgbaPng(file.Path(), RgbaImage{}).has_value());
        RgbaImage noHeight;
        noHeight.width = 4;
        EXPECT_FALSE(SaveRgbaPng(file.Path(), noHeight).has_value());
        EXPECT_FALSE(std::filesystem::exists(file.Path()));
    }

    TEST(SpriteImage, APixelCountThatDoesNotMatchTheSizeIsRefused) {
        TempFile file;
        RgbaImage image = MakeImage(5, 3);
        image.pixels.pop_back();
        EXPECT_FALSE(SaveRgbaPng(file.Path(), image).has_value());
        image.pixels.resize(5 * 3 * kBytesPerPixel + kBytesPerPixel);
        EXPECT_FALSE(SaveRgbaPng(file.Path(), image).has_value());
        EXPECT_FALSE(std::filesystem::exists(file.Path()));
    }

    TEST(SpriteImage, ReplaceOverAnExistingFileGivesTheBytesOfASave) {
        TempFile target;
        TempFile reference;
        ASSERT_TRUE(SaveRgbaPng(target.Path(), MakeImage(2, 2)).has_value());
        const RgbaImage image = MakeImage(5, 3);
        ASSERT_TRUE(SaveRgbaPng(reference.Path(), image).has_value());

        const auto replaced = ReplaceRgbaPng(target.Path(), image);
        ASSERT_TRUE(replaced.has_value()) << replaced.error().c_str();
        EXPECT_TRUE(ReadBytes(target.Path()) == ReadBytes(reference.Path()));

        std::filesystem::path temporary = target.Path();
        temporary += ".tmp";
        EXPECT_FALSE(std::filesystem::exists(temporary));
    }

    TEST(SpriteImage, ReplaceCreatesTheFileWhenThereIsNone) {
        TempFile target;
        ASSERT_TRUE(ReplaceRgbaPng(target.Path(), MakeImage(3, 3)).has_value());
        const auto loaded = LoadRgbaImage(target.Path());
        ASSERT_TRUE(loaded.has_value());
        EXPECT_TRUE(loaded->pixels == MakeImage(3, 3).pixels);
    }

    TEST(SpriteImage, ASaveAndAReplaceUnderANonAsciiFolderAndNameRoundTrip) {
        TestTempFolder folder("SpriteImageUtf8");
        const std::filesystem::path target = std::filesystem::path(ToStd(folder.PathOf("caf\xC3\xA9"))) / "pi\xC3\xA8"
                                                                                                          "ce.png";
        std::filesystem::create_directories(target.parent_path());
        const RgbaImage image = MakeImage(4, 3);

        ASSERT_TRUE(SaveRgbaPng(target, image).has_value());
        auto loaded = LoadRgbaImage(target);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().c_str();
        EXPECT_TRUE(loaded->pixels == image.pixels);

        const RgbaImage other = MakeImage(5, 5);
        const auto replaced = ReplaceRgbaPng(target, other);
        ASSERT_TRUE(replaced.has_value()) << replaced.error().c_str();
        loaded = LoadRgbaImage(target);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().c_str();
        EXPECT_TRUE(loaded->pixels == other.pixels);
        EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(target) += ".tmp"));
    }

    TEST(SpriteImage, ReplaceIntoAFolderThatDoesNotExistIsAnError) {
        TempFile folder(".dir");
        const std::filesystem::path target = folder.Path() / "atlas.png";
        const auto replaced = ReplaceRgbaPng(target, MakeImage(2, 2));
        ASSERT_FALSE(replaced.has_value());
        EXPECT_FALSE(replaced.error().empty());
        EXPECT_FALSE(std::filesystem::exists(target));
    }

    TEST(SpriteImage, AnImageOverTheSideLimitIsRefusedByTheLoader) {
        TempFile file;
        RgbaImage tall;
        tall.width = 1;
        tall.height = kMaxSpriteImageSide + 1;
        tall.pixels.assign(static_cast<size_t>(tall.height) * kBytesPerPixel, 255);
        ASSERT_TRUE(SaveRgbaPng(file.Path(), tall).has_value());
        const auto loaded = LoadRgbaImage(file.Path());
        ASSERT_FALSE(loaded.has_value());
        EXPECT_FALSE(loaded.error().empty());
    }

    TEST(SpriteImage, AnImageOfTheSideLimitIsLoaded) {
        TempFile file;
        RgbaImage tall;
        tall.width = 1;
        tall.height = kMaxSpriteImageSide;
        tall.pixels.assign(static_cast<size_t>(tall.height) * kBytesPerPixel, 255);
        ASSERT_TRUE(SaveRgbaPng(file.Path(), tall).has_value());
        const auto loaded = LoadRgbaImage(file.Path());
        ASSERT_TRUE(loaded.has_value()) << loaded.error().c_str();
        EXPECT_EQ(loaded->height, kMaxSpriteImageSide);
    }

    TEST(SpriteImage, APartlyTransparentPixelKeepsItsColour) {
        RgbaImage image;
        image.width = 3;
        image.height = 1;
        image.pixels = {200, 100, 50, 128, 200, 100, 50, 1, 200, 100, 50, 254};
        TempFile file;
        ASSERT_TRUE(SaveRgbaPng(file.Path(), image).has_value());
        const auto loaded = LoadRgbaImage(file.Path());
        ASSERT_TRUE(loaded.has_value()) << loaded.error().c_str();
        EXPECT_TRUE(loaded->pixels == image.pixels);
        EXPECT_TRUE(loaded->hasAlpha);
    }

    TEST(SpriteImage, TheGeneratePanelReadsThePixelsOfTheFileToo) {
        ArtGenPixels pixels;
        pixels.width = 2;
        pixels.height = 1;
        pixels.rgba = {200, 100, 50, 128, 200, 100, 50, 1};
        TempFile file;
        ASSERT_TRUE(WriteArtGenPng(file.Path(), pixels).has_value());
        const auto loaded = ReadArtGenPng(file.Path());
        ASSERT_TRUE(loaded.has_value()) << loaded.error().c_str();
        EXPECT_TRUE(loaded->rgba == pixels.rgba);
        EXPECT_TRUE(loaded->hasAlpha);
    }

    TEST(SpriteImage, AFileThatIsNotAPngIsStillDecoded) {
        Array<uint8_t, 8> rows = {10, 20, 30, 0, 40, 50, 60, 0};
        SDL_Surface* surface = SDL_CreateSurfaceFrom(1, 2, SDL_PIXELFORMAT_RGB24, rows.data(), 4);
        ASSERT_NE(surface, nullptr);
        TempFile file(".bmp");
        const auto utf8 = file.Path().u8string();  //-NoStd: SDL takes the path as UTF-8 chars
        const bool saved = SDL_SaveBMP(surface, reinterpret_cast<const char*>(utf8.c_str()));
        SDL_DestroySurface(surface);
        ASSERT_TRUE(saved);

        const auto loaded = LoadRgbaImage(file.Path());
        ASSERT_TRUE(loaded.has_value()) << loaded.error().c_str();
        const Vector<uint8_t> expected = {10, 20, 30, 255, 40, 50, 60, 255};
        EXPECT_TRUE(loaded->pixels == expected);
        EXPECT_FALSE(loaded->hasAlpha);
    }

    TEST(SpriteImage, APngCutInHalfIsAnError) {
        TempFile whole;
        ASSERT_TRUE(SaveRgbaPng(whole.Path(), MakeImage(16, 16)).has_value());
        const auto bytes = ReadBytes(whole.Path());
        TempFile cut;
        WriteFile(cut.Path(), StringView(reinterpret_cast<const char*>(bytes.data()), bytes.size() / 2));
        const auto loaded = LoadRgbaImage(cut.Path());
        ASSERT_FALSE(loaded.has_value());
        EXPECT_FALSE(loaded.error().empty());
    }

}  // namespace BECore::Tests
