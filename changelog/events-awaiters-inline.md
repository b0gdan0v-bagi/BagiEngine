# Subscriptions, event awaiters, timer links and tween waiters allocate on first use; MoveOnlyFunction heap path uncounted

- Version: 0.11.1
- Entry: events-awaiters-inline
- Date: 2026-11-23
- Bump: patch

## Changed
- `SubscriptionHolder` keeps two connections inside the object, an `EventAwaiter` holds its one connection, the link of a timed await comes from a pool and a tween handle keeps its first waiter inline: a subscription, a `co_await` of an event (with or without a timeout) and the first waiter of a tween no longer allocate from the heap. The price is size: `SubscriptionHolder` is 96 bytes (was 32), so `IComponent` is 128 (was 64), `IWidget` 112.
- The heap path of `MoveOnlyFunction` (a capture over its inline size) is counted under its own name in the allocation tracker.
