#pragma once

#include <BECore/Audio/IAudioBackend.h>
#include <BECore/Types/Types.h>
#include <Events/SubscriptionHolder.h>
#include <cstdint>

struct SDL_AudioStream;

namespace BECore {

    /**
     * @brief The sound over SDL3's own audio: the default playback device, one SDL_AudioStream per voice bound to it.
     *
     * SDL mixes the bound streams and converts each clip's rate and channels to the device; a voice's gain is its stream's gain. The default
     * device follows the system's output. Closes itself on ApplicationCleanUpEvent, before the window quits SDL. A headless backend opens SDL's
     * `dummy` driver, so a control smoke makes no noise. Main thread only.
     */
    class SDLAudioBackend : public IAudioBackend, public SubscriptionHolder {
        BE_CLASS(SDLAudioBackend)
    public:
        SDLAudioBackend() = default;
        ~SDLAudioBackend() override;

        /// Before Open(): the `dummy` driver instead of the system's
        void SetHeadless(bool headless) {
            _headless = headless;
        }

        bool Open(uint32_t voiceCount) override;
        void Close() override;
        [[nodiscard]] uint32_t GetVoiceCount() const override;
        bool Start(uint32_t voice, const AudioClip& clip, float gain) override;
        void Stop(uint32_t voice) override;
        void SetGain(uint32_t voice, float gain) override;
        [[nodiscard]] bool IsPlaying(uint32_t voice) const override;

    private:
        void OnCleanUp();

        Vector<SDL_AudioStream*> _streams;
        uint32_t _device = 0;
        bool _headless = false;
        bool _subsystem = false;
        bool _subscribed = false;
    };

}  // namespace BECore
