#include "SDLScreenshot.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Utils/Base64.h>
#include <BECore/Utils/ScopeGuard.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

namespace BECore {

    namespace {

        constexpr size_t kBytesPerPixel = 4;

        /// A surface that holds a copy of @p image (SDL wants writable pixels, and the copy lets it own them)
        SDL_Surface* MakeSurface(const PixelBuffer& image) {
            SDL_Surface* surface = SDL_CreateSurface(static_cast<int>(image.width), static_cast<int>(image.height), SDL_PIXELFORMAT_RGBA32);
            if (surface == nullptr) {
                return nullptr;
            }
            const size_t rowBytes = static_cast<size_t>(image.width) * kBytesPerPixel;
            auto* target = static_cast<uint8_t*>(surface->pixels);
            for (uint32_t row = 0; row < image.height; ++row) {
                std::copy_n(image.rgba.data() + static_cast<size_t>(row) * rowBytes, rowBytes, target + static_cast<size_t>(row) * static_cast<size_t>(surface->pitch));
            }
            return surface;
        }

        /// The size @p width by @p height gets when its longer side is cut to @p maxSide (each side is at least 1 pixel)
        std::pair<int, int> ScaledSize(uint32_t width, uint32_t height, uint32_t maxSide) {
            const uint32_t longer = std::max(width, height);
            if (maxSide == 0 || longer <= maxSide) {
                return {static_cast<int>(width), static_cast<int>(height)};
            }
            const double factor = static_cast<double>(maxSide) / static_cast<double>(longer);
            const auto scale = [factor](uint32_t side) { return std::max(1, static_cast<int>(std::lround(static_cast<double>(side) * factor))); };
            return {scale(width), scale(height)};
        }

        void HandleScreenshot(ControlCall& call) {
            uint32_t maxSize = kDefaultScreenshotSize;
            if (FindJsonMember(call.Params(), "maxSize") != nullptr) {
                const auto requested = GetJsonInt(call.Params(), "maxSize");
                if (!requested || *requested < kMinScreenshotSize || *requested > kMaxScreenshotSize) {
                    call.Fail(ControlErrors::kInvalidParams, Format("'maxSize' has to be an integer from {} to {}", kMinScreenshotSize, kMaxScreenshotSize));
                    return;
                }
                maxSize = static_cast<uint32_t>(*requested);
            }

            const auto& renderer = CoreManager::GetRenderer();
            if (!renderer) {
                call.Fail(ScreenshotErrors::kCaptureFailed, "there is no renderer");
                return;
            }

            // The renderer fills it when the frame is presented; the answer is built at the end of that frame
            auto capture = eastl::make_shared<Optional<CaptureResult>>();
            renderer->RequestCapture([capture](CaptureResult result) { *capture = std::move(result); });
            if (*capture && !**capture) {  // refused at once: this backend cannot capture
                call.Fail(ScreenshotErrors::kCaptureFailed, (*capture)->error());
                return;
            }

            call.DeferFrames(1, 0.0, [capture, maxSize](ControlCall& done) {
                if (!*capture) {
                    done.Fail(ScreenshotErrors::kCaptureFailed, "no frame was presented");
                    return;
                }
                if (!**capture) {
                    done.Fail(ScreenshotErrors::kCaptureFailed, (*capture)->error());
                    return;
                }
                const auto png = EncodePng(***capture, maxSize);
                if (!png) {
                    done.Fail(ScreenshotErrors::kCaptureFailed, png.error());
                    return;
                }

                Json result = Json::object();
                result["width"] = png->width;
                result["height"] = png->height;
                result["png_base64"] = MakeJsonString(Base64Encode(eastl::span<const uint8_t>(png->bytes.data(), png->bytes.size())));
                done.Reply(std::move(result));
            });
        }

    }  // namespace

    std::expected<PngImage, String> EncodePng(const PixelBuffer& image, uint32_t maxSide) {
        if (image.width == 0 || image.height == 0 || image.rgba.size() != static_cast<size_t>(image.width) * image.height * kBytesPerPixel) {
            return std::unexpected(Format("the {}x{} image has {} bytes of pixels", image.width, image.height, image.rgba.size()));
        }

        SDL_Surface* surface = MakeSurface(image);
        if (surface == nullptr) {
            return std::unexpected(Format("cannot make a surface for the picture: {}", SDL_GetError()));
        }
        ScopeGuard surfaceGuard([&surface]() { SDL_DestroySurface(surface); });

        const auto [width, height] = ScaledSize(image.width, image.height, maxSide);
        if (width != surface->w || height != surface->h) {
            SDL_Surface* scaled = SDL_ScaleSurface(surface, width, height, SDL_SCALEMODE_LINEAR);
            if (scaled == nullptr) {
                return std::unexpected(Format("cannot scale the picture to {}x{}: {}", width, height, SDL_GetError()));
            }
            SDL_DestroySurface(surface);
            surface = scaled;  // the guard destroys the scaled one
        }

        SDL_IOStream* stream = SDL_IOFromDynamicMem();
        if (stream == nullptr) {
            return std::unexpected(Format("cannot open a memory stream: {}", SDL_GetError()));
        }
        // closeio is false: the memory belongs to the stream until SDL_CloseIO, and the size is read first
        const bool saved = IMG_SavePNG_IO(surface, stream, false);
        PngImage png;
        png.width = static_cast<uint32_t>(surface->w);
        png.height = static_cast<uint32_t>(surface->h);
        if (saved) {
            const auto size = SDL_GetIOSize(stream);
            const auto* memory = static_cast<const uint8_t*>(SDL_GetPointerProperty(SDL_GetIOProperties(stream), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
            if (size > 0 && memory != nullptr) {
                png.bytes.assign(memory, memory + size);
            }
        }
        const String failure = saved ? String() : Format("cannot encode the PNG: {}", SDL_GetError());
        SDL_CloseIO(stream);

        if (!saved) {
            return std::unexpected(failure);
        }
        if (png.bytes.empty()) {
            return std::unexpected(String("the PNG encoder produced no bytes"));
        }
        return png;
    }

    void RegisterScreenshotControl(ControlService& service) {
        service.RegisterHandler("screenshot", HandleScreenshot);
    }

}  // namespace BECore
