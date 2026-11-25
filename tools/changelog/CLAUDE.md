# tools/changelog

The engine's changelog and version. Standard library only, Python 3.10+. `changelog.py` is run by the skill `todo-merge` after it merges an engine entry (never by a developer: the fragment of an entry is written from the merged result); the files it owns are `changelog/*.md`, `CHANGELOG.md` and the three macro lines of `src/Modules/BECore/Utils/EngineVersion.h`. Tests: `.venv/bin/python -m pytest tools/changelog/tests` (on a copy of the layout in a temp folder, never on the real files).

```bash
python3 <engine>/tools/changelog/changelog.py current                         # the version of the header
python3 <engine>/tools/changelog/changelog.py propose --draft FILE            # minor when the draft has a non-empty Breaking, else patch
python3 <engine>/tools/changelog/changelog.py add --draft FILE --bump minor|patch|auto [--version X] [--date D] [--content-version N] [--dry-run]
python3 <engine>/tools/changelog/changelog.py render [--check]                # regenerate CHANGELOG.md (--check: only compare)
python3 <engine>/tools/changelog/changelog.py check                           # ctest BagiChangelogCheck
python3 <engine>/tools/changelog/changelog.py breaking --since <commit|version|pointer> [--repo E] [--json] [--all] [--check]
python3 <engine>/tools/changelog/changelog.py renumber --plan|--apply [--floor V] [--ref R ...]
python3 <engine>/tools/changelog/changelog.py install-driver                   # once per clone (below)
python3 <engine>/tools/changelog/changelog.py settle                           # inside a merge in progress (below)
```

Every command takes `--root E` (the engine folder; default the one the tool is in); `breaking` calls it `--repo E` (`engine` in a game) too.

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

## What a version means

The number comes from the store's counter (`tools/todo/CLAUDE.md`, "Commands"), shared by every `games/*` branch: `todo-merge` takes it with `todo.py version next` and passes it as `add --version X`. A version therefore says **this tree holds the change numbered N**, not that it holds everything up to N: the numbers in between may belong to another branch, so gaps are normal. `breaking --since <version>` still lists the fragments of this tree by number. Without `--version`, `add` computes the header plus the bump; that is for the tool's tests and a repository without a store.

## What `add` refuses (exit 1, nothing written)

A `--version` that is not above every fragment of the tree (the bump is not compared with it); `Bump: patch` with a Breaking section; a Breaking bullet without `Adapt:`; an `Entry` that already has a fragment; a draft without `Content version` while `kContentVersion` (`Config/ContentVersion.h`) is higher than every `Content version` of the fragments (the entry bumped it and forgot its Migration section); a `Content version` above `kContentVersion` or not above the log's; a broken log, or a header whose version is not the highest fragment's.

The first fragment of an empty log (`baseline.md`) takes the header's version as it is: it records where the log starts and moves nothing. Then `add` writes the fragment, rewrites the three `#define BE_ENGINE_VERSION_*` lines (nothing else of the header, line endings kept) and regenerates `CHANGELOG.md`. `--dry-run` runs every refusal and prints what it would write. `--content-version N` stands for `kContentVersion` in the comparison: `todo-merge` dry-runs on the base, before the merge, where the header still holds the old number while the draft describes the branch, so it passes the branch's value; the real `add` after the merge never passes it. `--date` must be a calendar date.

## What `check` reports (exit 1, one line per problem naming the file)

Ctest `BagiChangelogCheck` (`tests/CMakeLists.txt`) and `tests/test_real_tree.py` run it on the real tree. It reports: a malformed fragment; a fragment not named after its `Entry`; a repeated version; the highest fragment version differing from the header; `CHANGELOG.md` missing or differing from the rendering. Gaps between versions are allowed. It does **not** compare `kContentVersion` with the fragments: a branch that bumped it has no fragment until `todo-merge` adds one after the merge, and the ctest would fail the branch.

## Merging: the two generated files never conflict

