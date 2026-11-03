# Match3 Module Guidelines

## Overview

`Match3Game` is an `IGame` (first scene `MainMenu`, then `Match3Scene`): a Bejeweled-style MVP. Executable `BagiMatch3`. Infrastructure and registration: `src/Modules/Games/CLAUDE.md`. Namespace `BECore::Match3`. The scenes are in `games/Match3/config/SceneConfig.xml`.

## Layout

```
src/Match3/
├── Match3Game.{h,cpp}            IGame; Start()/Reset() find the board component in the active scene and call Reset(seed); owns the control adapter
├── Match3GameControl.{h,cpp}     IControllableGame adapter (external control API): state, legal swaps, swap, reset(seed)
├── Game/                         plain C++ rules, no scene or renderer
│   ├── Match3Types.h             Cell (col 0, row 0 = top-left), GemType, kGemTypeCount, kMinMatchLength
│   ├── Match3Board.*             8x8 gem grid, storage only
│   ├── MatchFinder.*             stateless detector of horizontal/vertical runs of >= kMinMatchLength
│   └── Match3GameModel.*         board, score, selected cell, seed, version, deterministic RNG, legal swaps
└── Components/
    ├── Match3BoardComponent      owns the model; draws board and gems, routes clicks, publishes `score` and `seed` to UIVars, answers Match3Reset and Match3Menu
    └── Match3MenuComponent       answers Match3Play and Match3Quit in the menu scene
```

- `BagiMatch3` starts in Game mode (`config/ApplicationConfig.xml`, `mode="Game"`): no editor and no ImGui. Set `mode="Editor"` to get the scene editor back; the UI below is the engine's own, so it shows in the editor viewport too.

## Scenes and UI

`config/SceneConfig.xml` holds two scenes; `Match3Game::GetSceneName()` is `MainMenu`, so the game opens on the menu.

- **`MainMenu`**: a `Screen` canvas (`ScaleWithScreen` 1280 x 720, `minScale` 0.7, safe area on) with a centered `UIStack`: a title and the buttons Play (`Match3Play`) and Quit (`Match3Quit`). `Match3MenuComponent` on the same node answers them while the menu scene is active: Play resets the board with its configured seed and switches to `gameScene`, Quit emits `ApplicationEvents::QuitEvent`.
- **`Match3Scene`** has a view size, `viewWidth="712" viewHeight="672"`: the board (512 x 512 at (100, 80)) with the same 100 / 80 margin on every side, fitted to whatever the window is. Its HUD is a **`SceneView` canvas**, not a `Screen` one: it lives in the margins of the view, the score and the buttons Reset (`Match3Reset`) and Menu (`Match3Menu`) in the top margin and the seed in the bottom one, so it scales and moves with the board and cannot cover it in any shape of window (a `Screen` HUD in a corner did cover the board at 4:3). The buttons are 48 view units high; at the smallest fit scale of the size matrix (0.67) that is still a 32 px touch target.
- The labels are `text="Score: {score}"` and `text="seed {seed}"`: `Match3BoardComponent` sets the UIVars `score` and `seed` whenever the model version moved (before the HUD is drawn, since the board is on a lower layer). `Match3Reset` starts the board with its configured seed; `Match3Menu` switches to the component's `menuScene`.
- Actions are answered with `SubscribeInScene`, so a button of one scene does nothing in the other.
- The control API works while the menu is up: the model belongs to the board component, which exists in every loaded scene.

- Unlike Chess (BagiEngine-Chess), the model is owned by the scene component, not by the `IGame`.
- `Match3GameModel` never animates: a successful swap resolves the whole cascade (remove → gravity → refill → repeat) before returning, so the board is stable after every public call. Keep animation, if added, in the component.
- Public gameplay surface is `Reset(seed)`, `OnCellClicked` (two-step select/swap), `TrySwap`, and for a controller `WouldSwapMatch(a, b)` (tries the swap on a copy, changes nothing), `GetLegalSwaps()` (each neighbour pair once: a cell with its right and its lower neighbour, row by row), `GetSeed()` and `GetVersion()` (grows with every `Reset` and every committed swap).
- **Determinism:** the same seed gives the same board, and the same seed with the same swaps gives the same board and score, on every platform. The gems come from `std::mt19937` through `1 + rng() % kGemTypeCount`, not from `std::uniform_int_distribution` (its output differs between standard libraries). `Match3LegalSwapsTest` pins the boards of three seeds against an independent MT19937. Do not change how `RandomGem()` draws without updating those boards: every saved seed changes.
- Geometry and the RNG seed (`_originX`, `_originY`, `_cellSize`, `_seed`) are reflected for the scene editor.

## Control API

`Match3GameControl` (`IControllableGame`, see `Games/CLAUDE.md`) is how a program or a model plays. The model belongs to the active `Match3BoardComponent`, so the adapter gets it from a provider on every call (`Match3Game` hands it the active component); with no active board every method answers `no_active_model`.

- State: `{"size": 8, "board": [8 strings, row 0 on top], "score", "seed", "legalSwapCount", "status"}`. A cell is one letter: `R` red, `O` orange, `Y` yellow, `G` green, `B` blue, `P` purple, `.` empty. `status` is `playing` or `no_moves` (the game does not reshuffle; a controller resets).
- Actions: `{"type": "swap", "from": {"col", "row"}, "to": {"col", "row"}}`, every legal swap once. `perform_action` takes the same object (the cells in either order) and answers `{"scoreGained", "score"}`. Not neighbours or no match is `illegal_action`; a malformed or out-of-board cell is `invalid_params`.
- `reset` takes an optional `{"seed": 0..4294967295}`; without it the board's configured seed is used (what the Reset button does).
- `set_state` is not supported.
- **Playing enters the game.** The game opens on the menu, but a controller sees the board: a `reset` and an action that went through switch the active scene to the board's (`Match3ControlTarget::enterGame`, set in `Match3Game`), so a `screenshot` shows what is being played and the control smoke test can compare pictures. `get_state` and `list_actions` do not switch.

## Tests

Executable `Match3Tests`, no engine services needed:

- `Match3GameModelTest.cpp`: board, matches, swaps, cascade.
- `Match3LegalSwapsTest.cpp`: `GetLegalSwaps` against a brute force of `TrySwap` on copies, `WouldSwapMatch` changes nothing, seed determinism with golden boards, version.
- `Match3GameControlTest.cpp`: the adapter on a model the test owns.
- `Match3UiTest.cpp`: the shipped `SceneConfig.xml` (read from the source tree) in a scene manager with a `RecordingRenderer`: the menu and the HUD through the size matrix with the longest values, the HUD outside the board in five window shapes, Play / Quit / Reset / Menu, actions of the other scene ignored, the score on screen, a click on the Reset button at its drawn place. Needs the engine services.

New rules go into `Game/` with a gtest case (`.claude/rules/tests-module.md`).
