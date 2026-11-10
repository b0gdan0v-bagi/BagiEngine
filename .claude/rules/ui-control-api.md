---
paths:
  - "src/Modules/BECore/UI/UIControl*"
  - "tests/BECore/UIControlApi*"
  - "tools/control/**"
---

# UI over the control API (`ui_tree`, `ui_click`)

The module's contract: `src/Modules/BECore/UI/CLAUDE.md`; the protocol: `src/Modules/BECore/Control/CLAUDE.md`; the MCP tools: `tools/control/CLAUDE.md`.

- **Scope:** the canvases of the **active scene**, by the layout of the last draw (`step` a frame first). Bound values are read when asked, so no draw follows `UIVars::Set`.
- **`ui_tree`** gives every canvas (node, space, visibility, modal, input, scale, bounds, validator `issues`, and for a `SceneView` canvas `view` `{name, width, height}`: the active view, empty `name` the default) and its elements (`type`, `name`, `visible`, `interactive`, `rect` in canvas units, `outputRect` in screenshot pixels, `text` with variables, `box` when one is authored, and the control fields: `action` / `value` / `selected` / `disabled`, `bind` / `selectedValue`, `open` and the options of a dropdown (children with no rect while closed), `checked`, `fraction`).
- **`ui_click {name, canvas?, at?}`** presses the middle (`at` 0..1 sets the horizontal point: the way to set a slider) of the first visible interactive element through `UIInput`, exactly as a user would, after checking it is topmost in its canvas. It answers the `action` / `value` of the click, or `not_found`, `not_clickable` (why), `no_scene`. A dropdown's header emits nothing; an option of an open list answers the dropdown's `action` and its own `value` (`ui_click` header, `step`, `ui_click` option, `step`).
- A widget changes nothing by a click alone: `ui_click`, `step`, `ui_tree` is the order.
- Names are looked up with `PoolString::Find`, never interned.
