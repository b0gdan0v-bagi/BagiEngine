#include "AudioClip.h"

#include <Generated/AudioClip.gen.hpp>

namespace BECore {

    IntrusivePtr<AudioClip> AudioClip::Create(PoolString path, DecodedAudio audio) {
        auto clip = New<AudioClip>();
        clip->_path = path;
        clip->_audio = std::move(audio);
        clip->_state = clip->_audio.samples.empty() || clip->_audio.format.channels == 0 ? ResourceState::Failed : ResourceState::Loaded;
        return clip;
    }

    IntrusivePtr<AudioClip> AudioClip::CreateFailed(PoolString path) {
        auto clip = New<AudioClip>();
        clip->_path = path;
        clip->_state = ResourceState::Failed;
        return clip;
    }

    ResourceState AudioClip::GetState() const {
        return _state;
    }

    PoolString AudioClip::GetPath() const {
        return _path;
    }

    uint64_t AudioClip::GetMemoryUsage() const {
        return sizeof(AudioClip) + _audio.samples.capacity() * sizeof(float);
    }

    PoolString AudioClip::GetTypeName() const {
        return "AudioClip"_intern;
    }

    double AudioClip::GetDurationSeconds() const {
        return _audio.format.sampleRate == 0 ? 0.0 : static_cast<double>(GetFrameCount()) / _audio.format.sampleRate;
    }

}  // namespace BECore
