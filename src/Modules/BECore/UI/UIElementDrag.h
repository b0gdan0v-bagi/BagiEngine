#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/Math/Vec2.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIPreviewOverlay.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// Why the viewport shows no handles for a selected element (`Ok`: it can be dragged)
    CORE_ENUM(CanDragReason, uint8_t, Ok, NoLayout, Hidden, NoBox, VariantOverrides)

    /// What a press on the rect of an element grabs: the body moves it, a corner or an edge resizes it
    CORE_ENUM(UIDragHandle, uint8_t, None, Body, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left)

    /// One grab area of the handles of a rect
    struct UIHandleRect {
        UIDragHandle handle = UIDragHandle::None;
        Rect rect;
    };

    /// The grab areas of one rect, the most specific first (corners, edges). The body is the rect itself and is not listed.
    struct UIHandleSet {
        static constexpr size_t kMaxHandles = 8;
        Array<UIHandleRect, kMaxHandles> items;
        size_t count = 0;

        [[nodiscard]] Span<const UIHandleRect> View() const {
            return {items.data(), count};
        }
    };

    /// How a resize or a move reads the modifier keys
    struct UIResizeOptions {
        bool keepAspect = false;  ///< a corner resize keeps the width to height ratio of the start rect
        bool lockAxis = false;    ///< a move keeps only the axis the pointer travelled more along
    };

    /// The lines a drag snaps to, in canvas units: the left, centre and right of the box, and its top, middle and bottom
    struct UISnapLines {
        Array<float, 3> x{};
        Array<float, 3> y{};
    };

    /// A rect after the snap, and the lines it snapped to (for the guides): one per axis at most
    struct UISnapResult {
        Rect rect;
        Optional<float> guideX;
        Optional<float> guideY;
    };

    /**
     * @brief The pure part of dragging a canvas element in the viewport: where it may be dragged, what a press grabs, and the wanted rect or anchors for the pointer. No ImGui, no scene.
     *
     * Rects are in canvas units except where a function says output pixels (the handles and the anchor widget are drawn and hit in output pixels). Every result is computed from the start
     * (the rect at the press) and the pointer, never from the previous result, so a drag that comes back gives the start. The wanted rect becomes a spec with `UIRectEdit::SpecForRect`.
     */
    class UIElementDrag {
    public:
        /// A width or a height never goes below this (canvas units) and never flips sign
        static constexpr float kMinSize = 1.0f;

        /// Handles smaller than this many handle sizes on a side are dropped: an edge handle needs three, a rect under two on a side keeps only its body
        static constexpr float kEdgeHandleSizes = 3.0f;
        static constexpr float kMinHandleSizes = 2.0f;

        /**
         * @brief The box @p element lays out in, in canvas units: the region it names when the canvas published it, the whole canvas `bounds` for an `ignoreSafeArea` child of the root, else the
         *        rect of its parent. None when the canvas has no layout or the element is not in its tree.
         *
         * The rule of UITreeLayout, and of UIPreviewOverlay::AnchorsOf. The region is the one of `active.rect`, which is what the layout reads.
         */
        [[nodiscard]] static Optional<Rect> BoxOf(const UICanvasLayout& canvas, const UIElement& element);

        /// The same for a known @p parent and the region name @p region (the inspector asks with the one of `style.rect`, which it edits)
        [[nodiscard]] static Rect BoxOf(const UICanvasLayout& canvas, const UIElement& parent, const UIElement& element, PoolString region);

        /**
         * @brief Whether a drag of @p element writes `style.rect` and the layout shows it: the canvas is laid out, the element is visible with a rect, it is in the tree under a parent that lays out
         *        by anchors (not a stack, not a popup parent: `UIPreviewOverlay::AnchorsOf` has a value), and no variant overrides its rect (`active.rect == style.rect`).
         *
         * The checks come in the order of `CanDragReason`. That the node is the primary selection and not locked is the viewport's to ask.
         */
        [[nodiscard]] static CanDragReason CanDrag(const UICanvasLayout& canvas, const UIElement& element);

        /// The English sentence the viewport shows for a reason that is not `Ok`
        [[nodiscard]] static const char* ReasonText(CanDragReason reason);

        /**
         * @brief The grab areas of @p rect: four squares of @p handleSize on the corners and four on the middles of the edges, centred on them (output pixels).
         *
         * An edge handle is dropped on an axis shorter than three handles (the two on that axis' ends would touch), and a rect shorter than two handles on a side has none at all (the body only).
         */
        [[nodiscard]] static UIHandleSet HandleRects(const Rect& rect, float handleSize);

        /// What a press at @p point grabs: a corner before an edge before the body (the inside of @p rect); None outside all of them. Output pixels.
        [[nodiscard]] static UIDragHandle HitHandle(const Rect& rect, const Vec2& point, float handleSize);

        /**
         * @brief @p start moved or resized by @p delta (canvas units) as @p handle says: Body moves; an edge moves that edge, a corner two; the opposite side stays.
         *
         * The size never goes below `kMinSize` and never flips. `keepAspect` makes a corner resize uniform (the factor of the axis the pointer changed more); `lockAxis` makes the move one-axis.
         * None gives @p start.
         */
        [[nodiscard]] static Rect Resize(const Rect& start, UIDragHandle handle, const Vec2& delta, const UIResizeOptions& options = {});

        /// The snap lines of @p box (canvas units)
        [[nodiscard]] static UISnapLines SnapLines(const Rect& box);

        /**
         * @brief @p wanted with the edges that @p handle moves put on the nearest line of @p lines within @p threshold (canvas units).
         *
         * Body: the left, centre or right edge (the nearest to a line wins) on X, the same on Y, and the rect moves whole. An edge handle snaps its edge, a corner the two edges it moves; the
         * opposite sides stay and the size keeps `kMinSize`. A caller that keeps the aspect does not snap: a snapped edge would break it.
         */
        [[nodiscard]] static UISnapResult Snap(const Rect& wanted, UIDragHandle handle, const UISnapLines& lines, float threshold);
    };

}  // namespace BECore
