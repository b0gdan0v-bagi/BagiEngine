# Editor: no panel drives the active game (state, actions, reflected fields) without the console

- Version: 0.1.3
- Entry: editor-game-control-panel
- Date: 2026-11-16
- Bump: patch

## Added
- Editor Game panel (`view.game`, Ctrl+Shift+G): the active game's state, its `IControllableGame` actions as buttons, its reflected fields and its `BE_FUNCTION` methods with Console-style arguments. A game shows more there by marking fields `BE_REFLECT_FIELD` and methods `BE_FUNCTION`; no code of its own is needed.
- `ParseConsoleParams` (`Widgets/Console/ConsoleCommandLine.h`): the parameters of a Console command alone, `key=value ...` or a JSON object.
- View menu lists every editor panel (Viewport, Scene Editor, Config Editor, Sprite Library, Game, Console, Profiler); the first four get a close button and the commands `view.viewport`, `view.sceneEditor`, `view.configEditor`, `view.spriteLibrary` (no default chord).

## Changed
- `config/WidgetsConfig.xml` lists `GamePanelWidget`. A game that ships its own `WidgetsConfig.xml` gets no Game panel until it adds the entry. Adapt: add `<widget type="GamePanelWidget" />` after `ConsoleWidget` in the game's `WidgetsConfig.xml`, if it has one.
