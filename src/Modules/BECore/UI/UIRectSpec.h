#pragma once

#include <BECore/UI/UILength.h>

namespace BECore {

    /// The point of the parent that an element is pinned to. The element's own pivot is the same point of itself, so a BottomRight element with a
    /// negative offset moves up and left into the parent. Stretch fills the parent minus the margins.
    CORE_ENUM(UIAnchor, uint8_t, TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight, Stretch)

    /// Fit: the largest rect of the aspect ratio inside the width x height box. Fill: the smallest one that covers the box (it may stick out).
    CORE_ENUM(UIAspectMode, uint8_t, Fit, Fill)

    struct UIEdges {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;

        [[nodiscard]] constexpr bool operator==(const UIEdges&) const = default;

        /// For the inspector: a field of this type is shown as four numbers (a file holds it in one attribute, see UIText::FormatEdges)
        template <typename Visitor>
        bool VisitProperties(Visitor& v) {
            bool changed = false;
            changed |= v.Visit("left", left);
            changed |= v.Visit("top", top);
            changed |= v.Visit("right", right);
            changed |= v.Visit("bottom", bottom);
            return changed;
        }
    };

    /**
     * @brief How one element finds its rect in its parent (anchor layout) or its place in a stack.
     *
     * Everything is relative to the parent, never to the screen, so the same spec works at any canvas size.
     * `x` and `y` are an offset from the anchor point, in UI units or a percent of the parent's extent on that axis. A Stretch element ignores x, y, width and height on both axes.
     * The minimum and maximum limits come after the size and before the aspect ratio; when they disagree the minimum wins. A stack uses the
     * size, the limits and `flex` of its children and ignores their anchor, offset, margin and aspect ratio.
     */
    struct UIRectSpec {
        UIAnchor anchor = UIAnchor::TopLeft;
        UILength x;  ///< the offset from the anchor point on X: units, or a percent of the parent's width
        UILength y;  ///< the same on Y, against the parent's height
        UILength width;
        UILength height;
        std::optional<UILength> minWidth;
        std::optional<UILength> maxWidth;
        std::optional<UILength> minHeight;
        std::optional<UILength> maxHeight;
        float aspectRatio = 0.0f;  ///< width / height; 0 is off
        UIAspectMode aspectMode = UIAspectMode::Fit;
        UIEdges margin;     ///< Stretch only: the distance to each edge of the parent
        float flex = 0.0f;  ///< stack only: the weight of the share of the main axis that fixed children leave; 0 keeps the size
        PoolString region;  ///< anchor layout only: the name of a region the game published (UIRegionSet); it replaces the parent box. Empty is none

        [[nodiscard]] bool operator==(const UIRectSpec&) const = default;
    };

    CORE_ENUM(UIStackDirection, uint8_t, Vertical, Horizontal)

    /// Where a child sits on the cross axis of its line. Stretch makes it as wide (or tall) as the line.
    CORE_ENUM(UIStackAlign, uint8_t, Start, Center, End, Stretch)

    /// A container that puts its children one after another along `direction`.
    struct UIStackSpec {
        UIStackDirection direction = UIStackDirection::Vertical;
        float spacing = 0.0f;
        UIEdges padding;
        bool wrap = false;  ///< start a new line when the next child does not fit the main axis
        UIStackAlign crossAlign = UIStackAlign::Start;

        [[nodiscard]] constexpr bool operator==(const UIStackSpec&) const = default;
    };

}  // namespace BECore
