#pragma once

namespace BECore {

    class ControlService;

    /// Error codes of `ui_tree` and `ui_click` (the protocol part that is not shared with other methods; the common ones are in ControlErrors)
    namespace UIControlErrors {
        inline constexpr eastl::string_view kNoScene = "no_scene";
        inline constexpr eastl::string_view kNotFound = "not_found";
        inline constexpr eastl::string_view kNotClickable = "not_clickable";
    }  // namespace UIControlErrors

    /**
     * @brief Registers the control API methods that look at the in-game UI and press its buttons: `ui_tree` and `ui_click`.
     *
     * Both work on the canvases (`UICanvasComponent`) of the active scene, by the layout of their last draw, so a client that wants the rects of this
     * moment `step`s a frame first. Handlers run at frame stage 1b, before this frame's draw.
     *
     * | Method | Params | Result |
     * |--------|--------|--------|
     * | ui_tree  | none | {"scene", "canvases": [Canvas]} |
     * | ui_click | {"name": element name, "canvas": node name (optional)} | {"clicked", "canvas", "x", "y", "consumed", "action"?, "value"?}; errors `no_scene`, `not_found`, `not_clickable`,
     * `invalid_params` |
     *
     * Canvas: {"node", "space", "visible", "modal", "inputEnabled", "takesInput", "inputOrder", "laidOut"} and, once laid out, "scale" (output pixels per UI
     * unit), "bounds", "safeBounds", "contentBounds" (rects in UI units), "elements" (the tree) and "issues" (the layout validator's findings: {"kind",
     * "element", "other", "rect"}). A canvas that was never drawn has no layout, so it has the structure of the tree and no rects.
     * Element: {"type", "name", "visible", "interactive", "blocksInput", "hovered", "pressed", "rect" (UI units of the canvas), "outputRect" (render
     * output pixels, the pixels of a `screenshot`), "children"}, plus "text" for a label or a button (as shown, variables replaced) and "action", "disabled"
     * (the button's own flag or its group's: whether a click does anything), "value" (what the click emits with the action; "" for a plain button) and
     * "selected" (a segment of a choice group that the bound variable names) for a button, and "bind" (the variable), "selectedValue" (its value: "" when
     * unset) and "disabled" for a `UISegmented`. A rect is {"x","y","w","h"}. An element is visible when its rect is not empty.
     *
     * `selected` and `selectedValue` are those of **this moment**: `ui_tree` calls `UISegmented::SyncChildren()` on every group before it describes the
     * segments, so a variable that the game set since the last draw is already in the answer (the rects are still those of the last draw).
     *
     * `ui_click` presses the left mouse button on the middle of the first visible, interactive element of that name (in the scene's node order, or only on
     * the node named by "canvas") exactly as a user would: a mouse move, the press and the release go through `CoreManager::GetUIInput()`, so a modal canvas
     * above takes the click and a click that no canvas takes reaches game code. It fails with `not_found` when no element has the name and with `not_clickable`
     * (the message says why) when the ones that do are hidden, not interactive, covered by another element of their canvas, or in a canvas that takes no
     * input. "consumed" says whether a canvas took the press. "action" and "value" (empty for a plain button) are what the button emits with the click,
     * the same pair as the `ActionEvent`; a choice group does not change by it, the game answers by setting the
     * variable. A disabled button takes the click and does nothing with it. The answer comes after the click: its handlers (an action that switches the scene)
     * have run.
     *
     * Not seen: the documents of `RmlUiDocumentComponent` have a DOM of their own.
     *
     * Registering twice replaces the handlers, so calling this again is harmless.
     */
    void RegisterUIControl(ControlService& service);

}  // namespace BECore
