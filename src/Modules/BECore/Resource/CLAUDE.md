# Resource Module Guidelines

## Overview

Loading, caching and lifetime of game assets. Sync and async loading (`Task<T>`), a reference-counted cache and pluggable loaders. The headers carry one or two sentences of contract per call; the usage example is under "ResourceManager API" and `XmlResource` with `XmlResourceLoader` is the reference pair; this file records the rules and the non-obvious parts.

## Architecture

```
ResourceManager (CoreManager::GetResourceManager())
    ├── ResourceCache        thread-safe (SharedMutex), no automatic eviction (Evict drops one path)
    └── IResourceLoader[]    picked by file extension via CanLoad()
         ├── XmlResourceLoader   (.xml  → XmlResource)
         ├── SDLTextureLoader / VulkanTextureLoader   (images → ITexture)
         ├── FontLoader          (.ttf/.otf → Font, CPU only; `Text/CLAUDE.md`)
         ├── AudioClipLoader     (.wav/.ogg → AudioClip, decoded whole on the CPU; `Audio/CLAUDE.md`)
         ├── UIDocumentLoader    (.ui.xml → UIDocument; `UI/CLAUDE.md`)
         └── UIWebDocumentLoader (.ui.html → UIDocument, the web-style front-end; `UI/CLAUDE.md`)

IResource (RefCounted, BE_CLASS(IResource, FACTORY_BASE))
    ├── XmlResource
    ├── Font
    └── SerializedResource<T>   (reflected types; no loader fills it, `LoadSerialized` returns by value)
```

