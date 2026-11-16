# dr_libs (vendored)

`dr_flac.h` v0.13.4 (commit 806ee9a of https://github.com/mackron/dr_libs; public domain, or MIT No Attribution, the user's choice; the text is at the end of the file). Not edited.

`dr_flac.h` is compiled in one translation unit, `BECore/Audio/AudioDecode.cpp` (`DR_FLAC_NO_STDIO`: the engine reads the bytes; `DR_FLAC_NO_OGG`: an `OggS` file is Vorbis; `DRFLAC_API` and `DRFLAC_PRIVATE` are `static`, so a game that links another copy does not clash). It reads the FLAC files ComfyUI writes for the Generate panel as well as the assets; `DecodeFlac` is fuzzed (target `AudioFlac`, `tests/Fuzz/CLAUDE.md`).
