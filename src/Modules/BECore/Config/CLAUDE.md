# Config Module Guidelines

## Overview

XML configuration: every file under `config/` (the engine's and the game's, see layering below) is loaded once at startup and read through `ConfigManager`. Parsing is pugixml behind thin wrappers; reflected objects are filled from a config with the serialization layer (`.claude/rules/reflection-module.md`).

## Components

| Class | Role |
|-------|------|
| `ConfigManager` | Loads and caches all configs; reached via `CoreManager::GetConfigManager()` |
| `XmlDocument` | `RefCountedAtomic` owner of a `pugi::xml_document`: load/save file or string, `GetRoot()` |
| `XmlConfig` | Handle over an `IntrusivePtrAtomic<XmlDocument>`: `LoadFromVirtualPath`, `LoadFromFile`, `LoadFromString`, `SaveToFile` |
| `XmlNode` | Lightweight pugi node wrapper: `GetChild(name)`, `Children()`, `Name()`, `ParseAttribute<T>(name)` → `std::optional<T>`; `if (node)` is the validity check |
| `ConfigManager::CreateDeserializer(name)` | An `IDeserializer` (`eastl::unique_ptr`) over the root of a named config, or `nullptr` when the config is unknown. The one way for a caller to read a named config through the reflection; it must not outlive a `ReloadConfig` of that config |
| `OpenDeserializer(virtualPath)` (`Reflection/OpenDeserializer.h`) | An `IDeserializer` for a file, picked by extension (`.xml` gives `XmlDeserializer`, `.bin` gives `BinaryDeserializer`); `std::expected` with a message when the extension is unsupported or the file does not load. The header does not pull `CoreManager.h`. `ResourceManager::LoadSerialized` and `SDLMainWindow` use it |
| `MakeFromConfig<T>(name)` (`ConfigFactory.h`) | `New<T>()` + `Deserialize` through `CreateDeserializer`; returns defaults when the config is missing. Errors the deserializer reports (an unknown factory `type`, a malformed value) are written to the log, one `LOG_ERROR` each, by `LogDeserializeErrors(configName, deserializer)` (declared in `OpenDeserializer.h`, included by `ConfigFactory.h`; `Config 'LoggerConfig': sinks[0].type: Unknown factory type: 'Console'`); the object keeps what could be read. Include from `.cpp` files only (pulls in `CoreManager.h`) |

## ConfigManager

**DO**: read configs through the manager.
```cpp
auto rootNode = CoreManager::GetConfigManager().GetConfig("LoggerConfig"_intern);
```

**DON'T**: create loaders by hand or call `LoadFromVirtualPath()` directly for engine configs.

- `Initialize()` scans every layer of the `config` mount point (engine first, game on top) recursively, loads every XML in parallel through `TaskManager` and blocks until done. A second call is a no-op.
- The key is the **file stem** (without `.xml`). Two files with the same stem in different layers do not collide: the upper layer wins and the log says `Config 'X' from <path> overrides <path>`. A game's `SceneConfig.xml` therefore replaces the engine's entire file, it is not merged. Two files with the same stem inside one layer still collide (the one scanned later wins). The scan is recursive, so the node files of a scene (`config/nodes/X.xml`, referenced by `<node ref="nodes/X"/>`, see `Scene/CLAUDE.md`, "Node files") are loaded as configs named by their stem too: prefix them with the game name so that no stem equals a real config.
- `SaveConfig` writes to the file the config was loaded from, i.e. into the layer that won.
- `GetConfig` takes a `PoolString` or an `eastl::string_view` (the latter avoids interning) and returns an invalid `XmlNode` when the name is unknown, so check it.
- Documents live in `UnorderedPoolMap<ConfigEntry>`; writes are serialized by a mutex.
- Editor support: `GetAllConfigNames()`, `GetConfigFilePath(name)`, `SaveConfig(name)`, `ReloadConfig(name)` (replaces the cached document). Code that holds an `XmlNode` from before a reload must fetch it again.

## Reading values

```cpp
if (auto title = node.ParseAttribute<eastl::string_view>("title")) { /* use *title */ }
```

