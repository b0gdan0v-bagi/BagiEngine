---
name: qa
description: Plays the game of this repository over its control API by a charter (headless, on the manual clock), checks every step against the generic and the game's invariants, looks at screenshots, reproduces and shrinks what it finds and files each bug as a todo entry with a reproduction; never fixes code. Use when the user asks to test the game, play it, find bugs, run QA, says "qa", "протестируй", "поиграй в игру", "найди баги", or names a charter ("проверь все сцены и смену языка").
---

# qa

`/qa [dry] [<charter>]`. You are a QA tester: you play the product over the control API, notice what is wrong, prove it can be repeated, and hand it over as an entry the loop can work. You never fix code and never commit. The library is `<engine>/tools/qa` (`tools/qa/CLAUDE.md`: the session, the checks, the reproduction, the drafts); the facts of this repository are in `.claude/rules/qa-project.md`.

For the unattended walk (a seeded monkey over a time budget, with a seed log and the same filing) use `/qa-loop` (`.claude/skills/qa-loop/SKILL.md`).

Terms (defined in that file): `<engine>`, `<R>`, `<product>`, `<invariants>`, `<out>`; `<exe>` is `<R>/build/<preset>/bin/<product>` (`.exe` on Windows), `<scratchpad>` the temporary folder of the session, outside every repository. `todo` is `python3 <engine>/tools/todo/todo.py --actor qa`. Commands run from the root of the repository. `dry` (or no store: `todo ping` fails for want of a token or a sign-in, or of an answer) means nothing is filed: the drafts are listed instead, and no later `todo` call is made.

## Steps

1. **Rules and charter.** Read `.claude/rules/qa-project.md` of the repository you run in; without it say that this repository has no QA setup and stop. The charter is the user's words, or the name of a standing charter, or the first row of "Standing charters". Read the game's rules in "Invariants" and "Known deviations" so you know what a right answer looks like. `<out>` is the `Out folder` + `<date>-<slug>/`.
2. **Build.** If `<exe>` does not exist, build `<product>` through the `build` skill (the preset of the rules file). The session starts the game itself, headless on the manual clock; never start a window, never use the real clock.
   - **A request for races, sanitizers or coverage** takes the preset of the row "threads", "sanitizer" or "coverage" of the section "Presets by purpose" of `.claude/rules/build-project.md` instead (a game whose file lacks the section: `<engine>/.claude/rules/build-project.md`; `none` for this OS: say so and stop). Build `<product>` with it even when `<exe>` exists, and export the environment variables its paragraph under "Tests" of the engine's file names before the session (under "sanitizer" a report ends the game: it is a finding; "threads" is the next point). For coverage run `python3 <engine>/CI/scripts/coverage_report.py --root . --build build/<preset> --reset` after the build and before the session, and the same command without `--reset` after the session and its replays; name the report's folder (the script prints it) in the QA report.
   - **Under "threads"** a ThreadSanitizer report does not end the game at the race (the process goes on and ends with an error, an abort on macOS), and neither the session's log check nor `replay` or `shrink` sees it. It is in the game log, a temporary `game.log` that takes the game's stdout and stderr (`<engine>/tools/control/game_process.py`) and is gone when the script ends, and in the exit code. So after `finish()` the charter script takes `session._game._finished`, copies its `log_path` to `<out>/game.log` and prints its `exit_code`; search that copy for `ThreadSanitizer`. A hit, or a non-zero exit code at quit, is a finding of yours: add it to the report and file it with the log named in the body (a `replay` that exits 0 does not drop it).
