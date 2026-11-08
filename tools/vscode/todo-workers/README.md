# Todo Workers (VS Code)

A panel for the parallel `todo-loop` workers (`tools/todo/worker.py`, "Workers" in `tools/todo/CLAUDE.md`), one card per worker:

- the entry (id, area, title), the state of its background session (busy or idle and how long it runs; done and when its last step was, since a finished session can stay listed by `claude agents` for hours), the ticked boxes of its plan as a bar, the branches of the game and of `engine/`, uncommitted changes;
- **Now** (**Last** once the session is done): the last thing the session said, and the agents it runs (developer, reviewer, ...) with their last step, or their verdict once they are done;
- **Tokens**: what the session and its agents used in total and in the last 10 minutes (output, input, cache write, cache read), the loop against its agents, the context the session holds now, and the tokens by model; above the cards the sum over all workers;
- **Last steps**: the session's last tool calls and messages (folded);
- buttons: **Attach** (`claude attach todo-w<k>`, for a session that waits for a permission), **Log** (`claude logs`), **Folder** (a new window), **Terminal**, **Plan** (`~/.claude/plans/todo-loop-<branch-id>.md`), **Stop** (`worker.py stop <k>`, after a question, while the session runs); while it does not: **Update** (`worker.py update <k>`: the game and `engine/` detached at the tips of their base branches, as the loop's "go to the base"; the branch it held is kept; refused with uncommitted changes) and **Fast start** (`worker.py start <k>` at once: `/todo-loop` with the settings `todoWorkers.startSlices`, `todoWorkers.loopArgs` and `todoWorkers.reportTo`).

Below them the entries in `review` of the areas of `.claude/rules/todo-project.md` (other games' entries in the store are left out), with:

- **Review**: the changes of the branch against the point where it left its base, every file in one multi-diff editor (a terminal with `git diff --stat` when the editor cannot show it);
- **Review with Claude**: a terminal in the main checkout runs `claude` with a review prompt: it reads the entry (and its plan) and `git diff <base>...<branch>`, checks them against the entry and the repository's `CLAUDE.md` and rules, and reports findings with `file:line` and a verdict. It is told to stay read only (no checkout, edit, commit, merge or store change); a second click shows the same terminal;
- **Quick merge**: `worker.py merge <id>` without Claude. A dialog shows the commits and what follows (the entry closed, or open again for the next slice; the worker that holds the branch detached; the branch deleted; for the engine in a game the commit `Update engine: <title>`); a yes does it. Conflicts, uncommitted changes and an entry not in review are refused with nothing changed. It never builds: when the base moved since the branch was cut, the dialog says so and the tests are the owner's;
- an entry whose branch is not in this checkout (taken in another checkout or on another machine), or was cut from another base (it holds commits of `games/chess` that `games/mahjong` has not), says so on its card, and these four buttons are greyed out (`worker.py list --store`: `local`, `foreignBase`; `worker.py merge` refuses it too);
- **Merge with Claude**: a terminal in the main checkout runs `claude '/todo-merge <branch>'`: the skill reports, asks, and builds and tests when the base moved (a second click shows the same terminal);
- **Plan**.

Above the cards, next to **Refresh** and **Settings**, **Todo list** opens a tab of the editor with the entries of the todo store (`todo.py list --all --json --body`): the areas of this checkout by default (or one area, or all), chips per status with counts (closed ones off at first), a search over id, title and text; newest first within a status. A click on an entry unfolds its text (paragraphs, bullets, bold and code; everything as text, no HTML) with **Markdown** (`todo.py show` in a markdown preview), **Plan** (the local copy in `~/.claude/plans`, else `todo.py plan get`) and **Copy id**. An open entry, and one in progress that no running worker holds, has **Attach to free worker**: a dialog names the free workers (no session running, no uncommitted changes; those that hold no entry first; for an entry whose branch an idle worker holds, only that worker) and the one picked runs `worker.py start <k> --delay 0 --slices 1 --loop-args '<id> <todoWorkers.loopArgs>'` (and `--report-to`), so its loop takes exactly that entry (or continues its branch); the button is greyed out while no worker is free, and an entry a running worker holds shows that worker; one whose branch lives elsewhere (above) shows `branch elsewhere` and has no button. Otherwise it reads the store when it opens and on **Refresh** and writes nothing.

The notification of a new entry in review offers **Review**, **Review with Claude**, **Quick merge** and **Merge with Claude**. A line in the status bar counts busy and idle workers and entries in review, and a notification comes when an entry reaches review or a session ends.

**Updates.** `todo.py` rewrites `~/.claude/todo/changed.json` after every write the store accepts (a loop's claim, review or plan, a merge, a close, the owner's CLI), and the panel watches that folder: a change reads the workers and the Todo list tab again at once (writes within 0.7 s are one refresh). The timer below stays for what moves without a write (sessions, their steps and tokens); the Todo list tab also reads the store again when it is shown again, for edits made in Vikunja.

It reads `worker.py list --json --store --activity --usage` every 15 s from the first workspace folder that has `tools/todo/worker.py` or `engine/tools/todo/worker.py` (the main checkout); the activity and the tokens come from the session transcripts in `~/.claude/projects/`. Everything shown is put in as text. It writes only a merge the owner confirms, and starts no loop: the loops are started by `/todo-loop-parallel`.

## Install

No npm: the packer uses the standard library.

```bash
python3 tools/vscode/todo-workers/pack.py        # in a game: engine/tools/vscode/todo-workers/pack.py
code --install-extension tools/vscode/todo-workers/build/todo-workers-0.11.0.vsix
```

`code` is in `/Applications/Visual Studio Code.app/Contents/Resources/app/bin/` on macOS when it is not on PATH. Then **Developer: Reload Window**. After a change bump `version` in `package.json`, pack and install again.

## Settings

`todoWorkers.python` (`python3`), `todoWorkers.claude` (`claude`; a full path when VS Code started from the Dock does not see it: the panel says "claude agents did not answer"), `todoWorkers.refreshSeconds` (15), `todoWorkers.notify` (true).
