# Control API: answer strings and lists live on the calling thread's scratch arena

- Version: 0.15.2
- Entry: control-reply-string-arena
- Date: 2026-11-23
- Bump: patch

## Added
- `ArenaString` and `ArenaVector` work as `BE_CLASS` fields (written to JSON, exported in the schema). `TextView` (`BECore/Reflection/TextView.h`): a write-only reflected text field that views text owned elsewhere instead of copying it. `ControlService::GetMethodViews()` lists the methods on the scratch arena without copying the names. Adapt: nothing; a game's answer struct can hold `ArenaString` / `ArenaVector` / `TextView` fields and fill them inside its handler.

## Changed
- `ControlService` opens a `ScratchScope` around each handler, its `done` and `Publish`. The answers of the built-in methods, the schema, UI, Profiler, Audio, FileWatch, Clipboard, games and replay, and the room messages keep their texts and lists on that arena: a steady control call or room message allocates nothing on the heap. Adapt: a value passed to `ControlCall::ReplyValue` that holds arena members must be a local of the handler or of `done`, never kept or captured past the call.
