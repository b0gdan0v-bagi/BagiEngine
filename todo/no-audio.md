---
stage: 3
---
# No audio

**Where:** no module, no dependency (`external/` has SDL, SDL_image, Vulkan headers and loader, googletest).

**What is missing** (found by searching `src`, `games` and CMake): sound effects and music.

**A fix has to cover:**

- a library choice (SDL3_mixer or miniaudio) and a new module placed in the module order of `CLAUDE.md` without a circular dependency;
- `AudioClip` as a resource through `IResourceLoader`, an `AudioSource` component and playback events (so games trigger sounds without including the library);
- volume and mute in config; tests run without an audio device (a null or dummy driver).
