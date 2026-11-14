# tools/shots

Before / after pictures of a game's scenes for the todo loop, and a text diff of them: the reviewer of a slice that changes how something looks has the picture-level facts the layout tests miss. Standard library only, Python 3.10+. The engine knows no game: which scenes exist and how to take them is the **screenshot set** in the game's `.claude/rules/qa-project.md`. The tool **informs and never gates**: nothing here fails a review or stops the loop (the agents' reading rules are in `.claude/agents/todo-reviewer.md`).

| File | Purpose |
|------|---------|
| `shots.py` | the command line: `scenes`, `capture`, `diff` |
| `png_diff.py` | the pure comparison of two decoded pictures (`compare`, `describe`, `diff_map`, `image_diff`): no I/O, no process |
| `png_ops.py` | the picture operations: `crop`, `to_channels`, `scale_to` (bilinear), `fit_within`, `encode_png`; `inside_git_work_tree` (the one `git` call, shared with `shots.py` and the MCP tool) |
| `tests/` | pytest (`write_png` of `conftest.py` makes a picture; a fake game command makes the files) |

The PNG reader is `tools/control/tests/png_reader.py` (the game's `ui_shots.py` imports it from there too): `png_ops.py` puts that folder on `sys.path`, so it is not copied and not moved.

```bash
python3 <engine>/tools/shots/shots.py scenes [--root DIR]
python3 <engine>/tools/shots/shots.py capture --label before|after --out DIR --scenes a,b@720x1280 [--preset P] [--no-build] [--root DIR]
python3 <engine>/tools/shots/shots.py diff DIR [--tolerance N] [--root DIR]
.venv/bin/python -m pytest tools/shots/tests                  # in the engine
```

Run from the checkout root (`--root` otherwise). Exit code: 0 done, 1 an error (an unknown scene or size, `--out` inside a git work tree, a failed build or run, a picture missing after the run, a missing `before` / `after` folder), 77 skipped (no rules file or no section; the game's command exited 77, i.e. its executable is missing). `diff` exits 0 whatever the pictures show.

- `scenes` prints `sizes:`, `default size:` and one `name: about` per scene. The scene names come from here only; nobody guesses them.
- `capture` checks the scenes and sizes against the set, builds `targets` with `cmake --build --preset` (skipped by `--no-build` or when `targets` is empty), deletes the `*.png` already in `DIR/<label>/` (a leftover would hide a scene that was not written), runs `command` **once per size** (the scenes of that size joined by commas; 300 s each), then checks that every requested scene has a file in `DIR/<label>/` whose name contains the scene's name. `--out` must be outside every git work tree: a picture is never committed.
- `diff DIR` pairs `DIR/before/*.png` with `DIR/after/*.png` by file name, prints the text below and writes it to `DIR/diff.txt`.

## Folder and who takes what

`~/.claude/plans/todo-loop-<plan id>-shots/` with `before/`, `after/` and `diff.txt` (the plan id of `todo-loop`: the branch id, `-s<K>` for a later slice of a through entry). Outside every repository and not in the todo store (it holds text), so on another machine the folder is simply not there. A slice takes shots only when its plan has a `**Shots:**` line (`.claude/agents/todo-architect.md`): the conductor takes `before` at the slice start, the reviewer takes `after` and the diff, developers take none.

## The screenshot set of a game

A `## Screenshots` section of `.claude/rules/qa-project.md` with one fenced `json` block (the first after the heading; the same idea as the benchmark set of `.claude/rules/perf-project.md`, `tools/perf/CLAUDE.md`). A game without the section has no screenshots, and the loop goes on without them.

| Key | Meaning |
|-----|---------|
| `preset`, `targets` | what is built before a capture (`targets` may be empty: nothing is built) |
| `exe` | the executable's file name in `{bin}`; `{exe}` is its full path |
| `command` | a list, run from the root once per size. Placeholders: `{root}` `{engine}` `{python}` `{bin}` (`<root>/build/<preset>/bin`) `{exe}` `{label}` `{out}` `{scenes}` (comma-separated) `{window}` (`WxH`). Anything else in braces stays as written (JSON is safe) |
| `scenes` | object: scene name -> one line of what it shows |
| `sizes`, `defaultSize` | the allowed `WxH` and the one a scene without `@WxH` gets |

```json
{
  "preset": "macos-debug",
  "targets": ["BagiGame"],
  "exe": "BagiGame",
  "command": ["{python}", "{root}/tools/ui_shots.py", "--exe", "{exe}", "--label", "{label}", "--out", "{out}", "--scenes", "{scenes}", "--window", "{window}"],
  "scenes": {"table": "the table after the deal", "setup": "the setup screen"},
  "sizes": ["1920x1080", "720x1280"],
  "defaultSize": "1920x1080"
}
```

What the game's `command` must do:

- Write `*.png` files into `{out}/{label}/`: one per scene of `{scenes}` at `{window}`, **small** (the long side about 640), and nothing else there. A file name holds the scene's name and the same name is written for the same scene and size in every run: **the name is the pairing key** of `diff`, so `before` and `after` must agree.
- Be deterministic: the same seed and the manual clock, so the same build renders the same pixels (the diff's default tolerance is 0). Remove any temporary file it makes.
- Exit 77 when the executable is missing; any other non-zero exit is a failure. Never write inside the repository, and never commit a picture.

## The diff text

One line per pair in file-name order, then at most 5 regions of a changed pair by changed pixels (a constant of `png_diff.py`), then a summary. Corners are inclusive pixel coordinates.

```
<file>: unchanged
<file>: 3.20% changed (7373 of 230400 px), 2 regions
  [x0,y0,x1,y1] 1.10% of the picture, 85% changed inside
<file>: size 640x360 -> 360x640, not compared          (or: channels 4 -> 3)
<file>: cannot be read (<reason>), not compared
only in before: <file>        only in after: <file>
summary: N changed, N unchanged, N not compared, N only in one side
```

A pixel is changed when any channel differs by more than `--tolerance` (default 0). Regions: changed pixels are counted in cells of 8 px, 8-connected cells form a group, a group is one bounding box, overlapping boxes are merged. The number before `regions` is the count found, the lines below are the biggest. Rows that are byte-equal are skipped without a pixel loop.

## Comparing two pictures

`png_diff.image_diff(a, b, threshold=0, region=None)` is the whole comparison of two decoded pictures that the MCP tool `image_diff` and the reference check of the scenario player use (`tools/control/CLAUDE.md`); the `diff` command above is the same `compare` over folders and prints the same text as before.

- **A is the reference.** `region` is `[x0, y0, x1, y1]`, corners inclusive, in pixels of A (the form the lines above print, so a reported box can be fed back); it must lie inside A with `x0 <= x1`, `y0 <= y1`, else a `ValueError` with the reason. `threshold` is the `tolerance` of `compare` (0..255).
- **B is made comparable:** a different size is scaled to A's bilinearly (`scaled`; `aspectDiffers` when its shape was not A's, so it was stretched), a 3 beside a 4 channel picture loses the alpha of the one that has it and the compare is on RGB.
- **Result:** `ImageDiff.stats()` is JSON: `equal`, `changed`, `compared` (the area compared: the region's), `share` (0..1 of it), `boundingBox` (exact box of every changed pixel, `null` when none), `regions` (at most `MAX_REGIONS`: `box`, `changed`, `shareOfPicture` of the whole A), `threshold`, `region`, `a` / `b` `{width, height, channels}` as given, `scaled`, `aspectDiffers`. `ImageDiff.map` is the difference map: RGB, A's size, A as grey at half brightness and the changed pixels red (only inside the region); `png_ops.encode_png` writes it.
