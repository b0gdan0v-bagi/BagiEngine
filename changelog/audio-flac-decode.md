# Audio: FLAC is not decoded and no code writes a WAV file (generated sounds come from ComfyUI as FLAC)

- Version: 0.1.24
- Entry: audio-flac-decode
- Date: 2026-11-16
- Bump: patch

## Added
- `DecodeFlac` (vendored dr_flac 0.13.4, `src/Modules/dr_libs`): FLAC clips by `.flac` and by the magic `fLaC` in `DecodeAudio`, and FLAC music streamed by `PlayMusic` / `OpenAudioStream`; a whole decode is bounded by `kMaxFlacClipFrames`.
- `EncodeWav(samples, format, WavSampleFormat::Pcm16 | Float32)`: writes a RIFF WAVE file, the inverse of `DecodeWav`.
- Fuzz target `AudioFlac` (corpus `audio_flac`).
