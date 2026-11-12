---
paths:
  - "src/Modules/BECore/UI/UIControl*"
  - "tests/BECore/UIControlApi*"
  - "tools/control/**"
---

# UI over the control API (`ui_tree`, `ui_click`)

The module's contract: `src/Modules/BECore/UI/CLAUDE.md`; the protocol: `src/Modules/BECore/Control/CLAUDE.md`; the MCP tools: `tools/control/CLAUDE.md`.

- **Scope:** the canvases of the **active scene**, by the layout of the last draw (`step` a frame first). Bound values are read when asked, so no draw follows `UIVars::Set`.
- **`ui_tree`** gives every canvas (node, space, visibility, modal, input, scale, bounds, validator `issues`, `regions` (the published set, `name -> {x, y, w, h}` in canvas units, `{}` when none; present also before the first draw), and for a `SceneView` canvas `view` `{name, width, height}`: the active view, empty `name` the default) and its elements (`type`, `name`, `visible`, `interactive`, `rect` in canvas units, `outputRect` in screenshot pixels, `text` with variables (an element with a `UITextComponent`; a plain text element is `type` `UIElement`), `box` when one is authored, `region` (the name an element asks for, also when it is not published: the cause of a `MissingRegion` in `issues`; absent when none), and the control fields: `action` / `value` / `selected` / `disabled`, `icon` / `tooltip` of a button (only when set; the tooltip is the source text), for an element made of components (`UIElement::components`; a button, a toggle, a slider and a text are not classes) `components` (the type names, only when there are any) and the button fields read from its text, clicker, image and tooltip components (a clicker element with no text or image of its own answers `text` / `icon` from the first descendant that has one, not through another clicker: its caption and icon children), `bind` / `selectedValue`, `open` and the options of a dropdown (children, elements with a clicker, with no rect while closed; its `text` is the header as shown, the chosen caption or the placeholder), `checked`, `fraction`; a toggle or slider (`UIToggleComponent`, `UISliderComponent`) answers its own `action`, `bind`, `checked` / `value` / `fraction`, a toggle's `text` from its caption child).
- **`ui_click {name, canvas?, at?}`** presses the middle (`at` 0..1 sets the horizontal point: the way to set a slider) of the first visible interactive element through `UIInput`, exactly as a user would, after checking it is topmost in its canvas. It answers the `action` / `value` of the click (an element with a `UIInputClicker` as a button; a disabled one answers them empty; a toggle or slider: the component's `action`, the value asked for or emitted), or `not_found`, `not_clickable` (why), `no_scene`. A dropdown's header emits nothing; an option of an open list (an element with a clicker: `UIChoice::IsOption`) answers the dropdown's `action` and its own `value` (`ui_click` header, `step`, `ui_click` option, `step`).
- A widget changes nothing by a click alone: `ui_click`, `step`, `ui_tree` is the order.
- Names are looked up with `PoolString::Find`, never interned.
