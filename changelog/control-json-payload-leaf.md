# Control API: a reflected JSON payload leaf; envelopes, game, replay and room answers are reflected structs

- Version: 0.14.1
- Entry: control-json-payload-leaf
- Date: 2026-11-23
- Bump: patch

## Added
- `JsonPayload` (`BECore/Control/JsonPayload.h`): a reflected field that writes a JSON value inside an answer struct, as an owned `Json`, a borrowed `const Json&`, ready JSON text, or a writer callback that streams without building a tree. The schema export says `any` for it. Adapt: nothing; a game answer that was a hand-written writer because it held a `Json` can become a `BE_CLASS` struct with a `JsonPayload` field.
- `JsonWriter::Fail(ControlError)` and `GetFailure()`: a writer callback records why it failed, and the control call answers that error. Adapt: nothing.

## Changed
- The response, error and push envelopes, `call_method`, `batch`, `get_state`, `list_actions`, `perform_action`, `state_changed`, the replay mismatch and the room messages are written from reflected structs instead of by hand. The bytes on the wire are unchanged. Adapt: nothing.
