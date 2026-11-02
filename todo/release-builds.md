---
stage: 6
id: no-release-builds-games-run-only-as-developer-executables
---
# No release builds: games run only as developer executables

**Where:** `cmake/BagiEngine.cmake` (a target is Windows, Apple or else "Linux"; `SDL_SHARED ON`; `be_add_game_executable` bakes the checkout paths into `BAGI_ENGINE_ROOT` / `BAGI_GAME_ROOT` and copies the Vulkan loader), `CMakePresets.json` (macOS and Windows only; the release presets only turn asserts off), `src/Application/Application.cpp` (`Run()` is a blocking `while`), `src/Modules/TaskSystem/`, `Logger/FileSink.h` and `Control/ControlService` (threads and mutexes), `config/ApplicationConfig.xml` (`renderer="VulkanRendererBackend"`), `src/Modules/SDL/src/CoreSDL/SDLEventsProvider.cpp` (no touch or app-lifecycle events), `FileSystem/CLAUDE.md` (writes go to the resolved path in the checkout), `external/SDL/android-project` and the SDL Emscripten support (present, not used).

**What is missing** (found by reading the repository, not by building for these targets):

1. **PC:** no packaging. The content roots are the absolute paths of the developer's checkout and the assets are loose files, so a copied build runs only where the checkout is. There is no macOS `.app`, no Windows archive or installer, no version or icon. The Windows Vulkan DLL is git-ignored (see [the CI entry](no-ci.md)). Saves and edited configs would be written into the checkout.
2. **Android:** no NDK toolchain and no Gradle project (SDL ships a template), and CMake would call an Android build "Linux". There are no touch events, no pause / resume handling and no per-app storage path, and the layout is fixed pixels.
3. **Web:** no Emscripten toolchain. `Run()` never gives control back to the browser, `TaskSystem`, `FileSink` and the control service use threads (browser threads need SharedArrayBuffer and special headers, or a single-thread build), SDL is built shared, the assets are loose files, and Vulkan does not run in a browser.
4. The control API's server is a development tool and must not ship in a release.

**A fix has to cover:**

- **backend first:** web has no Vulkan, so Vulkan cannot be the only release backend. Recommendation: the SDL renderer on all release targets (option (a) in [the renderer entry](renderer-backends.md)); the SDL3 GPU option (c) must first be checked for a browser backend;
- a release configuration: Game mode, no ImGui or editor widgets, the control server off, content roots relative to the executable (`SDL_GetBasePath`), saves and user config in `SDL_GetPrefPath`, packed assets (the pak in [the entry on save games, hot-reload and packing](save-hot-reload-packing.md));
- **PC** (Windows and macOS; Linux if wanted, with the presets from [the test-gaps entry](test-gaps-platforms.md)): a release preset, a `.app` bundle and a Windows folder or zip, a version, the Vulkan DLL question settled;
- **web:** an Emscripten preset, static SDL, a frame callback instead of the blocking loop, a decision on threads, a preloaded pak, an HTML shell;
- **Android:** an NDK preset on top of SDL's `android-project`, touch and lifecycle events in `SDLEventsProvider` (with [the input mapping entry](input-mapping.md)), safe-area and screen size handling (with the UI entry), a signed APK or AAB;
- one command (or one CI job per target) that produces the artifacts, and a smoke run per target;
- the order: PC first (packaging and relative roots), then web, then Android. Mahjong is the first game to release because it needs only mouse and touch.