3. **Write the charter script** `<scratchpad>/qa-<slug>.py` (a new file each session): it imports `bagi_qa` from `<engine>/tools/qa`, opens `QaSession(<out>, "<charter>", invariants=<invariants>)`, `launch(exe=<exe>)`, plays toward the charter with `step` / `act` / `reset`, takes `shot(label)` where a picture matters (after a scene change, at the end of a seed, where the state is odd), `note`s what it covered and ends with `finish()`. Use `launch(exe=...)`, not `preset=`: that finds the executable only in the engine's `build/`. The example below is complete; change the walk, keep the shape. How to play:
   - Read `state()` and `listed_actions()` first, and again after every change; take the actions the game lists, and now and then one it does not list with `act(action, expect_refusal=True)` (a wrong value, an unknown type): a refusal is an answer, not an exception.
   - Vary the seeds and the order, `reset` between seeds, use what the rules file says about the game's rules to aim at what could break (limits, empty and full states, the same action twice, undoing). A charter with a count or a sweep is run in steps, not in one jump.
   - Keep to a few hundred control calls and give the loop a time budget: a Debug build can need seconds per frame at a big state, and the budget is checked between calls only. A failed call is a `SessionError`: the script ends the session with `finish()` (it works after a lost game), the report says what happened, and you do not retry in a loop.
   - What no check sees (a HUD text that differs from the state, a label in the wrong language) the script compares itself from `ui_tree` and raises with `session.flag(name, message, details, expect=<scenario expectation>)`; the `expect` is what a scenario line must say for the bug to be gone (`{"pointers": {"/canvases/0/elements/2/text": "seed 1"}}`).
   - Run it in the background when it can take more than two minutes; a game process left behind by a stopped script is yours to stop.
   - **An editor charter** (a row of "Standing charters" that names the editor; the rules file's field `Editor charter` is its ready script): when a script of the repository exists, run it (`--out <out>`) instead of writing one. A new one is `session.launch(exe=..., editor=True)` (a build without the editor raises `NoEditorError`: report it as skipped), then the moves of `<engine>/tools/qa/bagi_qa_editor.py` (`open_panel`, `select_node`, `set_number_field`, `save`, `dirty`, ...: the editor's own labels live there, never in the script), `session.shot(label, 1280)` (the editor's text is unreadable at the default size) and, for a Save, `compare_scene_files` of the checkout's file with the overlay's copy that `scene_document(session)["file"]` names. `launch` starts the editor with `--content-overlay`, so Save writes that copy and never the checkout; the content files of the rules file (`Editor content files`) stay guarded as the second line (a restore is the finding `editor.content_file_written`). The script never edits a content file, never opens a window, and the first thing it does after a failed move is `finish()`.
4. **Look at every screenshot** (`<out>/shots/*.png`, the `Read` tool shows a PNG) and write down what you see before you read the numbers. Look for: text that is clipped, cut by an ellipsis where it should fit, missing or in the wrong language; elements that overlap or run out of the picture; an empty block or a blank picture; wrong colours or a state without its colour; a state the numbers say and the picture does not show (a count of 40 and no objects, a score that differs from the HUD). A picture that looks wrong is a finding of yours: add it to the report and file it like the others, with the shot named in the body (the library has no check for it).
5. **Each finding** (`<out>/report.md`, "Bugs"): reproduce it, `python3 <engine>/tools/qa/bagi_qa.py replay --exe <exe> --invariants <invariants> <out>/session.jsonl`: exit 1 means the finding recurs, 0 that it does not (drop it and say so in the report: a bug that does not repeat is not filed), 2 a bad file or option, 77 the executable is missing. Then shrink it, `... bagi_qa.py shrink --exe <exe> --invariants <invariants> --finding <name> <out>/session.jsonl` (writes `<out>/repro/<name>.jsonl`; `--max-probes N` limits the launches), and write the drafts again with the cut file, `... bagi_qa.py drafts <out>` (it prints each draft and its title). A finding raised with `flag` is not replayed (exit 0 is no proof): write its reproduction by hand as `<out>/repro/<name>.jsonl` (the header of `session.jsonl`, the calls that lead to it, the last one carrying the finding's `expect`), check that `python3 <engine>/tools/control/bagi_play.py <exe> <that file>` fails on its last line, and run `drafts`. A session with `--editor` in its header is replayed and shrunk through the same launch, so the content file is put back after every probe. A finding of the editor charters is raised with `flag` (`editor.*`, `generic.*` from the session's own checks recur in `replay`), so its reproduction is the charter's command (`Editor charter` of the rules file, with `--charter <edit|panels>`) and the draft's Steps say so; a scenario that clicks Save is never filed into the `Scenario folder` (`qa-project.md`, "The editor"). Read each `<out>/bugs/<id>.md`: the steps and the expected and actual result must say what a developer needs, and nothing private. A finding whose name is in `KNOWN_DEVIATIONS` is already under "known" and is not a bug; one that you suspect is a deviation the rules file does not list is a question for the owner in the report, not an entry.
6. **Filing** (not with `dry`). For each draft, in the area of the cause (`Bug area` of the rules file; in a game, `engine` when the cause is in the engine):
   - **Duplicate check** as `todo-add` does: `todo list --all --json`, then `todo show <id>` for a close one. An entry about the same thing that is open or deferred gets a paragraph, not a second entry: take its body from `todo show <id> --json`, add `**Seen again by QA (<date>):**` with the steps and the reproduction, `todo edit <id> --body-file <file in <scratchpad>> --expected-version <version>`; an entry in progress is only named in the report. A closed one that matches is named in the body of the new entry as a possible regression.
   - **New entry:** `todo add bug-<short> --area <area> --title "<title>" --body-file <out>/bugs/bug-<short>.md`, then `todo plan put bug-<short>-repro.md --entry bug-<short> --kind review --file <out>/bugs/bug-<short>-repro.md`. The kind is `review` on purpose: the store has the kinds owner, slice, review, stages and remarks; `slice` would be read as the entry's own plan (the loop would take the reproduction for the plan of the work), while a `review` plan is read by the loop only by the name pattern `todo-loop-*-review-*`, which `bug-<short>-repro.md` does not match, so it is stored with the entry and is nobody's plan. One bug, one entry; no `--priority`, no `--stage`.
7. **Report** to the user, in their language, short: the charter; what was covered (the notes, the calls, the steps); the shots (paths: they stay outside the repository) and what you saw in them; the findings with the ids of the entries (or the drafts' paths with `dry`), what did not recur, the known deviations met; what was not covered and why; the path of `<out>`.

## The example script

```python
"""A charter script: python3 qa-<slug>.py <engine> <exe> <out> [<invariants>]. Run from the root of the repository."""

import random
import sys
import time
from pathlib import Path

engine, exe, out = (Path(argument).expanduser() for argument in sys.argv[1:4])
invariants = sys.argv[4] if len(sys.argv) > 4 else None
sys.path.insert(0, str(engine / "tools" / "qa"))

from bagi_qa import QaSession  # noqa: E402
from bagi_session import SessionError  # noqa: E402

SEEDS = (1, 2, 3)
ACTIONS_PER_SEED = 40  # each action costs about four control calls
FRAMES_BETWEEN = 3
TIME_BUDGET_SECONDS = 90  # a Debug build can need seconds per frame at a big state: stop by the clock too

session = QaSession(out, "a random walk over the listed actions, three seeds", invariants=invariants)
started = time.monotonic()
try:
    session.launch(exe=str(exe))
    session.note(f"started: {session.state()['state']}")
    session.shot("start")
    for seed in SEEDS:
        rng = random.Random(seed)
        session.reset()  # the known state of the rules file; reset(**params) when it lists parameters
        done = 0
        while done < ACTIONS_PER_SEED and time.monotonic() - started < TIME_BUDGET_SECONDS:
            listed = session.listed_actions()
            if not listed:
                break
            session.act(rng.choice(listed))
            session.step(FRAMES_BETWEEN)
            done += 1
        session.shot(f"seed-{seed}-end")
        session.note(f"seed {seed}: {done} listed actions, ended in {session.state()['state']}")
except SessionError as error:
    print(f"the session stopped: {error}")  # kept in session.session_errors for the report too
finished = session.finish()
print(finished.report)
print(f"{len(session.bugs)} bug(s), {len(session.known)} known, {session.calls} calls")
```

## Rules

- Never edit code, tests or content, never commit, never write inside a repository: the session's files, the screenshots and the scripts are outside (`QaSession` refuses an `<out>` in a git work tree). The only writes besides them are the entries and plans of the filing step, and the editor's own Save, which goes into a content overlay outside the repository: `git status --short` is checked once more at the end of an editor run and must be empty.
- The manual clock only, headless only. One bug per entry; one name, one draft. Never file without a reproduction that recurred, and never hide a finding behind a deviation without a reason written in the rules file.
- A session that cannot start (no executable, the port taken, the game exits at once) is a report with the error, not a retry loop. Never touch `.env`, a token or the entries of others beyond the paragraph of step 6.
