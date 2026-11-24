# The `rmlui` scene of BagiStress

The scene `StressRmlUi` (flag `rmlui`): `count` RmlUi documents, each its own `Rml::Context`, and one scrollable panel. It loads the paths of `RmlUiHost` that the other scenes never touch: many contexts, the font engine's glyph pages, the wheel, `Reload()`, and the release of a context.

## Only with the RmlUi host

- `StressScene::RmlUi`, its entry in `kSceneNames` and so `get_state.scenes`, `set_scene`, `reset` and `--stress-scene=rmlui` exist **only when `STRESS_WITH_RMLUI` is defined**. `tools/stress/CMakeLists.txt` defines it (PUBLIC, with the link to `RmlUiHostModule`) when the target exists, i.e. with `-DENGINE_UI_RMLUI=ON` (`macos-rmlui-debug`, `-release`). Without it `--stress-scene=rmlui` is the usual "not one of" warning.
- The scene in `config/SceneConfig.xml` and `StressRmlUiComponent` are in **every** build, so one content file is valid in every preset (`ContentFilesVersionTest`); without the option the component does nothing and the scene is never shown. The header names no RmlUi header, the body of the `.cpp` is under the same `#ifdef`.

## What `count` is

`count` is documents. Each is a `RmlUiDocumentComponent` (`Screen`, 1920 x 1080 reference) on a child node of `Docs`, given `SetMarkup(MakeDocumentMarkup(...))` (`RmlUiField`, pure): one block in its cell of the `UiField` grid with `kRmlUiSizes` lines of plain text, each at another `font-size`, English or Russian by the parity of the index and the language. The body says `pointer-events: none`, so a wheel over a document reaches what lies under it. `StressRmlUiComponent` appends or removes at the tail when `StressState::Version()` moved, and gives every document its markup again when the count or the language changed (the cell depends on the count). The panel node is made at the first update, after `Docs`, so it draws and takes input on top: `assets/ui/stress_rmlui.rml` (+ `.rcss`), a box of `overflow: scroll` with content larger in both axes and coloured corners (a screenshot shows which corner is visible). A test sets another path with `SetPanelSource`.

**What it does not make: many textures.** The glyph pages belong to the font engine, one per face, size and page, and RmlUi keeps **one callback texture per page for all contexts** (one `RenderManager` per render interface), and removing a context releases none: they go with `Rml::ReleaseTextures()` or at `Rml::Shutdown()`, which `RmlUiRuntime` calls with the last holder. So with the fixed sizes the texture count stays about a dozen at any count and a shrink releases none; a context costs geometry and layout, not a texture. A scene that passes the 512 descriptors of the Vulkan backend needs distinct sizes per document: measured with sizes `8 + (index * kRmlUiSizes + line) % 300` the textures were 108 at 50 documents, 245 at 200, 356 at 600 (1.5 GB resident: a page is 1 MB), so 512 is not reachable at a count a Debug build lays out. `<img>` goes through the resource cache and never takes the GPU wait either. `ReleaseTexture()` and its GPU wait are therefore reached only by `Shutdown` (nothing in the engine calls `Rml::ReleaseTextures()`); do not expect `set_count` to exercise them.

## What a controller sees

Frame counters, set every frame (`RmlUiCounters` in the header; `frame_stats` answers them, `get_state` is `StressGameControl`'s and does not change): `Stress rmlui documents`, `Stress rmlui textures` (the render interface's `GetTextureCount()`), `Stress rmlui reloads` (requests served), `Stress rmlui scroll x` / `scroll y` (the panel's `scroller`, whole pixels, 0 without a panel document). `Stress objects` stays the count.

- **Reload** is `call_method` on the root `game`, `{"root": "game", "method": "ReloadRmlUiDocuments"}`, not a `perform_action`: it raises a request counter (`StressGame::GetRmlUiReloadRequests()`) that the component compares with the number it served, and then `Reload()`s the panel and sets every document's markup again, once. A new action would change `StressGameControl` and its table; the method is a supported surface of `game`. The panel is a new document afterwards, so its scroll is 0.
- **The wheel** is the generic `wheel_at` (`BECore/Input/CLAUDE.md`): `{"x": 1700, "y": 920, "dy": -1}` is inside the panel's scroller at 1920 x 1080. SDL `y` negative (toward the user) scrolls down, `x` positive scrolls right, `flipped` reverses both (`RmlUiHost/CLAUDE.md`, "The component"). RmlUi scrolls smoothly over the following frames of the game clock, so `step` a dozen frames before reading the counters.

## Run it

`macos-rmlui-debug`: `build/macos-rmlui-debug/bin/BagiStress --headless --control --manual-clock --stress-scene=rmlui --stress-count=20`, then `set_count`, `set_language`, `wheel_at`, `call_method`, `screenshot`. The tests are `StressRmlUiTest` (`ctest --preset macos-rmlui-debug`; documents, textures, the wheel signs, the reload, a log with no warning) and `RmlUiFieldTest` (every preset).

**Not there:** a benchmark in `tools/perf` (the benchmark set has one preset, without RmlUi), a recorded `tests/scenarios/*.jsonl` (they run in every preset and the scene exists in one), a Vulkan readback for `RequestCapture()`.
