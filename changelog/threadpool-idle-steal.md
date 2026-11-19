# TaskSystem: an idle ThreadPool worker never steals; Cancel races, GetResult moves, DelaySec overflows

- Version: 0.5.1
- Entry: threadpool-idle-steal
- Date: 2026-11-18
- Bump: patch

## Changed
- An idle `ThreadPool` worker is woken by a job of any queue, so a job no longer waits behind a long one while another worker is free.
- `TaskHandle::Cancel` cannot turn a task that has just finished into a cancelled one: a finished task keeps its result.
- `TaskHandle<T>::GetResult` can be called again for a copyable `T`, also from several threads; a move-only result is handed out by the first call.
- `DelaySec` takes any number: NaN and a non-positive delay do not suspend, a huge one is clamped. A huge wall-clock delay of `TaskScheduler::ScheduleDelayed` saturates instead of overflowing.
