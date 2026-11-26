# Remote editor: the scene of another process

Editor code (`BE_WITH_EDITOR`) for editing a game the editor reaches over the control API. The files here are data and logic with no ImGui. The data classes know no transport: results of control methods go in as text, property visits and writes come out. Everything read is a peer's: a wrong shape is left out, every recursion is bounded, and nothing is interned (the editor's pool never frees).

| File | Holds |
|------|-------|
| `RemoteSchema` | `schema`: types by name with their parent chain, fields, attributes and value types; factories |
| `RemoteTree` | `scene_tree`: nodes under their parents (the answer is flat; a parent is found by the path) |
| `RemoteComponent` | one `scene_get` answer with all fields; `Walk` visits it |
| `RemoteFieldWalker` | the visit itself |
| `RemotePendingWrites` | edits not yet seen in read data |
| `RemoteSession` | the link: connects, keeps the data above current, sends the writes |
| `RemoteSessionRegistry` | how a panel finds the editor's one session |
| `RemoteSessionWidget` | owns that session ("The widget and the panel") |
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

Tests drive it over `tests/BECore/FakeControlEndpoint.h`: the real methods over a scene, behind a transport in memory that answers on `Serve()`.

## The widget and the panel

`RemoteSessionWidget` is listed in `config/WidgetsConfig.xml` right after `ImGuiWidget`: it polls the session with the steady clock (not the engine's, which a manual clock holds) before the panels read it. The session lasts as long as the widget, so a switch to Game mode detaches. `--attach=<address>` attaches at the start of the program only, not again at a switch back; a refused address is a logged error and the editor starts detached.

A panel asks `RemoteSessionRegistry` for the session in each call and keeps no pointer. `AttachPanelModel` only starts and ends the link and words its state; with no widget in the list it says so. The panel is `view.attach` (Game menu, "Attach to a Running Game..."), its buttons `remote.attach` and `remote.detach`. A browser editor offers none of them (`EditorLayout/CLAUDE.md`, "The editor in a browser").
