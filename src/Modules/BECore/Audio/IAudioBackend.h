#pragma once

#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <cstdint>

namespace BECore {

    class AudioClip;

    /**
     * @brief The platform half of the sound, as IRenderer is of the drawing: a fixed set of voices, each playing one clip at a gain.
     *
     * A backend is dumb: AudioManager picks the voice, computes the gain from the buses and keeps the handles. Made by name through
     * AbstractFactory<IAudioBackend> (SDLAudioBackend, NullAudioBackend). Main thread only.
     */
    class IAudioBackend : public RefCounted {
        BE_CLASS(IAudioBackend, FACTORY_BASE)
    public:
        ~IAudioBackend() override = default;

        /// Opens the output with @p voiceCount voices; false when there is no output, and then nothing is open
        virtual bool Open(uint32_t voiceCount) = 0;

        /// Stops every voice and closes the output; Open() may follow
        virtual void Close() = 0;

        /// The voices Open() made; 0 when closed
        [[nodiscard]] virtual uint32_t GetVoiceCount() const = 0;

        /// Plays @p clip on @p voice from its start, dropping what the voice played; the samples are copied, so the clip may go after the call
        virtual bool Start(uint32_t voice, const AudioClip& clip, float gain) = 0;

        virtual void Stop(uint32_t voice) = 0;

        virtual void SetGain(uint32_t voice, float gain) = 0;

        /// Whether @p voice still has samples to play
        [[nodiscard]] virtual bool IsPlaying(uint32_t voice) const = 0;
    };

}  // namespace BECore
