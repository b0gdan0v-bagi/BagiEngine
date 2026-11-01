# TODO: places where the engine still ties the model to the XML format

## Context

The UI plan (`magical-leaping-blossom.md`, design rule 3) says: the element model is separate from the file format. `AbstractFactory<T>` creates an object from a type name and the reflected properties are set through `IDeserializer`; where the name and the values came from (XML, a CSS-like file, a map built in code) does not matter.

The owner expects the engine to already work this way elsewhere. The task is to **find the places that do not**, and record them in `TODO.md` **without going into the implementation**. This plan only adds that record. No code changes.

## What the search found (read-only, nothing was run)

**Already on the approach** (no TODO, listed in the entry as the reference): scene components (`AbstractFactory<IComponent>` + `Initialize`), widgets (`IWidget::Initialize(IDeserializer&)`), runtime tests (`TestManager`: `Ser::Load(d, tests, "tests")`), games (`AbstractFactory<IGame>`), resource loaders, log sinks and assert handlers, `MakeFromConfig<LoggerManager | AssertHandlerManager | SceneManager>`, `SDLWindowConfig`.

**Not on the approach:**

| # | Place | What it does by hand |
|---|---|---|
| 1 | `ApplicationConfig`: `ApplicationFabric::Create`, `IApplicationFabric::Create(const XmlNode&, ...)`, `SDL3ApplicationFabric::Create`, `ReadRunMode(const XmlNode&)`, `ResolveControlSettings` | Four readers of one file, each pulling attributes by name (`type`, `path`, `renderer`, `mode`, `control`, `controlPort`, `manualClock`, `headless`). A factory-base interface takes `XmlNode`. `ControlSettings` has a second hand parser for the command-line flags and a merge rule (file first, flags override), with hand range checks |
| 2 | `GameManager::Initialize` | Reads `GamesConfig` `defaultGame/@name` through `XmlNode` |
| 3 | `TextureLibrary::Initialize` / `Save`, `SpriteEntry` | `SpriteEntry` is not reflected; load walks `<sprites>` with `XmlNode`, save rebuilds the tree with raw pugixml |
| 4 | `WidgetManager::CreateWidgets` | Own loop over `<widget type>` with its own factory call; `TestManager` does the same job with `Ser::Load`. Two ways to build a factory list from a config |
| 5 | `MakeFromConfig`, `TestManager`, `WidgetManager`, `SDLMainWindow`, `ResourceManager::LoadSerialized(Async)` | Each constructs `XmlDeserializer` and calls `LoadFromXmlNode` itself. `ConfigManager::GetConfig` returns `XmlNode`, never an `IDeserializer`. `LoadSerialized` is XML-only although `BinaryDeserializer` exists and loaders are otherwise picked by extension. `SDLMainWindow` also opens its own `XmlConfig` by path |
| 6 | Sources that are not a file: start-up flags, control API JSON, and the planned CSS declarations | Each re-implements "values for named fields of a typed object". Control API: `Match3GameControl` builds state and actions as hand-made `Json` and reads `PerformAction` / `Reset` parameters through the `Json.h` helpers; there is no `ISerializer` / `IDeserializer` over JSON or over a name → value map. The UI plan's task 4.2 needs the map one |
| 7 | `SceneEditorWidget::SaveScene` / `BuildNodeXml` | Writes `<scene>` attributes and `<node name layer>` by hand, so a new reflected field of `Scene` or `SceneNode` is dropped on save. The scene part is already in the entry "The scene editor saves only the active scene"; the node part is not |
| 8 | `ConfigEditorWidget`, `ConfigFieldSchema`, `StagedChanges` | Edits XML attributes and keys field hints by XML path (`"sink/@type"`), guessing the type from the text ("true" → Bool, parses as int → Int). The scene inspector already edits through `IPropertyVisitor` with real types. The entry "Config Editor: the sink `type` dropdown..." is a symptom of this |
| 9 | Doc comments `TypeTraits.h` and `ReflectionMarkers.h` | Still show `BE_FUNCTION bool Initialize(const XmlNode& node)` as the example; the real hook is `Initialize(IDeserializer&)` |

Note: `Application.cpp`, `ApplicationFabric.cpp`, `ControlSettings.*`, `IApplicationFabric.h` have uncommitted changes (the headless work in the working tree), so the entry names functions, not line numbers, for them.

## The change

One new entry in `TODO.md`, placed after "No save games, hot-reload or asset packing" and before "The reflection generator is regex-based" (both stage 5, same neighbourhood). One entry, not nine: the problem is one ("the model is read from XML by hand"), and the existing format is "numbered findings, then what a fix has to cover".

- Heading (its slug is the entry id): `## Config and data code reads XmlNode by hand where the factory and reflection path already fits`
- `**Stage:** 5`. No `**Status:**` (defaults to open), no `**Plan:**`.
- `**Where:**` the files of rows 1 to 8.
- `**What is wrong**` (found by reading the code, not by running anything): the nine rows above, each as a numbered item, one or two sentences, no design.
- `**Already on the approach:**` one line, the reference list above.
- `**A fix has to cover:**` goals only, no mechanism:
  - one way to get an `IDeserializer` for a named config, so a caller does not pick `XmlDeserializer` itself, and one way to build a factory list from a config (rows 4 and 5);
  - the application, game and control settings read as reflected structs, with the command line and the file as two sources of the same struct (rows 1 and 2);
  - `TextureLibrary` entries reflected and saved through `ISerializer` (row 3);
  - a source that is not a file: a name → value map and a JSON-backed pair, shared by the start-up flags, the control API and the UI plan's task 4.2 (row 6);
  - the editors writing through the reflection (rows 7 and 8; keep the two existing entries as the place for their bugs, do not duplicate them);
  - the loader for an unknown type reports it: the generic `Ser::Load` path drops an unknown type silently (the first TODO entry), so row 4 must not lose the error `WidgetManager` logs today;
  - the doc comments of row 9 fixed with the code, and `Config/CLAUDE.md` updated.
- Cross-links by name to the two existing entries (Config Editor sink dropdown, scene editor saves only the active scene) and to the UI plan. The existing entries stay unchanged.

## Verification

```bash
.venv/bin/python tools/todo_sync/todo_sync.py check     # exit 0, the new entry listed, no duplicate id
git diff --stat                                          # only TODO.md (and this plan file)
```

Then read the entry once for the style of the file: English, no line numbers in the in-flight files, every claim marked as found by reading.
