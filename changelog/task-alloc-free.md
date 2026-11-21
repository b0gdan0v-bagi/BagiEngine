# Tasks: submitting a task allocates (std::function, heap nodes); no move-only captures

- Version: 0.8.3
- Entry: task-alloc-free
- Date: 2026-11-21
- Bump: patch

## Added
- `ThreadPool::IsRunningInlineJob()`: on an inline pool, true while a job runs on this stack (the jobs queued by it included); always false on a worker pool.
- Benchmark `task-submit-cost` in the perf set: allocations and time per submitted task.

## Changed
- Submitting and running a task allocates nothing in a steady state on every route (pool submit, main-thread and delayed tasks, background slices, coroutines): the task queues are ring buffers that keep their storage, the live-task registry is an intrusive list through the handles, and task handles and coroutine frames come from size-class pools (`TaskMemory`, blocks of 64 to 1024 bytes; a larger frame stays on the heap).
  Adapt: nothing; a game's own `Task` coroutines take their frames from the pools without a change.
- The inline pool (single-thread build) no longer wraps each background job in a second callable.
  Adapt: nothing.
