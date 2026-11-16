# Todo Workers panel: the next slice of a held entry waited forever, and the CLI's start mark was not read

- Version: 0.1.18
- Entry: panel-starting-mark
- Date: 2026-11-16
- Bump: patch

## Changed
- Todo Workers 0.28.1 (`tools/vscode/todo-workers`): `Runs.queueStartBlock` lets a worker start when the head of its own queue is the entry (or `todo/*` branch) it holds, as `worker.py start` does: 0.28 left the next through slice and a remarks round waiting. It also holds the start while `worker.py list` shows the `starting` mark the CLI writes before its session is listed.