`EngineVersion.h` (the highest fragment's version) and `CHANGELOG.md` (the rendering) follow from the fragments, which never conflict, yet a merge where both sides added a fragment changes the same lines of both. Two parts take it off every merge (`sync-engine`, `todo-merge`; any other merge in a clone with the config gets the driver):

- **The git merge driver** `bagi-changelog` (`.gitattributes` names exactly those two paths; `install-driver` registers `merge-driver %O %A %B %P` in the clone's local config, which its worktrees share, so once per clone; `engine_sync.py status` reports `changelog_driver`). For the header it sets all three sides to the highest version and line-merges the rest with `git merge-file`, so a conflict is left only where the sides edit the same other line. For the log it takes the union of the version blocks of both sides minus a block one side removed (the fragments of the other side are not in the working tree yet, so it cannot render). The driver runs `tools/changelog/changelog.py` of the checkout that merges: a checkout without it, or a clone without the config (git then merges those two files as text), leaves the files conflicted, which `settle` also takes.
- **`settle`**, run in the checkout during a merge that has not been committed (`merge --no-commit`): exit 1, touching nothing, when any conflicted path is not one of the two. Otherwise it rebuilds a conflicted header from the stages with the same rule as the driver, sets the header to the highest fragment version, renders `CHANGELOG.md`, runs `check` and stages both; exit 1 on a problem of `check` (a version two fragments share, a malformed fragment), after which the caller aborts the merge. It makes the result exact whatever the driver left, so it runs after every such merge, conflicted or not.

## `breaking`: what an engine update breaks for a game

Read-only (`rev-parse`, `log`, `show-superproject-working-tree`; it never fetches, so the repository must already hold the commits). Lists the fragments added after a point, newest version first, with the Breaking bullets (their `Adapt:` text) and the Migration lines; by default only the entries that have one of the two, `--all` lists every entry. The point:

- a **commit** (or any name git resolves): the fragment files `git log --diff-filter=A` shows added in `<commit>..HEAD`, so the commits of a `--no-ff` merged branch count;
- a **version** `0.3.0`: the fragments with a higher version, no git;
- `pointer`: the commit the superproject of `E` records for it (`HEAD:<path>` there); not a submodule is an error. A game runs it before taking an engine merge.

A point older than the first fragment adds one line: the log starts at that version and `git log <point>..HEAD` has the rest. `--json` has `since`, `kind`, `commit`, `log_starts`, `before_the_log`, `breaking` (any listed entry has a Breaking section) and `entries` (the fields of a fragment, bullets as lists). `--check` exits 1 when `breaking` is true (a game's CI); without it the exit is 0 whatever is listed.

## `renumber`: the one-off move to the shared counter

Before the counter, `main` and each `games/*` numbered entries on their own, so one number could mean different entries. `renumber` gives every fragment above `--floor` one sequence over `main` and every local `games/*`, ordered by `Date`, then by when the fragment first appeared in git, then by id; each keeps its `Bump`. The order never reads the numbers being replaced, so the plan stays the same while the branches are renumbered one by one.

`--plan` prints the new numbers and `highest:`, and writes nothing. `--apply` rewrites this tree only: the `Version` line of its fragments, the header and `CHANGELOG.md`; it commits nothing and refuses a fragment above the floor that no read branch holds. This is the one sanctioned edit of a merged fragment.

The steps are the owner's, on one PC, with the other PCs pushed first: on `main` and each `games/*` run `--apply` and commit `Changelog: renumber after the shared counter`; then raise the counter to `highest:` when that is above its value (`todo.py version set <highest> --expected <value>`). Do it before the next `todo-merge` of an engine entry: until then `add --version` refuses a number that is not above the tree's fragments.

Exit codes: 0 ok, 1 a refusal of `add`, a failed `check` or `render --check`, or `breaking --check` with a Breaking entry; 2 a file or a point the command needs is not there (no version header, no draft, an unknown commit, not a submodule), for every command including `add` and `check`, and also input that `propose` or `breaking` cannot read (a malformed draft or log).
