#pragma once

#include <BECore/UI/UIRectSpec.h>
#include <BECore/UI/UIRegions.h>

namespace BECore {

    /// A spec made to lay out at a wanted rect, and whether it does
    struct UIRectEditResult {
        UIRectSpec spec;
        bool exact = true;  ///< false: the limits, the aspect ratio or a zero parent forbid the wanted rect; `spec` is the nearest one found
    };

    /**
     * @brief The inverse of UILayout::ResolveRect: from a rect to the spec that lays out there. Pure, so the editor holds no layout math.
     *
     * Everything the editor changes about a rect goes through here: a field edited by hand, an anchor preset, a drag.
     */
    class UIRectEdit {
    public:
        /**
         * @brief The spec that `UILayout::ResolveRect(parent, result.spec)` lays out at @p wanted.
         *
         * Only the numbers of each axis change: a point axis (min = max) gets its size (`width` / `height`) and its offset (`x` / `y`), a stretched
         * axis gets its `margin` (the span is taken between the anchors first, then inset, as in the layout). Anchors, pivot, limits, aspect ratio,
         * region, the unit of every length and the fields an axis ignores stay as they are. An axis that already lays out at @p wanted is left
         * untouched, so an edit of one axis does not rewrite the other.
         *
         * When the limits or the aspect ratio forbid the size, the nearest layout is returned with `exact` false; it is never silently wrong.
         * A percent length against a zero extent cannot be set (any number is zero there), so it keeps its value.
         *
         * @param parent the box the element lays out in: its parent, or the region it names
         */
        [[nodiscard]] static UIRectEditResult SpecForRect(const Rect& parent, const UIRectSpec& spec, const Rect& wanted);

        /**
         * @brief @p length in @p unit, laying out the same: 100 units against a parent of 400 are 25 percent.
         * @param parentExtent the extent of the axis the length is on (a width for `x` and `width`, a height for `y` and `height`)
         * @return nullopt when units cannot become percent because the extent is 0 (a unit value would be lost)
         */
        [[nodiscard]] static std::optional<UILength> ConvertUnit(const UILength& length, UIUnit unit, float parentExtent);

        /// The names a region picker offers: the published regions in the order of publishing, then @p current when it is named but not published
        [[nodiscard]] static eastl::vector<PoolString> RegionChoices(const UIRegionSet& regions, PoolString current);
    };

}  // namespace BECore
