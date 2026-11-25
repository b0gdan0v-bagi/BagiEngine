# Events Module Guidelines

## Overview

Every event type has its own channel of listeners and its own queue (CRTP `EventBase<T>`), so there is no central bus to register with. The channel (`EventChannel.h`, header-only because `BECoreModule` emits events without linking `EventsModule`) is a type-erased list of (instance, invoker) pairs.

**Delivery rules** (pinned by `EventDispatchSemanticsTest` and `EventChannelTest`):
- `Emit` calls the listeners from the **last subscribed to the first**, over the count at its start: a listener subscribed by a handler is called from the next `Emit`. A nested `Emit` is delivered at once.
- (handler, instance) is a listener's identity: subscribing the pair again moves it to the end, and a connection disconnects by the pair, not by a token. A disconnect moves the **last** listener into the freed slot, so the order of the others changes.
- A channel is for **one thread**. The exception is `AssertEvent` (any thread may fail an assert): it hides `Emit` / `Subscribe` / `Unsubscribe` / `UnsubscribeAll` with versions that take one recursive lock of its own (`BECore/CLAUDE.md`, "Assert").
- `Update()` delivers what was queued when it started, in order; an event that a handler enqueues waits for the next `Update()`.
- `EventsQueueRegistry::UpdateAll()` runs the updates registered when it started: a type a handler enqueues for the first time registers during the pass and is first updated by the next `UpdateAll()`.
- The queue registry (`EventsQueueRegistry`) keeps plain function pointers, not a `MoveOnlyFunction`: that header includes the assert macros, which emit events, so `EventBase.h` could not include it. An update is `[]() { Derived::Update(); }` and needs no capture.
- `SceneUpdateList` (the scene's update events) follows the same pass rules with a stable order: last added first, count at the start, a removal inside a run leaves a tombstone and the outermost run compacts, so the order of the others never changes.
- A disconnect inside a handler does not crash or skip the others (the entry is marked and the list compacted after the outermost dispatch); still avoid it, the order after it is not a contract.

## Event Structure

Derive from `EventBase<T>`:
- Simple event: `struct MyEvent : EventBase<MyEvent> {};`
- With data: add fields **and a constructor**. `Emit(args...)` builds the event with `Derived{args...}`, and an `EventBase` event is not an aggregate (`BaseEvent` has a virtual destructor), so the constructor must match the arguments (see `ActionEvent`).
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

- Connections are held by the object and dropped in its destructor; `SubscriptionHolder::UnsubscribeAll()` drops them early. A holder is neither copied nor moved: a connection is bound to the address that subscribed.
- The first `kInlineConnections` connections live inside the holder, so a component, widget or awaiter that subscribes that many times needs no allocation for the holder's own list (`SubscriptionAllocationTest`); the event channel's listener list still grows at a new peak of listeners. A further connection moves the list to the heap once, and `UnsubscribeAll()` keeps the capacity. Every holder is that much larger, subscribed or not: it is the base of `IComponent`, `IWidget` and `IRenderer`. The owner accepted the cost: `SubscriptionHolder` is 96 bytes (was 32), `IComponent` 128 (was 64), `IWidget` 112, `IRenderer` 208 (arm64 macOS, the same in Debug and Release); a base that grows again is a decision of the owner's.
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
| `RenderEvents.h` | Rendering (`NewFrameEvent`, `RenderPresentEvent`, `ImGui*Event`) |
| `InputEvents.h` | Input (`ActionEvent`) |
| `SceneEvents.h` | Scene (`SceneUpdateEvent` and `SceneFixedUpdateEvent` carry `deltaTime`), declared with `BE_EVENT`. Global, not per scene: components subscribe with `IComponent::SubscribeInScene`. For the two update events that registers in the scene's `SceneUpdateList`, run by `Scene::Update()` (a hand `Emit()` reaches plain `Subscribe`rs only); for the others only the active scene's react (`BECore/Scene/CLAUDE.md`). Drawing is not an event: `Scene::Draw()` runs the draw system in sorted order: it draws the quad and the sprite itself and calls `IComponent::OnDraw()` of the other components |
| `ConfigEvents.h` | `ConfigChangedEvent{name}`: a cached config document changed (file edited on disk, `SaveConfig`, `ReloadConfig`); `BECore/Config/CLAUDE.md`, "Hot reload" |
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
- Connect or disconnect inside an event handler unless you must (a coroutine awaiter does): the order of delivery after it is not a contract.
