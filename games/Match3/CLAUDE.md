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
│   ├── CascadeTrace.*            `SwapTrace` / `CascadeStep` / `GemFall` / `GemRefill`: what a committed swap did, as plain data; `ReplayTrace` applies it to the board before the swap
│   ├── Match3GameModel.*         board, score, selected cell, seed, version, deterministic RNG, legal swaps, `CaptureState` / `RestoreState`
│   └── Match3SaveState.*         `Match3SaveState` (reflected: seed, score, board letters, generator text), the schema constants, `SaveMatch3` / `LoadMatch3` over `SaveGameService`
└── Components/
    ├── BoardAnimation.*          `BoardAnimation`: the tweens of ONE swap as one `Timeline` on a `TweenManager` (swap slide, shrink of matched gems, fall, refill from above; a rejected swap slides back), the sprites it writes, `BoardAnimationSettings` (the durations), `ClipToBoard` (a rect against the board rect); no scene, no renderer, no `CoreManager`
    ├── Match3BoardComponent      owns the model and a `BoardAnimation`; draws board and gems (or the sprites of a running swap), routes clicks (`ClickCell`), publishes `score` and `seed` to UIVars, answers Match3Reset and Match3Menu
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
- `Match3GameModel` never animates: a successful swap resolves the whole cascade (remove → gravity → refill → repeat) before returning, so the board is stable after every public call. It learns no time, tween or position, but it RECORDS what the cascade did as plain data: `GetLastSwap()` is a `SwapTrace` (the version it belongs to, the two swapped cells, and per round the removed cells, the falls `{col, fromRow, toRow}` and the refills `{col, toRow, fromRow, gem}` with a negative `fromRow`: above the board). It is cleared by `Reset` and at the start of every `TrySwap`, so it is null after a rejected swap (a stale trace is never a new one); `ResolveCascade()` alone records nothing. Recording touches neither the RNG, the score nor the version. Time, tweens and drawing stay in the component.
- Public gameplay surface is `Reset(seed)`, `OnCellClicked` (two-step select/swap), `TrySwap`, and for a controller `WouldSwapMatch(a, b)` (tries the swap on a copy, changes nothing), `GetLegalSwaps()` (each neighbour pair once: a cell with its right and its lower neighbour, row by row), `GetSeed()` and `GetVersion()` (grows with every `Reset`, every committed swap and every `RestoreState`).
- **Determinism:** the same seed gives the same board, and the same seed with the same swaps gives the same board and score, on every platform. The gems come from `std::mt19937` through `1 + rng() % kGemTypeCount`, not from `std::uniform_int_distribution` (its output differs between standard libraries). `Match3LegalSwapsTest` pins the boards of three seeds against an independent MT19937. Do not change how `RandomGem()` draws without updating those boards: every saved seed changes.
- **Save** (`Match3SaveState.*`, `Save/CLAUDE.md`): `CaptureState()` gives a reflected `Match3SaveState` `{seed, score, board, rng}`: `board` is 64 letters row by row (the letters of `get_state`, no `.`), `rng` the text `std::mt19937` writes to a stream, which makes play after a restore continue exactly as without the save. **That text is not the same on every standard library:** MSVC and libc++ write the 624 numbers of the standard, libstdc++ writes one more (the position), so a save moves between the first two but a libstdc++ build refuses (`Corrupt`) a file of the others and the other way round; the gems themselves are the same everywhere (see Determinism). A draw counter replayed with `discard()` would be the portable form. Not saved: the selection and the last swap trace (a restore has neither). `RestoreState()` checks everything first (64 gem letters, no match on the board, score not negative, the generator text is what `operator<<` writes for what `operator>>` reads, and the generator does not give only zeros: a state of zeros would end no cascade) and changes nothing on a refusal; on success it adds one to `GetVersion()`. `SaveMatch3` / `LoadMatch3` pass it through `SaveGameService` as game `Match3`, version 1 (`kMatch3SaveSchema`); a state the model refuses is `Corrupt`. The model stays plain C++ (it names `Match3SaveState` only). There is no UI, scene or control-API hook yet: the service is not in `CoreManager` and no button saves.
- Geometry, the RNG seed and the animation switch (`_originX`, `_originY`, `_cellSize`, `_seed`, `_animate`) are reflected for the scene editor.

## Animation

A swap that a person makes with the mouse slides, the matched gems shrink away, the gems above fall and the refilled gems drop in from above the board. It runs on `CoreManager::GetTweens()` (game time) as one `Timeline` per swap (`BoardAnimation`, built from the `SwapTrace` of the model); a rejected swap (two neighbours, no match) slides and slides back. Defaults, in `BoardAnimationSettings`: swap 0.125 s, clear 0.125 s, fall 0.0625 s plus 0.03125 s per row (powers of two, so a fake clock in a test gives exact sums); about 0.5 s for a swap with a cascade.

