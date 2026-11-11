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
     * | ui_click | {"name": element name, "canvas": node name (optional), "at": 0..1 (optional)} | {"clicked", "canvas", "x", "y", "consumed", "action"?, "value"?}; errors `no_scene`, `not_found`,
     * `not_clickable`, `invalid_params` |
     *
     * Canvas: {"node", "space", "visible", "modal", "inputEnabled", "takesInput", "inputOrder", "laidOut"} and, once laid out, "scale" (output pixels per UI
     * unit), "bounds", "safeBounds", "contentBounds" (rects in UI units), "elements" (the tree) and "issues" (the layout validator's findings: {"kind",
     * "element", "other", "rect"}). A canvas that was never drawn has no layout, so it has the structure of the tree and no rects.
     * Element: {"type", "name", "visible", "interactive" (a button, or an element with a `UIInputClicker`), "blocksInput", "hovered", "pressed", "rect" (UI units of the canvas), "outputRect" (render
     * output pixels, the pixels of a `screenshot`), "children"}, plus "text" for a label or a button (as shown, variables replaced) and "action", "disabled"
     * (the button's own flag or its group's: whether a click does anything), "value" (what the click emits with the action; "" for a plain button) and
     * "selected" (a segment of a choice group that the bound variable names) for a button, and, for an element made of components (`UIElement::components`, a button is not a class there):
     * "components" (the type names in list order, only when it has any), "text" (the `UITextComponent`'s, as shown), "icon" (the sprite of its `UIImageComponent`; an element with a
     * `UIInputClicker` and none of its own takes them from the first descendant that has one, not going through another clicker: the caption and the icon child of a button), "tooltip" (its
     * `UITooltipComponent`'s source) and, with a `UIInputClicker`, "action", "value", "disabled" (the clicker's flag or its group's) and "selected", with the meaning they have for a button, and
     * "bind" (the variable), "selectedValue" (its value: "" when unset) and "disabled" for a `UISegmented`. The value controls add: for a `UIToggle` (also a label, so it has "text", the caption)
     * "bind", "onValue", "offValue", "checked" (the variable equals `onValue`), "action" and "disabled"; for a `UIProgressBar` "bind", "minValue", "maxValue" and "fraction" (0..1, how much is
     * filled); for a `UIDropdown` (also a label: "text" is the header as shown, the chosen option or the placeholder) "bind", "selectedValue", "open" (the list is open), "action" and "disabled", and
     * its options are its children (`UIButton`s with "value" and "selected", hidden, with no rect, while the list is closed); for a `UISlider` "bind", "minValue", "maxValue", "step", "value" (a
     * number: the variable as the slider reads it, clamped, the minimum when unset), "fraction" (0..1, where the thumb stands), "action" and "disabled". (A slider's "value" is a number here and the
     * text that a click emits in the `ui_click` answer; a button's "value" is its text in both.) A rect is {"x","y","w","h"}. An element is visible when its rect is not empty.
     *
     * `selected` and `selectedValue` are those of **this moment**: `ui_tree` calls `UISegmented::SyncChildren()` on every group before it describes the
     * segments, so a variable that the game set since the last draw is already in the answer (the rects are still those of the last draw). So are `checked`,
     * `value` and `fraction` of the toggle, the slider and the progress bar: they read the variable when asked, no draw or `Sync` call is needed.
     *
     * `ui_click` presses the left mouse button on the middle (or at "at") of the first visible, interactive element of that name (in the scene's node order, or only on
     * the node named by "canvas") exactly as a user would: a mouse move, the press and the release go through `CoreManager::GetUIInput()`, so a modal canvas
     * above takes the click and a click that no canvas takes reaches game code. It fails with `not_found` when no element has the name and with `not_clickable`
     * (the message says why) when the ones that do are hidden, not interactive, covered by another element of their canvas, or in a canvas that takes no
     * input. "consumed" says whether a canvas took the press. "action" and "value" (empty for a plain button) are what the button emits with the click,
     * the same pair as the `ActionEvent`; a choice group does not change by it, the game answers by setting the
     * variable. A click on a dropdown's header toggles the list and answers with no "action" and no "value" (it emits nothing); one on an option of an open list answers the dropdown's
     * "action" and the option's "value" (what was emitted: both "" for a disabled option or dropdown, which take the click and leave the list open), and the list closes at the next
     * frame, so a client does `ui_click` the header, `step`, `ui_tree`, `ui_click` the option, `step`. A press elsewhere while the list is open closes it and is consumed (no click
     * for what lies below), so the answer has "consumed" true and an empty "action" and "value" even for a button, a toggle or a slider. The same holds for a toggle (its "action" and the value it
     * asks for: `offValue` while it is on, `onValue` while it is off, read before the click) and a slider (its "action" and the text of the value the press emitted, "" when nothing was emitted: a
     * disabled slider or one without an action). A progress bar is not interactive, so it is `not_clickable`.
     *
     * "at" is a number from 0 to 1, the horizontal position of the press inside the element's rect (default 0.5, the middle; 0 and 1 are the left and the right
     * edge, a hair inside it so the point still hits the element); the vertical position is always the middle. It is how a client sets a slider with one click:
     * `at` 0 asks for the minimum, 1 for the maximum, and the answer's "value" says what that came to (a drag in several steps is not offered). Anything else
     * (a string, null, a number outside 0..1) is `invalid_params`, checked before anything is pressed. `at` is also used for the covered-by check, so a point
     * under another element is `not_clickable`. A click with `at` on a button is a press at that point of the button. A disabled button takes the click and does nothing with it. An element with a
     * `UIInputClicker` answers as a button, but a disabled one (its flag or its group's) answers "action" and "value" empty: it emitted nothing. The answer comes after the click: its handlers (an
     * action that switches the scene) have run.
     *
     * Not seen: the documents of `RmlUiDocumentComponent` have a DOM of their own.
     *
     * Registering twice replaces the handlers, so calling this again is harmless.
     */
    void RegisterUIControl(ControlService& service);

}  // namespace BECore
