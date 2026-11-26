# Remote editor: the scene of another process

Editor code (`BE_WITH_EDITOR`) for editing a game the editor reaches over the control API. The files here are data and logic with no ImGui and no transport: results of control methods go in as text, property visits and writes come out. Everything read is a peer's: a wrong shape is left out, every recursion is bounded, and nothing is interned (the editor's pool never frees).

| File | Holds |
|------|-------|
| `RemoteSchema` | `schema`: types by name with their parent chain, fields, attributes and value types; factories |
| `RemoteTree` | `scene_tree`: nodes under their parents (the answer is flat; a parent is found by the path) |
| `RemoteComponent` | one `scene_get` answer with all fields; `Walk` visits it |
| `RemoteFieldWalker` | the visit itself |
| `RemotePendingWrites` | edits not yet seen in read data |

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
