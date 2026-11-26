---
name: perf-render
description: Investigates why the rendering of one benchmark or scenario is slow or draws more than it should - CPU-bound or GPU-bound first, then draw calls and what breaks the batches, overdraw and fill, geometry and texture uploads, a frame for the eye - from the renderer's counters, the GPU time and the platform's GPU tools, makes hypotheses and files a todo entry with the numbers; changes no code. Use when the user asks about draw calls, batching, batch breaks, overdraw, fill rate, GPU time, whether a frame is GPU-bound, or why rendering is slow, says "perf-render", "почему столько draw calls", "тормозит рендер", "что ломает батчи", "разберись с рендером", "GPU-bound или CPU-bound", "overdraw".
---

# perf-render

`/perf-render <benchmark | scenario>`. You find what limits the picture and say what to change, with numbers; you change no code. Terms as in `.claude/skills/perf/SKILL.md` (`<engine>` is `.` in the engine and `engine` in a game); read `.claude/rules/perf-project.md` of the repository. Read first: `<engine>/src/Modules/BECore/Profiler/HowToProfile.md` ("GPU time") and `<engine>/.claude/rules/rendering-module.md` ("Batching", "Draw counters"); for `BagiStress`, `<engine>/tools/stress/src/Stress/CLAUDE.md` ("Loads").

## Steps

