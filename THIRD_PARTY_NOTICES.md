# Third-party notices

BagiEngine itself is under the Beerware licence (`LICENSE`). Everything below was written by others and keeps its own licence. A game
built on the engine contains the rows marked **ships** (always) or **ships if** (under the condition named); MIT, BSD, zlib, Apache 2.0
and OFL ask that their notice goes with the binary, so a released game carries the licence files named in the column `Licence file`.
The rows marked **build only** are tools or test code that never reach a player.

`CI/scripts/check_third_party_notices.py` (ctest `BagiThirdPartyNotices`) holds this file to the tree: every folder of `external/`,
every module of `src/Modules/` without a `CLAUDE.md` of its own (the engine's own modules have one), every font of `assets/fonts/` and
every source file that calls itself vendored in its first lines must have a row, each row's path must exist, and so must its licence
file. A new library adds its row here in the same commit.

## Libraries

| Path | Component | Version | Licence | Licence file | In a game |
|------|-----------|---------|---------|--------------|-----------|
| `src/Modules/EABase/EABase` | EABase (Electronic Arts) | 2.09.12 | BSD 3-Clause | `src/Modules/EABase/EABase/LICENSE` | ships |
| `src/Modules/EABase/EASTL` | EASTL (Electronic Arts) | 3.27.00 | BSD 3-Clause | `src/Modules/EABase/EASTL/LICENSE` | ships |
| `src/Modules/EnTT` | EnTT (Michele Caini) | 3.13.2 | MIT | `src/Modules/EnTT/LICENSE` | ships |
| `src/Modules/fmt` | {fmt} (Victor Zverovich) | 11.0.2 | MIT with the {fmt} binary exception (no notice needed for object code) | `src/Modules/fmt/LICENSE` | ships |
| `src/Modules/imgui` | Dear ImGui (Omar Cornut) | 1.92.6 WIP | MIT | `src/Modules/imgui/LICENSE.txt` | ships (the editor; a game links it too) |
| `src/Modules/nlohmann` | JSON for Modern C++ (Niels Lohmann) | 3.11.3 | MIT | `src/Modules/nlohmann/LICENSE.MIT` | ships |
| `src/Modules/pugixml` | pugixml (Arseny Kapoulkine) | 1.14 | MIT | `src/Modules/pugixml/LICENSE.md` | ships |
| `src/Modules/RmlUi` | RmlUi (core only) | 6.3 | MIT | `src/Modules/RmlUi/LICENSE.txt` | ships if `ENGINE_UI_RMLUI` |
| `src/Modules/stb` | stb_truetype (Sean Barrett) | 1.26 | public domain or MIT, the user's choice | `src/Modules/stb/stb_truetype.h` (end of file) | ships |
| `src/Modules/tracy` | Tracy Profiler client (Bartosz Taudul) | 0.15.0 | BSD 3-Clause; its `libbacktrace` BSD 3-Clause | `src/Modules/tracy/LICENSE` | ships if `ENGINE_PROFILER_TRACY` (profile builds only) |
| `src/Widgets/Icons/IconsLucide.h` | IconFontCppHeaders (Juliette Foucaut, Doug Binks) | commit 780f044 | zlib | `src/Widgets/Icons/IconsLucide.h` (its header) | ships with the editor |
| `external/SDL` | SDL 3 (Sam Lantinga and contributors) | release-3.2.0 | zlib (its `src/hidapi` is used under its BSD option) | `external/SDL/LICENSE.txt` | ships |
| `external/SDL_image` | SDL_image 3 (with its own stb_image) | release-3.2.0 | zlib | `external/SDL_image/LICENSE.txt` | ships |
| `external/SDL_image/external/libwebp` | libwebp (Google), vendored by SDL_image | 1.3.2 | BSD 3-Clause | `external/SDL_image/external/libwebp/COPYING` | ships on desktop (`SDLIMAGE_WEBP`, off on the web) |
| `external/SDL_image/external/libtiff` | libtiff, vendored by SDL_image | 4.6.0 | libtiff licence (BSD-like) | `external/SDL_image/external/libtiff/LICENSE.md` | ships on desktop where no system backend reads TIFF (Windows, Linux) |
| `external/Vulkan-Headers` | Vulkan headers (Khronos) | 1.4.344 | Apache 2.0 or MIT | `external/Vulkan-Headers/LICENSE.md` | compiled in (headers only) |
| `external/Vulkan-Loader` | Vulkan loader binaries (Khronos / LunarG SDK) | 1.4 (macOS `libvulkan.1.dylib`) | Apache 2.0 | none in the folder: see "Open points" | ships next to the executable with the Vulkan renderer |
| `external/googletest` | GoogleTest | 1.17.0 | BSD 3-Clause | `external/googletest/LICENSE` | build only (tests) |
| `external/emsdk` | Emscripten SDK | 4.0.23 | MIT (Emscripten itself: MIT or University of Illinois NCSA) | `external/emsdk/LICENSE` | build only (the toolchain; its runtime in a web build is under the same licence) |

## Fonts

| Path | Font | Version | Licence | Licence file | In a game |
|------|------|---------|---------|--------------|-----------|
| `assets/fonts/Inter-Regular.ttf` | Inter (The Inter Project Authors) | 4.001 | SIL OFL 1.1 | `assets/fonts/Inter-OFL.txt` | ships |
| `assets/fonts/JetBrainsMono-Regular.ttf` | JetBrains Mono | 2.304 | SIL OFL 1.1 | `assets/fonts/JetBrainsMono-OFL.txt` | ships |
| `assets/fonts/PT_Sans-Web-Regular.ttf` | PT Sans (ParaType), Reserved Font Name "PT Sans" | 2.003W | SIL OFL 1.1 | `assets/fonts/PT_Sans-OFL.txt` | ships |
| `assets/fonts/PT_Sans-Web-Bold.ttf` | PT Sans Bold (ParaType) | 2.003W | SIL OFL 1.1 | `assets/fonts/PT_Sans-OFL.txt` | ships |
| `assets/fonts/RussoOne-Regular.ttf` | Russo One (Jovanny Lemonad), Reserved Font Name "Russo" | 1.001 | SIL OFL 1.1 | `assets/fonts/RussoOne-OFL.txt` | ships |
| `assets/fonts/lucide.ttf` | Lucide icons (lucide-font) | 1.17.0 | ISC (parts from Feather: MIT) | `assets/fonts/Lucide-LICENSE.txt` | ships with the editor |

The OFL lets a font be bundled and shipped with any software, not sold on its own; a font changed under a Reserved Font Name must be
renamed.

## Open points for the owner

- **The Vulkan loader binaries** carry no licence file in `external/Vulkan-Loader` and no exact version (the macOS dylib says 1.4.0; the
  Windows `vulkan-1.lib` none). The loader is Apache 2.0 (KhronosGroup/Vulkan-Loader): its `LICENSE.txt` should be added next to the
  binaries, with the SDK version they came from.
- **SDL's `src/hidapi`** has three licence files (`LICENSE-bsd.txt`, `LICENSE-gpl3.txt`, `LICENSE-orig.txt`): it is used under BSD,
  as SDL does; nothing of the GPL option applies.
- **{fmt}'s exception** lets a binary go without its notice; it is carried anyway with the others.
