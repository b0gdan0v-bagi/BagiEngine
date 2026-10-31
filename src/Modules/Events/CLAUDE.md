# Events Module Guidelines

## Overview

Event system on top of EnTT's `entt::dispatcher`. Every event type has its own static dispatcher (CRTP `EventBase<T>`), so there is no central bus to register with.

## Event Structure

Derive from `EventBase<T>`:
- Simple event: `struct MyEvent : EventBase<MyEvent> {};`
- With data: add fields **and a constructor**. `Emit(args...)` builds the event with `Derived{args...}`, and an `EventBase` event is not an aggregate (`BaseEvent` has a virtual destructor), so the constructor must match the arguments (see `ActionEvent`, `SetRenderDrawColorEvent`).
- For reflection/serialization of an event, add `BE_EVENT(MyEvent)` (see `.claude/rules/reflection-events.md`).

## Subscription API

Subscribing is possible **only through `SubscriptionHolder`**: `EventBase::Subscribe` takes a `PassKey<SubscriptionHolder>`, so calling `MyEvent::Subscribe<...>(this)` from your own code does not compile.

```cpp
class MyWidget : public IWidget {   // IWidget and IRenderer already derive from SubscriptionHolder
    bool Initialize(IDeserializer&) override {
        Subscribe<RenderEvents::NewFrameEvent, &MyWidget::OnNewFrame>(this);
        return true;
    }
    void OnNewFrame();
};
```

- Connections are held by the object and dropped in its destructor; `SubscriptionHolder::UnsubscribeAll()` drops them early.
- `MyEvent::Unsubscribe<&Handler>(instance)` / `MyEvent::UnsubscribeAll(instance)` are the static counterparts for a single event type.

## Emitting API

| Method | Behavior |
|--------|----------|
| `MyEvent::Emit(args...)` | Immediate (synchronous) |
| `MyEvent::Enqueue(args...)` | Queued; the first `Enqueue` registers the event's `Update()` in `EventsQueueRegistry` |
| `MyEvent::Update()` | Dispatches the queue of this event type |

`EventsQueueRegistry::UpdateAll()` runs once per frame from `CoreManager::OnGameCycle`, after the widgets' `Update()`. Do not call `Update()` by hand.

## Event Categories

| File | Purpose |
|------|---------|
| `ApplicationEvents.h` | App lifecycle (`QuitEvent`, `ApplicationCleanUpEvent`) |
| `RenderEvents.h` | Rendering (`NewFrameEvent`, `RenderClearEvent`, `RenderPresentEvent`, `ImGui*Event`, `SetRenderDrawColorEvent`) |
| `InputEvents.h` | Input (`ActionEvent`) |
| `SceneEvents.h` | Scene (`SceneUpdateEvent`, `SceneDrawEvent`), declared with `BE_EVENT` |
| `SDL/CoreSDL/SDLEvents.h` | Raw SDL input, defined in the SDL module |

Other modules define their own events next to their code (for example `LogEvent` in `BECore/Logger`, `AssertEvent` in `BECore/Assert`).

## Best Practices

### DO
- Subscribe through `SubscriptionHolder` so lifetime is automatic.
- Use `PoolString` for string data.
- Keep events small (only necessary data).
- Prefer `Emit` over `Enqueue` unless the handler must run later in the frame.

### DON'T
- Store pointers to temporary objects in events.
- Emit events from constructors.
- Create circular event dependencies.
- Connect or disconnect inside an event handler (undefined behavior).
