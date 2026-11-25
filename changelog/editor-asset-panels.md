# Editor: tool panels described as XML assets drawn on ImGui; the File changes panel is the first

- Version: 0.25.0
- Entry: editor-asset-panels
- Date: 2026-11-25
- Bump: minor

## Added
- Panel assets (`config/panels/*.xml`, `BECore/Widgets/PanelAssets/`): a panel's layout, bound fields, tables and buttons are an XML asset, loaded with limits and fuzzed, drawn on ImGui by `PanelInterpreter` and hosted by `AssetPanelWidget` with hot reload (the last good asset stays, an error banner shows); data comes from an `IPanelModel` through `PanelDataSources`, custom parts through `PanelSlots`. Adapt: nothing; a game's editor panel can now be an asset (`src/Widgets/PanelAssets/CLAUDE.md`).

## Breaking
- `FileChangesWidget` is removed; the File changes panel is `config/panels/FileChangesPanel.xml` hosted by `AssetPanelWidget` over `FileChangesPanelModel`. Adapt: a game widgets config that lists `FileChangesWidget` replaces it with the `AssetPanelWidget` entry of the engine's `config/WidgetsConfig.xml`.
