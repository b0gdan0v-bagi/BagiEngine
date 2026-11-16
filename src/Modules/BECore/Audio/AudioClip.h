#pragma once

#include <BECore/Audio/AudioDecode.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Resource/IResource.h>

namespace BECore {

    /**
     * @brief A whole sound decoded into memory (AudioClipLoader: .wav, .ogg), what Audio::Play() takes.
     *
     * Immutable once made, so the pool thread that loads it and the main thread that plays it never race. A clip that failed to load is
     * Failed with no samples; playing it does nothing.
     */
    class AudioClip : public IResource {
        BE_CLASS(AudioClip)
    public:
        AudioClip() = default;
        ~AudioClip() override = default;

        [[nodiscard]] static IntrusivePtr<AudioClip> Create(PoolString path, DecodedAudio audio);
        [[nodiscard]] static IntrusivePtr<AudioClip> CreateFailed(PoolString path);

        ResourceState GetState() const override;
        PoolString GetPath() const override;
        uint64_t GetMemoryUsage() const override;
        PoolString GetTypeName() const override;

        [[nodiscard]] const AudioFormat& GetFormat() const {
            return _audio.format;
        }

        /// Interleaved samples in GetFormat(); empty for a failed clip
        [[nodiscard]] Span<const float> GetSamples() const {
            return _audio.samples;
        }

        [[nodiscard]] size_t GetFrameCount() const {
            return _audio.GetFrameCount();
        }

        [[nodiscard]] double GetDurationSeconds() const;

    private:
        PoolString _path;
        ResourceState _state = ResourceState::Unloaded;
        DecodedAudio _audio;
    };

}  // namespace BECore
