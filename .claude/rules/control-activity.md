---
paths:
  - "src/Modules/BECore/Control/**/*"
  - "src/Modules/RemoteControl/**/*"
  - "tools/control/**/*"
  - "tests/BECore/Control*"
  - "tests/BECore/WebSocketServerTest.cpp"
---

# Who asks the control API (`ControlClient`, `client_hello`) and the activity journal

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

## The activity journal (`service.Activity()`)

`ControlActivity` (`Control/ControlActivity.h`) keeps who did what, for the editor: the last `kCapacity` entries, oldest first, each with a sequence (never reused, `Clear()` included), the time of day, the client and a kind. Main thread only. `GetVersion()` moves with every entry and every `Clear()`, so a view rebuilds only then; a reader that wants the new entries keeps `GetNextSequence()`.

| Kind | Recorded when |
|------|---------------|
| `Connected`, `Disconnected` | `Drain()` applies the notice of a connection (a local client has none) |
| `Named` | `RenameClient()`; `text` is the name given then, while `client->GetName()` is the latest |
| `Call` | a call is answered: `method`, `text` (the params), `error` (the code, empty for a success) |

- A deferred call is listed when it is answered, a call nested in a `batch` under the outer client.
- Not listed: a method described as quiet; a request that ran no method (it could not be read, `unknown_method`); what `Shutdown()` answers with `shutting_down`.
- **`ControlMethodInfo::quiet`** is for a method that acts on nothing: a read, a poll, the clock (`step`, `run_until`), `batch` (its calls are listed) and what the art service reports. A method with no description is listed, so a game's acting methods show without a change and its reads need a description with `quiet`. `list_methods` does not show the flag.
- `text` is client data cut to `kMaxTextBytes` between two UTF-8 characters, its control characters turned into spaces. Neither it nor a name is ever interned or used as a format string.
