# cmake/ — the engine setup and the build options

`BagiEngine.cmake` is the engine setup and the `be_*` functions games build on (`.claude/rules/cmake-rules.md`); `BagiPlatformOptions.cmake` the platform options; `BagiWeb.cmake` the Emscripten side. A change to any `*.cmake` needs a configure.

## Presets

| OS | Presets |
|----|---------|
| macOS | `macos-debug`, `macos-release` (Clang + Ninja); `macos-release-editor`; `macos-rmlui-debug` / `-release` (`ENGINE_UI_RMLUI=ON`) |
| Windows | `windows-clang-{debug,release}` (default; configure and build through `python CI/scripts/vs_env.py cmake ...`), `windows-msvc-{debug,release}`, `windows-msvc-release-editor`, `windows-vs2022-*`, `windows-vs2022-clangcl-*`. The MSVC builds are fallbacks (their PDBs break parallel builds now and then: `LNK1103`, `C1090`) |
| Web | `web-release`, `web-debug` (threaded, the default: the task system's workers are Web Workers, the page needs cross-origin isolation, which `tools/web/serve.py` and the Docker image send), `web-release-single-thread`, `web-debug-single-thread` (needs nothing). Always listed: they find the SDK in the submodule `external/emsdk` (pinned tag, about 2 GB), installed once per checkout by `python CI/scripts/setup_emsdk.py` (in a game `engine/CI/...`; `--check`); without it configure stops with "could not find toolchain file". Build only, no editor (also in Debug), no tests. Output `build/<preset>/bin` (the page `BagiMatch3.html`). Entry `web-build` of the todo store; the browser side: `tools/web/CLAUDE.md` |

Per-machine settings go in `CMakeUserPresets.json` (git-ignored). Without presets: `cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`.

**Output goes under the binary dir of the build**: executables and shared libraries in `build/<preset>/bin`, static libraries in `build/<preset>/lib`, so Debug, Release and every other preset sit side by side and nothing needs deleting when you switch presets. A build without a preset gets `<-B dir>/bin`. A multi-config generator (the `windows-vs2022-*` presets) adds the configuration, `build/<preset>/bin/<Config>/`. The old `build/{Compiler}{Platform}/` folders are not written any more and not removed by the build; delete them by hand. A game that carries the engine as a submodule gets `build/<preset>/bin` in its own `build/`; its docs and tools need their own follow-up (Mahjong: entry `mahjong-build-paths`). Switching `BE_WITH_EDITOR` inside one preset still needs the rebuild of "The editor option".

## The editor option

`BE_WITH_EDITOR` decides whether the editor panels (`src/Widgets/{ConfigEditor,EditorLayout,SceneEditor,TextureEditor,Viewport}`) and their tests are built:

- `AUTO` (default): ON except in a single-config `Release` / `RelWithDebInfo` / `MinSizeRel`. A multi-config generator picks sources at configure time, so `AUTO` is ON there.
- `ON`: also in Release (`-DBE_WITH_EDITOR=ON`, or the `*-release-editor` presets). `OFF`: none; **a Debug build ignores `OFF`** with a configure WARNING (except the web presets: no editor there in any configuration).
- Configure prints `Editor: ENABLED|DISABLED`. The definition `BE_WITH_EDITOR=1|0` is on every target (`kEditorBuilt` in `BECore/Application/RunMode.h`); code that needs an editor header is guarded with `#if BE_WITH_EDITOR`. ImGui and `ImGuiWidget` are in every build.
- `mode="Editor"` in a build without the editor starts in Game mode with one warning. With the editor, Ctrl+Shift+E switches modes at run time (`src/Modules/BECore/CLAUDE.md`, "Run mode").

## Web-like build on the desktop

`BagiPlatformOptions.cmake`: `ENGINE_WITH_VULKAN`, `ENGINE_WITH_REMOTE_CONTROL` (ON), `ENGINE_STATIC_SDL`, `ENGINE_SINGLE_THREAD` (OFF); Emscripten forces the first three to the web values (not `ENGINE_SINGLE_THREAD`: its web default OFF is the threaded page, the `web-*-single-thread` presets set it ON) and sets `ENGINE_TARGET_WEB` / `PLATFORM_WEB`. To check the web configuration without emsdk: `cmake -S . -B build/webcheck -G "Visual Studio 17 2022" -A x64 -DENGINE_WITH_VULKAN=OFF -DENGINE_SINGLE_THREAD=ON`, then build `BagiMatch3 BagiEngineTests` with `--config Release`; run it with `ENGINE_SINGLE_THREAD` ON and OFF (the threaded page's task system is the desktop one). It writes into `build/<preset>/bin` like every preset, so nothing needs deleting.
