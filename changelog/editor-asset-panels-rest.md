# The rest of the editor's form parts are panel assets

- Version: 0.27.0
- Entry: editor-asset-panels-rest
- Date: 2026-11-26
- Bump: minor

## Added
- Panel asset format: conditions with `and`, `or` and compare; `PanelCheckbox`, `PanelCombo`, `PanelNumber` (drag, slider, stepper, typed input, `unit`, `decimals`), `PanelRadio`, `PanelInput lines`, `PanelTabs` / `PanelTab`, `PanelMenu` / `PanelMenuItem` / `PanelMenuItems`, `PanelDropdown`, `PanelProgress`, a checkbox table column, `tooltipBind` / `tooltipField`, `PanelSection icon` / `openBind` / `look="Tree"`, a button with a literal `argument`, a `width` (`-1` fills the line) and `closePopup`, a window title bound to a text (`titleBind`). Adapt: nothing; every new attribute is optional.
- Window kinds of an asset: `Docked`, `Floating`, `Dialogs` and `Bar` (drawn by a C++ window through `PanelBars`). Adapt: nothing.
- Shipped panel assets: `ConsolePanel`, `ProfilerPanel`, `PreferencesPanel`, `EditorDialogsPanel`, `SceneDialogsPanel`, `MainMenuPanel`, `MainToolbarPanel`, `ViewportToolbarPanel`, `SpriteLibraryPanel`, `GeneratePanel`, `SceneEditorToolbarPanel`, `SceneInspectorPanel`, with their models (`ConsolePanelModel`, `ViewportToolbarModel`, `SpriteLibraryPanelModel`, `ArtGenPanelModel`, `SceneEditorPanelModel`, ...) and host registries (`ViewportToolbarRegistry`, `ISceneEditorHost`). Adapt: nothing.

## Changed
- The Sprite Library window is `Sprite Library###SpriteLibrary`; a saved layout keeps its dock place through a row of `MigrateLegacyWindowNames`. Every `PanelSection` label is `<icon> <title>###<id>`, so a section's open state in the Game panel and the Profiler is not restored once. Adapt: nothing.

## Breaking
- The widget types `ConsoleWidget`, `ProfilerWidget`, `TextureEditorWidget` and `ArtGenWidget` and the class `PreferencesWindow` are removed; their windows are panel assets drawn by `AssetPanelWidget`. Adapt: in a game's own `config/WidgetsConfig.xml` replace each `<widget type="XWidget"/>` with `<widget type="AssetPanelWidget" asset="..."/>`: `ConsolePanel`, `ProfilerPanel`, `SpriteLibraryPanel`, `GeneratePanel`.
- The menu bar, the main toolbar, the Preferences window, the editor and Scene Editor dialogs, the Viewport toolbars and the Scene Editor's Save row and inspector frame are drawn only when their asset line is in `WidgetsConfig.xml`; without `ViewportToolbarPanel` F8 and W/E/R/X do nothing. Adapt: a game with its own `WidgetsConfig.xml` adds the `AssetPanelWidget` lines of the engine's `config/WidgetsConfig.xml`: `MainToolbarPanel`, `MainMenuPanel` and `ViewportToolbarPanel` before `EditorLayoutWidget`, `PreferencesPanel` and `EditorDialogsPanel` after it, `SceneEditorToolbarPanel` and `SceneInspectorPanel` before `SceneEditorWidget`, `SceneDialogsPanel` after it.
