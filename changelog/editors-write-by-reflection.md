# The scene and config editors write through reflection

- Version: 0.29.0
- Entry: editors-write-by-reflection
- Date: 2026-11-26
- Bump: minor

## Added
- `Reflection/XmlFieldDiff`: a write that leaves out the fields equal to their owner's default, opt-in per write. Adapt: nothing.
- The Config Editor edits a config whose type is registered with `CookableConfig<T>` as a reflected object (typed fields, list elements and their types) and saves only the changed fields into the file, keeping comments and unknown attributes. Adapt: register a game config's type with `CookableConfig<T>` to get typed editing.
- `OwnedObject::Adopt(UniquePtr<T>)`, a protected `XmlSerializer::IsAtRoot()`. Adapt: nothing.

## Changed
- The scene editor saves a node's own reflected fields through `SceneNode::Serialize`, so a new reflected field of a node survives load and save; a node instance whose field other than `name` / `layer` differs from its node file is written inline. Adapt: nothing.
- A config with no registered type is edited as text fields only (no guessed checkboxes or numbers; `SDLWindowConfig` `windowFlags` is text). `ConfigFieldSchema` stays one merge as deprecated no-ops. Adapt: drop the `ConfigFieldSchema::Register*` calls and register the type with `CookableConfig<T>`.

## Breaking
- `ConfigTreeDrawer::Draw` has no config-name parameter. Adapt: drop the argument.
- The cook refuses a node file whose node is a `SceneNode` subclass (the binary node carries no type, so it would load as a plain node). Adapt: keep such a node in XML until the binary node form carries its type.
