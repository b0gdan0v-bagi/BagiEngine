#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <Widgets/TextureEditor/SpriteImage.h>
#include <cstdint>
#include <expected>

namespace BECore {

    inline constexpr int kMaxIslandAlphaThreshold = 254;
    inline constexpr int kMaxIslandPadding = 64;
    inline constexpr int kMaxIslandMergeGap = 64;
    inline constexpr int kDefaultIslandAlphaThreshold = 16;
    inline constexpr int kDefaultIslandMinSize = 4;
    inline constexpr int kDefaultIslandPadding = 0;
    inline constexpr int kDefaultIslandMergeGap = 0;

    /// More raw 8-connected pieces than this is dust, not a sheet; the scan stops there, so the merge never runs on it
    inline constexpr size_t kMaxIslandComponents = 16384;

    /// How alpha islands are found. A pixel is solid when its alpha is above `alphaThreshold` (0 to kMaxIslandAlphaThreshold), so a
    /// threshold of 255 would find nothing. `minSize` (at least 1) is the least width and height an island keeps after the merge.
    /// Boxes closer than `mergeGap` empty pixels on both axes merge; `padding` grows each result by that many pixels per side.
    struct IslandSpec {
        int alphaThreshold = kDefaultIslandAlphaThreshold;
        int minSize = kDefaultIslandMinSize;
        int padding = kDefaultIslandPadding;
        int mergeGap = kDefaultIslandMergeGap;
    };

    CORE_ENUM(SheetIslandError, uint8_t, NoImage, BadSpec, TooManyIslands, NoIslands)

    /// One line for the panel's status
    [[nodiscard]] StringView SheetIslandErrorText(SheetIslandError error);

    /**
     * @brief The rects of the alpha islands of @p image, in whole pixels, ready for PlanRectSlice.
     *
     * - An island is a set of 8-connected solid pixels; each is first reduced to its bounding box.
     * - Two boxes merge when they overlap, touch or lie within `mergeGap` empty pixels on both axes, until none does (the dot of an "i",
     *   a spark inside a ring). The result does not depend on the order of the scan.
     * - A merged box under `minSize` in width or height is dropped after the merge, so a dot that joins a body is kept with it.
     * - Padding grows a box on every side and clamps it to the sheet. Padded rects may overlap a neighbour's padding: that is not
     *   resolved, the caller lowers the padding.
     * - The order is the reading order and a contract (names follow it): the topmost remaining box leads a row, every remaining box
     *   whose vertical centre lies inside the leader's extent (before padding) joins it, a row goes left to right, rows top to bottom.
     *
     * More than kMaxSheetGridCells results is `TooManyIslands`, like a grid; none is `NoIslands`.
     */
    [[nodiscard]] std::expected<Vector<Rect>, SheetIslandError> FindAlphaIslands(const RgbaImage& image, const IslandSpec& spec);

}  // namespace BECore
