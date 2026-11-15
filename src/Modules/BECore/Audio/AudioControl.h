#pragma once

namespace BECore {

    class AudioManager;
    class ControlService;

    /**
     * @brief Registers the audio methods of the control API on @p manager, so a client sees what a game played and sets the levels.
     *
     * - `audio_state` answers `{"backend": name or null, "voices", "playing", "started", "requested", "levels": {"master", "sfx", "music",
     *   "ui": {"volume", "muted"}}, "recent": [{"index", "clip", "bus", "volume", "played"}]}`; `recent` is the last kAudioRecentPlays
     *   requests, oldest first, also those that played nothing (no backend, a clip that did not load).
     * - `audio_set {"bus": "master" / "sfx" / "music" / "ui", "volume"?: 0 to 1, "muted"?: bool}` changes that level and answers the
     *   `levels` object; an unknown bus, a volume that is not a number, a mute that is not a bool, or neither of the two is `invalid_params`
     *   and changes nothing.
     *
     * @p manager must outlive the registration (CoreManager registers its own). Registering twice replaces the handlers.
     */
    void RegisterAudioControl(ControlService& service, AudioManager& manager);

}  // namespace BECore