1. **Target and backend.** A name of the set: its command. Anything else is a scenario: write its command and say what you chose. A question about the GPU needs a GPU backend: `vulkan` (the only one with a GPU time) or `sdl-gpu`, in a hidden window with the present uncapped. The headless software renderer leaves textured quads unbatched and has no GPU time, so the draw calls of a headless benchmark say nothing about batching on a GPU: measure its scene again with a `--backend`. A machine without the backend answers `skip:` and exit code 77: say what stays unanswered.
2. **Measure.** Build the set's `preset` for the numbers and its `profilePreset` for the zones (`build` skill, by name). Check the load as `/perf`, step 1.
   - `BagiStress`, one JSON line: `python3 <engine>/tools/perf/stress_frames.py --exe build/<preset>/bin/BagiStress --scene <sprites|ui|entities> --count <N> --frames 300 --warmup 30 --backend vulkan`, with the load flags of the benchmark's command (`--sprite-size`, `--world`, `--churn`, `--mix` with `--warmup 90`). Per frame: `drawCalls`, `primitives`, `triangles`, `textureSwitches`, `batchBreakTexture`, `batchBreakBlend`, `batchBreakFull`, `batchBreakClip`, `batchBreakUnbatched`, `batchBreakBoundary`, `geometryUploadBytes`, `textureUploadBytes`; times: `cpuMs`, `workMs`, `waitMs`, `presentMs`, `updateMs`, `drawMs`, `submitMs`, `gpuMs`, `gpuMsP50`, `gpuMsP99`.
   - A benchmark with `traceCommand`, the trace: `python3 <engine>/tools/perf/perf.py --trace <name> --trace-file <scratchpad>/<name>.json`, then `python3 <engine>/tools/perf/trace_summary.py <scratchpad>/<name>.json --top 30`. Counters: `Draw calls`, `Primitives`, `Triangles`, `Texture switches`, `Batch breaks: texture` (and `blend`, `full`, `clip`, `unbatched`, `boundary`), `Geometry upload bytes`, `Texture upload bytes`, `GPU ms` (Vulkan). The trace holds the start-up and the warm-up too, so take the steady values from the line, and the zones and maxima from the trace.
   - A heavy scene fills the trace's buffer and the file ends early, which `trace_summary.py` does not say: check `python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["otherData"].get("dropped",0))' <file>`. Above 0, record fewer frames, with the profile build: the `stress_frames.py` command above with `--exe build/<profilePreset>/bin/BagiStress`, a smaller `--frames` and `--trace <file>`.
   - A game: `python3 <engine>/tools/perf/headless_frames.py --exe <exe> --frames 600` gives the counts a headless run has (`drawCalls`, `primitives`, `triangles`, `textureSwitches`). The breaks, the upload bytes and the GPU time come from a windowed run of its own in the profile build: a scenario file in the scratchpad played by `python3 <engine>/tools/control/bagi_play.py <exe> <scratchpad>/render.jsonl`, then `trace_summary.py` on its trace.

     ```json
     {"scenario": 1, "game": "<name of list_games>", "flags": ["--hidden-window", "--backend=vulkan", "--manual-clock", "--trace=<scratchpad>/game.json"], "title": "render"}
     {"method": "step", "params": {"frames": 300}}
     {"method": "quit"}
     ```

     The calls that bring the game to the scene in question go before the `step`.
3. **CPU-bound or GPU-bound,** first, by the table of `HowToProfile.md` ("CPU-bound or GPU-bound"), from the line of a Vulkan run (a game: from its trace, where the zone `Frame` is the CPU frame, `Vulkan::WaitFence` and `Vulkan::AcquireImage` are the wait, `Present` the present and the counter `GPU ms` the GPU). CPU-bound in the update, the tasks or anything outside the draw pass (`updateMs` and the zones): that goes back to `/perf-investigate`; say so. CPU-bound in the draw pass (`drawMs`, `submitMs`): go on with steps 4 and 6. GPU-bound: steps 4 to 6.
4. **Draw calls against what the scene needs:** about one per run of neighbouring draws of one texture and one blend mode inside one clip, plus each draw that is never batched. `drawCalls` minus the sum of the `batchBreak*` values is the draws made at once (shapes, particle emitters and every other `DrawGeometry`; everything with batching off); each break value is the batches that reason ended. Take the largest first. A batch a frame under `batchBreakBoundary` is the end of the scene pass. The unbatched side of a comparison: the same `stress_frames.py` command with `--no-batching`. `drawCalls` near `primitives` on a GPU backend means the batcher does nothing there. Vulkan draws every blend mode as straight alpha, yet a change of mode still ends a batch.
5. **Overdraw and fill** have no counter. Move the pixels and hold the calls: `stress_frames.py --sprite-size <S>` at two or three sizes, same count; `gpuMs` (and `waitMs`) that grows with the area is fill. Hold the size and move `--count` to see the cost per object. A game: the same scene with its large or full-screen layers smaller or off (a hidden window has the configured size, no flag changes it). On `sdl-gpu` read `presentMs` instead, with the caution of `HowToProfile.md`. Which pass or draw pays is the outside tool's answer ("Tools outside the engine"; on macOS "Metal System Trace").
6. **Uploads.** `textureUploadBytes` above 0 in a steady scene is a finding by itself: a texture is filled every frame or every few frames (find the `CreateTexture` caller or the loader; glyph pages of new text). `geometryUploadBytes` over `primitives` is the bytes per primitive: steady for plain quads on one backend (compare within a backend only), so a rise at the same `primitives` is geometry with many vertices (`DrawGeometry`: particles, shapes, shadows of UI boxes). On SDL, draws made at once may add nothing.
7. **The frame for the eye,** when the numbers do not say what is drawn: the `screenshot` of the control API on the headless SDL renderer, or for `BagiStress` on Vulkan `python3 <engine>/tools/stress/qa/vulkan_look.py --exe <exe> --scene sprites --count <N> --out <scratchpad>/look` (it opens a window: ask first; `--dry-run` shows what it would start). A headless picture is a two-line header and calls played by `python3 <engine>/tools/control/bagi_backends.py <exe> <scenario> --out <scratchpad>/pictures --backends software`:

   ```json
   {"scenario": 1, "game": "<name of list_games>", "flags": ["--headless", "--manual-clock"], "title": "frame"}
   {"method": "step", "params": {"frames": 30}}
   {"method": "screenshot"}
   ```

8. **Hypotheses,** two to four, as step 5 of `perf-investigate`, ordered by the expected gain:

   | Cause | What proves it | Fix | Verify by |
   |-------|----------------|-----|-----------|
   | sprites of many textures mixed | `batchBreakTexture`, `textureSwitches` high | an atlas: one texture for what is drawn together (`TextureLibrary`) | both fall, `drawCalls` with them |
   | materials or blend modes alternate (the renderer never reorders) | `batchBreakTexture` or `batchBreakBlend` far above the number of textures or modes | order the nodes or layers so that one material runs together | the same two counters |
   | many clips | `batchBreakClip` | fewer clip pushes: one clip around a list, none where nothing sticks out | `batchBreakClip` |
   | draws that are never batched between quads | `batchBreakUnbatched` and `drawCalls` minus the sum of the breaks | draw them together, or fewer of them; the engine does not batch `DrawGeometry` yet | both |
   | batches at their size limit | `batchBreakFull` | by design; nothing unless it is most of `drawCalls` | `batchBreakFull` |
   | passes cut by render target switches | `batchBreakBoundary` above one a frame with offscreen targets | fewer target switches | `batchBreakBoundary` |
   | work for what is off screen | `primitives` above what is visible, or an `OnDraw()` component off screen | the scene culls quads and sprites; a component that draws itself drops its own | `primitives`, `workMs` |
   | overdraw and fill | step 5: `gpuMs` follows the area | smaller or fewer overlapping layers, trimmed transparent borders | `gpuMs` at the same count |
   | textures filled in a steady frame | `textureUploadBytes` above 0 | make the texture once and keep it | `textureUploadBytes` 0 |
   | heavy geometry | `geometryUploadBytes` per primitive | fewer vertices or particles | `geometryUploadBytes` |
   | one draw per copy of a mesh | `drawCalls` follows the object count | instancing: **not available yet** (entry `gpu-instancing`) | - |
   | a shadow map redrawn although nothing in it moves | - | a static shadow map: **not there yet** (3D, entry `gpu-backend-first` and after) | - |
   | full frames although nothing changes | the counts and times of an idle scene | render on demand: **not available yet** (entry `render-on-demand`) | - |

9. **Entry and report:** steps 6 and 7 of `<engine>/.claude/skills/perf-investigate/SKILL.md`, with the backend and the numbers of the line beside the machine, the build and the commit. Add **not measured yet**: pipeline binds, instances per draw, GPU time per pass or zone, an overdraw counter, GPU time outside Vulkan, and the batch breaks and upload bytes in `frame_stats` and the line of `headless_frames.py`.

## Rules

As `perf-investigate`: no code changes and no commits, the files stay in the scratchpad, every number quoted comes from a run of this session with its preset, backend and commit, one run at a time on a quiet machine. Counts are exact on the manual clock and in a fixed run; a time from one run is not a result.
