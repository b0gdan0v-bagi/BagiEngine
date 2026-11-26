#include "AudioSourceComponent.h"

#include <BECore/Audio/AudioManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/AudioSourceComponent.gen.hpp>

namespace BECore {

    void AudioSourceComponent::OnAttached() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &AudioSourceComponent::OnUpdate>(this);
        SubscribeInScene<InputEvents::ActionEvent, &AudioSourceComponent::OnAction>(this);
        LoadClip();
    }

    void AudioSourceComponent::OnPropertyWritten(StringView field) {
        if (field == "clip") {
            ReloadClip();
        }
    }

    void AudioSourceComponent::OnEnabledChanged() {
        if (!IsEnabled()) {
            Stop();
        }
    }

    void AudioSourceComponent::LoadClip() {
        _loadedPath = _clip;
        _loaded.Reset();
        if (_clip.Empty()) {
            return;
        }
        // Cached by path; a file that does not decode is a failed clip there (its loader logs why), which plays nothing
        _loaded = CoreManager::GetResourceManager().Load<AudioClip>(_clip.ToStringView());
    }

    void AudioSourceComponent::Play() {
        if (_clip != _loadedPath) {
            LoadClip();
        }
        if (!_loaded) {
            // A path no loader takes: still a request, which AudioManager records (and looks up again)
            _handle = CoreManager::GetAudio().Play(_clip, _bus, _volume);
            return;
        }
        _handle = CoreManager::GetAudio().Play(*_loaded, _bus, _volume);
    }

    void AudioSourceComponent::Stop() {
        CoreManager::GetAudio().Stop(_handle);
        _handle = AudioHandle{};
    }

    bool AudioSourceComponent::IsPlaying() const {
        return CoreManager::GetAudio().IsPlaying(_handle);
    }

    void AudioSourceComponent::OnUpdate(const SceneEvents::SceneUpdateEvent& /*e*/) {
        if (_playOnStart && !_started) {
            _started = true;
            Play();
        }
    }

    void AudioSourceComponent::OnAction(const InputEvents::ActionEvent& e) {
        if (!_playAction.Empty() && e.action == _playAction) {
            Play();
        }
    }

}  // namespace BECore
