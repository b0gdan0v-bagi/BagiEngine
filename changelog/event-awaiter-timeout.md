# TaskSystem: WaitForEventWithTimeout never times out

- Version: 0.4.4
- Entry: event-awaiter-timeout
- Date: 2026-11-18
- Bump: patch

## Changed
- `co_await WaitForEventWithTimeout<E>(duration)` now resumes with an empty result when the time runs out; before, it waited for the event alone. The wait and the event belong to the main thread. A zero, negative or NaN timeout and a run without a task scheduler do not suspend.
