# tools/docs

The tools of the guide for people, `docs/guide/` (rules for its authors: `.claude/rules/guide-docs.md`). The guide has no `CLAUDE.md` and is outside the docs budget: an agent reads it only when it is sent there. Standard library only, Python 3.10+.

| File | Purpose |
|------|---------|
| `check_guide.py` | the page check; `check(root) -> list[Finding]` and `main` |
| `tests/` | pytest on temp trees (`.venv/bin/python -m pytest tools/docs/tests`) |

## The page check

```bash
python3 tools/docs/check_guide.py [--root DIR]
```

Run by the ctest `BagiGuideCheck` in the everyday `ctest --preset <p>`: it reads a few Markdown files and writes nothing. Exit 0 clean, 1 with one line per finding, `<page>:<line>: <message>`. No `docs/guide/` folder (a game that carries the engine): exit 0 with a note.

A page that drifts from the code teaches a wrong thing to a reader who cannot tell, so what can be checked by a file name is checked:

- **Every fenced block has a marker** as the nearest non-blank line before it. `<!-- source: <path> -->`: an excerpt. The block split at lines that are only `...` is a list of segments; each segment's lines (blank ones ignored, indentation ignored) are a contiguous run of that file's lines, the segments in order. Excerpts of the engine's own content, not copies, so a content migration moves the page with the file. `<!-- command -->`: a shell block; a token whose first path part is a top-level entry of the repository (not `build`, not `.venv`) must exist, and `--preset`, `--build` and `--test-dir build/X` name a preset of `CMakePresets.json`. `<!-- illustration: <reason> -->`: an output, a diagram or text that exists in no file; the reason is mandatory and the run prints how many there are.
- **Links and images** (relative, anchors not checked) resolve. **A backticked path** that starts with a top-level entry and has a `/` and an extension or a trailing `/` exists; a word with `<`, `*`, `{`, `$` or `...` is a placeholder and skipped.
- **The index** `docs/guide/README.md` links every other page. **Every `docs/guide/img/*.png` has a row** in `guide_shots.json`: a picture that cannot be taken again is a bug. No `CLAUDE.md` under `docs/guide/`. The repository `README.md` links the index.