- **Which changes animate:** only `Match3BoardComponent::ClickCell(Cell)`, the click of a person (`OnMouseDown` converts the pixel to a cell and calls it). Everything else (the control API `perform_action` / `reset`, the Reset button, a test's `TrySwap` or `Reset`) is drawn at once: the component keeps the model version of the running animation (`_animatedVersion`) and a draw that finds another version drops the sprites (`Finish()`) and shows the model board. The scene follows the model, which is what a `screenshot` after a control call relies on.
- **The model does not wait:** while the sprites play, `GetModel()` already holds the final board and score, so `get_state`, `list_actions` and the score label never lag behind the picture. The score label shows the new score at once, not at the end of the animation.
- **Manual clock and headless:** the animation is ordinary tween time and moves only when frames run with a `dt` (`step`). The control API cannot click the board, so no control-driven run starts one and every screenshot of the smoke test shows a settled board (`check_control_changes_are_shown_at_once` in `tools/control/tests/smoke.py` pins it: a screenshot after `perform_action` shows the new board, and 30 stepped frames change nothing). A frame time is clamped to 0.25 s (`Time::kMaxFrameTime`), so a pause does not skip the animation in one frame.
- **Input during an animation:** a click finishes it at once (snap to the model board), then the click is handled normally and may start the next one. `Reset` and `Menu` finish it too.
- **Drawing:** the component draws the sprites only while `IsAnimating()` (the run goes on for the current model version) and the model board otherwise. Every sprite rect goes through `ClipToBoard` against the board rect in scene units (the refilled gems start above the board; `IRenderer::PushClipRect` works in output pixels), and a shrinking gem shrinks around its cell centre.
- **API of the component:** `ClickCell(Cell)`, `IsAnimating()`, `SkipAnimation()` (ends the run at once: how a test or a caller gets the end state without waiting). `_animate` (reflected, default true) switched off makes every swap show its result at once, as before the animation existed.
- Nothing about the animation is in the model, the control adapter or the scene config.

## Control API

`Match3GameControl` (`IControllableGame`, see `Games/CLAUDE.md`) is how a program or a model plays. The model belongs to the active `Match3BoardComponent`, so the adapter gets it from a provider on every call (`Match3Game` hands it the active component); with no active board every method answers `no_active_model`.

- State: `{"size": 8, "board": [8 strings, row 0 on top], "score", "seed", "legalSwapCount", "status"}`. A cell is one letter: `R` red, `O` orange, `Y` yellow, `G` green, `B` blue, `P` purple, `.` empty. `status` is `playing` or `no_moves` (the game does not reshuffle; a controller resets).
- Actions: `{"type": "swap", "from": {"col", "row"}, "to": {"col", "row"}}`, every legal swap once. `perform_action` takes the same object (the cells in either order) and answers `{"scoreGained", "score"}`. Not neighbours or no match is `illegal_action`; a malformed or out-of-board cell is `invalid_params`.
- `reset` takes an optional `{"seed": 0..4294967295}`; without it the board's configured seed is used (what the Reset button does).
- `set_state` is not supported.
- **Playing enters the game.** The game opens on the menu, but a controller sees the board: a `reset` and an action that went through switch the active scene to the board's (`Match3ControlTarget::enterGame`, set in `Match3Game`), so a `screenshot` shows what is being played and the control smoke test can compare pictures. `get_state` and `list_actions` do not switch.

## Tests

`tests/scenarios/*.jsonl` are recorded control sessions of the real game (`tools/control/CLAUDE.md`, "Scenario files"), each a ctest entry `BagiMatch3Scenario.<file>`; re-record with `bagi_play.py --update` after a deliberate change of the rules.

Executable `Match3Tests`, no engine services needed:

- `Match3GameModelTest.cpp`: board, matches, swaps, cascade.
- `Match3LegalSwapsTest.cpp`: `GetLegalSwaps` against a brute force of `TrySwap` on copies, `WouldSwapMatch` changes nothing, seed determinism with golden boards, version.
- `Match3CascadeTraceTest.cpp`: `SwapTrace` / `ReplayTrace`: replay equals `TrySwap` for every legal swap of six seeds, removed cells add up to the score, refills start above the board, a hand-made two-step cascade by value, the trace version, empty after `Reset` and after a rejected swap.
- `Match3BoardAnimationTest.cpp`: `BoardAnimation` on a `TweenManager` of the test (no engine services): sprites at t = 0 on the board before the swap, the swap slide mid-way and after, the shrink, every cell with one visible sprite of the right gem at the end (also for one huge `dt`), the settings change the length, `Finish`, destroying mid-way and a second `Start`, the rejected swap, `ClipToBoard`.
- `Match3SaveStateTest.cpp`: `CaptureState` / `RestoreState` (round trip into a fresh model, play after a restore equals play without the save for four seeds, version grows, selection and trace cleared, refused states (bad board, score or generator, a generator that gives only zeros) leave the model as it was) and `SaveMatch3` / `LoadMatch3` through a `SaveGameService` on a temp folder with a local `FileSystem` (round trip and continuation, `NotFound`, an invalid board `Corrupt`, `WrongGame`, `VersionTooNew`; the model is kept on each failure). No engine services.
- `Match3SaveDamageTest.cpp`: mutated bytes of a real save, edited states and a folder, an empty file or an oversized file in the slot, all through `LoadMatch3`: a refusal leaves the model as it was (version, board, score, generator, selection) with a short `detail`, an acceptance is a legal game. Own mutator, because the engine's `tests/Fuzz` library does not exist when the game builds in its own repository.
- `Match3GameControlTest.cpp`: the adapter on a model the test owns.
- `Match3UiTest.cpp`: the shipped `SceneConfig.xml` (read from the source tree) in a scene manager with a `RecordingRenderer`: the menu and the HUD through the size matrix with the longest values, the HUD outside the board in five window shapes, Play / Quit / Reset / Menu, actions of the other scene ignored, the score on screen, a click on the Reset button at its drawn place. The fixture `Match3AnimationTest` (same file) covers the animation of the board component with `CoreManager::GetTweens().Advance(1/32)`: two clicks animate while the model is already final, a mid-way draw differs from the settled one and the end equals a board that never animated, `TrySwap` / reset / the control path never animate, a click during a run finishes it and is handled, a rejected swap by two clicks, a click at the pixel of a cell, `_animate` off, no tween left after the scene is destroyed mid-run. Needs the engine services.

New rules go into `Game/` with a gtest case (`.claude/rules/tests-module.md`).
