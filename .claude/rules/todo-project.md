---
paths:
  - "tools/todo/**/*"
  - ".claude/agents/**/*"
---

# The todo areas of this repository

Read by the skills `todo-loop`, `todo-merge`, `todo-dispatch`, `todo-loop-parallel`, `todo-add`, `todo-db` and by the agents `todo-*`. They are one generic text kept in BagiTodo (`workspace/.claude/`, its `docs/reference/skills.md`) and copied here as they are: everything of this repository is in this file, under the sections that `skills.md` fixes. A game that carries this repository as the submodule `engine/` has a file of the same name with its own content under the same headings.

## Areas

| Area | `<area>` (todo store) | `<R>` (git repository) | `<base>` |
|------|-----------------------|------------------------|----------|
| engine | `engine` | `.` | `main` |

`<engine>` is `.` (the folder with `tools/` and `CI/`; `engine` in a game). The CLI is `python3 tools/todo/todo.py` (the loader of the CLI the service hands out: `tools/todo/CLAUDE.md`); `TODO_API_URL` and `TODO_API_TOKEN` come from the git-ignored `.env` (`.env.example`); the token is this checkout's own, limited to its areas. **The root** the skills run and claim from is the main checkout (the folder whose `git rev-parse --path-format=absolute --git-common-dir` is its own `.git`), or a worker folder `../BagiEngine.workers/w<k>` for a loop; the claim records `origin` `BagiEngine@<host>`. Every area has an **inbox**: an entry for an area this token does not hold is filed into that area's inbox, and the loop and `todo-dispatch` take this area's inbox entries first. **The store's docs:** `tools/todo/CLAUDE.md` ("Dependency", "Inbox", "Workers", "Queues", tags), and for what it does not hold BagiTodo's `docs/reference/` (`workers.md` "Features", `data.md` "Tags", `skills.md`).

## Read first

