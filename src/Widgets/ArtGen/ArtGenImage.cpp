#include "ArtGenImage.h"

#include <BECore/Control/ControlParams.h>
#include <BECore/File/File.h>
#include <BECore/Utils/ScopeGuard.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <Widgets/ArtGen/ArtGenInterimJson.h>
#include <Widgets/TextureEditor/SpriteImage.h>
#include <cmath>

namespace BECore {

    namespace {

        constexpr size_t kChannels = 4;
        constexpr size_t kAlphaOffset = 3;
        constexpr int32_t kPercent = 100;
        constexpr int32_t kHalfPercent = 50;
        constexpr long kMaxByte = 255;

        std::expected<void, String> Check(const ArtGenImageSettings& settings) {
            if (settings.padding < ArtGenImageSettings::kMinPadding || settings.padding > ArtGenImageSettings::kMaxPadding) {
                return std::unexpected(Format("\"padding\" must be {} to {} (percent)", ArtGenImageSettings::kMinPadding, ArtGenImageSettings::kMaxPadding));
            }
            if (settings.maxSize != 0 && (settings.maxSize < ArtGenImageSettings::kMinMaxSize || settings.maxSize > ArtGenImageSettings::kMaxMaxSize)) {
                return std::unexpected(Format("\"maxSize\" must be 0 (no downscale) or {} to {}", ArtGenImageSettings::kMinMaxSize, ArtGenImageSettings::kMaxMaxSize));
            }
            return {};
        }

        /// An integer that fits `int32_t`: `5.0` and `1e1` are not whole numbers here, as they were not to the DOM's `is_number_integer`
        std::expected<int32_t, String> WholeNumber(JsonView object, StringView key, int32_t fallback) {
            const Optional<JsonView> value = FindParam(object, key);
            if (!value) {
                return fallback;
            }
            const Optional<int64_t> number = value->AsInt64();
            if (!number || *number < INT32_MIN || *number > INT32_MAX) {
                return std::unexpected(Format("\"{}\" must be a whole number", key));
            }
            return static_cast<int32_t>(*number);
        }

        size_t ByteCountOf(uint32_t width, uint32_t height) {
            return static_cast<size_t>(width) * height * kChannels;
        }

        /// The source pixels [first, last) an output pixel covers on one axis, with the fraction of each that lies inside it
        struct Window {
            double start = 0.0;
            double end = 0.0;
            int32_t first = 0;
            int32_t last = 0;

            Window(int32_t sourceSize, int32_t outSize, int32_t index)
                : start(static_cast<double>(index) * sourceSize / outSize), end(static_cast<double>(index + 1) * sourceSize / outSize), first(static_cast<int32_t>(std::floor(start))),
                  last(std::min(sourceSize, static_cast<int32_t>(std::ceil(end)))) {}

            [[nodiscard]] float Weight(int32_t source) const {
                return static_cast<float>(std::min(end, static_cast<double>(source) + 1.0) - std::max(start, static_cast<double>(source)));
            }
        };

        uint8_t ToByte(float value) {
            return static_cast<uint8_t>(std::clamp(std::lround(value), 0L, kMaxByte));
        }

    }  // namespace

    void WriteArtGenImageSettings(JsonWriter& writer, const ArtGenImageSettings& settings) {
        writer.BeginObject();
        writer.Member("maxSize", settings.maxSize);
        writer.Member("padding", settings.padding);
        writer.Member("trim", settings.trim);
        writer.EndObject();
    }

    std::expected<ArtGenImageSettings, String> ReadArtGenImageSettings(JsonView value) {
        ArtGenImageSettings settings;
        if (!value.IsValid() || value.IsNull()) {
            return settings;
        }
        if (!value.IsObject()) {
            return std::unexpected(String("the image settings must be an object"));
        }
        if (const Optional<JsonView> trim = FindParam(value, "trim")) {
            const Optional<bool> flag = trim->AsBool();
            if (!flag) {
                return std::unexpected(String("\"trim\" must be a boolean"));
            }
            settings.trim = *flag;
        }
        auto padding = WholeNumber(value, "padding", settings.padding);
        if (!padding) {
            return std::unexpected(padding.error());
        }
        auto maxSize = WholeNumber(value, "maxSize", settings.maxSize);
        if (!maxSize) {
            return std::unexpected(maxSize.error());
        }
        settings.padding = *padding;
        settings.maxSize = *maxSize;
        if (auto checked = Check(settings); !checked) {
            return std::unexpected(checked.error());
        }
        return settings;
    }

