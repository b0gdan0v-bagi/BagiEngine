# Editor: the Config Editor floats instead of docking with an imgui.ini saved before it became a panel asset

- Version: 0.26.2
- Entry: bug-config-editor-not-docked
- Date: 2026-11-25
- Bump: patch

## Changed
- The editor renames the windows of an older `imgui.ini` and of a saved named layout to their current ids (`MigrateLegacyWindowNames`, `Widgets/ImGuiLayoutText.h`) when it starts and when a layout is queued, so the Config Editor docks where the layout puts it. Adapt: nothing.
