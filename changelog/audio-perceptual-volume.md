# Audio: master and bus volumes are linear gains, so a volume slider barely changes the loudness

- Version: 0.1.14
- Entry: audio-perceptual-volume
- Date: 2026-11-16
- Bump: patch

## Added
- `AudioManager::LevelToGain(level)`: the gain of a master or bus level (the position clamped to 0..1, NaN as 0, then cubed: 50 % is -18 dB, 20 % is -42 dB).

## Changed
- The master and bus volumes (`AudioLevel::_volume`, `SetMasterVolume`, `SetBusVolume`, `AudioConfig.xml`, the saved `audio` slot, `audio_set` / `audio_state`) are slider positions now, and the gain is their cube; a sound's own volume, a track's volume and fades stay linear. The same stored number below 1 sounds quieter than before. Adapt: nothing to change in code; a game that tuned a bus level by ear (a config default below 1) listens again and raises it (the old gain g is now the position g^(1/3)).
