#include "SpriteImage.h"

#include <BECore/Utils/ScopeGuard.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

namespace BECore {

    namespace {

        constexpr size_t kBytesPerPixel = 4;

        bool PixelCountMatches(const RgbaImage& image) {
            return image.pixels.size() == static_cast<size_t>(image.width) * image.height * kBytesPerPixel;
        }

    }  // namespace

    std::expected<RgbaImage, String> LoadRgbaImage(const std::filesystem::path& file) {
        const auto utf8 = file.u8string();  //-NoStd: SDL_image takes the path as UTF-8 chars
        SDL_Surface* loaded = IMG_Load(reinterpret_cast<const char*>(utf8.c_str()));
        if (loaded == nullptr) {
            return std::unexpected(Format("cannot read the image: {}", SDL_GetError()));
        }
        ScopeGuard loadedGuard([loaded]() { SDL_DestroySurface(loaded); });

        if (loaded->w <= 0 || loaded->h <= 0) {
            return std::unexpected(String("the image is empty"));
        }
        // Checked before the conversion allocates a second copy of a huge picture
        if (static_cast<uint32_t>(loaded->w) > kMaxSpriteImageSide || static_cast<uint32_t>(loaded->h) > kMaxSpriteImageSide) {
            return std::unexpected(Format("the {}x{} image is over {} pixels on a side", loaded->w, loaded->h, kMaxSpriteImageSide));
        }

        SDL_Surface* converted = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
        if (converted == nullptr) {
            return std::unexpected(Format("cannot convert the image to RGBA: {}", SDL_GetError()));
        }
        ScopeGuard convertedGuard([converted]() { SDL_DestroySurface(converted); });

        RgbaImage image;
        image.width = static_cast<uint32_t>(converted->w);
        image.height = static_cast<uint32_t>(converted->h);
        const size_t rowBytes = static_cast<size_t>(image.width) * kBytesPerPixel;
        image.pixels.resize(rowBytes * image.height);
        const auto* source = static_cast<const uint8_t*>(converted->pixels);
        // The surface rows are `pitch` bytes apart, which can be more than the width needs
        for (uint32_t row = 0; row < image.height; ++row) {
            std::copy_n(source + static_cast<size_t>(row) * static_cast<size_t>(converted->pitch), rowBytes, image.pixels.data() + static_cast<size_t>(row) * rowBytes);
        }
        return image;
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

}  // namespace BECore
