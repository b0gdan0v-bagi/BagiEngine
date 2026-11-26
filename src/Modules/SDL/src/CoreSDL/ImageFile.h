#pragma once

#include <BECore/File/Path.h>
#include <BECore/Types/Types.h>
#include <cstdint>
#include <expected>
#include <limits>

namespace BECore {

    /// Bytes of one pixel of an RgbaImage: R, G, B, A
    inline constexpr size_t kBytesPerPixel = 4;

    /// The `maxSide` of ReadImageFile that refuses no size
    inline constexpr uint32_t kNoImageSideLimit = std::numeric_limits<uint32_t>::max();

    /// Pixels on the CPU: row-major, `kBytesPerPixel` bytes per pixel, straight alpha, no row padding, so `pixels.size() == width * height * kBytesPerPixel`
    struct RgbaImage {
        uint32_t width = 0;
        uint32_t height = 0;
        Vector<uint8_t> pixels;
        bool hasAlpha = true;  // false: the file has no alpha channel, so every pixel is opaque
    };

    /**
     * @brief Reads an image file (any format SDL_image decodes) as straight RGBA.
     *
     * A PNG keeps the exact colour of a partly transparent pixel on every platform. A side above @p maxSide is refused before the pixels are allocated.
     * The error is a line for a log or a status text.
     */
    [[nodiscard]] std::expected<RgbaImage, String> ReadImageFile(const Path& file, uint32_t maxSide = kNoImageSideLimit);

    /**
     * @brief Decodes a PNG held in memory as straight RGBA; @p bytes may be a peer's.
     *
     * Another format is refused. A side above @p maxSide is refused before the pixels are allocated, and so is data that inflates far past what the
     * sides of the header can hold. The error is a line for a log or a status text.
     */
    [[nodiscard]] std::expected<RgbaImage, String> DecodePngImage(Span<const uint8_t> bytes, uint32_t maxSide);

}  // namespace BECore
