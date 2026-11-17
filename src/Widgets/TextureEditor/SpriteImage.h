#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>
#include <expected>
#include <filesystem>

namespace BECore {

    /// The longest side the loader accepts: 8192 x 8192 is 256 MB of pixels
    inline constexpr uint32_t kMaxSpriteImageSide = 8192;

    /// Pixels on the CPU: row-major, 4 bytes (R, G, B, A) per pixel, straight alpha, no row padding, so `pixels.size() == width * height * 4`
    struct RgbaImage {
        uint32_t width = 0;
        uint32_t height = 0;
        Vector<uint8_t> pixels;
    };

    /// Reads an image file (any format SDL_image decodes) as RGBA. The error is a line for a status text: unreadable, empty or over kMaxSpriteImageSide
    [[nodiscard]] std::expected<RgbaImage, String> LoadRgbaImage(const std::filesystem::path& file);

    /// Writes @p image as a PNG at @p file (a plain write, no atomic replace). The same image always gives the same bytes
    [[nodiscard]] std::expected<void, String> SaveRgbaPng(const std::filesystem::path& file, const RgbaImage& image);

}  // namespace BECore
