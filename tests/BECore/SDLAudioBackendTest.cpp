#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/AudioManager.h>
#include <Common/Printers.h>
#include <CoreSDL/SDLAudioBackend.h>
#include <SDL3/SDL.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr uint32_t kRate = 48000;
        constexpr uint32_t kVoices = 4;

        /// Long enough that the dummy device, which takes samples in real time, cannot have played it before the test looks
        IntrusivePtr<AudioClip> TenSecondsOfSilence() {
            DecodedAudio audio;
            audio.format = AudioFormat{kRate, 2};
            audio.samples.assign(static_cast<size_t>(kRate) * 2 * 10, 0.0f);
            return AudioClip::Create("test/silence.wav"_intern, std::move(audio));
        }

    }  // namespace

    /// SDL's dummy audio driver: a device with no sound card, so the backend runs on any machine and makes no noise
    class SDLAudioBackendTest : public ::testing::Test {
    protected:
        void SetUp() override {
            backend = New<SDLAudioBackend>();
            backend->SetHeadless(true);
            if (!backend->Open(kVoices)) {
                GTEST_SKIP() << "SDL has no dummy audio driver here: " << SDL_GetError();
            }
        }

        void TearDown() override {
            if (backend) {
                backend->Close();
            }
        }

        IntrusivePtr<SDLAudioBackend> backend;
        IntrusivePtr<AudioClip> clip = TenSecondsOfSilence();
    };

    TEST_F(SDLAudioBackendTest, OpenMakesTheVoices) {
        EXPECT_EQ(backend->GetVoiceCount(), kVoices);
        for (uint32_t voice = 0; voice < kVoices; ++voice) {
            EXPECT_FALSE(backend->IsPlaying(voice));
        }
    }

    TEST_F(SDLAudioBackendTest, AStartedVoicePlaysUntilItIsStopped) {
        ASSERT_TRUE(backend->Start(1, *clip, 0.5f));
        EXPECT_TRUE(backend->IsPlaying(1));
        EXPECT_FALSE(backend->IsPlaying(0));
        backend->SetGain(1, 0.25f);
        backend->Stop(1);
        EXPECT_FALSE(backend->IsPlaying(1));
    }

    TEST_F(SDLAudioBackendTest, AStartReplacesWhatTheVoicePlayed) {
        ASSERT_TRUE(backend->Start(0, *clip, 1.0f));
        ASSERT_TRUE(backend->Start(0, *clip, 1.0f));
        EXPECT_TRUE(backend->IsPlaying(0));
    }

    TEST_F(SDLAudioBackendTest, AVoiceOutOfRangeOrAnEmptyClipIsRefused) {
        EXPECT_FALSE(backend->Start(kVoices, *clip, 1.0f));
        EXPECT_FALSE(backend->Start(0, *AudioClip::CreateFailed("test/failed.wav"_intern), 1.0f));
        EXPECT_FALSE(backend->IsPlaying(kVoices));
        backend->Stop(kVoices);
        backend->SetGain(kVoices, 1.0f);
    }

    TEST_F(SDLAudioBackendTest, CloseLeavesNoVoiceAndOpenWorksAgain) {
        backend->Close();
        EXPECT_EQ(backend->GetVoiceCount(), 0u);
        EXPECT_FALSE(backend->Start(0, *clip, 1.0f));
        ASSERT_TRUE(backend->Open(2));
        EXPECT_EQ(backend->GetVoiceCount(), 2u);
    }

    TEST_F(SDLAudioBackendTest, TheManagerPlaysThroughIt) {
        backend->Close();
        AudioManager manager;
        AudioSettings settings;
        settings._voices = kVoices;
        manager.Initialize(settings);
        manager.SetBackend(backend);
        ASSERT_EQ(manager.GetBackend().Get(), backend.Get());
        const AudioHandle handle = manager.Play(*clip, AudioBus::Sfx);
        EXPECT_TRUE(manager.IsPlaying(handle));
        manager.Stop(handle);
        EXPECT_FALSE(manager.IsPlaying(handle));
        manager.Shutdown();
    }

}  // namespace BECore::Tests
