# tools/docs

The tools of the guide for people, `docs/guide/` (rules for its authors: `.claude/rules/guide-docs.md`). The guide has no `CLAUDE.md` and is outside the docs budget: an agent reads it only when it is sent there. Standard library only, Python 3.10+.

| File | Purpose |
|------|---------|
| `check_guide.py` | the page check; `check(root) -> list[Finding]` and `main` |
| `guide_shots.py`, `guide_shots.json` | the picture tool and its rows |
| `docs/guide/scripts/*.py` | the Python a page owns: excerpted by the page, self-checking with `assert`, one ctest `BagiDemoGuideScript.<name>` each (registered in `tools/demo/CMakeLists.txt` by a glob, the lock of the game-starting tests, skipped with 77 when `BagiDemo` is not built; the game's path is the first argument, and `--cleanup` is passed to every script, so each accepts it) |
| `tests/` | pytest: temp trees for the check, the control tests' fake game for the picture tool (`.venv/bin/python -m pytest tools/docs/tests`) |

## The page check

```bash
python3 tools/docs/check_guide.py [--root DIR]
```

Run by the ctest `BagiGuideCheck` in the everyday `ctest --preset <p>`: it reads a few Markdown files and writes nothing. Exit 0 clean, 1 with one line per finding, `<page>:<line>: <message>`. No page under `docs/guide/` (a game that carries the engine, or the pictures taken before the first page): exit 0 with a note.

A page that drifts from the code teaches a wrong thing to a reader who cannot tell, so what can be checked by a file name is checked:

- **Every fenced block has a marker** as the nearest non-blank line before it. `<!-- source: <path> -->`: an excerpt. The block split at lines that are only `...` is a list of segments; each segment's lines (blank ones ignored, indentation ignored) are a contiguous run of that file's lines, the segments in order. Excerpts of the engine's own content, not copies, so a content migration moves the page with the file. `<!-- command -->`: a shell block; a token whose first path part is a top-level entry of the repository (not `build`, not `.venv`) must exist, and `--preset`, `--build` and `--test-dir build/X` name a preset of `CMakePresets.json`. `<!-- illustration: <reason> -->`: an output, a diagram or text that exists in no file; the reason is mandatory and the run prints how many there are.
- **Links and images** (relative, anchors not checked) resolve. **A backticked path** that starts with a top-level entry and has a `/` and an extension or a trailing `/` exists; a word with `<`, `*`, `{`, `$` or `...` is a placeholder and skipped.
- **The index** `docs/guide/README.md` links every other page. **Every `docs/guide/img/*.png` has a row** in `guide_shots.json`: a picture that cannot be taken again is a bug. No `CLAUDE.md` under `docs/guide/`. The repository `README.md` links the index.

## The pictures

```bash
python3 tools/docs/guide_shots.py [--only NAME[,NAME...]] [--preset P] [--bin DIR] [--out DIR]
```

No capture mechanism of its own: it starts a built game through `GameSession` (`tools/control/CLAUDE.md`, headless, manual clock), calls the steps of a row and writes the control API `screenshot` as `docs/guide/img/<name>.png`. Unlike `tools/shots` (before / after pictures that are never committed) these are committed, so the page and the picture stay one commit apart and a changed UI is one command away from a new picture. `guide_shots.json` is `{"shots": [row, ...]}`; a row:

| Key | Meaning |
|-----|---------|
| `name` | lower case, digits, dashes: the file name and the key the page check looks for |
| `exe` | the target name in `build/<preset>/bin` |
| `preset` | optional; `--preset` wins, then this, then `macos-debug` on macOS, else an error |
| `window`, `density` | optional start size (`WxH`, window units) and density of the headless window |
| `contentOverlay` | optional folder of the repository (relative, no `..`): copied to a temporary folder outside it, which is the game's content overlay (a game writes there, and `GameSession` refuses an overlay inside a git work tree); removed after the picture, the source never changes; a missing folder is an error naming the row |
| `editor` | start with `--editor`; a build without the editor is an error, not a Game picture |
| `steps` | `[method, params]` control calls before the picture (`step`, `imgui_input`, ...); a click on an editor panel is a `move`, a few frames, then the `click`, at a point of that window size |
| `maxSize` | the longer side of the picture, 640 by default |
| `about` | one line: what it shows |

`docs/guide/examples/<page>/` holds the real content files a page needs (a scene, a screen, strings, themes, a node file) in a game's layout, `config/` and `assets/`: the single copy, which the page excerpts (`<!-- source: ... -->`) and the rows copy (`contentOverlay`). `python3 tools/migrate/migrate.py --root .` finds its content files, so a content migration moves them with the demo's; `GuideExamplesTest` (`tests/BECore/`) loads them, runs the screen on the size matrix and checks that the prefab example gives its three instances with their own overrides. `DemoGuideTweenTest` (`tools/demo/tests/`) loads the tween example and pins the places of its movers that the page states; `DemoGuideActionTest` loads the input example and pins that the pad's action and the emitter's `burstAction` meet and that a press outside its ellipse does not burst; the C++ the pages show is excerpted from real files that tests run: the demo's own component, or a `tests/BECore/Guide<Topic>Example*` file the page owns (`GuideTweenExampleTest` pins the values of the sequence timeline at its times, `GuideConfigExampleTest` the config read, its warnings, the reload and the subscriber, `GuideSaveExampleTest` the save round trip, the fix-up of an older file and the refusals, `GuideCppRulesExampleTest` one unit for each C++ rule of the rules page, the steady-frame label with its allocation check), never from an uncompiled block and never from C++ under `docs/guide/examples/`.

The editor reads and writes its layout and preferences in a per-user folder, so each picture runs with `BAGI_EDITOR_DIR` set to an empty temporary one: a picture does not depend on who takes it, and nothing lands in the person's own folder. Exit 0 done, 1 an error (a bad row, a failed call), 77 an executable is missing (nothing is written then). It writes only into `--out`. The pictures are of the headless software render of the machine that took them; the editor's frame-rate readout differs between runs, so a retake is a different file even when nothing changed.
