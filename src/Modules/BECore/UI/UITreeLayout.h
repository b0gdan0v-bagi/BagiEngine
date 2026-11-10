#pragma once

#include <BECore/UI/UIElement.h>

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
         */
        static void Run(UIElement& root, const Rect& contentBounds, const Rect& fullBounds);
    };

}  // namespace BECore
