# Remote editor: the scene of another process

Editor code (`BE_WITH_EDITOR`) for editing a game the editor reaches over the control API. All but `RemoteSceneView` is data and logic with no ImGui. The data classes know no transport: results of control methods go in as text, property visits and writes come out. Everything read is a peer's: a wrong shape is left out, every recursion is bounded, and nothing is interned (the editor's pool never frees).

| File | Holds |
|------|-------|
| `RemoteSchema` | `schema`: types by name with their parent chain, fields, attributes and value types; factories |
| `RemoteTree` | `scene_tree`: nodes under their parents (the answer is flat; a parent is found by the path) |
| `RemoteComponent` | one `scene_get` answer with all fields; `Walk` visits it |
| `RemoteFieldWalker` | the visit itself |
| `RemotePendingWrites` | edits not yet seen in read data |
| `RemoteSession` | the link: connects, keeps the data above current, sends the writes |
| `RemotePicture` | the last `screenshot` frame as RGBA ("The picture") |
| `RemoteCallError` | why the program did not give what was asked |
| `RemoteSessionRegistry` | how a panel finds the editor's one session |
| `RemoteSessionWidget` | owns that session ("The widget and the panel") |
| `RemoteLinkStatus` | the state of the link in a sentence |
| `RemoteSceneView` | the Scene Editor's content in remote mode ("The Scene Editor") |
| `AttachPanelModel` | the Attach panel (`config/panels/AttachPanel.xml`) |
| `RemoteHierarchySource` | the session's tree and selection as the hierarchy panel reads them: read-only, rows named to ImGui by path (`.claude/rules/widgets-module.md`, "Scene Editor Conventions") |

`Load` of the three data classes rebuilds only for another text, so polling an unchanged scene costs a compare; a rebuild invalidates every pointer into the object. A walk allocates nothing in a steady state.

## The walk

The aim is that the local inspector's visitor (`ImGuiPropertyVisitor`) draws a remote component as it draws a local one, so the walker mirrors `PropVisit` call for call (`Reflection/PropertyVisitorDispatch.h`): a change of the dispatch, of a `VisitProperties` or of a wire shape (`Reflection/CLAUDE.md`, "JSON shapes of the reflection pair") is a change here.

The schema cannot say two things, and the walker holds them itself: an `opaque` object of numbers is a colour or a math type, told by its member names; a field whose local visit is a stub (`UICanvasComponent.variants`) is passed over by name. A new such type or stub needs its entry.

Read-only or absent remotely, because a write cannot do it (`scene_set` writes one leaf):

- a list or a map allows no add or remove;
- the presence of an `Optional` and the alternative of a `Variant`;
- `value`, `any`, unknown kinds and any other `opaque` value: a read-only text.

No edit is taken below a `readonly` or `hidden` field.

## Pending writes

An edit overrides the received value until a read **sent after** the write was answered arrives: a read sent earlier may still carry the old value. The caller numbers its requests in the order sent, and releases by component (`scene_get`) or for the layers (`scene_tree`).

One unsent write per target, a newer value replacing it, and at most one in flight. A text is held until the caller says no item is active: the visitor reports every keystroke, and a half typed name would be refused (`interning`: a name field of the game takes only a text it already knows). A refusal drops that write's override and is kept for the panel until the next edit.

## The session

`RemoteSession` is polled once a frame on the main thread (`Poll(now)`) over an `IRoomTransport` of its factory. The default, `MakeRemoteTransport`, is the desktop socket with room for the longest answer a control server sends, and null where there are no sockets (the web build too), which `Attach` reports.

| State | Meaning |
|-------|---------|
| `Detached` | no link. Every other state is remote mode |
| `Connecting` | the first connection of an `Attach` is being made or read |
| `Attached` | this connection has read the schema and the tree. The only state that sends writes and reads components: every other one is read-only |
| `Lost` | the link ended. What was read stays; the state lasts through the retries, until a new connection has read the schema and the tree |

- **Polling.** On open `status`, `schema` and `scene_tree`; then the tree again, and the selected node's components as one `batch` of `scene_get` with `continueOnError`, each on its interval (`RemoteSessionTiming`). At most one request of each kind is unanswered. Answers are matched by id; pushes and unreadable messages are dropped. `no_scene` is an empty tree.
- **Selection** is a node path, kept while the tree lists it. An answer about an earlier selection is dropped.
- **Writes** go out in the order of `GetWrites()`, one unanswered per target. A new connection drops what is pending: it was edited for what answered before.
- **A loss** is retried with a doubling wait, which starts over once attached. An outage is one Info line of the session's and no warning (QA reads warnings as findings); the transport adds an Info line per connection that ends. A peer that answers `unknown_method` to what the session reads with is `Lost` and not retried; to a write it is a refusal like any other.
- **Stalled** (`IsStalled`) is not a loss: requests wait and nothing is answered, as with a hidden browser tab or another client's deferred call holding the program's queue.
- A poll with nothing due, or with answers that repeat the last ones, allocates nothing in the session (the desktop transport allocates per message).

