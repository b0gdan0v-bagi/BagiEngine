#pragma once

#include <BECore/Audio/AudioSettings.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Save/SaveGame.h>
#include <BECore/Types/Types.h>
#include <expected>

namespace BECore {

    class AudioManager;

    /// The player's own levels, kept between runs in a save slot of the game's folder (the config holds the defaults)
    struct AudioPreferences {
        BE_CLASS(AudioPreferences)
        BE_REFLECT_FIELD AudioLevel _master;
        BE_REFLECT_FIELD AudioLevel _sfx;
        BE_REFLECT_FIELD AudioLevel _music;
        BE_REFLECT_FIELD AudioLevel _ui;
    };

    /// The slot of the levels in a game's save folder, beside the game's own slots
    inline constexpr StringView kAudioPreferencesSlot = "audio";

    /// The engine's schema of the slot: the same in every game
    inline constexpr SaveSchema kAudioPreferencesSchema{"BagiAudio", 1, 1};

    /// The levels of @p manager in force now
    [[nodiscard]] AudioPreferences CaptureAudioPreferences(const AudioManager& manager);

    /// Writes the levels of @p manager to kAudioPreferencesSlot of @p service
    [[nodiscard]] std::expected<void, SaveError> SaveAudioPreferences(SaveGameService& service, const AudioManager& manager);

    /**
     * @brief Reads kAudioPreferencesSlot of @p service and applies it to @p manager (clamped like the config)
     *
     * Read into a temporary and applied only after a success, so a refused or missing file (NotFound: the first run) leaves the manager as it
     * was. The game decides the folder and when to read and write, under its own rules for control and test runs.
     */
    [[nodiscard]] std::expected<SaveInfo, SaveError> LoadAudioPreferences(const SaveGameService& service, AudioManager& manager);

}  // namespace BECore
