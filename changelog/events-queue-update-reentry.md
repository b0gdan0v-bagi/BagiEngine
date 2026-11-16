# Events: EventsQueueRegistry::UpdateAll walks a vector that a delivered event can grow (iterator invalidation)

- Version: 0.1.28
- Entry: events-queue-update-reentry
- Date: 2026-11-16
- Bump: patch

## Changed
- `EventsQueueRegistry::UpdateAll` runs the updates registered when it started; a handler that enqueues an event type for the first time no longer invalidates the walk, and that type is first updated by the next `UpdateAll`.
