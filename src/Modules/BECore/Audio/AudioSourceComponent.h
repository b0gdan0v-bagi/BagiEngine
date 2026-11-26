#pragma once

#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/AudioTypes.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }  // namespace InputEvents

    namespace SceneEvents {
        struct SceneUpdateEvent;
    }  // namespace SceneEvents

    /**
     * @brief A sound of a scene node: holds one clip, loaded when the component is attached, and plays it on CoreManager::GetAudio().
     *
     * Plays when asked (Play()), when an InputEvents::ActionEvent of `_playAction` comes while its scene is active, and with `_playOnStart`
     * at the first update of its scene as the active one (every loaded scene attaches its components, so not at attach). One sound at a
     * time: Play() while it plays starts it again on a new voice and Stop() stops the latest.
     */
    class AudioSourceComponent : public IComponent {
        BE_CLASS(AudioSourceComponent, ICON = volume - 2)
    public:
        AudioSourceComponent() = default;
        ~AudioSourceComponent() override = default;
        bool HasOnDraw() const override {
            return false;
        }

        BE_REFLECT_FIELD BE_FIELD_ATTRS(asset(Sound)) PoolString _clip;  ///< the path of a .wav or .ogg
        BE_REFLECT_FIELD AudioBus _bus = AudioBus::Sfx;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1)) float _volume = 1.0f;
        BE_REFLECT_FIELD bool _playOnStart = false;
        BE_REFLECT_FIELD PoolString _playAction;  ///< an InputEvents::ActionEvent of this name plays the clip; empty is none

        void OnAttached() override;
        void OnPropertyWritten(StringView field) override;

        void Play();
        void Stop();

        [[nodiscard]] bool IsPlaying() const;

        /// Loads `_clip` again: after its file changed under the same path, once the old clip was evicted from the resource cache
        void ReloadClip() {
            LoadClip();
        }

        /// The clip that plays: null before the component is attached; a failed clip when `_clip` names nothing playable
        [[nodiscard]] const AudioClip* GetLoadedClip() const {
            return _loaded.Get();
        }

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent& e);
        void OnAction(const InputEvents::ActionEvent& e);
        void LoadClip();

        ResourceHandle<AudioClip> _loaded;
        PoolString _loadedPath;  ///< the `_clip` that `_loaded` was loaded for: an edit in the inspector loads again
        AudioHandle _handle;
        bool _started = false;  ///< `_playOnStart` was served
    };

}  // namespace BECore
