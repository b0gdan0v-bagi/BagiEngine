# Reflection: no access by name at run time: no type registry, property paths or callable methods

- Version: 0.1.16
- Entry: reflection-runtime-access
- Date: 2026-11-16
- Bump: patch

## Added
- `TypeRegistry` (`BECore/Reflection/`): every reflected type (`BE_CLASS`, `BE_EVENT`) registers a `TypeInfo` at start-up, with its name, parent, fields (attributes, `TypeDesc`, offset), methods (`callable` when every parameter and the result go through `Ser::`) and a creator for a factory type. A game's types appear after its next `cmake --preset`, with no change of its own.
- Property paths (`a.b[2].c`): `GetProperty` / `SetProperty` through `IPropertyVisitor`, with type, range and `readonly` checks, and compiled paths (offset and `TypeDesc` of the fixed part).
- `MethodHandle`: a `BE_FUNCTION` method called by name, its arguments read through an `IDeserializer` and its result written through an `ISerializer`.
- `JsonSerializer` / `JsonDeserializer` (`BECore/Control/`): an interim JSON pair over the checked `Json` DOM, with a fuzz target. The allocation-free reader stays `json-deserializer`.
- Control API over named roots (`ControlRoots`; `game` is the active game): `list_types`, `describe_type`, `schema`, `get_property`, `set_property`, `call_method`, each described for `list_methods`.
- `tools/schema`: an XSD per `config/*.xml` made from the live schema, for completion and checking in VS Code. The `BagiStress` control smoke validates the engine's own config files against it.

## Changed
- The `BE_FUNCTION` marks on the widget lifecycle overrides (`IWidget::Initialize` / `Update` / `DrawScreen` / `Draw` and the same on the engine's widgets) are removed: a marked method is one that may be called from outside. Adapt: a game widget that marked its lifecycle overrides with `BE_FUNCTION` drops the mark; nothing called them by name.
