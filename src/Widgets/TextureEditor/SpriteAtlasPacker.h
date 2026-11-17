#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <Widgets/TextureEditor/SpriteImage.h>
#include <cstdint>
#include <expected>

namespace BECore {

    inline constexpr int kDefaultAtlasPadding = 1;
    inline constexpr int kMaxAtlasPadding = 16;
    inline constexpr uint32_t kDefaultAtlasMaxSide = 4096;
    static_assert(kDefaultAtlasMaxSide <= kMaxSpriteImageSide, "an atlas the loader could not read back is no atlas");

    /// More sprites than this is a mistake, not an atlas; it also bounds the cost of trying every candidate width
    inline constexpr size_t kMaxAtlasSprites = 1024;

    /// `PowerOfTwo`: both sides of the atlas are powers of two (the shape a GPU texture likes). `Tight`: the bounding box of the sprites.
    CORE_ENUM(AtlasSizeMode, uint8_t, PowerOfTwo, Tight)

    /// `padding` is the empty gap between two sprites on both axes (0 to kMaxAtlasPadding); it is not added at the atlas edges.
    /// `maxSide` (1 to kMaxSpriteImageSide) bounds both sides of the result.
    struct PackSettings {
        int padding = kDefaultAtlasPadding;
        AtlasSizeMode sizeMode = AtlasSizeMode::PowerOfTwo;
        uint32_t maxSide = kDefaultAtlasMaxSide;
    };

    struct PackSize {
        uint32_t width = 0;
        uint32_t height = 0;
    };

    /// The top-left pixel of one sprite in the atlas; its size is the input's
    struct PackedRect {
        uint32_t x = 0;
        uint32_t y = 0;
    };

    struct PackedLayout {
        uint32_t width = 0;
        uint32_t height = 0;
        Vector<PackedRect> placements;
    };

    /// `BadSettings` also covers a sprite of zero width or height
    CORE_ENUM(AtlasPackError, uint8_t, NoSprites, TooManySprites, BadSettings, SpriteTooLarge, DoesNotFit)

    /// One line for the panel's status
    [[nodiscard]] StringView AtlasPackErrorText(AtlasPackError error);

    /**
     * @brief Places rectangles of the given sizes in one atlas; `placements[i]` is where `sizes[i]` goes (input order, not packing order).
     *
     * The order and the ties below are a contract, not an implementation detail: the atlas PNG must be the same file for the same
     * sprites, so a repack does not show up as a change of the asset.
     *
     * - Sprites are placed on shelves in the order height descending, width descending, input index ascending. A sprite goes right of the
     *   previous one on its shelf, or starts a new shelf when it would pass the atlas width; the first sprite of a shelf is its tallest.
     * - Padding separates neighbours on both axes and is not added at the atlas edges, so sprites grown by the padding on their right and
     *   bottom never overlap and every sprite lies inside the atlas.
     * - The atlas width is tried among candidates: the powers of two from the smallest at or above the widest sprite up to `maxSide`,
     *   and for `Tight` also the smallest width at or above the square root of the padded area and the widest sprite (at most `maxSide`).
     *   The winner has the smallest resulting area, then the smaller resulting width, then the earlier candidate; a candidate whose
     *   result passes `maxSide` is dropped, none left is `DoesNotFit`.
     * - `PowerOfTwo` rounds both sides of the placed sprites up to a power of two, `Tight` keeps their bounding box.
     */
    [[nodiscard]] std::expected<PackedLayout, AtlasPackError> PackSizes(const Vector<PackSize>& sizes, const PackSettings& settings);

}  // namespace BECore
