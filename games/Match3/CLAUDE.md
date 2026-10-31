# Match3 Module Guidelines

## Overview

`Match3Game` is an `IGame` (scene `Match3Scene`): a Bejeweled-style MVP. Infrastructure and registration: `src/Modules/Games/CLAUDE.md`. Namespace `BECore::Match3`.

## Layout

```
src/Match3/
├── Match3Game.{h,cpp}            IGame; stateless wrapper: Start()/Reset() find the board component in the active scene and call Reset(seed)
├── Game/                         plain C++ rules, no scene or renderer
│   ├── Match3Types.h             Cell (col 0, row 0 = top-left), GemType, kGemTypeCount, kMinMatchLength
│   ├── Match3Board.*             8x8 gem grid, storage only
│   ├── MatchFinder.*             stateless detector of horizontal/vertical runs of >= kMinMatchLength
│   └── Match3GameModel.*         board, score, selected cell, deterministic RNG
└── Components/
    └── Match3BoardComponent      owns the model; draws board, gems and an ImGui HUD (score, Reset); routes clicks
```

- Unlike Chess, the model is owned by the scene component, not by the `IGame`.
- `Match3GameModel` never animates: a successful swap resolves the whole cascade (remove → gravity → refill → repeat) before returning, so the board is stable after every public call. Keep animation, if added, in the component.
- Public gameplay surface is `Reset(seed)`, `OnCellClicked` (two-step select/swap) and `TrySwap`. Use the seed for reproducible tests.
- Geometry and the RNG seed (`_originX`, `_originY`, `_cellSize`, `_seed`) are reflected for the scene editor.

## Tests

`tests/Match3/Match3GameModelTest.cpp` exercises the model without engine services. New rules go into `Game/` with a gtest case (`.claude/rules/tests-module.md`).