`CLAUDE.md` (style, DO and DON'T), the nested `CLAUDE.md` of the touched directories and the matching `.claude/rules/` file; for a new test `.claude/rules/tests-module.md` (a test pins a contract or reproduces a bug).

## Build and test (baseline and reviewer)

- Before a build: the submodules and `external/googletest` are initialized (`git submodule update --init --recursive`), the preset of this OS is in `cmake --list-presets=build` (the `build` skill picks it). Never put Homebrew LLVM on `PATH` for `cmake --preset` (it switches the compiler and the build fails).
- Build through the presets of `CMakePresets.json`: `cmake --preset <p>` after a new source file or a change to `BE_CLASS` / `BE_REFLECT_FIELD`; `cmake --build --preset <p> --target BagiEngineTests StressTests BagiStress` (`BagiStress` is what the smoke tests need); `ctest --preset <p>`, the default test preset, which leaves out the label `extended` (only `alltests` runs `<p>-all`, `build-project.md`). A new test goes next to the code it tests.
- A diff that touches `src/Modules/BECore/FileSystem/*FileWatch*`, `tests/BECore/FileWatchBackendTest.cpp`, `tests/BECore/FileWatcherRescanTest.cpp` or `tests/Common/NativeFileWatch.h` also runs `ctest --test-dir build/<p> -L native-watch` (the real OS watch backend; no preset runs it).
- **Baseline** (yes): `python3 CI/scripts/baseline_cache.py get --root <R> --preset <p> --out <file>` reuses the one any worker took on the same commit and preset. On a miss (exit 1) build the targets above, run `ctest --preset <p>` into a log, write the failing names to `<file>` (for a failing gtest shard `<Exe>.Shard<k>`, the case names of its `[  FAILED  ]` lines, the case of its last `[ RUN      ]` line when it crashed or timed out) and `baseline_cache.py put --root <R> --preset <p> --file <file>`.
- The reviewer: the full build with the targets above and `ctest --preset <p>`; its second round builds and runs `ctest --preset <p>` again. After a merge whose base moved (`todo-merge`): build with the preset of `build` and `ctest --preset <p>`.
- The report of a run: build with the `build` skill (after the switch a rebuild is expected) and how to start the game.
- LSP is clangd: its diagnostics are a hint (headers that rely on the PCH can show errors the build does not have); the build and the tests decide.
- The build, format and include commands in full: `build-project.md`.

## Checks of a change

- **Format** before each commit: `clang-format` is not on `PATH` on this machine, and `format_sources.py --changed` skips untracked files. Tracked files: `PATH=/opt/homebrew/opt/llvm/bin:$PATH python3 CI/scripts/format_sources.py --changed --root <R>`; new files: `git ls-files --others --exclude-standard -- '<dir>/*.h' '<dir>/*.cpp' | xargs /opt/homebrew/opt/llvm/bin/clang-format -i --style=file`. The `PATH=` prefix on that one command only. Every WP of a plan has the box `- [ ] clang-format`.
- **Includes** changed: `python3 CI/scripts/strip_pch_includes.py --all --root <R> --dir <dir>` on the touched directory (`strip-pch-includes`).
- **Docs:** the nested `CLAUDE.md` of the folder the change is about, within the budget of `python3 CI/scripts/check_doc_budget.py --root <R>`; when your text pushes a file over, cut older detail of that file rather than yours.
- **Comment-only change:** only `*.md` and C-family sources, and `python3 CI/scripts/check_comment_only.py --base <start> --root <R>` exits 0 (the code is the same without the comments). The `comment` kind of a blocking finding is about comments in C-family sources.
- **Types check** (the row `Types check` of `build-project.md`), when the diff touches C++ files: a new hit is fixed with the alias or marked `//-NoStd: <why>`, never added to the baseline with `--update-baseline`; a baseline line the run made obsolete is removed with `--update-baseline`. For the reviewer a new hit, or a baseline diff that adds a line or raises a count, is blocking; a new `//-NoStd:` without a real reason is a finding.
- **Code points** of the self-review and the review: naming, the `BECore` type names, `PoolString`, `CORE_ENUM`, `IntrusivePtr`, `Format()`, no `std::string` where `PoolString` fits, no C-style casts, no magic numbers, `override`, namespace closing comments; exceptions are disabled, `ASSERT()` has no side effects.
- **Content formats:** a change that renames or removes a reflected class or field that a content file (`SceneConfig.xml`, a node file) can name, moves a field, or turns a control into components gets a WP for the migration: a step in `tools/migrate/steps/` with a before/after fixture pair, a test that the migrated content behaves as before, a bump of `kContentVersion` (`BECore/Config/ContentVersion.h`) and `python3 tools/migrate/migrate.py --root .` on the engine's content (`tools/migrate/CLAUDE.md`). A bump without a step makes `tools/migrate/tests/test_agree_with_engine.py` fail. A game's content files are never edited: the plan says in "Left for the owner" that the game runs the tool after `Update engine`; such an edit in the diff is blocking.
- **Writing skill:** `cpp-senior-programmer`.
- **Checklist skill** (the reviewer's flag): `kovalev-review-checklist`; the conductor passes `checklist: available` when `~/.claude/skills/kovalev-review-checklist/SKILL.md` exists, else `unavailable`. If the Skill call does not work from a subagent, read that `SKILL.md` and `patterns.md`. Apply the general points (AI-slop and stale comments, duplication, names and files, braces on every loop and condition, default parameters in virtual functions, performance); skip the MEF-specific ones. Where it disagrees with the `CLAUDE.md` files, the repository wins (`BECore` type names, `PoolString`, `CORE_ENUM`, `Format()`).
- A finding that repeats across reviews may be proposed as a check in `CI/scripts/`.

## Shots

Opt-in, only where a game's `.claude/rules/qa-project.md` (the template is the engine's file of that name) has a `## Screenshots` set (`tools/shots/CLAUDE.md`). Scenes: `python3 tools/shots/shots.py scenes` at the checkout root (no output or exit 77: no scene). Before: `python3 <engine>/tools/shots/shots.py capture --label before --out ~/.claude/plans/todo-loop-<plan id>-shots --scenes "<list>"` from the root the loop runs in; after: the same with `--label after`; the comparison `python3 <engine>/tools/shots/shots.py diff <folder>` writes `<folder>/diff.txt` (a `summary:` line, a line per pair; a pair that is not `unchanged` is changed). Exit 0: the folder; exit 1 or 77: unavailable (`shots unavailable: <reason>`, never a stop). Everything is written only into the shots folder outside the repository.

## Changelog and version

On for an entry of the area `engine` (in a game: the area whose `<R>` is `engine`) when `<engine>/tools/changelog/changelog.py` exists on `<base>` (`todo-merge` looks once); an entry of a game area has none. `<changelog>` is `python3 <engine>/tools/changelog/changelog.py` (`tools/changelog/CLAUDE.md`), `<header>` is `src/Modules/BECore/Utils/EngineVersion.h`. A developer never writes a fragment. What `todo-merge` does:

- **Generated files (dry check).** A conflict in exactly `<header>` and `CHANGELOG.md` (the base moved and both sides carry a fragment) is no conflict: run `<changelog> install-driver --root <R>` first (idempotent) and the dry check again, so the driver takes them (`tools/changelog/CLAUDE.md`, "Merging").
- **Draft and forecast (report).** The draft fragment (`tools/changelog/CLAUDE.md`, "Fragments": English, no `Version` and `Date` lines; `Entry` is the entry id, and when `<changelog> key --entry <id>` prints `<id>@<n>` (a fragment of an earlier slice) the line `- Merge: <n>` follows it) written to the scratchpad as `changelog-<branch-id>.md` and printed. Write it from the entry body, the slice plan, `git -C <R> diff --stat <base>...<branch>` and the diff of the public headers: `Added` and `Changed` for what a game can now use or what behaves differently, `Breaking` for what stops compiling or changes behaviour, each bullet ending `Adapt: <how a game follows>.` (a deprecated name that stays one merge is a `Changed` bullet), `Migration` and `Content version: <kContentVersion of the branch>` when `ContentVersion.h` changed or `tools/migrate/steps/` got a file. Then `<changelog> propose --draft <file>` for the bump, `todo version get` for the counter's value, and `<changelog> add --draft <file> --bump <it> --version <forecast> --content-version <kContentVersion of the branch> --dry-run` (`<forecast>`: `patch` PATCH + 1; `minor` MINOR + 1, PATCH 0; read the branch's number with `git -C <R> show <branch>:src/Modules/BECore/Config/ContentVersion.h`). The report calls it a forecast: `version next` is never run before the owner's yes. When `version get` fails, dry-run without `--version` for the refusals alone and say there is no forecast; a refusal of the dry run is shown and the draft fixed first.
- **Question.** After the full answer, a second question on the bump: the proposed one first (`minor` / `patch`; a draft with a non-empty `Breaking` offers only `minor`). `Merge only` writes no fragment.
- **Merge.** When the base moved: merge with `--no-commit`, then `<changelog> settle --root <R>` (derives the generated files from the fragments and runs `check`), then `git -C <R> commit --no-edit --cleanup=strip`. A failing `settle`: `git -C <R> merge --abort`.
- **Commit.** Take the draft (with the owner's corrections), `todo version next --bump <chosen> --entry <key>` (`<key>` as `key --entry` printed); a non-zero exit stops this step (no number is made up; the merge and the close stay; rerun once the service answers, the same bump returns the same number). Then `<changelog> add --draft <file> --bump <chosen> --version <number>` (writes `changelog/<key>.md`, the three version lines of `<header>`, regenerates `CHANGELOG.md`); a refusal stops this step with its message. Then `git -C <R> add changelog CHANGELOG.md <header>` and one commit `Changelog: <title> (<version>)` on `<base>`; nothing else is staged.

## Shared submodule

`none` here: this repository is the submodule of the games, not a carrier. A game's file says: `engine/` is the area `engine` (`<R>` `engine`, base `games/<game>`); its clean checks use `--ignore-submodules=all`; in a worker `engine/` is a worktree of the main checkout's engine module and `git submodule update` is never run in the game repository; a game entry built on an engine entry is paired (`todo-loop`, "Paired", `<S>` = `engine`); after the merge of an engine entry `todo-merge` commits the pointer: `<old>` = `git rev-parse HEAD:engine` before `git add engine`, one commit `Update engine: <title> (<version>)` (no version with the changelog off), then `<changelog> breaking --since <old> --repo engine` into the report; a game entry whose premise is missing in the engine is `skip: needs engine change`.

## Workers

Parallel loops run in worker folders (`todo-loop`, "Parallel workers"; `tools/todo/CLAUDE.md`, "Workers"): `python3 tools/todo/worker.py create 3` in the main checkout makes `../BagiEngine.workers/w1` ... `w3` (it configures a build per worker, about a minute each), `/todo-loop-parallel` (or `worker.py start`) runs `/todo-loop` in all of them as background sessions a minute apart (`worker.py list` shows them, `worker.py run <k>` opens one in a terminal). Three suit the owner's machine (14 cores; a full build takes about 17 s there, and `worker.py run` gives each session a share of the cores).

- **Shared files**, which do not count alone when the loop compares busy files: `CLAUDE.md`, `README.md` (every entry touches them a little; the developer keeps its edits to its own lines).
- Everything else counts as it is: two entries that change the same source, config or test file wait for each other.

## Automatic picking

**Off in this file.** `/todo-loop 1 auto` (`todo-loop`, "Automatic picking") lets a worker whose queue is empty choose its next entry by the classifier, only from the owner's allowlist. It is on only when the rules file of the repository the loops run in (in a game: the game's own) has this section with at least one allowlist line **and** a `Daily cap` line; no section, no cap or an empty allowlist means off. This file holds the format as an example, no live line:

