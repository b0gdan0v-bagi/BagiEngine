---
paths:
  - "src/Modules/BECore/Control/**/*"
  - "src/Modules/RemoteControl/**/*"
  - "tools/control/**/*"
  - "tests/BECore/Control*"
  - "src/Modules/BECore/Scene/SceneControl*"
  - "tests/BECore/WebSocketServerTest.cpp"
---

# Who asks the control API (`ControlClient`, `client_hello`), the activity journal and the touch record

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
- **Who is named:** the engine's command line (Console panel) is `console`, the request of `--replay` is `replay`; the MCP server calls itself `mcp` and the art bridge `artgen` (`tools/control/CLAUDE.md`, "Client names").

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
| `Call` | a call is answered: `method`, `text` (the params), `error` (the code, empty for a success), `node` (the node it touched first, 0 for none) |

- A deferred call is listed when it is answered, a call nested in a `batch` under the outer client.
- Not listed: a method described as quiet; a request that ran no method (it could not be read, `unknown_method`); what `Shutdown()` answers with `shutting_down`.
- **`ControlMethodInfo::quiet`** is for a method that acts on nothing: a read, a poll, the clock (`step`, `run_until`), `batch` (its calls are listed) and what the art service reports. A method with no description is listed, so a game's acting methods show without a change and its reads need a description with `quiet`. `list_methods` does not show the flag.
- `text` is client data cut to `kMaxTextBytes` between two UTF-8 characters, its control characters turned into spaces. Neither it nor a name is ever interned or used as a format string.

## The touch record (`service.Touches()`)

`ControlTouches` (`Control/ControlTouches.h`) keeps who touched which node, for the editor: the last `kCapacity` reports of `{sequence, nodeId, component, field, client}`, read like the journal (oldest first, `GetNextSequence()`, `GetVersion()`). Main thread only.

- A node is named by `SceneNode::GetRuntimeId()`, never by a pointer: the record keeps no node alive and the pool reuses a dead node's address. A reader finds the node with `FindNodeByRuntimeId`.
- `ControlService::ReportTouch(nodeId, component, field)` records for the call whose handler or `done` runs now; inside `RunNested` that is the nested call. It also puts the first node of a call into that call's journal entry, so report before the answer.
- Nothing is recorded outside a call or for a call with no client: a panel's own button that shares code with a method leaves no trace.
- A change applied after its call (a game that queues it for a later frame) keeps `GetCurrentClient()` in an `IntrusivePtr` and calls `ReportTouch(client, nodeId, ...)`, which writes no journal entry.
- A game calls `ReportNodeTouched(node, component, field)` (`Scene/SceneControl.h`) from a method that changes or acts on a node; nothing is reported for it by itself.
- `component` and `field` may be client data: cut like the journal's `text`, never interned, never a format string.
