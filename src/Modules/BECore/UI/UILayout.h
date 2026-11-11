#pragma once

#include <BECore/UI/UIRectSpec.h>

namespace BECore {

    /// The anchors of a spec after the shorthand and the explicit fields are merged: fractions of the parent box, min <= max on each axis
    struct UIEffectiveAnchors {
        Vec2 min;
        Vec2 max;
        Vec2 pivot;

        [[nodiscard]] constexpr bool operator==(const UIEffectiveAnchors&) const = default;
    };

    /**
     * @brief Pure layout rules: specs and rects in, rects out. No window, renderer or scene is involved, so every rule is tested alone.
     *
     * All rects are in the UI units of one canvas.
     */
    class UILayout {
    public:
        /// The point of a parent that an anchor names, as fractions of its width and height (BottomRight is {1, 1}); Stretch is the center
        [[nodiscard]] static constexpr std::pair<float, float> AnchorFraction(UIAnchor anchor);

        /**
         * @brief The anchors that lay @p spec out: the shorthand of `spec.anchor` (a point: min = max = pivot = AnchorFraction; Stretch: min 0, max 1,
         *        pivot 0.5), each of `anchorMin`, `anchorMax` and `pivot` replaced by its field when that is set.
         *
         * Per axis the two edges are put in order. A field that is not finite counts as unset.
         */
        [[nodiscard]] static UIEffectiveAnchors EffectiveAnchors(const UIRectSpec& spec);

        /// @return @p rect moved inward by @p edges; never narrower or lower than 0
        [[nodiscard]] static Rect Inset(const Rect& rect, const UIEdges& edges);

        /**
         * @brief The rect of an element inside @p parent (anchor layout).
         *
         * Each axis is either a point (the effective min equals max) or stretched (min is less than max). The size of a point axis comes from the
         * width or height (a percent is of the parent); the size of a stretched one is its span, between the two anchors, minus the margins.
         * Then the minimum and maximum limits apply, then the aspect ratio, then the element is placed: on a point axis its pivot sits on
         * the anchor point, moved by the offset; on a stretched axis it sits in the span at its pivot (a Stretch element smaller than its span, by limits or
         * aspect ratio, is centered). Size, offset and margin of the other kind of axis are ignored.
         */
        [[nodiscard]] static Rect ResolveRect(const Rect& parent, const UIRectSpec& spec);

        /**
         * @brief The rects of the children of a stack, in the order of @p children.
         *
         * The content area is @p parent minus the padding. A child's size on the main axis is its spec size clamped by its limits; a child with
         * `flex` > 0 instead takes a share of what the others and the spacing leave on its line (shares follow the weights, each is clamped
         * by the child's limits, and the clamp does not redistribute). Without `wrap` all children are one line; with it a child starts
         * a new line when its spec size does not fit the rest of the line (a flex child counts with its spec size there). Lines are stacked on
         * the cross axis, separated by `spacing`. Without `wrap` the line is as wide as the content on the cross axis; with it, as the widest child.
         * @param[out] rects one rect per child, resized to children.size()
         */
        static void LayoutStack(const Rect& parent, const UIStackSpec& stack, eastl::span<const UIRectSpec> children, eastl::vector<Rect>& rects);
    };

    constexpr std::pair<float, float> UILayout::AnchorFraction(UIAnchor anchor) {
        switch (anchor) {
            case UIAnchor::TopLeft:
                return {0.0f, 0.0f};
            case UIAnchor::Top:
                return {0.5f, 0.0f};
            case UIAnchor::TopRight:
                return {1.0f, 0.0f};
            case UIAnchor::Left:
                return {0.0f, 0.5f};
            case UIAnchor::Right:
                return {1.0f, 0.5f};
            case UIAnchor::BottomLeft:
                return {0.0f, 1.0f};
            case UIAnchor::Bottom:
                return {0.5f, 1.0f};
            case UIAnchor::BottomRight:
                return {1.0f, 1.0f};
            case UIAnchor::Center:
            case UIAnchor::Stretch:
                break;
        }
        return {0.5f, 0.5f};
    }

}  // namespace BECore
