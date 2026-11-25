# The Game panel and the Config Editor are panel assets

- Version: 0.26.0
- Entry: editor-asset-panels
- Merge: 2
- Date: 2026-11-25
- Bump: minor

## Added
- Panel assets learn a whole-object `Fields` bind, input columns, `enableField`, `hint`, `maxLines`, a scrolling table with a clipper, `PanelChild`, `PanelPopup` and a selected row; `PanelBindingWriter` writes a bound value back. Adapt: nothing; a game's own panel assets may use them.
- `config/panels/GamePanel.xml` and `config/panels/ConfigEditorPanel.xml`, drawn by `AssetPanelWidget` over `GamePanelAssetModel` and `ConfigEditorPanelModel`. Adapt: nothing.

## Breaking
- `GamePanelWidget` and `ConfigEditorWidget` are removed. Adapt: in a game's `WidgetsConfig.xml` replace `<widget type="GamePanelWidget"/>` with `<widget type="AssetPanelWidget" asset="GamePanel"/>` and `<widget type="ConfigEditorWidget"/>` with `<widget type="AssetPanelWidget" asset="ConfigEditorPanel"/>`.