    Json ArtGenImageSettingsToJson(const ArtGenImageSettings& settings) {
        return Detail::ArtGenDomOf([&](JsonWriter& writer) { WriteArtGenImageSettings(writer, settings); });
    }

    std::expected<ArtGenImageSettings, String> ArtGenImageSettingsFromJson(const Json& json) {
        return Detail::ArtGenReadDom(json, [](JsonView view) { return ReadArtGenImageSettings(view); });
    }

    Optional<ArtGenIntRect> DrawnBounds(const ArtGenPixels& pixels) {
        if (pixels.width == 0 || pixels.height == 0 || pixels.rgba.size() != ByteCountOf(pixels.width, pixels.height)) {
            return {};
        }
        const auto width = static_cast<int32_t>(pixels.width);
        const auto height = static_cast<int32_t>(pixels.height);
        if (!pixels.hasAlpha) {
            return ArtGenIntRect{0, 0, width, height};
        }
        int32_t minX = width;
        int32_t minY = height;
        int32_t maxX = -1;
        int32_t maxY = -1;
        for (int32_t y = 0; y < height; ++y) {
            const uint8_t* row = pixels.rgba.data() + static_cast<size_t>(y) * pixels.width * kChannels;
            for (int32_t x = 0; x < width; ++x) {
                if (row[static_cast<size_t>(x) * kChannels + kAlphaOffset] > kArtGenAlphaCutoff) {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = y;
                }
            }
        }
        if (maxX < 0) {
            return {};
        }
        return ArtGenIntRect{minX, minY, maxX - minX + 1, maxY - minY + 1};
    }

    std::expected<ArtGenImageFrame, String> MakeArtGenFrame(Span<const ArtGenIntRect> bounds, int32_t imageWidth, int32_t imageHeight, const ArtGenImageSettings& settings) {
        if (auto checked = Check(settings); !checked) {
            return std::unexpected(checked.error());
        }
        if (bounds.empty()) {
            return std::unexpected(String("nothing drawn: the picture is empty"));
        }
        if (imageWidth <= 0 || imageHeight <= 0) {
            return std::unexpected(String("the image has no size"));
        }
        int32_t left = imageWidth;
        int32_t top = imageHeight;
        int32_t right = 0;
        int32_t bottom = 0;
        for (const ArtGenIntRect& box : bounds) {
            if (box.w <= 0 || box.h <= 0 || box.x < 0 || box.y < 0 || box.x + box.w > imageWidth || box.y + box.h > imageHeight) {
                return std::unexpected(Format("a drawn box {} x {} at {}, {} is not inside the {} x {} image", box.w, box.h, box.x, box.y, imageWidth, imageHeight));
            }
            left = std::min(left, box.x);
            top = std::min(top, box.y);
            right = std::max(right, box.x + box.w);
            bottom = std::max(bottom, box.y + box.h);
        }

        ArtGenImageFrame frame;
        if (settings.trim) {
            const int64_t longer = std::max(right - left, bottom - top);
            const auto pad = static_cast<int32_t>((longer * settings.padding + kHalfPercent) / kPercent);
            frame.x = std::max(0, left - pad);
            frame.y = std::max(0, top - pad);
            frame.w = std::min(imageWidth, right + pad) - frame.x;
            frame.h = std::min(imageHeight, bottom + pad) - frame.y;
        } else {
            frame.w = imageWidth;
            frame.h = imageHeight;
        }

        frame.outWidth = frame.w;
        frame.outHeight = frame.h;
        const int32_t longerSide = std::max(frame.w, frame.h);
        if (settings.maxSize != 0 && longerSide > settings.maxSize) {
            const double scale = static_cast<double>(settings.maxSize) / longerSide;
            frame.outWidth = std::max<int32_t>(1, static_cast<int32_t>(std::lround(frame.w * scale)));
            frame.outHeight = std::max<int32_t>(1, static_cast<int32_t>(std::lround(frame.h * scale)));
        }
        return frame;
    }

    bool FrameContains(const ArtGenImageFrame& frame, const ArtGenIntRect& inner) {
        return inner.x >= frame.x && inner.y >= frame.y && inner.x + inner.w <= frame.x + frame.w && inner.y + inner.h <= frame.y + frame.h;
    }

    void WriteArtGenImageFrame(JsonWriter& writer, const ArtGenImageFrame& frame) {
        writer.BeginObject();
        writer.Member("h", frame.h);
        writer.Member("outHeight", frame.outHeight);
        writer.Member("outWidth", frame.outWidth);
        writer.Member("w", frame.w);
        writer.Member("x", frame.x);
        writer.Member("y", frame.y);
        writer.EndObject();
    }

