# stb (vendored)

`stb_truetype.h` v1.26 and `stb_vorbis.c` v1.24 (commit 9600522 of the master branch) from https://github.com/nothings/stb (public domain, or MIT for jurisdictions without a public domain; the text is at the end of each file). Not edited.

`stb_vorbis.c` is compiled in one translation unit, `BECore/Audio/AudioDecode.cpp` (`STB_VORBIS_NO_STDIO`, `STB_VORBIS_NO_PUSHDATA_API`). It decodes Ogg Vorbis clips of the engine's and the games' `assets/` only: it is not fuzzed here and is not for files from players or the network.

The header says it must not be used on untrusted font files (it does no range checking of offsets in the file). The engine loads fonts only from its own and the game's `assets/`.
