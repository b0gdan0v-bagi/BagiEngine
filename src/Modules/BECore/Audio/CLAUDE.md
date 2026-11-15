# Audio (BECore)

The sound of a game: `CoreManager::GetAudio()` (`AudioManager`) plays `AudioClip`s on the voices of an `IAudioBackend`, under bus and master volumes. The split is the renderer's: the interface and the logic here, the platform in a module (`SDL/CoreSDL/SDLAudioBackend`). Short sound effects for now; streamed music, the `AudioSource` component, play events and the player's own volume are later slices of the todo entry `no-audio`.

| File | Purpose |
|------|---------|
| `AudioTypes.h` | `AudioBus` (`Sfx`, `Music`, `Ui`), `AudioFormat` (rate, channels of interleaved float samples), `AudioHandle` (voice + generation), the channel and rate limits |
| `AudioDecode.h/.cpp` | `DecodeWav` (the engine's own RIFF WAVE reader), `DecodeOgg` (vendored `stb_vorbis`, compiled in this file), `DecodeAudio` (by the magic); pure |
| `AudioClip.h/.cpp` | The resource: a whole decoded sound, immutable once made |
| `AudioClipLoader.h/.cpp` | `.wav` / `.ogg` through `ResourceManager`; reads and decodes on a pool thread |
| `IAudioBackend.h` | The platform half (`BE_CLASS` `FACTORY_BASE`, made by name like `IRenderer`): `Open(voices)`, `Close`, `Start(voice, clip, gain)`, `Stop`, `SetGain`, `IsPlaying` |
| `NullAudioBackend.h` | Plays nothing: where there is no output |
| `AudioSettings.h/.cpp` | The reflected root of `config/AudioConfig.xml`: `backend`, `voices`, `<master>`, `<sfx>`, `<music>`, `<ui>` (each `volume` 0 to 1 and `muted`) |
| `AudioManager.h/.cpp` | The service |

## Rules

- **Who makes the backend.** `AudioManager::Initialize()` (in `InitializeCoreServices`) only reads the settings. `SDL3ApplicationFabric::Create` makes the backend `backend` names (empty: `SDLAudioBackend`) after the window and the renderer and hands it to `SetBackend`, which opens it with `voices` voices. A name that is not registered is one `LOG_ERROR`; a backend that does not open (no device) is one `LOG_WARNING`; either way the game gets `NullAudioBackend` and runs silent, never stops. The gtest executable has no backend: every call is a no-op, `Play()` gives an invalid handle.
- **Voices.** A fixed set made at `Open`. `Play` takes the first voice that does not play, else the one started first; the handle of the sound it drops goes stale (its generation moves), and a stale handle is ignored by `Stop` and reads as not playing. A failed clip plays nothing.
- **Gain** = volume of the sound × its bus × the master, each 0 to 1 (a NaN is 0), and 0 when the bus or the master is muted; a muted level keeps its volume. Every `Set*` and `ApplySettings` reaches the voices that play. `ApplySettings` takes the levels only: the backend and the voice count are read once, at `SetBackend`.
- **Decoding.** A clip is decoded whole when it loads, to interleaved floats at the file's own rate and channels; the backend converts to the device. WAV: PCM of 8, 16, 24 and 32 bits, IEEE float of 32 bits, also inside `WAVE_FORMAT_EXTENSIBLE`; every size is checked against the bytes (fuzz target `AudioWav`, `tests/Fuzz/CLAUDE.md`). Ogg Vorbis: `stb_vorbis`, which trusts its input, so only for the assets of the engine and the games (`src/Modules/stb/README.md`). 1 to 8 channels, 1 kHz to 384 kHz.
- **Main thread only**, except the loader's decode. A frame that plays nothing allocates nothing (the voices are made at `Open`); a play copies the samples into the backend.

## The SDL backend

`SDLAudioBackend` (`SDL/src/CoreSDL/`): `SDL_InitSubSystem(SDL_INIT_AUDIO)`, the default playback device (it follows the system's output), one `SDL_AudioStream` per voice bound to it; SDL mixes the bound streams. `Start` clears the stream, sets the clip's format as its input, the gain, puts the samples and flushes; `IsPlaying` is "queued or converted samples left". `SetHeadless(true)` (the fabric passes `--headless`) opens SDL's `dummy` driver, so a control smoke makes no noise. It closes itself on `ApplicationCleanUpEvent`, which runs before the window's handler (handlers run in the reverse order of the subscription) and so before `SDL_Quit`; `AudioManager` keeps the closed backend until `OnApplicationDeinit`. On the web SDL plays through Web Audio, which a browser starts only after the first click or key of the page (not checked here: no emsdk run).

## Tests

`tests/BECore/AudioDecodeTest.cpp` (WAV files built in memory, the Ogg fixture `tests/BECore/AudioData/stereo-tone.ogg`: a stereo 440 Hz tone, made for the test, right = -left), `AudioManagerTest.cpp` (a fake backend), `AudioClipLoaderTest.cpp` (the fixture folder mounted), `SDLAudioBackendTest.cpp` (SDL's `dummy` driver; skips where SDL has none; a test never waits for a sound to end: the dummy device plays in real time).