```
## Automatic picking
- engine: *
- chess: chess-, qa-
- Daily cap: $20
- Review limit: 2
```

- An allowlist line is `- <area>: <id prefix>, <id prefix>` (`*` = every id of the area); an entry is allowed when its area has a line and its id starts with one of the prefixes.
- `Daily cap` (dollars) is mandatory. `Review limit` is optional and only lowers the number of branches waiting in `review` over every area (at most 3, the default).
- Two entries in a row parked by the loop stop the picking until the owner clears the ledger (`~/.claude/todo/auto-pick.json`).
- Before switching it on, the classifier must get the known cases right, e.g. `chess-escape-key-closes-panels` comes out `skip` (`needs engine change`: the engine had no key-to-action binding).

**Stays the owner's, never automatic:** the merge into the base branch, `git push`, licences, product decisions, a change of a public format or protocol the entry does not ask for, anything that goes against the text of an entry.

## Zones nobody edits

The vendored code: `src/Modules/EABase/`, `fmt/`, `imgui/`, `pugixml/`, `RmlUi/`, `stb/`, `tracy/`, and `external/`.

## Classifier

`eligible` is a concrete, bounded piece of engine or tool work. Examples of reasons:

- `owner decision`: an entry that names what is "to be decided with the owner first" (for example the binary form of sound assets, `audio-binarization`), and similar.
- `owner step`: the work is in Railway, Google, a bank, Telegram, a phone, a check on production or a deploy. When an open entry of the area `owner` is that step, name it: `owner step: <owner id>` (the conductor links the entry to it).
- `needs platform`: Windows, Vulkan, a GPU, a device or a browser this machine lacks.
- A `deep` level: scoring, the hand model, the computer player, the control protocol, an engine core change.
- Case to hold to: `chess-escape-key-closes-panels` (2026-11-18) was called `eligible`, but the engine had no binding of a key to an action; the architect answered `BLOCKED` before the first commit. The right verdict was `skip: needs engine change`.
- Example answer lines: `{"area": "engine", "id": "no-ci", "verdict": "eligible", "size": "M", "level": "normal", "touches": ["CMakeLists.txt", "cmake/BagiEngine.cmake"], "reason": "one line"}`, `{"area": "engine", "id": "audio-binarization", "verdict": "skip", "size": null, "reason": "owner decision: the binary form of sound assets is to be decided with the owner"}`.

## Outside the contract (other rules files of the engine)

- `.claude/rules/qa-project.md` gets the replay commands the agents no longer name: a reproduction `<engine>/tools/control/bagi_play.py <exe> <scenario>` fails before the fix and passes after; when the plan says `bagi_play` passes, `<engine>/tools/qa/bagi_qa.py replay --exe <exe> --invariants <module> <scenario>` finds the finding before and exits 0 after; an uncut recording is cut with `bagi_qa.py shrink`; the ctests are `<Exe>Scenario.<short>` and `<Exe>QaReplay.<short>` when the product registers them.
- `.claude/rules/perf-project.md` (the sentinel of `todo-merge`) names its commands: the fast run `python3 <engine>/tools/perf/perf.py --fast --json <scratchpad>/sentinel-<branch-id>.json`, the repeat `--only <names> --repetitions 10 --no-build`, the preflight `.claude/skills/perf/SKILL.md` step 1, the investigate command `/perf-investigate <benchmark>`.
- The cost of a stopped or merged entry is recorded by `worker.py stats record` (the served CLI); `tools/tokstats/` stays or goes as the owner decides.
