#pragma once

#include <BECore/Control/Json.h>
#include <BECore/Types/Types.h>
#include <cstdint>
#include <expected>
#include <filesystem>

namespace BECore {

    /// Straight-alpha 8-bit RGBA, row after row with no padding: `rgba.size() == width * height * 4`
    struct ArtGenPixels {
        uint32_t width = 0;
        uint32_t height = 0;
        Vector<uint8_t> rgba;
        bool hasAlpha = true;  // false: the source file has no alpha channel and every pixel counts as drawn
    };

    /// A pixel is drawn when its alpha is above this (of 255): the faint haze a generator leaves around a cut-out is not part of the object
    inline constexpr uint8_t kArtGenAlphaCutoff = 8;

    /**
     * @brief What Accept does to a generated image before it becomes a sprite: cut to what is drawn with a margin, then made smaller.
     * Applied in that order.
     */
    struct ArtGenImageSettings {
        static constexpr int32_t kMinPadding = 0;
        static constexpr int32_t kMaxPadding = 25;
        static constexpr int32_t kMinMaxSize = 16;
        static constexpr int32_t kMaxMaxSize = 4096;
        static constexpr int32_t kDefaultPadding = 2;
        static constexpr int32_t kDefaultMaxSize = 256;

        bool trim = true;                   // cut to the drawn box
        int32_t padding = kDefaultPadding;  // percent of the box's longer side, on every side
        int32_t maxSize = kDefaultMaxSize;  // pixels of the longer side of the result; 0: no downscale. A picture is never made bigger

        [[nodiscard]] bool operator==(const ArtGenImageSettings&) const = default;

        /// No trim and no downscale: Accept copies the PNG byte for byte
        [[nodiscard]] bool IsIdentity() const {
            return !trim && maxSize == 0;
        }
    };

    /// `{trim, padding, maxSize}`: a value inside a `Json` record (a preset, the provenance, the parameters of `artgen_accept`), not an answer of its own
    [[nodiscard]] Json ArtGenImageSettingsToJson(const ArtGenImageSettings& settings);
    /// Reads what ArtGenImageSettingsToJson writes; null is the defaults, a missing key keeps the default, a wrong type or a value out of
    /// range is the error and names the key
    [[nodiscard]] std::expected<ArtGenImageSettings, String> ArtGenImageSettingsFromJson(const Json& json);

    struct ArtGenIntRect {
        int32_t x = 0;
        int32_t y = 0;
        int32_t w = 0;
        int32_t h = 0;

        [[nodiscard]] bool operator==(const ArtGenIntRect&) const = default;
    };

    /// The box of the pixels whose alpha is above kArtGenAlphaCutoff; the whole image when it has no alpha channel; nothing for an image
    /// with nothing drawn (or one whose buffer does not match its size)
    [[nodiscard]] Optional<ArtGenIntRect> DrawnBounds(const ArtGenPixels& pixels);

    /// The cut every file of a set is made from: a rectangle of the source and the size it is scaled to
    struct ArtGenImageFrame {
        int32_t x = 0;
        int32_t y = 0;
        int32_t w = 0;
        int32_t h = 0;
        int32_t outWidth = 0;
        int32_t outHeight = 0;

        [[nodiscard]] bool operator==(const ArtGenImageFrame&) const = default;

        [[nodiscard]] ArtGenIntRect Crop() const {
            return ArtGenIntRect{x, y, w, h};
        }
    };

    /**
     * @brief The frame over the union of @p bounds (the drawn boxes of one image or of every image of a set, all of @p imageWidth x
     * @p imageHeight): grown by the padding and clamped to the image, then scaled down to maxSize. With `trim` off it is the whole image.
     * An error for no bounds, a box outside the image or settings out of range.
     */
    [[nodiscard]] std::expected<ArtGenImageFrame, String> MakeArtGenFrame(Span<const ArtGenIntRect> bounds, int32_t imageWidth, int32_t imageHeight, const ArtGenImageSettings& settings);

    /// Whether @p inner lies wholly inside the frame's crop
    [[nodiscard]] bool FrameContains(const ArtGenImageFrame& frame, const ArtGenIntRect& inner);

    /// `{x, y, w, h, outWidth, outHeight}`: a value inside a `Json` record, like ArtGenImageSettingsToJson
    [[nodiscard]] Json ArtGenImageFrameToJson(const ArtGenImageFrame& frame);
    [[nodiscard]] std::expected<ArtGenImageFrame, String> ArtGenImageFrameFromJson(const Json& json);

    /**
     * @brief Crops @p pixels to @p frame and scales the crop to the frame's output size with an area average over premultiplied alpha (so an
     * edge has no halo). Never scales up; a frame of the crop's own size is a pixel-for-pixel copy. An error when the crop is outside the
     * image or the sizes are not positive. Pure.
     */
    [[nodiscard]] std::expected<ArtGenPixels, String> ProcessArtGenImage(const ArtGenPixels& pixels, const ArtGenImageFrame& frame);

    /// What Accept does with one image: the settings and, for a set, the frame fixed for all its files; no frame is the image's own
    /// (the frame over its single drawn box). `atlas` names the group the sprite joins and `deferPack` leaves packing it to the caller
    /// (`ArtGenAtlasAccept.h`); `CopyVariantIntoAssets` ignores both
    struct ArtGenImagePlan {
        ArtGenImageSettings settings;
        Optional<ArtGenImageFrame> frame;
        String atlas;
        bool deferPack = false;
    };

    /// Decodes a PNG (any format `LoadRgbaImage` reads) into 8-bit straight RGBA; `hasAlpha` follows the file's format
    [[nodiscard]] std::expected<ArtGenPixels, String> ReadArtGenPng(const std::filesystem::path& file);

    /// Writes @p pixels as an RGBA PNG to a temporary file beside @p file and renames it over, so a failure keeps the old file
    [[nodiscard]] std::expected<void, String> WriteArtGenPng(const std::filesystem::path& file, const ArtGenPixels& pixels);

}  // namespace BECore
