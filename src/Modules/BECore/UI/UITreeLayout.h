#pragma once

#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIRegions.h>

namespace BECore {

    /// Lays out a whole UI tree with the rules of UILayout
    class UITreeLayout {
    public:
        /**
         * @brief Sets `rect` of every element under @p root from its `active` style.
         *
         * The root is the canvas itself: its rect is @p contentBounds, and its children are laid out in it by the root's layout (anchor, or stack).
         * A child of the root with `ignoreSafeArea` is laid out against @p fullBounds instead. A hidden element and everything under it
         * gets an empty rect and takes no room in a stack. The children of an element with `HasPopupChildren()` are out of flow: empty while it is closed, placed by `LayoutPopup` against
         * @p contentBounds while open. Rects are in canvas coordinates.
         *
         * A child of an anchor layout whose `region` is published in @p regions is laid out in that rect instead of in its parent's rect (and also instead
         * of @p fullBounds: a region wins over `ignoreSafeArea`). A region with no width or height is a valid region and gives a zero rect. A region that is
         * not published leaves the element in its parent, as if it named none. A child of a stack and a child of a popup element have their place from the
         * parent, so their `region` is ignored; the stack itself or the popup element, like any element, may name one.
         */
        static void Run(UIElement& root, const Rect& contentBounds, const Rect& fullBounds, const UIRegionSet& regions = {});
    };

}  // namespace BECore
