#include <Common/TempPath.h>
#include <CoreSDL/ImageFile.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kPixelBytes = static_cast<int>(kBytesPerPixel);

        /// A PNG of @p pixels under the temp folder, removed with the object
        class TempPng {
        public:
            TempPng(int width, int height, Vector<uint8_t>& pixels) : _path(TestTempDirectory("ImageFile")) {
                _path += ".png";
                SDL_Surface* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels.data(), width * kPixelBytes);
                const auto utf8 = _path.u8string();  //-NoStd: SDL_image takes the path as UTF-8 chars
                _saved = surface != nullptr && IMG_SavePNG(surface, reinterpret_cast<const char*>(utf8.c_str()));
                SDL_DestroySurface(surface);
            }
            ~TempPng() {
                std::error_code ignored;
                std::filesystem::remove(_path, ignored);
            }
            TempPng(const TempPng&) = delete;
            TempPng& operator=(const TempPng&) = delete;

            [[nodiscard]] const std::filesystem::path& Path() const {
                return _path;
            }
            [[nodiscard]] bool Saved() const {
                return _saved;
            }

        private:
            std::filesystem::path _path;
            bool _saved = false;
        };

    }  // namespace

    TEST(ImageFile, APartlyTransparentPixelOfAPngKeepsItsColour) {
        Vector<uint8_t> pixels = {200, 100, 50, 255, 200, 100, 50, 128, 200, 100, 50, 1, 200, 100, 50, 254};
        const TempPng file(4, 1, pixels);
        ASSERT_TRUE(file.Saved());

        const auto image = ReadImageFile(file.Path());
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

        EXPECT_FALSE(ReadImageFile(file.Path(), 2).has_value());
        EXPECT_TRUE(ReadImageFile(file.Path(), 3).has_value());
        EXPECT_TRUE(ReadImageFile(file.Path()).has_value());
    }

    TEST(ImageFile, AMissingFileIsAnError) {
        const std::filesystem::path missing = TestTempDirectory("ImageFileMissing");
        const auto image = ReadImageFile(missing);
        ASSERT_FALSE(image.has_value());
        EXPECT_FALSE(image.error().empty());
    }

}  // namespace BECore::Tests
