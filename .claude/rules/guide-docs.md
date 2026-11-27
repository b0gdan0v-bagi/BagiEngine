---
paths:
  - "docs/guide/**/*"
  - "tools/docs/**/*"
---

# The guide for people (`docs/guide/`)

The guide is for a person who learns the engine, not for an agent: plain English, short sentences, one task per page end to end, the reason in a clause. It has no `CLAUDE.md` and is outside the docs budget; the agents' docs stay the source of truth, so a page that one of them contradicts is a bug (in the page, or in the doc: say which). The tools: `tools/docs/CLAUDE.md`.

- **Run it once.** Every procedure and command on a page was run by the writer; what an action leaves on disk is read from the code or the nested `CLAUDE.md`, not guessed.
- **Every fenced block has a marker** as the nearest non-blank line before it: `<!-- source: <path> -->` (an excerpt of a real file, so it follows the file; `...` on a line of its own cuts a gap), `<!-- command -->` (a shell block of paths and presets that exist), or `<!-- illustration: <reason> -->` (an output or text that exists in no file; the reason is mandatory). Snippets are excerpts of the engine's own content, never a second copy.
- **Pictures** are taken by the engine, never drawn or edited: a row in `tools/docs/guide_shots.json`, then `python3 tools/docs/guide_shots.py --only <name>` (build the game's preset first; exit 77 says it is missing). A picture is a small PNG in `docs/guide/img/` (about 150 KB at most: lower `maxSize` or the window in its row), has alt text where the page uses it, and is taken again when the UI it shows changes. A picture that cannot be taken is dropped from its page, not faked.
- **The index** `docs/guide/README.md` links every page; a page of a later stage is plain text marked "planned" there, so every link resolves.
- **Before a commit:** `python3 tools/docs/check_guide.py` (also the ctest `BagiGuideCheck`).