Prefer a reflected struct plus `MakeFromConfig<T>` / `Deserialize` over hand-written attribute loops. A caller that needs an `IDeserializer` asks the engine (`CreateDeserializer`, `OpenDeserializer`); it does not construct `XmlDeserializer` for a named config. Hand-parsing with `XmlNode` is for small, fixed shapes that no reflected struct has replaced yet. A reflected struct per config is the shape: `ApplicationConfig` (`BECore/Application/`, loaded by `LoadApplicationConfig()`) and `GamesConfig` (`Games/`, read by `GameManager::Initialize`) are the examples. The strict-values mode (`IDeserializer::SetStrictValues(true)`, `reflection-module.md`, "Serialization") is opt-in for a config whose bad boolean or number must be an error and not a zero; `ApplicationConfig` uses it, the default stays lenient.

A reflected entry in a list whose element name the file fixes is read and written by a hand-written array: `TextureLibrary` (`BECore/Resource/`, `Resource/CLAUDE.md`, "TextureLibrary") has `SpriteEntry` as `<sprite>` elements of `<sprites>` (a generic `eastl::vector` would write `<item>`), `Load(IDeserializer&)` / `Write(ISerializer&)` as the pure halves and `Initialize` / `Save` as the glue over `CreateDeserializer` and `SaveConfig`.

A list of factory objects (widgets, runtime tests, log sinks, assert handlers) is a reflected holder with one `eastl::vector<IntrusivePtr...<Base>>` field, filled by `MakeFromConfig<Holder>(name)`; do not write a `<x type>` loop with your own `AbstractFactory` call. `WidgetList` (`BECore/Widgets/`, field `widgets`) and `Tests::TestList` (`BECore/Tests/`, field `tests`) are the shape, `LoggerManager` and its sinks another. The loader reports an unknown `type` (`Config 'GameWidgetsConfig': widgets[1].type: Unknown factory type: 'X'`), skips `enabled="false"` and an element without `type` (no message), and calls the element's `bool Initialize(IDeserializer&)` in its own element scope when the base declares it (`IWidget`): `false` drops the element with `<Type> failed to initialize`. Spell the pointer `IntrusivePtrAtomic` or `IntrusivePtrNonAtomic` in the field, the generator does not match the `IntrusivePtr` alias.

## Adding a config

1. Drop `config/<Name>.xml`; it is picked up by name at the next start.
2. Consume it with `GetConfig("<Name>"_intern)` or `MakeFromConfig<T>("<Name>")`.
3. If users should edit it in the Config Editor, register enum or flag hints in `ConfigEditorWidget::Initialize` (`src/Widgets/ConfigEditor/`): `RegisterEnum<E>` for an enum, `RegisterFactoryTypes<Base>` for a `type` attribute resolved by `AbstractFactory<Base>`. The options must be the strings the loader accepts (for a factory, the class names), so take them from the factory or the enum, never from a copy.

## Content version

A **content file** is a file of a `config/` folder whose root element is `SceneManager` (a `SceneConfig.xml`) or `node` (a node file): `IsContentRoot` (`ContentVersion.h`). Its root may carry `contentVersion="N"`; **no attribute is version 1**, the format before versions (an explicit `0` is older than anything). The engine reads exactly one number, `kContentVersion`: a format change that old files cannot satisfy adds a step to `tools/migrate/steps/` and bumps it (`tools/migrate/CLAUDE.md`). Other files (`UIConfig.xml`, `TextureLibrary.xml`, ...) are not content files and carry no version; the first step that rewrites one adds its root to `IsContentRoot`.

- **One number for the engine, one stamp per file:** a game's files are migrated together with the engine's, never read through an in-memory upgrade, so a loader only compares and refuses. A file is stamped by the tool, not by hand.
- **Who checks:** `LoadSceneManagerFromConfig()` (the root of the game's `SceneConfig`; an older, newer or malformed root is one `LOG_ERROR` and an empty `SceneManager`) and `MakeFileSystemNodeLoader` (the node file of a reference is dropped like one that does not parse). **Who does not:** `LoadSceneManager(XmlNode, ...)` (a parsed root of tests and tools) and `MakeDirectoryNodeLoader`, so inline XML in tests needs no stamp; `CheckContentVersion` is the pure check a test of a content file calls. The message names the file, its version and the engine's, and says `python3 engine/tools/migrate/migrate.py <your game folder>` (older) or "update the engine" (newer).
- **The stamp is not part of the scene:** `ExpandNodeReferences` removes it from the copy of a node it inserts; the deserializer ignores it on the `SceneConfig` root; the scene editor's save keeps that root as it is.
- **A game's own steps** use the attribute `contentVersion.<track>`, which no engine loader reads: the game's test runs the tool with `--check`.
- A test of a game's own content: `ExpectContentFilesAtCurrentVersion(configFolder)` (`tests/Common/ContentFilesCheck.h`).
