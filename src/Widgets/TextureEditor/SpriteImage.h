#pragma once

#include <BECore/File/Path.h>
#include <BECore/Types/Types.h>
#include <CoreSDL/ImageFile.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /// The longest side the loader accepts: 8192 x 8192 is 256 MB of pixels
    inline constexpr uint32_t kMaxSpriteImageSide = 8192;

    /// ReadImageFile with the editor's limit. The error is a line for a status text: unreadable, empty or over kMaxSpriteImageSide
    [[nodiscard]] std::expected<RgbaImage, String> LoadRgbaImage(const Path& file);

    /// Writes @p image as a PNG at @p file (a plain write, no atomic replace). The same image always gives the same bytes
    [[nodiscard]] std::expected<void, String> SaveRgbaPng(const Path& file, const RgbaImage& image);

    /**
     * @brief Writes @p image as a PNG to a temporary file beside @p file and renames it over @p file, so a failure leaves the old file as it was.
     *
     * The bytes are those of `SaveRgbaPng`. The folder of @p file must exist; the temporary file is removed on failure.
     */
    [[nodiscard]] std::expected<void, String> ReplaceRgbaPng(const Path& file, const RgbaImage& image);

}  // namespace BECore
