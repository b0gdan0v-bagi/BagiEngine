#include "ImageFile.h"

#include <BECore/Profiler/Alloc/AllocRouting.h>
#include <BECore/Utils/ScopeGuard.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <climits>

// The one translation unit that holds stb_image (src/Modules/stb/README.md). Every function is static, so the copy inside SDL_image links beside it
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_FAILURE_USERMSG
#define STBI_MALLOC(size) ::BECore::AllocRouting::Malloc(::BECore::AllocSource::Stb, (size))
#define STBI_REALLOC(ptr, size) ::BECore::AllocRouting::Realloc(::BECore::AllocSource::Stb, (ptr), (size))
#define STBI_FREE(ptr) ::BECore::AllocRouting::Free(::BECore::AllocSource::Stb, (ptr))
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_image.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace BECore {

    namespace {

        constexpr Array<uint8_t, 8> kPngSignature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        constexpr int kGreyAlphaChannels = 2;
        constexpr int kRgbaChannels = 4;

        std::expected<void, String> CheckSides(int width, int height, uint32_t maxSide) {
            if (width <= 0 || height <= 0) {
                return std::unexpected(String("the image is empty"));
            }
            if (static_cast<uint32_t>(width) > maxSide || static_cast<uint32_t>(height) > maxSide) {
                return std::unexpected(Format("the {}x{} image is over {} pixels on a side", width, height, maxSide));
            }
            return {};
        }

        bool IsPng(const uint8_t* bytes, size_t size) {
            return size >= kPngSignature.size() && std::equal(kPngSignature.begin(), kPngSignature.end(), bytes);
        }

        // SDL_image on Apple decodes a PNG through ImageIO, which premultiplies the colour by the alpha and never reverses it, so the colour of every
        // partly transparent pixel would differ from the file and from the other platforms. stb_image gives the file's own bytes everywhere
        std::expected<RgbaImage, String> DecodePng(const uint8_t* bytes, size_t size, uint32_t maxSide) {
            if (size > static_cast<size_t>(INT_MAX)) {
                return std::unexpected(String("the PNG file is too large"));
            }
            const int length = static_cast<int>(size);
            int width = 0;
            int height = 0;
            int channels = 0;
            if (stbi_info_from_memory(bytes, length, &width, &height, &channels) == 0) {
                return std::unexpected(Format("cannot read the PNG: {}", stbi_failure_reason()));
            }
            if (auto sides = CheckSides(width, height, maxSide); !sides) {
                return std::unexpected(std::move(sides.error()));
            }

            int decodedChannels = 0;
            stbi_uc* decoded = stbi_load_from_memory(bytes, length, &width, &height, &decodedChannels, static_cast<int>(kBytesPerPixel));
            if (decoded == nullptr) {
                return std::unexpected(Format("cannot decode the PNG: {}", stbi_failure_reason()));
            }
            ScopeGuard decodedGuard([decoded]() { stbi_image_free(decoded); });

            RgbaImage image;
            image.width = static_cast<uint32_t>(width);
            image.height = static_cast<uint32_t>(height);
            image.hasAlpha = channels == kGreyAlphaChannels || channels == kRgbaChannels;
            image.pixels.assign(decoded, decoded + static_cast<size_t>(image.width) * image.height * kBytesPerPixel);
            return image;
        }

        std::expected<RgbaImage, String> DecodeThroughSdlImage(const char* utf8Path, uint32_t maxSide) {
            SDL_Surface* loaded = IMG_Load(utf8Path);
            if (loaded == nullptr) {
                return std::unexpected(Format("cannot read the image: {}", SDL_GetError()));
            }
            ScopeGuard loadedGuard([loaded]() { SDL_DestroySurface(loaded); });

            if (auto sides = CheckSides(loaded->w, loaded->h, maxSide); !sides) {
                return std::unexpected(std::move(sides.error()));
            }

            bool hasAlpha = SDL_ISPIXELFORMAT_ALPHA(loaded->format) || SDL_SurfaceHasColorKey(loaded);
            if (const SDL_Palette* palette = SDL_GetSurfacePalette(loaded); palette != nullptr) {
                for (int i = 0; i < palette->ncolors; ++i) {
                    hasAlpha = hasAlpha || palette->colors[i].a != SDL_ALPHA_OPAQUE;
                }
            }

            SDL_Surface* converted = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
            if (converted == nullptr) {
                return std::unexpected(Format("cannot convert the image to RGBA: {}", SDL_GetError()));
            }
            ScopeGuard convertedGuard([converted]() { SDL_DestroySurface(converted); });

            RgbaImage image;
            image.width = static_cast<uint32_t>(converted->w);
            image.height = static_cast<uint32_t>(converted->h);
            image.hasAlpha = hasAlpha;
            const size_t rowBytes = static_cast<size_t>(image.width) * kBytesPerPixel;
            image.pixels.resize(rowBytes * image.height);
            const auto* source = static_cast<const uint8_t*>(converted->pixels);
            // The surface rows are `pitch` bytes apart, which can be more than the width needs
            for (uint32_t row = 0; row < image.height; ++row) {
                std::copy_n(source + static_cast<size_t>(row) * static_cast<size_t>(converted->pitch), rowBytes, image.pixels.data() + static_cast<size_t>(row) * rowBytes);
            }
            return image;
        }

    }  // namespace

    std::expected<RgbaImage, String> ReadImageFile(const Path& file, uint32_t maxSide) {
        const char* path = file.CStr();

        size_t size = 0;
        void* bytes = SDL_LoadFile(path, &size);
        if (bytes == nullptr) {
            return std::unexpected(Format("cannot read the image: {}", SDL_GetError()));
        }
        ScopeGuard bytesGuard([bytes]() { SDL_free(bytes); });

        const auto* data = static_cast<const uint8_t*>(bytes);
        return IsPng(data, size) ? DecodePng(data, size, maxSide) : DecodeThroughSdlImage(path, maxSide);
    }

}  // namespace BECore
