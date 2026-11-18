#include "SpriteImage.h"

#include <BECore/Utils/ScopeGuard.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

namespace BECore {

    namespace {

        constexpr const char* kReplaceTemporarySuffix = ".tmp";

        bool PixelCountMatches(const RgbaImage& image) {
            return image.pixels.size() == static_cast<size_t>(image.width) * image.height * kBytesPerPixel;
        }

    }  // namespace

    std::expected<RgbaImage, String> LoadRgbaImage(const std::filesystem::path& file) {
        return ReadImageFile(file, kMaxSpriteImageSide);
    }

    std::expected<void, String> SaveRgbaPng(const std::filesystem::path& file, const RgbaImage& image) {
        if (image.width == 0 || image.height == 0 || !PixelCountMatches(image)) {
            return std::unexpected(Format("the {}x{} image has {} bytes of pixels", image.width, image.height, image.pixels.size()));
        }

        // SDL takes writable pixels; the encoder only reads them, so the image is not changed
        void* pixels = const_cast<uint8_t*>(image.pixels.data());
        SDL_Surface* surface = SDL_CreateSurfaceFrom(static_cast<int>(image.width), static_cast<int>(image.height), SDL_PIXELFORMAT_RGBA32, pixels, static_cast<int>(image.width * kBytesPerPixel));
        if (surface == nullptr) {
            return std::unexpected(Format("cannot make a surface for the image: {}", SDL_GetError()));
        }
        ScopeGuard surfaceGuard([surface]() { SDL_DestroySurface(surface); });

        const auto utf8 = file.u8string();  //-NoStd: SDL_image takes the path as UTF-8 chars
        if (!IMG_SavePNG(surface, reinterpret_cast<const char*>(utf8.c_str()))) {
            return std::unexpected(Format("cannot write the PNG: {}", SDL_GetError()));
        }
        return {};
    }

    std::expected<void, String> ReplaceRgbaPng(const std::filesystem::path& file, const RgbaImage& image) {
        std::filesystem::path temporary = file;
        temporary += kReplaceTemporarySuffix;

        if (auto saved = SaveRgbaPng(temporary, image); !saved) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return saved;
        }
        std::error_code error;
        std::filesystem::rename(temporary, file, error);
        if (error) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return std::unexpected(Format("cannot replace the PNG: {}", error.message()));
        }
        return {};
    }

}  // namespace BECore