    std::expected<ArtGenImageFrame, String> ReadArtGenImageFrame(JsonView value) {
        if (!value.IsObject()) {
            return std::unexpected(String("the frame must be an object"));
        }
        ArtGenImageFrame frame;
        struct Member {
            StringView key;
            int32_t* target;
            int32_t least;
        };
        for (const Member& member : {Member{"x", &frame.x, 0}, Member{"y", &frame.y, 0}, Member{"w", &frame.w, 1}, Member{"h", &frame.h, 1}, Member{"outWidth", &frame.outWidth, 1},
                                     Member{"outHeight", &frame.outHeight, 1}}) {
            if (!FindParam(value, member.key)) {
                return std::unexpected(Format("the frame has no \"{}\"", member.key));
            }
            auto number = WholeNumber(value, member.key, 0);
            if (!number) {
                return std::unexpected(number.error());
            }
            if (*number < member.least) {
                return std::unexpected(Format("the frame's \"{}\" must be {} or more", member.key, member.least));
            }
            *member.target = *number;
        }
        if (frame.outWidth > frame.w || frame.outHeight > frame.h) {
            return std::unexpected(String("the frame's output is bigger than its crop"));
        }
        return frame;
    }

    Json ArtGenImageFrameToJson(const ArtGenImageFrame& frame) {
        return Detail::ArtGenDomOf([&](JsonWriter& writer) { WriteArtGenImageFrame(writer, frame); });
    }

    std::expected<ArtGenImageFrame, String> ArtGenImageFrameFromJson(const Json& json) {
        return Detail::ArtGenReadDom(json, [](JsonView view) { return ReadArtGenImageFrame(view); });
    }

    std::expected<ArtGenPixels, String> ProcessArtGenImage(const ArtGenPixels& pixels, const ArtGenImageFrame& frame) {
        if (pixels.rgba.size() != ByteCountOf(pixels.width, pixels.height)) {
            return std::unexpected(String("the image's buffer does not match its size"));
        }
        if (frame.w <= 0 || frame.h <= 0 || frame.outWidth <= 0 || frame.outHeight <= 0 || frame.outWidth > frame.w || frame.outHeight > frame.h) {
            return std::unexpected(Format("the frame {} x {} to {} x {} is not a crop and a downscale", frame.w, frame.h, frame.outWidth, frame.outHeight));
        }
        if (frame.x < 0 || frame.y < 0 || static_cast<int64_t>(frame.x) + frame.w > pixels.width || static_cast<int64_t>(frame.y) + frame.h > pixels.height) {
            return std::unexpected(Format("the frame {} x {} at {}, {} is not inside the {} x {} image", frame.w, frame.h, frame.x, frame.y, pixels.width, pixels.height));
        }

        ArtGenPixels out;
        out.width = static_cast<uint32_t>(frame.outWidth);
        out.height = static_cast<uint32_t>(frame.outHeight);
        out.hasAlpha = pixels.hasAlpha;
        out.rgba.resize(ByteCountOf(out.width, out.height));

        const size_t sourceStride = static_cast<size_t>(pixels.width) * kChannels;
        const uint8_t* origin = pixels.rgba.data() + static_cast<size_t>(frame.y) * sourceStride + static_cast<size_t>(frame.x) * kChannels;
        if (frame.outWidth == frame.w && frame.outHeight == frame.h) {
            const size_t rowBytes = static_cast<size_t>(frame.w) * kChannels;
            for (int32_t y = 0; y < frame.h; ++y) {
                std::memcpy(out.rgba.data() + static_cast<size_t>(y) * rowBytes, origin + static_cast<size_t>(y) * sourceStride, rowBytes);
            }
            return out;
        }

        // Colour is averaged as colour * alpha and divided by the average alpha, so a transparent pixel's own colour never leaks into an edge
        const auto outWidth = static_cast<size_t>(frame.outWidth);
        Vector<Window> columns;
        columns.reserve(outWidth);
        for (int32_t x = 0; x < frame.outWidth; ++x) {
            columns.emplace_back(frame.w, frame.outWidth, x);
        }
        const auto columnShare = static_cast<float>(frame.outWidth) / static_cast<float>(frame.w);
        Vector<float> across(static_cast<size_t>(frame.h) * outWidth * kChannels, 0.0f);
        for (int32_t y = 0; y < frame.h; ++y) {
            const uint8_t* row = origin + static_cast<size_t>(y) * sourceStride;
            float* target = across.data() + static_cast<size_t>(y) * outWidth * kChannels;
            for (int32_t x = 0; x < frame.outWidth; ++x) {
                const Window& window = columns[static_cast<size_t>(x)];
                float sum[kChannels] = {};
                for (int32_t source = window.first; source < window.last; ++source) {
                    const uint8_t* pixel = row + static_cast<size_t>(source) * kChannels;
                    const float weight = window.Weight(source);
                    const float alpha = static_cast<float>(pixel[kAlphaOffset]);
                    sum[0] += weight * static_cast<float>(pixel[0]) * alpha;
                    sum[1] += weight * static_cast<float>(pixel[1]) * alpha;
                    sum[2] += weight * static_cast<float>(pixel[2]) * alpha;
                    sum[kAlphaOffset] += weight * alpha;
                }
                for (size_t channel = 0; channel < kChannels; ++channel) {
                    target[static_cast<size_t>(x) * kChannels + channel] = sum[channel] * columnShare;
                }
            }
        }
        for (int32_t y = 0; y < frame.outHeight; ++y) {
            const Window window(frame.h, frame.outHeight, y);
            const auto share = static_cast<float>(frame.outHeight) / static_cast<float>(frame.h);
            for (int32_t x = 0; x < frame.outWidth; ++x) {
                float sum[kChannels] = {};
                for (int32_t source = window.first; source < window.last; ++source) {
                    const float weight = window.Weight(source);
                    const float* cell = across.data() + (static_cast<size_t>(source) * outWidth + static_cast<size_t>(x)) * kChannels;
                    for (size_t channel = 0; channel < kChannels; ++channel) {
                        sum[channel] += weight * cell[channel];
                    }
                }
                const float alpha = sum[kAlphaOffset] * share;
                uint8_t* pixel = out.rgba.data() + (static_cast<size_t>(y) * outWidth + static_cast<size_t>(x)) * kChannels;
                for (size_t channel = 0; channel < kAlphaOffset; ++channel) {
                    pixel[channel] = alpha > 0.0f ? ToByte(sum[channel] * share / alpha) : uint8_t{0};
                }
                pixel[kAlphaOffset] = ToByte(alpha);
            }
        }
        return out;
    }

