# Editor: the Generate panel listens to sounds and accepts one as a clip

- Version: 0.1.27
- Entry: editor-generate-sound
- Date: 2026-11-16
- Bump: patch

## Added
- The Generate panel lists sound variants with Play / Stop, a waveform, the seed, the length and the peak; under the picked one the processing settings (mono, trim of the silence, a length limit, a sample rate, fades, the peak level) and Listen as accepted.
- Accept of a sound writes a 16-bit WAV to `assets/artgen/<name>.wav` or over a game clip (`assets/.../x.wav`), with `.artgen.json` beside it (the provenance plus `accepted`), and reloads the scene's audio sources that play it; Use on selected sets the clip of the node's `AudioSourceComponent`.
- `ArtGenSound.{h,cpp}` in Widgets: `ArtGenSoundSettings`, `ProcessArtGenSound`, `MeasureArtGenSound`, `BuildArtGenWaveform`.
- `artgen_accept` of a sound variant takes `name` or `clip` and `sound` (the settings) and answers `{clip, wav, sampleRate, channels, seconds, peakDb, reloadedSources, usedOn}`.
- `AudioSourceComponent::ReloadClip()` loads the clip again after its file changed under the same path.
