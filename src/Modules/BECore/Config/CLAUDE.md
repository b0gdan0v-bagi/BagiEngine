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
| `MakeFromConfig<T>(name)` (`ConfigFactory.h`) | `New<T>()` + `Deserialize` from the named config; returns defaults when the config is missing. Errors the deserializer reports (an unknown factory `type`, a malformed value) are written to the log, one `LOG_ERROR` each, by `LogDeserializeErrors(configName, deserializer)` (`Config 'LoggerConfig': sinks[0].type: Unknown factory type: 'Console'`); the object keeps what could be read. Include from `.cpp` files only (pulls in `CoreManager.h`) |

## ConfigManager

**DO**: read configs through the manager.
```cpp
auto rootNode = CoreManager::GetConfigManager().GetConfig("LoggerConfig"_intern);
```

**DON'T**: create loaders by hand or call `LoadFromVirtualPath()` directly for engine configs.

- `Initialize()` scans every layer of the `config` mount point (engine first, game on top) recursively, loads every XML in parallel through `TaskManager` and blocks until done. A second call is a no-op.
- The key is the **file stem** (without `.xml`). Two files with the same stem in different layers do not collide: the upper layer wins and the log says `Config 'X' from <path> overrides <path>`. A game's `SceneConfig.xml` therefore replaces the engine's entire file, it is not merged. Two files with the same stem inside one layer still collide (the one scanned later wins).
- `SaveConfig` writes to the file the config was loaded from, i.e. into the layer that won.
- `GetConfig` takes a `PoolString` or an `eastl::string_view` (the latter avoids interning) and returns an invalid `XmlNode` when the name is unknown, so check it.
- Documents live in `UnorderedPoolMap<ConfigEntry>`; writes are serialized by a mutex.
- Editor support: `GetAllConfigNames()`, `GetConfigFilePath(name)`, `SaveConfig(name)`, `ReloadConfig(name)` (replaces the cached document). Code that holds an `XmlNode` from before a reload must fetch it again.

## Reading values

```cpp
if (auto title = node.ParseAttribute<eastl::string_view>("title")) { /* use *title */ }
```

Prefer a reflected struct plus `MakeFromConfig<T>` / `Deserialize` over hand-written attribute loops. Hand-parsing with `XmlNode` is for small, fixed shapes (for example `ApplicationConfig.xml`).

## Adding a config

1. Drop `config/<Name>.xml`; it is picked up by name at the next start.
2. Consume it with `GetConfig("<Name>"_intern)` or `MakeFromConfig<T>("<Name>")`.
3. If users should edit it in the Config Editor, register enum or flag hints in `ConfigEditorWidget::Initialize` (`src/Widgets/ConfigEditor/`): `RegisterEnum<E>` for an enum, `RegisterFactoryTypes<Base>` for a `type` attribute resolved by `AbstractFactory<Base>`. The options must be the strings the loader accepts (for a factory, the class names), so take them from the factory or the enum, never from a copy.