- `ResourceState` (`CORE_ENUM`): `Unloaded` → `Loading` → `Loaded` or `Failed`.
- `ResourceHandle<T>` wraps `IntrusivePtr<T>`: implicit `bool` for validity, `->` / `*`, last-access time (kept for a future LRU, not used yet).
- `TextureLibrary` (`CoreManager::GetTextureLibrary()`) maps sprite names to texture path + source rect. It is loaded from `TextureLibrary.xml` (the active game's `config/`, the engine ships none) after `ConfigManager`; a missing file leaves the library empty, and `SpriteRendererComponent` and the texture editor use it. See "TextureLibrary" below.

## TextureLibrary

`SpriteEntry` (`name`, `texturePath`, `srcRect`) is a reflected struct, so its fields are exactly the attributes of the file: `<root><sprites><sprite name texturePath><srcRect x y w h/></sprite>...</sprites></root>` (`PoolString` attributes, `Rect` the child object `srcRect`). The field names stay without an underscore because `UISprite`, `SpriteRendererComponent` and the texture editor use them. `BE_CLASS` gives the struct a vptr, so `SpriteEntry` is no longer an aggregate: make one by default construction and assign the fields.

- **Pure halves:** `Load(IDeserializer&)` and `Write(ISerializer&) const` need no engine services and no file (tests: `tests/BECore/TextureLibraryTest.cpp`, on `XmlDeserializer::LoadFromString` and `XmlSerializer`). `Load` clears the library, skips an entry without a `name` (silently), lets a repeated name overwrite in place and reads numbers leniently (the default `IDeserializer`, a malformed number is 0; not strict values). `Write` always writes `sprites`, also when empty, in insertion order.
- **Glue:** `Initialize()` = `ConfigManager::CreateDeserializer("TextureLibrary")` (null: the library stays empty, no message) + `Load` + `LogDeserializeErrors`. `Save()` = `Write` into an `XmlSerializer`, then it replaces the `sprites` child of the cached document root with a copy (`remove_child` + `append_copy`, so the other children and the comments of the file stay) and calls `SaveConfig`. That replace is the only `pugi` left; a `ConfigManager` helper for "replace a section of a config" is for the editors' slice.
- **Hand-written array:** `Write` and `Load` call `BeginArray("sprites", "sprite", count)` themselves, because a reflected `eastl::vector<SpriteEntry>` is written with `<item>` elements and the file has `<sprite>`.
- **Accepted differences from the old `XmlNode` loop:** `XmlDeserializer::BeginArray` hands out every child node of `<sprites>`, so an element not named `sprite` is read as a sprite (it was skipped), and a comment inside `<sprites>` is one element without a `name` (skipped like any entry without a name); the index in the path of a bad sprite counts such nodes.

## Asset parts

One asset file holds the data of several consumers, so a headless run does not load what only a window needs. A reflected class has a purpose (`BE_CLASS(Name, PURPOSE=sim|visual|editor)`, default `Common`, declared per class and not inherited; `.claude/rules/reflection-module.md`), and `AssetParts.h` loads a file one part at a time.

- **Layout:** `<name>.asset.xml` is `<asset><sim><T .../></sim><visual>...</visual><editor>...</editor></asset>`: one object per part under the part's name, the object named by its type like in a `LoadSerialized` file. A part may be absent. A `.bin` whole file is read the same way (`BeginObject` by part name); the engine splits only XML, `.bin` splitting is left to `asset-pak`.
- **`LoadSerializedPart<T>(path, part)`** (the logic of both calls is `OpenAssetPart`) enters `<part>` and then `<T>` and never visits another part, so no object of another part is created or filled and a headless build need not link the visual classes. It sets the deserializer's **purpose filter** (`IDeserializer::SetPurposeFilter`) to the part: any reflected object whose purpose is neither `Common` nor the part (a field, an array element, a factory element) is an error at its path and is not filled, and a factory element is refused before `Create`. If `T` itself is refused, the result is a default object plus the error line. Errors are logged like `LoadSerialized` does. A reader without a filter (every other load) accepts everything.
- **Error paths** start at the document root of the whole file (`sim.PartSimHolder.field`) but at the part's first child in a split file (`PartSimHolder.field`): the split file's root is the part element itself.
- **Split files:** `SplitAssetFile` (the packer step) writes `gem.sim.xml`, `gem.visual.xml` and `gem.editor.xml` for the parts present (`SplitPartPath`; only `.asset.xml` names split) and removes the file of a part that is gone, so the files mirror the source; `SplitAssetXml` is the pure text half. The loader takes the split file of its part when it is **not older than the whole file** (or the whole file is missing) and falls back to the part of the whole file otherwise, so a stale split file left by an earlier run is never served. `asset-pak` packs the split files and needs no knowledge of the format.
- **Build-time check:** the generator refuses a class with a field of a class of a conflicting purpose (it sees names in field types only); the filter catches what it cannot, such as factory-created and `type="..."` elements.

## ResourceManager API

| Call | Notes |
|------|-------|
| `Load<T>(path)` / `LoadAsync<T>(path)` | `T` derives from `IResource`; cached by path; returns `ResourceHandle<T>` (async: `Task<ResourceHandle<T>>`) |
| `LoadSerialized<T>(path)` / `LoadSerializedAsync<T>(path)` | `T` is a `BE_CLASS` type; opened with `OpenDeserializer` (XML or binary, picked by the file extension), returned **by value** (default-constructed when the file does not load) |
| `LoadSerializedPart<T>(path, part)` / `LoadSerializedPartAsync<T>` | one part of a `*.asset.xml` file, `Optional<T>` (empty when the file does not open or the part or the object is absent); see "Asset parts" |
| `RegisterLoader(loader)` | rarely needed, see below |
| `ClearCache()` | releases every cached resource |

```cpp
Task<void> LoadAssets() {
    auto& rm = CoreManager::GetResourceManager();
    auto xml = co_await rm.LoadAsync<XmlResource>("config/game.xml");
    if (xml && xml->GetState() == ResourceState::Loaded) {
        // use xml->GetRoot()
    }
}
```

## Adding a loader

Derive from `IResourceLoader` with `BE_CLASS(MyLoader)`, implement `CanLoad` / `LoadAsync` / `LoadSync` (see `SDL/CoreSDL/SDLTextureLoader.h`). A loader is chosen by the longest suffix of the file name that some loader accepts (`ResourceManager::FindLoaderForPath`): `ui/hud.ui.xml` asks for `.ui.xml` and then `.xml`, so a format inside XML files gets a loader of its own and `CanLoad` receives the whole suffix. **No manual registration**: the generator registers the class in `AbstractFactory<IResourceLoader>` from its `.gen.hpp`, and `ResourceManager::Initialize()` creates and registers every registered type. Include the `.gen.hpp` in the loader's own `.cpp`, and make sure its directory is in `reflection_targets.json`.

`LoadAsync` must not block the caller. To leave the calling thread, `co_await SwitchToBackground()` (`TaskSystem/Awaitables.h`).

## Thread Safety

- `ResourceCache` takes shared locks for reads and an exclusive lock for `Put()` / `Clear()`.
- Resources are immutable after loading, so concurrent reads are safe.
- Async loading runs on the TaskSystem thread pool.
- Tests that call `LoadAsync` must run it through `CoreManager::GetTaskManager().Run(...)` and wait on the handle (`.claude/rules/tests-module.md`).

## Best Practices

**DO**
- `LoadAsync<T>()` at startup and loading screens; check `if (handle)` and `GetState()` before use.
- `LoadSerialized<T>()` for configuration-like objects.
- Create a loader for each new asset type.
- Hold `ResourceHandle<T>` / `IntrusivePtr`, not raw pointers.

**DON'T**
- Call sync `Load()` on the main thread during gameplay.
- Load the same path repeatedly: the cache already deduplicates.
- Create circular resource dependencies.
- Log with format arguments directly: `LOG_ERROR("Failed {}"_format(path))`.

## Testing

`tests/BECore/ResourceManagerTest.cpp` covers sync/async loading, cache hits, invalid paths and memory tracking; `AssetPartsTest.cpp` and `AssetPurposeTest.cpp` the parts and the filter.
