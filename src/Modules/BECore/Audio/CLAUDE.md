# Audio (BECore)

The sound of a game: `CoreManager::GetAudio()` (`AudioManager`) plays `AudioClip`s on the voices of an `IAudioBackend`, under bus and master volumes. The split is the renderer's: the interface and the logic here, the platform in a module (`SDL/CoreSDL/SDLAudioBackend`). Sound effects are whole clips in memory; music is decoded while it plays, on two voices of its own.

| File | Purpose |
|------|---------|
| `AudioTypes.h` | `AudioBus` (`Sfx`, `Music`, `Ui`), `AudioFormat` (rate, channels of interleaved float samples), `AudioHandle` (voice + generation), the channel and rate limits |
| `AudioDecode.h/.cpp` | `DecodeWav` (the engine's own RIFF WAVE reader), `DecodeOgg` (vendored `stb_vorbis`, compiled in this file), `DecodeAudio` (by the magic), `OpenAudioStream` (an `IAudioSampleSource` for music); pure |
| `AudioClip.h/.cpp` | The resource: a whole decoded sound, immutable once made |
| `AudioClipLoader.h/.cpp` | `.wav` / `.ogg` through `ResourceManager`; reads and decodes on a pool thread. `ReadAudioFile`: the bytes of a file, also for the music |
| `IAudioBackend.h` | The platform half (`BE_CLASS` `FACTORY_BASE`, made by name like `IRenderer`): `Open(voices)`, `Close`, `Start(voice, clip, gain)`, `Stop`, `SetGain`, `IsPlaying`; a voice as a stream: `StartStream(voice, format, gain)`, `QueueStream`, `EndStream`, `GetQueuedFrames` |
| `NullAudioBackend.h` | Plays nothing: where there is no output; it starts no stream, so no music is decoded for nobody |
| `AudioSettings.h/.cpp` | The reflected root of `config/AudioConfig.xml`: `backend`, `voices`, `<master>`, `<sfx>`, `<music>`, `<ui>` (each `volume` 0 to 1 and `muted`) |
| `AudioManager.h/.cpp` | The service |
| `AudioEvents.h` | `PlaySoundEvent{clip, bus, volume}`, `StopAllSoundsEvent`, `PlayMusicEvent{clip, volume, loop, fadeSeconds}`, `StopMusicEvent{fadeSeconds}`: asked for by code that does not hold the manager |
| `AudioSourceComponent.h/.cpp` | The sound of a scene node: `clip`, `bus`, `volume`, `playOnStart`, `playAction` |
| `AudioPreferences.h/.cpp` | The player's levels in a save slot: `SaveAudioPreferences`, `LoadAudioPreferences` |
| `AudioControl.h/.cpp` | The control API: `audio_state`, `audio_set` |

## Rules

- **Who makes the backend.** `AudioManager::Initialize()` (in `InitializeCoreServices`) only reads the settings. `SDL3ApplicationFabric::Create` makes the backend `backend` names (empty: `SDLAudioBackend`) after the window and the renderer and hands it to `SetBackend`, which opens it with the config's `voices` plus `kMusicVoices`. A name that is not registered is one `LOG_ERROR`; a backend that does not open (no device) is a `LOG_WARNING` of the backend with the reason and one of `AudioManager`; either way the game gets `NullAudioBackend` and runs silent, never stops. The gtest executable has no backend: every call is a no-op, `Play()` gives an invalid handle.
- **Voices.** A fixed set made at `Open`: the config's `voices` for the effects, then `kMusicVoices` (2) for the music, which the effects never take. `Play` takes the first voice that does not play, else the one started first; the handle of the sound it drops goes stale (its generation moves), and a stale handle is ignored by `Stop` and reads as not playing. A failed clip plays nothing.
- **Gain** = volume of the sound × its bus × the master, each 0 to 1 (a NaN is 0), and 0 when the bus or the master is muted; a muted level keeps its volume. Every `Set*` and `ApplySettings` reaches the voices that play. `ApplySettings` takes the levels only: the backend and the voice count are read once, at `SetBackend`.
- **Decoding.** A clip is decoded whole when it loads, to interleaved floats at the file's own rate and channels; the backend converts to the device. WAV: PCM of 8, 16, 24 and 32 bits, IEEE float of 32 bits, also inside `WAVE_FORMAT_EXTENSIBLE`; every size is checked against the bytes (fuzz target `AudioWav`, `tests/Fuzz/CLAUDE.md`). Ogg Vorbis: `stb_vorbis`, which trusts its input, so only for the assets of the engine and the games (`src/Modules/stb/README.md`). 1 to 8 channels, 1 kHz to 384 kHz.
- **By path and by event.** `Play(path, bus, volume)` loads the clip through `ResourceManager` (synchronous the first time; the cache keeps it, a file that does not decode as a failed clip), so a scene preloads its clips: an `AudioSourceComponent` loads at attach, other code with `LoadAsync`. `AudioEvents::PlaySoundEvent` is the same call for code that should not include the manager (the manager subscribes in `Initialize`).
- **The record of requests.** Every `Play`, also one that plays nothing (no backend, a failed clip), is a numbered `AudioPlayRecord` in a ring of the last `kAudioRecentPlays`; `audio_state` shows it, so a game's smoke test checks its sounds on a machine without an output.
- **Main thread only**, except the loader's decode. A frame that plays nothing allocates nothing (the voices are made at `Open`); a play copies the samples into the backend.

## Music

`PlayMusic(path, volume, loop, fadeSeconds)` plays a `.ogg` or a `.wav` on the `Music` bus. The file is read whole on the main thread (an Ogg file is small: it stays compressed) and an `IAudioSampleSource` gives its samples: stb_vorbis's pull API decodes the Ogg a chunk at a time, a WAVE file is decoded whole at once. `Update(deltaSeconds)`, once a frame from `CoreManager` (every kind of frame, and the idle cycle of the manual clock with 0), decodes `kMusicChunkFrames` at a time until `kMusicBufferSeconds` are queued, at most `kMaxMusicChunksPerUpdate` chunks, into a scratch buffer made at `SetBackend`: a steady frame allocates nothing. At the end a looping track rewinds (one that gave nothing since its start ends instead), another ends its stream and stops once it played out.

- **Two tracks.** A new track starts on the free music voice and fades in over `fadeSeconds` while the old one fades out from its level and stops at silence; a third track stops the oldest at once. The same path as the track that plays changes only its volume and loop (a scene that asks for its music again does not restart it), unless that track plays out its end without a loop: then it starts again. A fade time that is not a finite positive number is none.
- **Fades move by the frame time**, so a paused game and the manual clock hold them while the stream is still fed. Gain = fade x volume x the music bus x the master; every level change reaches the tracks.
- `StopMusic(fadeSeconds)` fades every track out; `StopAll()` and `Shutdown()` stop it at once. `GetMusicState()` is the last track asked for: `clip`, `playing`, `loop`, `volume`, `fading`.
- Not checked: a real device by ear, the web build (the browser's main loop feeds the stream like any frame).

## The source component

`AudioSourceComponent` holds one clip and plays it: `Play()`, an `InputEvents::ActionEvent` of `playAction` while its scene is active, and `playOnStart` at the first `SceneUpdateEvent` of its scene as the active one (not at attach: every loaded scene attaches). A change of `clip` (the inspector) loads at the next `Play()`; `Stop()` and `IsPlaying()` are about the latest sound it started.

## The player's levels

The config holds the defaults; the player's choice is the slot `audio` (`kAudioPreferencesSlot`, schema `{"BagiAudio", 1, 1}`: the engine's in every game) of the game's own `SaveGameService` folder. The game decides when and whether, under its own gate (as a game keeps control and test runs away from a person's saves): `LoadAudioPreferences` at the start (into a temporary, applied after a success, so `NotFound` or a refused file changes nothing), `SaveAudioPreferences` when `GetLevelsRevision()` moved. Only the four levels are saved: the backend and the voice count stay the config's.

