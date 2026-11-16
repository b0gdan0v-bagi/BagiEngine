# Todo Workers panel: the queue start trusted the session state alone and started a second loop in a busy worker

- Version: 0.1.17
- Entry: panel-queue-start-guard
- Date: 2026-11-16
- Bump: patch

## Changed
- Todo Workers 0.28 (`tools/vscode/todo-workers`): the panel starts a worker's next queued entry only when no `todo/*` branch whose entry is not in review is checked out in the game or `engine/`, no session started in the last 3 minutes, and the worker has looked free for 45 s on end (`Runs.queueStartBlock`). `worker.py list` names the newest session of `todo-w<k>`, so a live loop could hide behind a newer one that ended.
