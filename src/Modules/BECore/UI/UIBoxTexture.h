#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/UI/UIBoxStyle.h>

namespace BECore {

    /**
     * @brief Everything the look of a box depends on except where it is and the size of an axis that is stretched: the key of a cached box texture.
     *
     * Radii, widths and offsets are UI units, @ref scale is the output pixels per unit. @ref corners are the radii as clamped to the real rect, so two boxes of different size
     * with the same style share a texture. Equal means every number is equal: the cache compares keys, nothing is rounded.
     */
    struct UIBoxLook {
        UICorners corners;
        float borderWidth = 0.0f;
        Color borderColor;
        UIGradientDirection gradientDirection = UIGradientDirection::None;
        Color fill;
        Color gradientEnd;
        UIShadowSpec shadow;
        UIShadowSpec glow;
        Color highlight;
        float scale = 1.0f;

        [[nodiscard]] bool operator==(const UIBoxLook& other) const;
    };

    /// The look and the pixel size of the axes that are not stretched (0 for a stretched one)
    struct UIBoxTextureKey {
        UIBoxLook look;
        int wholeWidth = 0;
        int wholeHeight = 0;

        [[nodiscard]] bool operator==(const UIBoxTextureKey& other) const {
            return wholeWidth == other.wholeWidth && wholeHeight == other.wholeHeight && look == other.look;
        }
    };

    /// One axis of a box texture, in output pixels. A **sliced** axis has two caps (everything within reach of an edge: the corner, the blur, the border) and a middle of
    /// `UIBoxTexture::kMiddlePixels` equal pixels the drawer stretches to the box; a **whole** axis is the box itself, drawn one to one.
    struct UIBoxTextureAxis {
        bool sliced = false;
        int size = 0;          ///< the box on this axis
        int marginBefore = 0;  ///< how far the paint reaches outside the box before it and after it (the shadow, the glow, the soft edge)
        int marginAfter = 0;
        int capBefore = 0;  ///< the part of the box next to each edge that is not the same along the axis (sliced axis only)
        int capAfter = 0;

        /// The size of the box that is drawn into the texture: the box itself, or the two caps and the middle
        [[nodiscard]] int ReferenceSize() const;
        /// The length of the texture on this axis
        [[nodiscard]] int Length() const;
    };

    struct UIBoxTextureLayout {
        UIBoxTextureKey key;
        UIBoxTextureAxis x;
        UIBoxTextureAxis y;

        [[nodiscard]] int Width() const {
            return x.Length();
        }
        [[nodiscard]] int Height() const {
            return y.Length();
        }
    };

    /// A part of the texture and where it goes: `destination` is in output pixels relative to the top-left pixel of the box, `source` in texels
    struct UIBoxTexturePiece {
        Rect source;
        Rect destination;
    };

    /**
     * @brief The look of a box as pixels: shadow, glow, fill with its gradient, border and highlight, composed into one RGBA picture that is small whatever the box.
     *
     * Pure, no renderer: the caller makes the texture. For a renderer that pays for every triangle (`GeometryDetail::Reduced`, the SDL software renderer) a box that was
     * a few thousand blended triangles is nine blits of a cached texture (`UIBoxTextureCache`).
     *
     * The picture is **not** a second formula of the look: `Render` draws the box of `UIBoxMesh` at full detail into a pixel buffer with a small triangle rasterizer
     * (a pixel centre is covered by the triangle that holds it, the colour is interpolated, layers go over each other in the order of the mesh), so the pieces of a style
     * stay in one place. An axis is sliced when the box is long enough for a middle that is the same along it; the box is then drawn at the size of its caps and a
     * 3-pixel middle. A vertical gradient is not the same along y and a horizontal one along x: that axis is whole, and the key holds its size.
     */
    class UIBoxTexture {
    public:
        static constexpr int kMiddlePixels = 3;     ///< the stretched middle of a sliced axis; the pixel in the middle of it is the one that is stretched, its neighbours are equal
        static constexpr int kMaxSide = 4096;       ///< the longest side of a texture; a box that would need more is drawn as geometry
        static constexpr int kMaxPixels = 1 << 21;  ///< the most pixels of a texture (8 MB of RGBA)
        static constexpr size_t kMaxPieces = 9;

        /// The look of a box of @p width x @p height units from the style and the colours of the draw. Corners are clamped to that size
        [[nodiscard]] static UIBoxLook MakeLook(const UIBoxStyle& style, const Color& fill, const Color& gradientEnd, float scale, float width, float height);

        /// The texture of a box of @p widthPixels x @p heightPixels output pixels, or nothing when the look cannot be drawn (a size or scale out of range, a texture too large)
        [[nodiscard]] static Optional<UIBoxTextureLayout> MakeLayout(const UIBoxLook& look, int widthPixels, int heightPixels);

        /// The RGBA8 pixels (straight alpha, rows from the top) of @p layout: `Width() * Height() * 4` bytes
        [[nodiscard]] static Vector<uint8_t> Render(const UIBoxTextureLayout& layout);

        /// The blits that draw the texture over the box whose top-left output pixel is the origin (at most kMaxPieces; a piece with no area is left out). Returns their number
        static size_t MakePieces(const UIBoxTextureLayout& layout, Array<UIBoxTexturePiece, kMaxPieces>& pieces);
    };

}  // namespace BECore
