# UI control API: ui_click on a disabled toggle or slider answers an action that was never emitted

- Version: 0.4.2
- Entry: ui-control-click-answers
- Date: 2026-11-18
- Bump: patch

## Changed
- `ui_click` answers an empty `action` and `value` for a disabled toggle and for a toggle without an action, and a slider answers its `action` only when the click emitted a value. A client can trust a non-empty answer as an action that happened.
