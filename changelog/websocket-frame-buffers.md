# WebSocket frames: one copy per encode, one encode per broadcast, views on the way in, ring queues

- Version: 0.13.0
- Entry: websocket-frame-buffers
- Date: 2026-11-23
- Bump: minor

## Added
- `Fifo<T>` (`BECore/Types/Fifo.h`): a ring-buffer queue that keeps its capacity, for queues that churn (the connection outbox, the client and room server queues, the control service). Adapt: nothing; use it instead of `eastl::deque` for a steady FIFO.
- `WebSocketListener::BroadcastText(StringView)` and the optional `HubSink::broadcast`: one text to several connections is encoded once into a shared refcounted frame. `FindJsonMember(Json&, StringView)` returns a mutable member. Adapt: nothing.

## Changed
- `EncodeFrame` reserves once and copies the payload once; the client encodes into a reused buffer; control requests and calls are pooled and move `id` / `params` out of the parsed document. A room send to 4 clients allocates 968 times instead of 9276. Adapt: nothing.

## Breaking
- `WebSocketHandler::onMessage` takes a `WebSocketMessageView` (kind, a `Span<const uint8_t>` payload, the close code; `GetText()`) instead of an owning `WebSocketMessage`; the view is valid only during the call. Adapt: change the parameter type of a game's own `onMessage` handler and copy the payload (`String(view.GetText())`) where it is kept past the call.
