# RmlUi (vendored)

RmlUi 6.3 from https://github.com/mikke89/RmlUi (tag `6.3`, MIT; the licence is `LICENSE.txt`). Not edited.

Only the core is here: `Include/RmlUi/Core`, `Include/RmlUi/Config`, `Include/RmlUi/Core.h` and `Source/Core` (with `Elements`, `Layout` and `FontEngineDefault`). The upstream `CMakeLists.txt` files are replaced by the one next to this file. Left out:

- `Source/Debugger` and `Include/RmlUi/Debugger*`: the debugger's fonts are under the SIL Open Font License.
- The Lua, SVG and Lottie plugins, `Backends/`, `Samples/`, `Tests/` and `Dependencies/`.

The third-party headers inside `Include/RmlUi/Core/Containers/` (`robin_hood.h`, `itlib/`) are MIT and carry their own notices.

It is built only with `-DENGINE_UI_RMLUI=ON` (off by default), and only the host module `src/Modules/RmlUiHost` links it (`CLAUDE.md` there): it is the third candidate of stage 5 of `.claude/plans/magical-leaping-blossom.md`, see `.claude/plans/ui-authoring-decision.md`.

Build notes:

- The engine builds without exceptions. `robin_hood.h` detects that; `itlib::flat_map` needs `ITLIB_FLAT_MAP_NO_THROW`, which the target defines.
- `RMLUI_FONT_ENGINE` is `freetype` (default, needs FreeType: `brew install freetype` on macOS) or `none` (the host registers its own `FontEngineInterface`).
- `Include/RmlUi/Config/Config.h` lets the host swap RmlUi's container types (it even has a switch for EASTL). Not done here: the defaults (std containers and the bundled robin_hood) are used.
- To update: take the new tag's `Include/RmlUi/{Core,Config,Core.h}` and `Source/Core`, drop the upstream `CMakeLists.txt` files, and re-check the build with `ENGINE_NO_EXCEPTIONS=ON`.
