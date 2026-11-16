#include "AudioPreferences.h"

#include <BECore/Audio/AudioManager.h>
#include <Generated/AudioPreferences.gen.hpp>

namespace BECore {

    AudioPreferences CaptureAudioPreferences(const AudioManager& manager) {
        const AudioSettings& settings = manager.GetSettings();
        AudioPreferences preferences;
        preferences._master = settings._master;
        preferences._sfx = settings._sfx;
        preferences._music = settings._music;
        preferences._ui = settings._ui;
        return preferences;
    }

    std::expected<void, SaveError> SaveAudioPreferences(SaveGameService& service, const AudioManager& manager) {
        return service.Save(kAudioPreferencesSlot, kAudioPreferencesSchema, CaptureAudioPreferences(manager));
    }

    std::expected<SaveInfo, SaveError> LoadAudioPreferences(const SaveGameService& service, AudioManager& manager) {
        AudioPreferences loaded;
        auto info = service.Load(kAudioPreferencesSlot, kAudioPreferencesSchema, loaded);
        if (!info) {
            return info;
        }
        AudioSettings settings = manager.GetSettings();
        settings._master = loaded._master;
        settings._sfx = loaded._sfx;
        settings._music = loaded._music;
        settings._ui = loaded._ui;
        manager.ApplySettings(settings);
        return info;
    }

}  // namespace BECore
