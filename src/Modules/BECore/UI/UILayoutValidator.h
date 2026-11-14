#pragma once

#include <BECore/UI/UICanvasScaler.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIRegions.h>

namespace BECore {

    /// What a laid-out canvas can get wrong
    /// OutsideCanvas: an element sticks out of the canvas. OutsideSafeArea: it is inside the canvas but outside the safe area the canvas keeps its content in.
    /// SiblingsOverlap: two children of a stack overlap. TouchTargetTooSmall: an interactive element is smaller than the minimum touch size.
    /// TextDoesNotFit: the content of an element does not fit its rect and the element has no way to cut or shrink it.
    /// OverlapsSceneView: an element of a subtree the caller keeps off the view (`UILayoutValidatorOptions::keepOffView`) lies over the scene's view, where the board is, and not on a bar.
    /// MissingRegion: an element names a region (`UIRectSpec::region`) that the canvas has not published, so it was laid out in its parent's box. OutsideRegion: an element laid out in a
    /// published region sticks out of it. RegionIgnored: a child of a stack or of a popup element names a region, which only an anchor layout uses.
    CORE_ENUM(UILayoutIssueKind, uint8_t, OutsideCanvas, OutsideSafeArea, SiblingsOverlap, TouchTargetTooSmall, TextDoesNotFit, OverlapsSceneView, MissingRegion, OutsideRegion, RegionIgnored)

    struct UILayoutIssue {
        UILayoutIssueKind kind = UILayoutIssueKind::OutsideCanvas;
        PoolString element;  ///< the element at fault
        PoolString other;    ///< SiblingsOverlap: the sibling it overlaps. MissingRegion, OutsideRegion, RegionIgnored: the name of the region
        Rect rect;           ///< the rect of `element`, in canvas units
    };

    struct UILayoutValidatorOptions {
        /// The smallest side of an interactive element in window units, as the user sees it (UISettings::minTouchSize)
        float minTouchSize = 32.0f;
        /// Output pixels per window unit, to turn the size of an element on screen into window units
        float pixelDensity = 1.0f;
        /// Names of children of the canvas root that must stay off the scene's view (a row of buttons that belongs on a bar), each with its whole subtree: the first element on
        /// every path of it (the child or a descendant, visible, with a rect) that overlaps `UICanvasResolved::viewBounds` is reported as OverlapsSceneView, and its own
        /// descendants are not looked at. Meant for an `extendToOutput` canvas: on any other canvas `viewBounds` is `bounds`, so everything overlaps. The caller names the
        /// elements (a screen test lists its bar row), the element has no flag of its own. Empty (the default): not checked.
        Vector<PoolString> keepOffView;
    };

    /**
     * @brief Checks the invariants of a laid-out canvas. Pure: it reads rects and reports, it draws nothing and needs no window.
     *
     * Only visible elements are checked (an element hidden by its style or by an ancestor is skipped, with its subtree), and an element with an
     * empty rect cannot stick out. A rect sticking out by less than a hundredth of a unit is not reported (float noise of percent sizes). Every screen the
     * repository ships goes through this on the size matrix (`tests/Common/UILayoutMatrix.h`) and must report nothing.
     *
     * The regions are the canvas's published set (`UICanvasLayout::regions`), the one the layout ran with. A descendant of an element that has a region is checked against the canvas
     * only, not against the region of its ancestor.
     */
    class UILayoutValidator {
    public:
        /// @param root the canvas root, laid out (UITreeLayout::Run) for @p resolved
        /// @param regions the regions the layout ran with (UITreeLayout::Run); the default, none, reports every `region` of the tree as MissingRegion
        [[nodiscard]] static Vector<UILayoutIssue> Validate(const UIElement& root, const UICanvasResolved& resolved, const UILayoutValidatorOptions& options, const UIRegionSet& regions = {});
    };

}  // namespace BECore
