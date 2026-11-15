# tools/changelog

The engine's changelog and version. Standard library only, Python 3.10+. `changelog.py` is run by the skill `todo-merge` after it merges an engine entry (never by a developer: the fragment of an entry is written from the merged result); the files it owns are `changelog/*.md`, `CHANGELOG.md` and the three macro lines of `src/Modules/BECore/Utils/EngineVersion.h`. Tests: `.venv/bin/python -m pytest tools/changelog/tests` (on a copy of the layout in a temp folder, never on the real files).

```bash
python3 <engine>/tools/changelog/changelog.py current                         # the version of the header
python3 <engine>/tools/changelog/changelog.py propose --draft FILE            # minor when the draft has a non-empty Breaking, else patch
python3 <engine>/tools/changelog/changelog.py add --draft FILE --bump minor|patch|auto [--date D] [--dry-run]
python3 <engine>/tools/changelog/changelog.py render [--check]                # regenerate CHANGELOG.md (--check: only compare)
python3 <engine>/tools/changelog/changelog.py check                           # ctest BagiChangelogCheck
```

Every command takes `--root E` (the engine folder; default the one the tool is in).

## Fragments

One fragment per merged engine entry, `changelog/<entry id>.md`, a file of its own so that two `games/*` branches that each merge an entry never touch the same lines. It is never edited by hand after the merge; `CHANGELOG.md` is generated from the fragments (newest version first) and `check` holds it to them.

```
# <entry title>

- Version: 0.4.0
- Entry: <id>
- Date: 2026-11-15
- Bump: minor | patch
- Content version: 6            (only when the entry bumped kContentVersion)

## Added            (each section optional: `- ` bullets, indented lines continue a bullet)
## Changed
## Breaking         (every bullet carries `Adapt: <how a game follows>.`)
## Migration        (what a game runs: the migrate command and the step)
```

A **draft** is the same without the `Version` and `Date` lines (`add` fills them and refuses a draft that has them). The numbers are `0.MINOR.PATCH` while the engine is young: a non-empty `Breaking` forces `minor`, anything else is `patch`, and a minor bump resets the patch to 0. The tool never bumps MAJOR.

## What `add` refuses (exit 1, nothing written)

`Bump: patch` with a Breaking section; a Breaking bullet without `Adapt:`; an `Entry` that already has a fragment; a draft without `Content version` while `kContentVersion` (`Config/ContentVersion.h`) is higher than every `Content version` of the fragments (the entry bumped it and forgot its Migration section); a `Content version` above `kContentVersion` or not above the log's; a broken log, or a header whose version is not the highest fragment's.

The first fragment of an empty log (`baseline.md`) takes the header's version as it is: it records where the log starts and moves nothing. Then `add` writes the fragment, rewrites the three `#define BE_ENGINE_VERSION_*` lines (nothing else of the header, line endings kept) and regenerates `CHANGELOG.md`. `--dry-run` runs every refusal and prints what it would write.

## What `check` reports (exit 1, one line per problem naming the file)

A malformed fragment; a fragment not named after its `Entry`; a repeated version; the highest fragment version differing from the header; `CHANGELOG.md` missing or differing from the rendering. It does **not** compare `kContentVersion` with the fragments: a branch that bumped it has no fragment until `todo-merge` adds one after the merge, and the ctest would fail the branch.

Exit codes: 0 ok, 1 a refusal or a failed check, 2 the layout is not there (no header, no draft).
