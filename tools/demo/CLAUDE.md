# BagiDemo: the engine's demo scene

`BagiDemo` is an executable of the engine repository with an `IGame` of its own (`DemoGame`, name `Demo`, namespace `BECore::Demo`), no rules and no game state of its own: one scene, `DemoScene`, that shows the engine's components and is made to be browsed and changed in the editor. It is **not a benchmark** and nothing measures it (that is `BagiStress`, `tools/stress/CLAUDE.md`), so it is free to change what it shows. How a game plugs into the engine: `src/Modules/Games/CLAUDE.md`.

## Running it

```bash
cmake --build --preset macos-debug --target BagiDemo DemoTests
build/macos-debug/bin/BagiDemo            # Game mode (config/ApplicationConfig.xml)
build/macos-debug/bin/BagiDemo --editor   # the editor on the scene; Ctrl+Shift+E switches
```

The editor page in a browser: `python3 tools/web/build_and_deploy.py --game BagiDemo --preset web-editor`, then `python3 tools/web/serve.py --root build/web-editor-dist --host 127.0.0.1`. `web/targets.json` names it as the target `engine-demo`, the page `demo` of the engine's site (`tools/web/DEPLOY.md`).

## The scene

A 1280 x 720 view. Each kind of thing sits under a group node (no components of its own), so the hierarchy reads as a list of features; the transforms of the children are scene positions, not offsets from the group.

| Group | Nodes | Shows |
|-------|-------|-------|
| `Background` (layer -1) | `Sky`, `Stage`, `TweenStage` | quads on a lower layer |
| `Text` | `Title`, `Subtitle`, `WrappedText`, `*Caption` | outline, shadow, a second font family, a bold face, wrapping, Cyrillic; the captions name what stands above them |
| `Sprites` | `Gem`, `CrateStretched`, `ContainBox`, `Arrow`, `ArrowFlipped`, `AddBlend` / `AddGlow`, `MultiplyBlend` / `MultiplyGlass` | sprites by `spriteId`, the two fits, flip, the `Add` and `Multiply` blends |
| `Tweens` | `SlidingStar`, `TurningRing`, `BobbingArrow` | `DemoTweenComponent`: move and turn there and back |
| `Particles` | `Fountain`, `Sparks` | a looping emitter, and a one-shot one that fires at the start and again on the action `DemoBurst` |
| `Panel` (layer 10) | the node itself | a `SceneView` canvas (it scales with the editor's viewport): `Burst`, the `Animate` toggle, the `Emission` slider and its label; beside it `DemoControlsComponent` |

Moving a node does not move its caption.

## The two components (`src/Demo/Components/`)

The contract of each is on its class.

- **`DemoTweenComponent`** writes the transform as a change of what it added before (the delta rule), so an editor drag or a typed value is kept and the node moves around it; with `Animate` off the node stands without the addition. **Limit:** the transform is the node's one position, so a desktop Save (or a Duplicate) while a tween runs takes the pose of that moment: switch `Animate` off first.
- **`DemoControlsComponent`** is the game side of the panel (`UI/CLAUDE.md`: a widget owns no state): it sets `demoAnimate` from the toggle's action, and the slider's action sets the rate of the emitter named by `emitterNode`, from which `demoEmission` follows. A lower rate restarts that emitter's schedule, or it would stay silent (`Particles/CLAUDE.md`, "The simulation"). `Burst` needs no code: the button's action is `Sparks`' `burstAction`.
- The names the XML shares with the code are in `DemoNames.h`.

## Content

- `config/`: `ApplicationConfig.xml` (the engine's file with `mode="Game"`), `SceneConfig.xml` (a content file: a migration step rewrites it too, `tools/migrate/CLAUDE.md`), `TextureLibrary.xml`.
- `assets/textures/demo_atlas.png`: the engine's own drawing, cells of 64 pixels; the transparent pixels are white, so a sprite of it cannot show `Multiply` (a quad does).

## Tests

- `DemoTests` (`tests/`): `DemoSceneFixture.h` is the shipped scene and sprite library on a `RecordingRenderer` the size of the view, with clicks on the panel through `UIInput`; the configs are checked here and not in `BagiEngineTests`, which does not link the demo's classes. `DemoGuideTweenTest` loads the example scene of the tween page (`docs/guide/examples/tween/`) and pins where its three movers are after a fixed time. `DemoGuideActionTest` loads the example of the input page (`docs/guide/examples/input-actions/`): a press on its pad bursts the emitter, one beside the pad does not.
- `BagiDemoControlSmoke` is the engine's generic control smoke (`tools/control/tests/smoke.py`), `BagiDemoCookVerify` cooks the demo's content.
- `BagiDemoScenario.title` (`tests/scenarios/title.jsonl`) pins the title text written over `scene_set` and what the first frames draw: the screenshot is compared with `references/title.png` at `threshold` 24 and `maxShare` 0.2%, chosen to absorb another machine's anti-aliasing (taken and checked on macOS only) and to fail a changed word; `bagi_play.py <exe> <file> --update` retakes it and the PNG is reviewed like code. `BagiDemoGuideScript.<name>` run the Python of the guide (`docs/guide/scripts/`).
