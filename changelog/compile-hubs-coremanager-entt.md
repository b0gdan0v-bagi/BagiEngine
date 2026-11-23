# Compile time: CoreManager.h includes every manager and EnTT sits in every TU through the event headers

- Version: 0.9.0
- Entry: compile-hubs-coremanager-entt
- Date: 2026-11-23
- Bump: minor

## Changed
- `CoreManager.h` no longer includes the header of any service: every service is held by pointer and only declared there, so a file that includes it parses none of the managers. A clean build of the engine's tests costs about 10 % less CPU.
- `FrameKind` lives in `BECore/Time/FrameKind.h`; `Time/GamePause.h` includes it, so code that took it from there is unchanged.

## Breaking
- A file that uses what a `CoreManager` getter returns must include that service's header itself, as `CoreManager.h` no longer brings it in: `GetTime()` needs `<BECore/Time/Time.h>`, `GetFileSystem()` `<BECore/FileSystem/FileSystem.h>`, `GetConfigManager()` `<BECore/Config/ConfigManager.h>`, `GetCommandLine()` `<BECore/Application/CommandLine.h>`, `GetResourceManager()` / `GetTextureLibrary()` `<BECore/Resource/...>`, `GetUIVars()` / `GetUIStrings()` / `GetUIThemes()` / `GetUIEnvironment()` `<BECore/UI/...>`, `GetTweens()` `<BECore/Animation/TweenManager.h>`, `GetControlService()` `<BECore/Control/ControlService.h>`, `GetAudio()` `<BECore/Audio/AudioManager.h>`, and so on for each getter. Code that called `IGame::Cast<T>()` and relied on `CoreManager.h` for `ClassMeta` includes `<BECore/Reflection/ClassMeta.h>`. Adapt: build the game, and for each "member access into incomplete type 'X'" add the include of X's header to that file (the getters' names and return types are unchanged).