    std::expected<ArtGenPixels, String> ReadArtGenPng(const Path& file) {
        auto image = LoadRgbaImage(file);
        if (!image) {
            return std::unexpected(Format("cannot read '{}': {}", file.View(), image.error()));
        }
        ArtGenPixels pixels;
        pixels.width = image->width;
        pixels.height = image->height;
        pixels.hasAlpha = image->hasAlpha;
        pixels.rgba = std::move(image->pixels);
        return pixels;
    }

    std::expected<void, String> WriteArtGenPng(const Path& file, const ArtGenPixels& pixels) {
        if (pixels.width == 0 || pixels.height == 0 || pixels.rgba.size() != ByteCountOf(pixels.width, pixels.height)) {
            return std::unexpected(String("the image to write is empty or its buffer does not match its size"));
        }
        SDL_Surface* surface = SDL_CreateSurface(static_cast<int>(pixels.width), static_cast<int>(pixels.height), SDL_PIXELFORMAT_RGBA32);
        if (surface == nullptr) {
            return std::unexpected(Format("cannot make a surface: {}", SDL_GetError()));
        }
        ScopeGuard surfaceGuard([&surface] { SDL_DestroySurface(surface); });
        const size_t rowBytes = static_cast<size_t>(pixels.width) * kChannels;
        auto* target = static_cast<uint8_t*>(surface->pixels);
        for (uint32_t y = 0; y < pixels.height; ++y) {
            std::memcpy(target + static_cast<size_t>(y) * static_cast<size_t>(surface->pitch), pixels.rgba.data() + y * rowBytes, rowBytes);
        }

        const Path temp(Format("{}{}", file.View(), kAtomicTempSuffix));
        if (!IMG_SavePNG(surface, temp.CStr())) {
            const String reason = SDL_GetError();
            static_cast<void>(RemoveFile(temp));
            return std::unexpected(Format("cannot write '{}': {}", temp.View(), reason));
        }
        // A game may have the old picture open: the rename replaces it whole, never a half-written file
        if (const auto renamed = RenameFile(temp, file); !renamed) {
            static_cast<void>(RemoveFile(temp));
            return std::unexpected(Format("cannot replace '{}': {}", file.View(), renamed.error()));
        }
        return {};
    }

}  // namespace BECore
