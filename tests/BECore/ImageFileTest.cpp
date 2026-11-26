#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <CoreSDL/ImageFile.h>
#include <CoreSDL/SDLScreenshot.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kPixelBytes = static_cast<int>(kBytesPerPixel);

        /// A PNG of @p pixels under the temp folder, removed with the object
        class TempPng {
        public:
            TempPng(int width, int height, Vector<uint8_t>& pixels, const String& name = "ImageFile") : _file(TestTempDirectory(name).ReplaceExtension(".png")) {
                SDL_Surface* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels.data(), width * kPixelBytes);
                _saved = surface != nullptr && IMG_SavePNG(surface, _file.CStr());
                SDL_DestroySurface(surface);
            }
            ~TempPng() {
                RemoveFileIfPresent(_file);
            }
            TempPng(const TempPng&) = delete;
            TempPng& operator=(const TempPng&) = delete;

            [[nodiscard]] const Path& File() const {
                return _file;
            }
            [[nodiscard]] bool Saved() const {
                return _saved;
            }

        private:
            Path _file;
            bool _saved = false;
        };

    }  // namespace

    TEST(ImageFile, APartlyTransparentPixelOfAPngKeepsItsColour) {
        Vector<uint8_t> pixels = {200, 100, 50, 255, 200, 100, 50, 128, 200, 100, 50, 1, 200, 100, 50, 254};
        const TempPng file(4, 1, pixels);
        ASSERT_TRUE(file.Saved());

        const auto image = ReadImageFile(file.File());
        ASSERT_TRUE(image.has_value()) << image.error().c_str();
        EXPECT_EQ(image->width, 4u);
        EXPECT_EQ(image->height, 1u);
        EXPECT_TRUE(image->pixels == pixels);
        EXPECT_TRUE(image->hasAlpha);
    }

    TEST(ImageFile, ASideAboveTheLimitIsRefusedAndNoLimitReadsIt) {
        Vector<uint8_t> pixels(static_cast<size_t>(3) * kBytesPerPixel, 255);
        const TempPng file(3, 1, pixels);
        ASSERT_TRUE(file.Saved());

        EXPECT_FALSE(ReadImageFile(file.File(), 2).has_value());
        EXPECT_TRUE(ReadImageFile(file.File(), 3).has_value());
        EXPECT_TRUE(ReadImageFile(file.File()).has_value());
    }

    TEST(ImageFile, AFileWithANonAsciiNameIsRead) {
        Vector<uint8_t> pixels = {10, 20, 30, 255, 40, 50, 60, 128};
        const TempPng file(2, 1, pixels, "ImageFile_caf\xC3\xA9");
        ASSERT_TRUE(file.Saved());

        const auto image = ReadImageFile(file.File());
        ASSERT_TRUE(image.has_value()) << image.error().c_str();
        EXPECT_EQ(image->width, 2u);
        EXPECT_TRUE(image->pixels == pixels);
    }

    TEST(ImageFile, AMissingFileIsAnError) {
        const Path missing(TestTempDirectory("ImageFileMissing"));
        const auto image = ReadImageFile(missing);
        ASSERT_FALSE(image.has_value());
        EXPECT_TRUE(StringView(image.error()).starts_with("cannot read the image")) << image.error().c_str();
    }

    TEST(ImageFile, APngInMemoryIsDecodedWithinTheSideLimit) {
        const PixelBuffer pixels{3, 1, {200, 100, 50, 255, 1, 2, 3, 128, 4, 5, 6, 1}};
        const auto png = EncodePng(pixels, 0);
        ASSERT_TRUE(png.has_value()) << png.error().c_str();
        const Span<const uint8_t> bytes(png->bytes.data(), png->bytes.size());

        const auto image = DecodePngImage(bytes, 3);
        ASSERT_TRUE(image.has_value()) << image.error().c_str();
        EXPECT_EQ(image->width, 3u);
        EXPECT_EQ(image->height, 1u);
        EXPECT_TRUE(image->pixels == pixels.rgba);
        EXPECT_FALSE(DecodePngImage(bytes, 2).has_value());
    }

    TEST(ImageFile, BytesThatAreNoWholePngAreRefused) {
        const auto png = EncodePng(PixelBuffer{8, 8, Vector<uint8_t>(size_t{8} * 8 * kBytesPerPixel, 7)}, 0);
        ASSERT_TRUE(png.has_value()) << png.error().c_str();

        EXPECT_FALSE(DecodePngImage({}, 8).has_value());
        const Vector<uint8_t> text{'n', 'o', ' ', 'p', 'i', 'c', 't', 'u', 'r', 'e'};
        EXPECT_FALSE(DecodePngImage(Span<const uint8_t>(text.data(), text.size()), 8).has_value());
        // Cut after the signature, inside the header, inside the pixels
        for (const size_t length : {size_t{8}, size_t{20}, png->bytes.size() / 2}) {
            EXPECT_FALSE(DecodePngImage(Span<const uint8_t>(png->bytes.data(), length), 8).has_value()) << length;
        }
    }

    TEST(ImageFile, APngThatInflatesFarPastItsHeaderIsRefused) {
        constexpr uint32_t kSide = 1024;
        auto png = EncodePng(PixelBuffer{kSide, kSide, Vector<uint8_t>(size_t{kSide} * kSide * kBytesPerPixel, 0)}, 0);
        ASSERT_TRUE(png.has_value()) << png.error().c_str();
        // The width and the height of IHDR, big-endian, say 1 by 1; the data stays that of the large picture, and the checksum is not read
        constexpr size_t kWidthAt = 16;
        for (size_t i = 0; i < 8; ++i) {
            png->bytes[kWidthAt + i] = i % 4 == 3 ? 1 : 0;
        }

        const auto image = DecodePngImage(Span<const uint8_t>(png->bytes.data(), png->bytes.size()), kSide);

        EXPECT_FALSE(image.has_value());
    }

}  // namespace BECore::Tests
