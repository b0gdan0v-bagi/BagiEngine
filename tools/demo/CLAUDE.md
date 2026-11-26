# BagiDemo: the engine's demo scene

`BagiDemo` is an executable of the engine repository with an `IGame` of its own (`DemoGame`, name `Demo`, namespace `BECore::Demo`), no rules and no game state: one scene, `DemoScene`, that shows the engine's components and is made to be browsed and changed in the editor. It is **not a benchmark** and nothing measures it (that is `BagiStress`, `tools/stress/CLAUDE.md`), so it is free to change what it shows. How a game plugs into the engine: `src/Modules/Games/CLAUDE.md`.

## Running it

```bash
cmake --build --preset macos-debug --target BagiDemo DemoTests
build/macos-debug/bin/BagiDemo            # Game mode (config/ApplicationConfig.xml)
build/macos-debug/bin/BagiDemo --editor   # the editor on the scene; Ctrl+Shift+E switches
```

## The scene

A 1280 x 720 view. Each kind of thing sits under a group node (no components of its own), so the hierarchy reads as a list of features; the transforms of the children are scene positions, not offsets from the group.

| Group | Nodes | Shows |
|-------|-------|-------|
| `Background` (layer -1) | `Sky`, `Stage` | quads on a lower layer |
| `Text` | `Title`, `Subtitle`, `WrappedText`, `*Caption` | outline, shadow, a second font family, a bold face, wrapping, Cyrillic; the captions name what stands above them |
| `Sprites` | `Gem`, `CrateStretched`, `ContainBox`, `Arrow`, `ArrowFlipped`, `AddBlend` / `AddGlow`, `MultiplyBlend` / `MultiplyGlass` | sprites by `spriteId`, the two fits, flip, the `Add` and `Multiply` blends |
| `Particles` | `Fountain`, `Sparks` | a looping emitter, and a one-shot one that fires at the start and again on the action `DemoBurst` |

Moving a node does not move its caption.

## Content

- `config/`: `ApplicationConfig.xml` (the engine's file with `mode="Game"`), `SceneConfig.xml` (a content file: a migration step rewrites it too, `tools/migrate/CLAUDE.md`), `TextureLibrary.xml`.
- `assets/textures/demo_atlas.png`: the engine's own drawing, cells of 64 pixels; the transparent pixels are white, so a sprite of it cannot show `Multiply` (a quad does).

## Tests

- `DemoTests` (`tests/`): `DemoSceneFixture.h` is the shipped scene and sprite library on a `RecordingRenderer`; the configs are checked here and not in `BagiEngineTests`, which does not link the demo's classes.
- `BagiDemoControlSmoke` is the engine's generic control smoke (`tools/control/tests/smoke.py`), `BagiDemoCookVerify` cooks the demo's content.
