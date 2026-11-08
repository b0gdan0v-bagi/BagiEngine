# Todo Workers (VS Code)

A panel for the parallel `todo-loop` workers (`tools/todo/worker.py`, "Workers" in `tools/todo/CLAUDE.md`): each worker with its entry, the state of its background session, its branches, the ticked boxes of its plan and uncommitted changes, and the entries in `review` that wait for `/todo-merge`. A line in the status bar counts them, and a notification comes when an entry reaches review or a session ends.

It only reads `worker.py list --json --store` every 20 s (from the first workspace folder that has `tools/todo/worker.py` or `engine/tools/todo/worker.py`, the main checkout) and opens terminals: **attach** (`claude attach todo-w<k>`, for a session that waits for a permission), **log** (`claude logs`), **open folder**, **terminal**, **stop** (`worker.py stop <k>`, after a question), and for an entry in review **copy `/todo-merge <branch>`** and **open the plan** (`~/.claude/plans/todo-loop-<branch-id>.md`). The loops are started by `/todo-loop-parallel`, not here.

## Install

No npm: the packer uses the standard library.

```bash
python3 tools/vscode/todo-workers/pack.py        # in a game: engine/tools/vscode/todo-workers/pack.py
code --install-extension tools/vscode/todo-workers/build/todo-workers-0.1.0.vsix
```

`code` is in `/Applications/Visual Studio Code.app/Contents/Resources/app/bin/` on macOS when it is not on PATH. Then **Developer: Reload Window**. After a change bump `version` in `package.json`, pack and install again.

## Settings

`todoWorkers.python` (`python3`), `todoWorkers.claude` (`claude`; a full path when VS Code started from the Dock does not see it: the panel says "claude agents did not answer"), `todoWorkers.refreshSeconds` (20), `todoWorkers.notify` (true).
