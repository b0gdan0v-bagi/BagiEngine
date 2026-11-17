#include <Common/TempPath.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/TextureEditor/SpriteImage.h>
#include <fstream>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kBytesPerPixel = 4;
        constexpr Array<uint8_t, 8> kPngSignature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

        /// Every pixel differs: alpha runs through 255, 128, 1 and one pixel is fully transparent
        RgbaImage MakeImage(uint32_t width, uint32_t height) {
            RgbaImage image;
            image.width = width;
            image.height = height;
            image.pixels.resize(static_cast<size_t>(width) * height * kBytesPerPixel);
            constexpr Array<uint8_t, 3> alphas = {255, 128, 1};
            for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
                image.pixels[i * 4 + 0] = static_cast<uint8_t>(10 + i * 7);
                image.pixels[i * 4 + 1] = static_cast<uint8_t>(200 - i * 5);
                image.pixels[i * 4 + 2] = static_cast<uint8_t>(i * 13 + 1);
                image.pixels[i * 4 + 3] = alphas[i % alphas.size()];
            }
            if (width * height > 1) {
                image.pixels[3] = 0;  // the first pixel is transparent
            }
            return image;
        }

        Vector<uint8_t> ReadBytes(const std::filesystem::path& file) {
            std::ifstream stream(file, std::ios::binary | std::ios::ate);  //-NoStd: a test reads the file back
            Vector<uint8_t> bytes(static_cast<size_t>(std::max<std::streamoff>(stream.tellg(), 0)));
            stream.seekg(0);
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return bytes;
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
        {
            std::ofstream stream(file.Path(), std::ios::binary);  //-NoStd: a test writes the file
            stream << "this is not a picture";
        }
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

}  // namespace BECore::Tests
