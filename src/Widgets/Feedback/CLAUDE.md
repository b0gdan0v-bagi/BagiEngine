# Feedback

What the editor says back to its user. Editor only (`.claude/rules/widgets-module.md`).

| File | What |
|------|------|
| `ToastEvents.h` | `ToastLevel` and `EditorToastEvent{level, text}`, with which any editor code asks for a toast |
| `ToastList.{h,cpp}` | the toasts that live now: bounded, merged, ageing, fading; no ImGui |
| `ToastFeed.{h,cpp}` | what fills the list from events and the control journal; no ImGui |
| `ToastOverlay.{h,cpp}` | the toasts on screen; ImGui |
| `EditorProgress.{h,cpp}` | `EditorProgressRegistry`: the sources of the status bar's progress strip; no ImGui |
| `ActivityPanelModel.{h,cpp}` | the model of the Activity panel (`config/panels/ActivityPanel.xml`): the control journal as rows; no ImGui |
| `UIMotion.{h,cpp}` | what moves by the frame's time: `Fade`, `HoverBlendTable`, the durations; no ImGui |
| `TouchHighlights.{h,cpp}` | the flashes and the "who changed it" marks of what control API clients touched; no ImGui |
| `TouchFlashDraw.{h,cpp}` | the fill of a flash over an item or a rect and the "changed: <client>" line; ImGui |
| `ControlClientLabel.{h,cpp}` | what the editor calls a control client, one rule for the Activity panel and the marks; no ImGui |

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

## Activity panel

- "Who did what": the control journal (`.claude/rules/control-activity.md`) newest first, hidden until `view.activity`. `activity.clear` empties the journal itself, so every reader of it loses the old entries.
- A row whose call touched a node has a button (`activity.selectNode`, the node's runtime id as its argument) that selects and reveals the node in the Scene Editor (`SelectSceneNodeRequestEvent`). It shows the first node of the call; a node that is gone, or of a scene that is no longer active, is ignored.
- A client is shown by the name it has **now** (also in rows older than its `client_hello`), an unnamed one as `client N`, a call with no client as `engine`. Names and params are client text: the table draws them unformatted.
- A game with its own `config/WidgetsConfig.xml` adds the `AssetPanelWidget` line of `ActivityPanel`, or has no panel behind the menu item.
- Tests: `ActivityPanelModelTest`, `ActivityAssetTest` (over a service of its own, `ActivityTestModel.h`).

## Touched nodes

- `TouchHighlights` (a `Singleton`, so a panel needs no widget line) reads the touch record (`.claude/rules/control-activity.md`). `EditorLayoutWidget` connects it, advances it by `io.DeltaTime` before the panels are drawn and resets it when it dies, so without that widget nothing flashes or is marked. What the record held at the connect is old news.
- A **flash** belongs to a report and fades; a **mark** (the client who touched a field) belongs to the field and stays until it is dismissed or newer marks push it out; Activity's Clear keeps it. A report that names no field (`ui_click`) flashes and marks nothing. Calls of the Console and of a replay count like any client's.
- Shown on the hierarchy row of the node, only when the row is shown (nothing is opened or scrolled), and as an outline in the viewport, in both input modes, for a node with a rect or a canvas. The viewport walks the tree only while a flash lives.
- **Inspector.** The header of a flashing component flashes, also while collapsed. `RenderComponentInspector` takes a `ComponentTouches`: the generic inspector marks the **top-level** field (`color.r` marks `color`), with its flash over it and the line under it; a custom inspector draws a line per mark at its top, with the field's name. Components are matched by type name, so two of one type on a node share their marks. The Game panel's use of the visitor passes nothing and stays unmarked.
- **A mark goes with the user's edit** of the field: in the inspector (a custom one cannot say which field, so any edit drops the component's marks), an element's rect for a canvas's `elements`, and a committed gizmo drag for the transform fields it moved. Other panels that write a component (the Generate panel's accept) leave the mark.
- Tests: `TouchHighlightsTest`, `InspectorMarksTest`; the header's flash and the look are not covered.

## Motion

- Besides the flashes above, three things move: a docked or floating asset panel fades in and out (`src/Widgets/PanelAssets/CLAUDE.md`), the command palette fades in, and the hover colour of a button blends (`IconButton`, `IconTextButton` and the asset panels' buttons, all through `BlendedButton` of `EditorLayout/EditorControls.h`). A new button of a panel goes through one of them.
- A disabled button and a filled one (`ScopedHoverSnap`: a `fill`, a toggle that is on) keep ImGui's colours, which change at once.
- The blend follows the hover one frame late, and an item that is not drawn for a frame starts from rest.
- Time is `io.DeltaTime`: on the manual clock only a `step` moves them, so a closed panel's window stays drawn and a hovered button unlit until the clock goes on.
- Nothing at rest is animated or allocates. Tests: `UIMotionTest`; the fill's colours, `PanelInterpreterTest`.