Tests drive it over `tests/BECore/FakeControlEndpoint.h`: the real methods over a scene (`screenshot` alone is a stand-in), behind a transport in memory that answers on `Serve()`.

## Pick and save

- `RequestPick` takes a point of the picture and asks `scene_pick`; the hit becomes the selection and `TakeReveal` hands it to the view once. A choice made by `Select` or `ClearSelection` after the pick wins over its answer; a tree that drops the selected node does not.
- A hit may be a node newer than the tree: the tree is read at once, and the first read sent after the answer selects it or gives it up.
- A refusal (`not_screen_view`: the program runs its editor) is `GetPickError` until a pick works.
- `RequestSave` sends `scene_save` only when no write is unsent (a held text too) or unanswered, so the file holds what was edited before it. A link that ends first leaves `Unconfirmed` and nothing is resent: the program may have written, and a later resend could write over another client's changes.
- `HasUnsavedWrites` counts this session's accepted writes against what a save that worked held; the program has no dirty flag to ask. It lasts over a reconnect, and `Detach` forgets it.

## The picture

Whoever shows the program's screen calls `RequestPicture` in each frame it does, so a hidden panel costs the program nothing: a poll sends `screenshot` only while `Attached`, when asked since the poll before, with one unanswered and the next one `kPictureInterval` after the answer.

- The longer side asked for stops at `kMaxRemotePictureSide`: the frame is decoded on the main thread.
- `RemotePicture` decodes only a `png_base64` that differs from the one of the frame; its revision is the key of a texture.
- The frame stays through `Lost` and a reconnect; `Detach` and a new `Attach` drop it.
- A refusal (`capture_failed`: a renderer that cannot read its output back) is the picture's error, retried after `kPictureRetry`; `unknown_method` is not asked again on that connection. Neither ends the session.
- On a program with the manual clock each `screenshot` runs a frame of zero seconds, so a watched game draws while nobody steps it; its state does not move.

## The widget and the panel

`RemoteSessionWidget` is listed in `config/WidgetsConfig.xml` right after `ImGuiWidget`: it polls the session with the steady clock (not the engine's, which a manual clock holds) before the panels read it. The session lasts as long as the widget, so a switch to Game mode detaches. `--attach=<address>` attaches at the start of the program only, not again at a switch back; a refused address is a logged error and the editor starts detached.

A panel asks `RemoteSessionRegistry` for the session in each call and keeps no pointer. `AttachPanelModel` only starts and ends the link and words its state; with no widget in the list it says so. The panel is `view.attach` (Game menu, "Attach to a Running Game..."), its buttons `remote.attach` and `remote.detach`. A browser editor offers none of them (`EditorLayout/CLAUDE.md`, "The editor in a browser").

## The Scene Editor

What remote mode does to the local editor: `.claude/rules/widgets-module.md`, "Scene Editor Conventions". `RemoteSceneView` draws what the local widget draws, from the session: the toolbar bar, a line on the link (a warning when lost or stalled, with the reason), the hierarchy, the inspector bar and the component list over `ImGuiPropertyVisitor`.

- Only a node's layer and its component fields take an edit, and only while `Attached`: the whole view is disabled otherwise, over what was read. A name would intern client text into the program's pool, and no method changes the structure.
- Typed texts are released once a frame while no ImGui item is active, also while the window is closed.
- The bars ask `ISceneEditorHost`, which answers from `Inspect`: the name is the tree's (never interned), the layer the pending one while there is one, and the last refusal is shown under the inspector's title.
- A component header comes from the tree and its fields from the last read, matched by type and namesake, since the tree may be the newer of the two.

Tests: `RemoteSceneEditorTest` runs the widget and its shipped bars over the fake endpoint, beside a local scene that must not change.

## Trying it by hand

Start a game with its control API and an editor attached to it, two processes of any build with the editor:

```bash
build/macos-debug/bin/BagiStress --control --stress-scene=entities &
build/macos-debug/bin/BagiStress --editor --attach=ws://127.0.0.1:8765
```

ctest `BagiStressAttachSmoke` (`tools/control/tests/attach_smoke.py`) does the same headless: a layer step in the editor's UI reaches the game, and a quit of the game leaves the editor read-only.
