---
paths:
  - "**/*Event.h"
  - "**/*Events.h"
  - "tests/BECore/ReflectionEvent*"
---

# Reflection Events Guidelines

## Overview

`BE_EVENT` is a lightweight reflection macro for event types: static type metadata, field iteration and non-virtual `Serialize` / `Deserialize`, with no virtual dispatch and no vptr of its own. It is independent of `EventBase<T>` (the dispatch side, see `src/Modules/Events/CLAUDE.md`).

## BE_EVENT vs BE_CLASS

| Feature | BE_EVENT | BE_CLASS |
|---------|----------|----------|
| **Memory overhead** | 0 bytes (no vptr) | adds a vptr unless the class already has virtual methods |
| **GetStaticTypeMeta()** | yes | yes |
| **GetStaticTypeHash()** | yes | no (use `GetStaticTypeMeta().typeHash`) |
| **GetTypeMeta() instance** | no | yes (virtual, correct through a base pointer) |
| **Is\<T\>() / Cast\<T\>()** | no | yes |
| **Serialize / Deserialize** | yes (non-virtual) | yes (virtual) |
| **ForEachFieldStatic** | const only | mutable and const |
| **BE_REFLECT_FIELD** | yes | yes |
| **BE_FUNCTION**, `AcceptPropertyVisitor` | no | yes |
| **Use case** | Events | Classes / polymorphic types |

## When to Use

**Use `BE_EVENT` when:**
- The type is known statically (no polymorphism).
- The event data is immutable.
- You need reflection for serialization or logging.

**Use `BE_CLASS` when:**
- You need `Is<T>()` / `Cast<T>()`, polymorphic inheritance with a factory, mutable field iteration or method reflection.

## Usage Pattern

```cpp
struct MyEvent {
    BE_EVENT(MyEvent)
    BE_REFLECT_FIELD int32_t playerId = 0;
    BE_REFLECT_FIELD float value = 0.0f;
};
```

## Generated API

| Method | Description |
|--------|-------------|
| `GetStaticTypeMeta()` | `ClassMeta&` |
| `GetStaticTypeHash()` | `uint64_t` type hash |
| `GetStaticTypeName()` | `eastl::string_view` |
| `GetStaticFieldCount()` | Number of reflected fields |
| `ForEachFieldStatic(obj, func)` | Constexpr iteration over fields (const only) |
| `Serialize(ISerializer&) const` / `Deserialize(IDeserializer&)` | Write / read the reflected fields |

## Combining with EventBase

`BE_EVENT` does not make a type dispatchable; `Emit` / `Enqueue` / subscriptions come from `EventBase<T>`. The two compose:

```cpp
struct FastEvent : EventBase<FastEvent> { int value = 0; FastEvent() = default; explicit FastEvent(int v) : value(v) {} };  // dispatch only
struct SceneDrawEvent : EventBase<SceneDrawEvent> { BE_EVENT(SceneDrawEvent) };   // dispatch + reflection (Events/SceneEvents.h)
struct DebugData { BE_EVENT(DebugData) BE_REFLECT_FIELD int value = 0; }; // data only, cannot be Emit()ted
```

`EventBase<T>` derives from `BaseEvent`, which has a virtual destructor, so an event that derives from it carries a vptr regardless of `BE_EVENT`. The zero-overhead numbers apply to a standalone `BE_EVENT` struct (`sizeof` of an empty one is 1, one with an `int32_t` and a `float` is 8; see `tests/BECore/ReflectionEventTest.cpp`).

Choose:
- **EventBase only** for hot-path events that need no introspection.
- **EventBase + BE_EVENT** when the event is also logged, serialized or inspected.
- **BE_EVENT only** for plain reflected data that is never dispatched.

## Best Practices

### DO
- Use `BE_EVENT` for event-like types that need reflection.
- Mark fields with `BE_REFLECT_FIELD`.
- Keep events immutable and use `constexpr` constructors where possible.
- Use `GetStaticTypeHash()` as a stable per-type key when you need one.

### DON'T
- Use it for polymorphic types (use `BE_CLASS`).
- Modify event fields after construction.
- Expect `Is<T>()` / `Cast<T>()` (not available).
- Use `BE_FUNCTION` (events have no reflected methods).
