---
paths:
  - "src/Modules/BECore/Control/**/*"
  - "src/Modules/RemoteControl/**/*"
  - "tools/control/**/*"
  - "tests/BECore/Control*"
  - "tests/BECore/WebSocketServerTest.cpp"
---

# Who asks the control API (`ControlClient`, `client_hello`)

The module: `src/Modules/BECore/Control/CLAUDE.md`; the transport: `src/Modules/RemoteControl/CLAUDE.md`. Additive to protocol version 1.

## Clients

A request may carry a `ControlClient` (`Control/ControlClient.h`, ref-counted): the last argument of `ControlService::Post()`, `call.GetClient()` in a handler, and the same one in every nested call (`batch`). No client means the engine's own call.

| Kind | Made by | Told to the main thread |
|------|---------|-------------------------|
| connection | `OpenConnectionClient()` / `CloseConnectionClient()`, any thread; the control server does both, once per connection | a connect and a disconnect notice |
| local | `OpenLocalClient(name)`: a part of the program that posts requests of its own | nothing |

- **Whoever keeps a client keeps it alive**: a request, a call and a record hold it, so a connection that has ended still has its id and name.
- **Notices** are queued apart from the requests and applied at the start of `Drain()`, in the order they happened; the barrier holds none. So a connect is known before any request of that client runs, while a request may run after its client's disconnect (it was held, or both arrived between two drains).
- The id counts from 1 and is never reused by a service. The name and `IsConnected()` belong to the main thread.

## `client_hello`

`{name}` to `{id, name}`: the connection names itself; asking again renames it. Optional: a client that never asks works as before and has an empty name.

- `name` is a string of 1 to `kMaxControlClientNameBytes` bytes with no control character, else `invalid_params`; the old name stays. The text is not checked for UTF-8.
- A call with no client (posted by the engine without one) fails with `no_client`.
- The name is client text: it is never interned and never used as a format string.
