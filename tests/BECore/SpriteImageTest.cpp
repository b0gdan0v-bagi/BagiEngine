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

        Vector<uint8_t> ReadBytes(const Path& file) {
            return ReadFileBytes(file);
        }

        /// A test file under the system temp folder that is removed with the object
        class TempFile {
        public:
            explicit TempFile(const char* extension = ".png") : _path(Format("{}_{}{}", Path(TestTempDirectory("SpriteImage")).View(), ++_counter, extension)) {}
            ~TempFile() {
                RemoveFileIfPresent(_path);
            }
            TempFile(const TempFile&) = delete;
            TempFile& operator=(const TempFile&) = delete;

            [[nodiscard]] const Path& Location() const {
                return _path;
            }

        private:
            // One test makes several files, and TestTempDirectory names them alike
            static inline int _counter = 0;
            Path _path;
        };

        void ExpectRoundTrip(const RgbaImage& image) {
            TempFile file;
            const auto saved = SaveRgbaPng(file.Location(), image);
            ASSERT_TRUE(saved.has_value()) << saved.error().c_str();
            const auto loaded = LoadRgbaImage(file.Location());
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
        ASSERT_TRUE(SaveRgbaPng(first.Location(), image).has_value());
        ASSERT_TRUE(SaveRgbaPng(second.Location(), image).has_value());
        const auto a = ReadBytes(first.Location());
        const auto b = ReadBytes(second.Location());
        EXPECT_FALSE(a.empty());
        EXPECT_TRUE(a == b);
    }

    TEST(SpriteImage, TheFileIsAPngOfTheImageSize) {
        TempFile file;
        ASSERT_TRUE(SaveRgbaPng(file.Location(), MakeImage(5, 3)).has_value());
        const auto bytes = ReadBytes(file.Location());
        ASSERT_GE(bytes.size(), kPngSignature.size());
        EXPECT_TRUE(std::equal(kPngSignature.begin(), kPngSignature.end(), bytes.begin()));
        const auto size = ReadPngSize(file.Location());
        ASSERT_TRUE(size.has_value());
        EXPECT_EQ(size->first, 5u);
        EXPECT_EQ(size->second, 3u);
    }

    TEST(SpriteImage, AMissingFileIsAnError) {
        TempFile file;
        const auto loaded = LoadRgbaImage(file.Location());
        ASSERT_FALSE(loaded.has_value());
        EXPECT_FALSE(loaded.error().empty());
    }

    TEST(SpriteImage, AFileOfTextIsNotAnImage) {
        TempFile file(".txt");
        WriteFile(file.Location(), "this is not a picture");
        const auto loaded = LoadRgbaImage(file.Location());
        ASSERT_FALSE(loaded.has_value());
        EXPECT_FALSE(loaded.error().empty());
    }

    TEST(SpriteImage, AnEmptyImageIsRefused) {
        TempFile file;
        EXPECT_FALSE(SaveRgbaPng(file.Location(), RgbaImage{}).has_value());
        RgbaImage noHeight;
        noHeight.width = 4;
        EXPECT_FALSE(SaveRgbaPng(file.Location(), noHeight).has_value());
        EXPECT_FALSE(PathExists(file.Location()));
    }

    TEST(SpriteImage, APixelCountThatDoesNotMatchTheSizeIsRefused) {
        TempFile file;
        RgbaImage image = MakeImage(5, 3);
        image.pixels.pop_back();
        EXPECT_FALSE(SaveRgbaPng(file.Location(), image).has_value());
        image.pixels.resize(5 * 3 * kBytesPerPixel + kBytesPerPixel);
        EXPECT_FALSE(SaveRgbaPng(file.Location(), image).has_value());
        EXPECT_FALSE(PathExists(file.Location()));
    }

    TEST(SpriteImage, ReplaceOverAnExistingFileGivesTheBytesOfASave) {
        TempFile target;
        TempFile reference;
        ASSERT_TRUE(SaveRgbaPng(target.Location(), MakeImage(2, 2)).has_value());
        const RgbaImage image = MakeImage(5, 3);
        ASSERT_TRUE(SaveRgbaPng(reference.Location(), image).has_value());

        const auto replaced = ReplaceRgbaPng(target.Location(), image);
        ASSERT_TRUE(replaced.has_value()) << replaced.error().c_str();
        EXPECT_TRUE(ReadBytes(target.Location()) == ReadBytes(reference.Location()));

        EXPECT_FALSE(PathExists(Path(Format("{}.tmp", target.Location().View()))));
    }

    TEST(SpriteImage, ReplaceCreatesTheFileWhenThereIsNone) {
        TempFile target;
        ASSERT_TRUE(ReplaceRgbaPng(target.Location(), MakeImage(3, 3)).has_value());
        const auto loaded = LoadRgbaImage(target.Location());
        ASSERT_TRUE(loaded.has_value());
        EXPECT_TRUE(loaded->pixels == MakeImage(3, 3).pixels);
    }

    TEST(SpriteImage, ASaveAndAReplaceUnderANonAsciiFolderAndNameRoundTrip) {
        TestTempFolder folder("SpriteImageUtf8");
        const Path target = Path(folder.PathOf("caf\xC3\xA9")) / "pi\xC3\xA8"
                                                                 "ce.png";
        MakeDirectory(target.Parent());
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
        EXPECT_FALSE(PathExists(Path(Format("{}.tmp", target.View()))));
    }

    TEST(SpriteImage, ReplaceIntoAFolderThatDoesNotExistIsAnError) {
        TempFile folder(".dir");
        const Path target = folder.Location() / "atlas.png";
        const auto replaced = ReplaceRgbaPng(target, MakeImage(2, 2));
        ASSERT_FALSE(replaced.has_value());
        EXPECT_FALSE(replaced.error().empty());
        EXPECT_FALSE(PathExists(target));
    }

    TEST(SpriteImage, AnImageOverTheSideLimitIsRefusedByTheLoader) {
        TempFile file;
        RgbaImage tall;
        tall.width = 1;
        tall.height = kMaxSpriteImageSide + 1;
        tall.pixels.assign(static_cast<size_t>(tall.height) * kBytesPerPixel, 255);
        ASSERT_TRUE(SaveRgbaPng(file.Location(), tall).has_value());
        const auto loaded = LoadRgbaImage(file.Location());
        ASSERT_FALSE(loaded.has_value());
        EXPECT_FALSE(loaded.error().empty());
    }

    TEST(SpriteImage, AnImageOfTheSideLimitIsLoaded) {
        TempFile file;
        RgbaImage tall;
        tall.width = 1;
        tall.height = kMaxSpriteImageSide;
        tall.pixels.assign(static_cast<size_t>(tall.height) * kBytesPerPixel, 255);
        ASSERT_TRUE(SaveRgbaPng(file.Location(), tall).has_value());
        const auto loaded = LoadRgbaImage(file.Location());
        ASSERT_TRUE(loaded.has_value()) << loaded.error().c_str();
        EXPECT_EQ(loaded->height, kMaxSpriteImageSide);
    }

    TEST(SpriteImage, APartlyTransparentPixelKeepsItsColour) {
        RgbaImage image;
        image.width = 3;
        image.height = 1;
        image.pixels = {200, 100, 50, 128, 200, 100, 50, 1, 200, 100, 50, 254};
        TempFile file;
        ASSERT_TRUE(SaveRgbaPng(file.Location(), image).has_value());
        const auto loaded = LoadRgbaImage(file.Location());
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
        ASSERT_TRUE(WriteArtGenPng(file.Location(), pixels).has_value());
        const auto loaded = ReadArtGenPng(file.Location());
        ASSERT_TRUE(loaded.has_value()) << loaded.error().c_str();
        EXPECT_TRUE(loaded->rgba == pixels.rgba);
        EXPECT_TRUE(loaded->hasAlpha);
    }

    TEST(SpriteImage, AFileThatIsNotAPngIsStillDecoded) {
        Array<uint8_t, 8> rows = {10, 20, 30, 0, 40, 50, 60, 0};
        SDL_Surface* surface = SDL_CreateSurfaceFrom(1, 2, SDL_PIXELFORMAT_RGB24, rows.data(), 4);
        ASSERT_NE(surface, nullptr);
        TempFile file(".bmp");
        const bool saved = SDL_SaveBMP(surface, file.Location().CStr());
        SDL_DestroySurface(surface);
        ASSERT_TRUE(saved);

        const auto loaded = LoadRgbaImage(file.Location());
        ASSERT_TRUE(loaded.has_value()) << loaded.error().c_str();
        const Vector<uint8_t> expected = {10, 20, 30, 255, 40, 50, 60, 255};
        EXPECT_TRUE(loaded->pixels == expected);
        EXPECT_FALSE(loaded->hasAlpha);
    }

    TEST(SpriteImage, APngCutInHalfIsAnError) {
        TempFile whole;
        ASSERT_TRUE(SaveRgbaPng(whole.Location(), MakeImage(16, 16)).has_value());
        const auto bytes = ReadBytes(whole.Location());
        TempFile cut;
        WriteFile(cut.Location(), StringView(reinterpret_cast<const char*>(bytes.data()), bytes.size() / 2));
        const auto loaded = LoadRgbaImage(cut.Location());
        ASSERT_FALSE(loaded.has_value());
        EXPECT_FALSE(loaded.error().empty());
    }

}  // namespace BECore::Tests
