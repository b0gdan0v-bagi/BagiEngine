# stb (vendored)

`stb_truetype.h` v1.26 and `stb_vorbis.c` v1.24 (commit 9600522 of the master branch) from https://github.com/nothings/stb (public domain, or MIT for jurisdictions without a public domain; the text is at the end of each file). Not edited.

`stb_vorbis.c` is compiled in one translation unit, `BECore/Audio/AudioDecode.cpp` (`STB_VORBIS_NO_STDIO`, `STB_VORBIS_NO_PUSHDATA_API`). It decodes Ogg Vorbis clips of the engine's and the games' `assets/` only: it is not fuzzed here and is not for files from players or the network.

The header says it must not be used on untrusted font files (it does no range checking of offsets in the file). The engine loads fonts only from its own and the game's `assets/`.

`stb_image.h` v2.32 comes from the same commit 9600522 and is not edited (the copy inside SDL_image has most of its API cut out, so it cannot be used here). It is compiled in one translation unit, `CoreSDL/ImageFile.cpp`, for PNG only and with every function static. It reads the textures of the engine's and the game's `assets/`: it is not fuzzed here and is not for files from players or the network.

The three libraries allocate through `AllocRouting` (`BECore/Profiler/Alloc/`) with the source `Stb`, so the allocation tracker of the profile build counts them: `STBTT_malloc` / `STBTT_free` in `Font.cpp`, `STBI_MALLOC` / `STBI_REALLOC` / `STBI_FREE` in `ImageFile.cpp`, and in `AudioDecode.cpp` function-like `malloc` / `realloc` / `free` macros that exist only around `#include <stb_vorbis.c>` (it has no allocator macro; the system headers it needs are included before them, so the macros never reach a header). A shipped build routes to the plain libc functions at no cost. The decoded buffer of `stb_vorbis_decode_memory` is freed with `AllocRouting::Free`, never `std::free`. A copy of these libraries in a game keeps its own malloc and stays invisible to the tracker.
