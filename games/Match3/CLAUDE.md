# Match3 Module Guidelines

## Overview

`Match3Game` is an `IGame` (scene `Match3Scene`): a Bejeweled-style MVP. Executable `BagiMatch3`. Infrastructure and registration: `src/Modules/Games/CLAUDE.md`. Namespace `BECore::Match3`. The scene is in `games/Match3/config/SceneConfig.xml`.

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
    └── Match3BoardComponent      owns the model; draws board, gems and an ImGui HUD (score, Reset); routes clicks
```

- `BagiMatch3` starts in Game mode (`config/ApplicationConfig.xml`, `mode="Game"`): no editor, no ImGui, so the HUD is skipped and there is no score or Reset on screen until the engine has text rendering (TODO.md). Set `mode="Editor"` to get the HUD and the scene editor back.

- Unlike Chess, the model is owned by the scene component, not by the `IGame`.
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

## Tests

Executable `Match3Tests`, no engine services needed:

- `Match3GameModelTest.cpp`: board, matches, swaps, cascade.
- `Match3LegalSwapsTest.cpp`: `GetLegalSwaps` against a brute force of `TrySwap` on copies, `WouldSwapMatch` changes nothing, seed determinism with golden boards, version.
- `Match3GameControlTest.cpp`: the adapter on a model the test owns.

New rules go into `Game/` with a gtest case (`.claude/rules/tests-module.md`).
