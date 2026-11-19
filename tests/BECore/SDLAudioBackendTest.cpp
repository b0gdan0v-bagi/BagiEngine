#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/AudioManager.h>
#include <Common/Printers.h>
#include <CoreSDL/SDLAudioBackend.h>
#include <SDL3/SDL.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr uint32_t kVoices = 4;
        // The dummy device takes samples in real time: the silence lasts so long that no stall of a loaded machine lets it play out
        // before the test looks, and its low rate keeps those minutes small in memory
        constexpr uint32_t kSilenceRate = 4000;
        constexpr uint32_t kSilenceSeconds = 300;
        constexpr AudioFormat kSilenceFormat{kSilenceRate, 1};

        IntrusivePtr<AudioClip> LongSilence() {
            DecodedAudio audio;
            audio.format = kSilenceFormat;
            audio.samples.assign(static_cast<size_t>(kSilenceRate) * kSilenceSeconds, 0.0f);
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
        IntrusivePtr<AudioClip> clip = LongSilence();
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

    TEST_F(SDLAudioBackendTest, AStreamTakesSamplesPartByPartAndPlaysOutAfterItsEnd) {
        ASSERT_TRUE(backend->StartStream(2, kSilenceFormat, 0.5f));
        EXPECT_EQ(backend->GetQueuedFrames(2), 0u);
        const Vector<float> part(static_cast<size_t>(kSilenceRate) * kSilenceSeconds, 0.0f);
        ASSERT_TRUE(backend->QueueStream(2, part));
        EXPECT_GT(backend->GetQueuedFrames(2), 0u);
        EXPECT_TRUE(backend->IsPlaying(2));
        backend->EndStream(2);
        EXPECT_TRUE(backend->IsPlaying(2));
        backend->Stop(2);
        EXPECT_EQ(backend->GetQueuedFrames(2), 0u);
        EXPECT_FALSE(backend->IsPlaying(2));
    }

    TEST_F(SDLAudioBackendTest, AClipAfterAStreamOnTheSameVoiceIsNotAStream) {
        ASSERT_TRUE(backend->StartStream(0, AudioFormat{22050, 1}, 1.0f));
        ASSERT_TRUE(backend->Start(0, *clip, 1.0f));
        EXPECT_EQ(backend->GetQueuedFrames(0), 0u);
        EXPECT_FALSE(backend->QueueStream(0, Vector<float>(4, 0.0f)));
    }

    TEST_F(SDLAudioBackendTest, AStreamOutOfRangeIsRefused) {
        EXPECT_FALSE(backend->StartStream(kVoices, AudioFormat{22050, 1}, 1.0f));
        EXPECT_FALSE(backend->StartStream(0, AudioFormat{22050, 0}, 1.0f));
        EXPECT_FALSE(backend->QueueStream(kVoices, Vector<float>(4, 0.0f)));
        EXPECT_FALSE(backend->QueueStream(1, Vector<float>(4, 0.0f))) << "not started as a stream";
        backend->EndStream(kVoices);
        EXPECT_EQ(backend->GetQueuedFrames(kVoices), 0u);
    }

}  // namespace BECore::Tests
