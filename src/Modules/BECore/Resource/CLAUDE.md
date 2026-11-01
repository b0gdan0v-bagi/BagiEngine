# Resource Module Guidelines

## Overview

Loading, caching and lifetime of game assets. Sync and async loading (`Task<T>`), a reference-counted cache and pluggable loaders. The Doxygen on `ResourceManager.h`, `IResourceLoader.h` and `IResource.h` has usage examples; this file only records the rules and the non-obvious parts.

## Architecture

```
ResourceManager (CoreManager::GetResourceManager())
    ├── ResourceCache        thread-safe (std::shared_mutex), no eviction in v1
    └── IResourceLoader[]    picked by file extension via CanLoad()
         ├── XmlResourceLoader   (.xml  → XmlResource)
         ├── SDLTextureLoader / VulkanTextureLoader   (images → ITexture)
         └── FontLoader          (.ttf/.otf → Font, CPU only; `Text/CLAUDE.md`)

IResource (RefCounted, BE_CLASS(IResource, FACTORY_BASE))
    ├── XmlResource
    ├── Font
    └── SerializedResource<T>   (reflected types)
```

- `ResourceState` (`CORE_ENUM`): `Unloaded` → `Loading` → `Loaded` or `Failed`.
- `ResourceHandle<T>` wraps `IntrusivePtr<T>`: implicit `bool` for validity, `->` / `*`, last-access time (kept for a future LRU, not used yet).
- `TextureLibrary` (`CoreManager::GetTextureLibrary()`) maps sprite names to texture path + source rect. It is loaded from `TextureLibrary.xml` (the active game's `config/`, the engine ships none) after `ConfigManager`; a missing file leaves the library empty, and `SpriteRendererComponent` and the texture editor use it.

## ResourceManager API

| Call | Notes |
|------|-------|
| `Load<T>(path)` / `LoadAsync<T>(path)` | `T` derives from `IResource`; cached by path; returns `ResourceHandle<T>` (async: `Task<ResourceHandle<T>>`) |
| `LoadSerialized<T>(path)` / `LoadSerializedAsync<T>(path)` | `T` is a `BE_CLASS` type; deserialized from XML, returned **by value** |
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

Derive from `IResourceLoader` with `BE_CLASS(MyLoader)`, implement `CanLoad` / `LoadAsync` / `LoadSync` (see `SDL/CoreSDL/SDLTextureLoader.h`). **No manual registration**: the generator registers the class in `AbstractFactory<IResourceLoader>` from its `.gen.hpp`, and `ResourceManager::Initialize()` creates and registers every registered type. Include the `.gen.hpp` in the loader's own `.cpp`, and make sure its directory is in `reflection_targets.json`.

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

`tests/BECore/ResourceManagerTest.cpp` covers sync/async loading, cache hits, invalid paths and memory tracking.
