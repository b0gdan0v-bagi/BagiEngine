# Feedback

What the editor says back to its user. Editor only (`.claude/rules/widgets-module.md`).

| File | What |
|------|------|
| `ToastEvents.h` | `ToastLevel` and `EditorToastEvent{level, text}`, with which any editor code asks for a toast |
| `ToastList.{h,cpp}` | the toasts that live now: bounded, merged, ageing, fading; no ImGui |
| `ToastFeed.{h,cpp}` | what fills the list from events and the control journal; no ImGui |
| `ToastOverlay.{h,cpp}` | the toasts on screen; ImGui |
| `EditorProgress.{h,cpp}` | `EditorProgressRegistry`: the sources of the status bar's progress strip; no ImGui |

## Toasts

- **Sources.** `EditorToastEvent`; `ConfigChangedEvent` with the cause saved (every `SaveConfig`, whoever calls it) or reloaded (`Config/CLAUDE.md`, "Hot reload and `ConfigChangedEvent`"); `ConfigReloadFailedEvent`; the connects and disconnects of the control journal (`.claude/rules/control-activity.md`). An edit in place (`NotifyConfigChanged`) says nothing: panels send it while the user types.
- **A failed save is the caller's to tell**: the manager sends no event for it. The scene's save and the Config Editor's emit an error toast themselves.
- The journal is read by `ToastFeed::Update` from the first entry it has not seen, and only when its version moved. What it held before `Connect` is old news, so a switch of the run mode repeats nothing. A local client has no connect entry, hence no toast.
- **A repeat is one toast** with a counter: same level and text. It moves to the newest place, so a busy source cannot push itself out, and its life starts again without a second fade-in.
- A feed with nothing new, and a list whose texts it has held before, allocate nothing; a new toast may.

## On screen

- `EditorLayoutWidget` owns the overlay and draws it after its host window, so there is no widget line to add to a `WidgetsConfig.xml`. Without that widget no toast is shown.
- One window above the status bar, begun only while a toast lives: no input, no focus, no docking, nothing in `imgui.ini`. It is moved to the front of the display order every frame, so a floating window cannot cover it.
- A card is **one line**: the first of its text, drawn unformatted (it may hold client text) and cut with an ellipsis.
- The toasts age by `io.DeltaTime`, so on the manual clock only a `step` moves them.
- Tests: `ToastListTest` (the list and the feed), `ToastOverlayTest` (a context with no backend). The look is not covered.

## Progress strip

- An owner of an operation that spans frames registers a source and keeps the handle; the source ends with it. `StatusBar` asks every source once per frame and shows the first operation, in the order of registration, with "+N" for the others.
- **A source tells the truth while its panel is closed**, where the model's `Update` does not run: it takes the end of its operation itself. The export's polls its process there, which is also what lets a cancelled script be killed.
- Sources: `ExportPanelModel` (no fraction: the build's output has no reliable total) and `ArtGenPanelModel` (a setup action, else a run; nothing while no service is connected). Their tests are with the models' own.
- A load that blocks inside one frame has no source, since nothing can be drawn meanwhile; an asynchronous one registers its own.

