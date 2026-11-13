# tools/shots

Before / after pictures of a game's scenes for the todo loop, and a text diff of them: the reviewer of a slice that changes how something looks has the picture-level facts the layout tests miss. Standard library only, Python 3.10+. The engine knows no game: which scenes exist and how to take them is the **screenshot set** in the game's `.claude/rules/qa-project.md`. The tool **informs and never gates**: nothing here fails a review or stops the loop (the agents' reading rules are in `.claude/agents/todo-reviewer.md`).

| File | Purpose |
|------|---------|
| `shots.py` | the command line: `scenes`, `capture`, `diff` |
| `png_diff.py` | the pure comparison of two decoded pictures (`compare`, `describe`): no I/O, no process |
| `tests/` | pytest (`write_png` of `conftest.py` makes a picture; a fake game command makes the files) |

The PNG reader is `tools/control/tests/png_reader.py` (the game's `ui_shots.py` imports it from there too): `png_diff.py` puts that folder on `sys.path`, so it is not copied and not moved.

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
