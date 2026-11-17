# Idioms of the engine, with the files that use them

Each idiom is a shape that already exists in the code: open the file and copy it rather than the sketch here. Paths are from the engine root.

## Rebuild only when the key moved

`src/Widgets/EditorLayout/StatusBar.cpp` (`StatusBar::Refresh`), `src/Widgets/Console/ConsoleWidget.cpp` (`RefreshMethods`).

The source keeps a revision that only grows; the consumer keeps the last one it saw and compares for equality. A frame with nothing new costs the comparison.

```cpp
if (_meter.Version() != _shownMeterVersion) {
    _shownMeterVersion = _meter.Version();
    FormatFps(_meter.GetFps(), _fpsText);   // char _fpsText[kTextSize]: a member, no heap
}
CopyIfChanged(game ? game->GetName() : PoolString(), _seenGame, _game);
```

A name is copied into its `String` member only when the `PoolString` differs from the one last seen. The key can be anything that moves with the input: a version, the window size, the source strings (`src/Modules/BECore/UI/UITextPainter.cpp`).

## Scratch kept as a member

`src/Modules/BECore/Scene/Components/ParticleEmitterComponent.h` (`_vertices`), `src/Modules/BECore/Audio/AudioManager.h` (`_musicScratch`), `src/Modules/BECore/UI/UIBoxMesh.h` (pure functions that append to lists the caller keeps).

A list a frame needs is a member: `clear()` keeps its capacity, so after the first frames nothing allocates. A function that builds geometry takes the list to append to instead of returning a new one.

## Subscribing

`src/Application/Application.cpp` (a `SubscriptionHolder`), `src/Modules/BECore/Scene/Components/ClickableComponent.cpp` (a component).

```cpp
Subscribe<ApplicationEvents::QuitEvent, &Application::StopApplication>(this);                          // Application : SubscriptionHolder
SubscribeInScene<SDLEvents::MouseButtonDownEvent, &ClickableComponent::OnMouseButtonDown>(this);   // in OnAttached()
```

The holder owns the connections and drops them in its destructor. `IComponent`, `IWidget` and `IRenderer` already derive from it.

## A task and `co_await`

`src/Modules/BECore/Text/FontLoader.cpp`, `src/Modules/BECore/Audio/AudioClipLoader.cpp`.

```cpp
Task<IntrusivePtr<IResource>> FontLoader::LoadAsync(PoolString path) {
    co_await SwitchToBackground(TaskPriority::Normal, BE_TASK_LABEL("Load font"));
    co_return LoadInternal(path);
}
```

The synchronous and the asynchronous load share one function; the coroutine only says on which thread it runs. Back on the main thread with `co_await SwitchToMainThread()` before touching the scene.

## `CORE_ENUM` and `EnumUtils`

`src/Modules/BECore/Save/SaveGame.h`, `src/Modules/BECore/Control/ReflectionControl.cpp`.

```cpp
CORE_ENUM(SaveErrorKind, uint8_t, NotInitialized, InvalidSlotName, NotFound, WriteFailed, ReadFailed, Corrupt, WrongGame, VersionTooNew, VersionTooOld)
EnumUtils<PropertyErrorCode>::ToString(code)
```

The reflection reads the values, so the enum is written and read by name in content and in the control API.

## An error that changes nothing

`src/Modules/BECore/Save/SaveGame.cpp` (`SaveGameService::Load`).

Every check that can fail (the slot, the envelope, the game, the version) runs and returns `std::unexpected` before `readState` is called, so a refused save leaves the game's state as it was. The error carries a kind (`CORE_ENUM`) and a message made with `Format`.

## Callables

`src/Modules/BECore/Save/SaveGame.h` (`FunctionRef<void(ISerializer&)>`: called only during `Save`), `src/Modules/BECore/Animation/Tween.h` (`FixedFunction<kTweenApplyBytes, void(float)>`: stored per tween, never allocates).

## `PassKey` and `Singleton`

`src/Modules/BECore/Time/Time.h` (`Tick(PassKey<Application>)`), `src/Modules/BECore/Scene/Scene.h` (`SetActive(bool, PassKey<SceneManager>)`): a public method only one class may call. `src/Modules/Games/src/Games/GameManager.h` (`class GameManager : public Singleton<GameManager>` with `friend class Singleton<GameManager>`): a global service.

## A lock with a site

`src/Modules/BECore/PoolString/PoolString.cpp`.

```cpp
SharedMutex _mutex{BE_LOCK_SITE("PoolString")};
```

The site names the lock in the profiler's lock waits (`Profiler/CLAUDE.md` "Lock waits").

## A lookup by outside text

`src/Modules/BECore/UI/UIControl.cpp`.

```cpp
const auto name = PoolString::Find(StringView(nameText->c_str(), nameText->size()));
```

`Find` never adds to the pool; an unknown name is a miss that allocates nothing.
