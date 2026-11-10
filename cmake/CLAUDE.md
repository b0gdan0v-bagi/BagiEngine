# cmake/ — the engine setup and the build options

`BagiEngine.cmake` is the engine setup and the `be_*` functions games build on (`.claude/rules/cmake-rules.md`); `BagiPlatformOptions.cmake` the platform options; `BagiWeb.cmake` the Emscripten side. A change to any `*.cmake` needs a configure.

## Presets

| OS | Presets |
|----|---------|
| macOS | `macos-debug`, `macos-release` (Clang + Ninja); `macos-release-editor`; `macos-rmlui-debug` / `-release` (`ENGINE_UI_RMLUI=ON`) |
| Windows | `windows-clang-{debug,release}` (default; configure and build through `python CI/scripts/vs_env.py cmake ...`), `windows-msvc-{debug,release}`, `windows-msvc-release-editor`, `windows-vs2022-*`, `windows-vs2022-clangcl-*`. The MSVC builds are fallbacks (their PDBs break parallel builds now and then: `LNK1103`, `C1090`) |
| Web | `web-release`, `web-debug` (Emscripten, shown only when `EMSDK` is set); build only, no editor, no tests. Entry `web-build` of the todo store |

Per-machine settings go in `CMakeUserPresets.json` (git-ignored). Without presets: `cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`.

**Output lands in `build/{Compiler}{Platform}/` whatever the preset**, so Debug, Release and release-editor overwrite each other's executables and libraries: after switching, delete every `build/ClangmacOS/lib/*.a`, or the other configuration's objects get linked and the tests crash.

## The editor option

`BE_WITH_EDITOR` decides whether the editor panels (`src/Widgets/{ConfigEditor,EditorLayout,SceneEditor,TextureEditor,Viewport}`) and their tests are built:

- `AUTO` (default): ON except in a single-config `Release` / `RelWithDebInfo` / `MinSizeRel`. A multi-config generator picks sources at configure time, so `AUTO` is ON there.
- `ON`: also in Release (`-DBE_WITH_EDITOR=ON`, or the `*-release-editor` presets). `OFF`: none; **a Debug build ignores `OFF`** with a configure WARNING.
- Configure prints `Editor: ENABLED|DISABLED`. The definition `BE_WITH_EDITOR=1|0` is on every target (`kEditorBuilt` in `BECore/Application/RunMode.h`); code that needs an editor header is guarded with `#if BE_WITH_EDITOR`. ImGui and `ImGuiWidget` are in every build.
- `mode="Editor"` in a build without the editor starts in Game mode with one warning. With the editor, Ctrl+Shift+E switches modes at run time (`src/Modules/BECore/CLAUDE.md`, "Run mode").

## Web-like build on the desktop

`BagiPlatformOptions.cmake`: `ENGINE_WITH_VULKAN`, `ENGINE_WITH_REMOTE_CONTROL` (ON), `ENGINE_STATIC_SDL`, `ENGINE_SINGLE_THREAD` (OFF); Emscripten forces the web values and sets `ENGINE_TARGET_WEB` / `PLATFORM_WEB`. To check the web configuration without emsdk: `cmake -S . -B build/webcheck -G "Visual Studio 17 2022" -A x64 -DENGINE_WITH_VULKAN=OFF -DENGINE_SINGLE_THREAD=ON`, then build `BagiMatch3 BagiEngineTests` with `--config Release`. It writes into `build/MSVCWindows/` like every MSVC configuration, so delete that folder before and after.