## Control API

`audio_state`: `backend` (the reflected type name, or null), `voices` (of the effects), `playing`, `started`, `requested`, `levels` (`master`, `sfx`, `music`, `ui`: `volume`, `muted`), `music` (`clip` or null, `playing`, `loop`, `volume`, `fading`) and `recent` (`index`, `clip`, `bus`, `volume`, `played`, oldest first; music requests too). `audio_set {bus, volume?, muted?}` sets one level and answers `levels`; anything else is `invalid_params` and changes nothing. The bus names on the wire are lower case.

## The SDL backend

`SDLAudioBackend` (`SDL/src/CoreSDL/`): `SDL_InitSubSystem(SDL_INIT_AUDIO)`, the default playback device (it follows the system's output), one `SDL_AudioStream` per voice bound to it; SDL mixes the bound streams. `Start` clears the stream, sets the clip's format as its input, the gain, puts the samples and flushes; `IsPlaying` is "queued or converted samples left". A stream voice takes the track's own format as its input and is fed by `QueueStream`; `GetQueuedFrames` is SDL's queued input over the frame size, and `EndStream` flushes the resampler's tail. `SetHeadless(true)` (the fabric passes `--headless`) opens SDL's `dummy` driver, so a control smoke makes no noise. It closes itself on `ApplicationCleanUpEvent`, which runs before the window's handler (handlers run in the reverse order of the subscription) and so before `SDL_Quit`; `AudioManager` keeps the closed backend until `OnApplicationDeinit`. On the web SDL plays through Web Audio, which a browser starts only after the first click or key of the page (not checked here: no emsdk run).

## Tests

`tests/BECore/AudioDecodeTest.cpp` (WAV files built in memory, the Ogg fixture `tests/BECore/AudioData/stereo-tone.ogg`: a stereo 440 Hz tone, made for the test, right = -left), `AudioManagerTest.cpp` (`FakeAudioBackend.h`, a fake backend for every audio test), `AudioPlaybackTest.cpp` (by path and by event, on the core's manager), `AudioSourceComponentTest.cpp` (scenes from XML), `AudioPreferencesTest.cpp` (a real `SaveGameService` in a temp folder), `AudioMusicTest.cpp` (the fake's streams drained by the test; the fixtures `stereo-tone.ogg` and `mono-blip.wav`, an 880 Hz blip of 0.2 s, mono 11025 Hz, made for the test), `AudioControlTest.cpp`, `AudioClipLoaderTest.cpp` (the fixture folder mounted), `SDLAudioBackendTest.cpp` (SDL's `dummy` driver; skips where SDL has none; a test never waits for a sound to end: the dummy device plays in real time).
