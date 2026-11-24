#include "SpriteImage.h"

#include <BECore/File/File.h>
#include <BECore/Utils/ScopeGuard.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

namespace BECore {

    namespace {

        bool PixelCountMatches(const RgbaImage& image) {
            return image.pixels.size() == static_cast<size_t>(image.width) * image.height * kBytesPerPixel;
        }

    }  // namespace

    std::expected<RgbaImage, String> LoadRgbaImage(const Path& file) {
        return ReadImageFile(file, kMaxSpriteImageSide);
    }

    std::expected<void, String> SaveRgbaPng(const Path& file, const RgbaImage& image) {
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

        if (!IMG_SavePNG(surface, file.CStr())) {
            return std::unexpected(Format("cannot write the PNG: {}", SDL_GetError()));
        }
        return {};
    }

    std::expected<void, String> ReplaceRgbaPng(const Path& file, const RgbaImage& image) {
        const Path temporary(Format("{}{}", file.View(), kAtomicTempSuffix));

        if (auto saved = SaveRgbaPng(temporary, image); !saved) {
            static_cast<void>(RemoveFile(temporary));
            return saved;
        }
        if (const auto renamed = RenameFile(temporary, file); !renamed) {
            static_cast<void>(RemoveFile(temporary));
            return std::unexpected(Format("cannot replace the PNG: {}", renamed.error()));
        }
        return {};
    }

}  // namespace BECore
