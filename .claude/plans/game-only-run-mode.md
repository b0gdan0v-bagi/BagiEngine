# Game-only run mode

Roadmap stage 2, TODO entry "No game-only run mode". Done on `main`.

## Problem

The scene is drawn only by `ViewportWidget` (offscreen target + `ImGui::Image`) and mouse clicks reach it only through
the viewport remap. A game executable therefore always ships the editor.

## Design

- `mode` attribute in `ApplicationConfig.xml`: `Editor` (default, behavior unchanged) or `Game`. `RunMode` is a
  `CORE_ENUM` in `BECore/Application/RunMode.h`; `ReadRunMode()` parses it, `GetWidgetsConfigName()` maps it to a config.
- `WidgetManager::CreateWidgets()` reads the mode and loads `WidgetsConfig` (Editor) or `GameWidgetsConfig` (Game).
  `GameWidgetsConfig.xml` in the engine: `ClearScreenWidget` + `SceneRenderWidget`, no ImGui.
- `SceneRenderWidget` (`src/Widgets/`): `DrawScreen()` calls `SceneManager::DrawAll()`, which records into the swapchain
  pass that `ClearScreenWidget::Update()` opened. Window coordinates are scene coordinates, no remap.
- `SDLEventsProvider` forwards the raw mouse button when there is no ImGui context or ImGui does not want the mouse
  (today it reads `ImGui::GetIO()` unconditionally, which has no context in Game mode).
- `Match3BoardComponent` draws its ImGui HUD only when an ImGui context exists.
- Chess and Match3 get `config/ApplicationConfig.xml` with `mode="Game"` (a game's file replaces the engine's whole file,
  so it repeats `type`, `renderer`, `path`).

## Steps

1. [x] Read the frame path in both backends, the widget manager, the event provider.
2. [x] `RunMode`, `WidgetManager`, `GameWidgetsConfig.xml`, `ApplicationConfig.xml` (`mode="Editor"`).
3. [x] `SceneRenderWidget`.
4. [x] `SDLEventsProvider` guard, Match3 HUD guard.
5. [x] Game `ApplicationConfig.xml` for Chess and Match3.
6. [x] Tests: `RunModeTest` (parse, config names, engine configs), `SDLEventsProviderTest` (click without and with an ImGui
   context).
7. [x] Build, run `ctest`, start BagiMatch3 and BagiChess in Game mode, drive a click through an injected SDL event.
8. [x] Docs: `BECore/CLAUDE.md`, `widgets-module.md`, `Games/CLAUDE.md`, `SDL/CLAUDE.md`, `Match3/CLAUDE.md`, `TODO.md`.
9. [x] `clang-format`, `strip-pch-includes`.

## Not in this step

Match3 has no score or Reset in Game mode until the text entry is done (the HUD is ImGui). A command-line switch for the
mode, headless rendering and the control API are separate TODO entries.
