#pragma once

#include <BECore/UI/UIRectSpec.h>

namespace BECore {

    /// One of the nine pieces of a stretched picture: a part of the source and the place it is drawn at
    struct UINineSlicePiece {
        Rect source;
        Rect destination;
    };

    /**
     * @brief Cuts a picture into nine pieces so that its corners keep their size and its edges stretch, for a panel or a button of any size.
     *
     * Pure. The borders are in pixels of the source picture and are drawn one unit per pixel (the canvas scale turns units into output pixels). The pieces are in
     * reading order: top-left, top, top-right, left, center, right, bottom-left, bottom, bottom-right. A destination smaller than the two borders on an axis
     * shrinks both borders of that axis by the same factor, so the corners never overlap; a piece with no area (a zero border, an empty center) has an empty source
     * and destination and the drawer skips it.
     */
    class UINineSlice {
    public:
        static constexpr size_t PieceCount = 9;

        [[nodiscard]] static eastl::array<UINineSlicePiece, PieceCount> Compute(const Rect& source, const UIEdges& border, const Rect& destination);
    };

}  // namespace BECore
