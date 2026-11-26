# Feedback

What the editor says back to its user. Editor only (`.claude/rules/widgets-module.md`).

| File | What |
|------|------|
| `ToastEvents.h` | `ToastLevel` and `EditorToastEvent{level, text}`, with which any editor code asks for a toast |
| `ToastList.{h,cpp}` | the toasts that live now: bounded, merged, ageing, fading; no ImGui |
| `ToastFeed.{h,cpp}` | what fills the list from events and the control journal; no ImGui |

## Toasts

- **Sources.** `EditorToastEvent`; `ConfigChangedEvent` with the cause saved (every `SaveConfig`, whoever calls it) or reloaded (`Config/CLAUDE.md`, "Hot reload and `ConfigChangedEvent`"); `ConfigReloadFailedEvent`; the connects and disconnects of the control journal (`.claude/rules/control-activity.md`). An edit in place (`NotifyConfigChanged`) says nothing: panels send it while the user types.
- **A failed save is the caller's to tell**: the manager sends no event for it. The scene's save and the Config Editor's emit an error toast themselves.
- The journal is read by `ToastFeed::Update` from the first entry it has not seen, and only when its version moved. What it held before `Connect` is old news, so a switch of the run mode repeats nothing. A local client has no connect entry, hence no toast.
- **A repeat is one toast** with a counter: same level and text. It moves to the newest place, so a busy source cannot push itself out, and its life starts again without a second fade-in.
- A feed with nothing new, and a list whose texts it has held before, allocate nothing; a new toast may.
