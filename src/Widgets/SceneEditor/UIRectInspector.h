#pragma once

#include <BECore/UI/UICanvasComponent.h>

namespace BECore {

    /**
     * @brief The tree of the elements of @p canvas (the children of its root), one line each; a click on a line selects the element.
     *
     * @p selected is cleared when it is no longer in the tree. The selection is an IntrusivePtr, so an element removed from the tree stays alive while it is held.
     * ImGui only: the caller has an ImGui window open.
     */
    void DrawUIElementTree(UICanvasComponent& canvas, IntrusivePtr<UIElement>& selected);

    /**
     * @brief The "Rect" inspector of one element of @p canvas: the anchor preset grid, anchors and pivot, offsets and sizes with a px / % switch, margins, the region.
     *
     * It edits the authored `style.rect` (not `active`, not a variant) live. Changing the anchors or the pivot keeps the element where it is: the numbers of the layout
     * come from UIRectEdit and UIAnchorPresets, this file only draws. Every change invalidates the canvas.
     *
     * @p inexactElement is the caller's state: the element that the last edit could not keep exactly (limits or an aspect ratio forbid the rect); a warning shows
     * while it is @p element, and the next edit of another kind clears it. It is held, so a freed element's address is never taken for a new one.
     * @return true when the style changed
     */
    bool InspectUIRect(UICanvasComponent& canvas, UIElement& element, IntrusivePtr<UIElement>& inexactElement);

}  // namespace BECore
