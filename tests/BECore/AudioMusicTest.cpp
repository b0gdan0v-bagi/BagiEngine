#include <BECore/Audio/AudioEvents.h>
#include <BECore/Audio/AudioManager.h>
#include <BECore/Audio/NullAudioBackend.h>
#include <BECore/FakeAudioBackend.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <filesystem>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kMountPoint = "test_audio_music";
        constexpr uint32_t kEffectVoices = 2;
        constexpr uint64_t kToneFrames = 5512;  ///< stereo-tone.ogg: 22050 Hz, stereo, a quarter of a second
        constexpr uint64_t kBlipFrames = 2205;  ///< mono-blip.wav: 11025 Hz, mono, a fifth of a second
        constexpr float kGainTolerance = 1e-5f;

        std::filesystem::path FixtureFolder() {
            return std::filesystem::path(__FILE__).parent_path() / "AudioData";
        }

        PoolString Tone() {
            return PoolString::Intern(Format("{}/stereo-tone.ogg", kMountPoint));
        }

        PoolString Blip() {
            return PoolString::Intern(Format("{}/mono-blip.wav", kMountPoint));
        }

        uint64_t BufferFrames(uint32_t sampleRate) {
            return static_cast<uint64_t>(static_cast<float>(sampleRate) * kMusicBufferSeconds);
        }

    }  // namespace

    /// A manager of its own over a fake backend; the core's services only for the files (the fixture folder is mounted)
    class AudioMusicTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            ASSERT_TRUE(CoreManager::GetFileSystem().Mount(PoolString::Intern(kMountPoint), FixtureFolder()));
            AudioSettings settings;
            settings._voices = kEffectVoices;
            manager.Initialize(settings);
            backend = New<FakeAudioBackend>();
            manager.SetBackend(backend);
            ASSERT_EQ(backend->GetVoiceCount(), kEffectVoices + kMusicVoices);
        }

        void TearDown() override {
            manager.Shutdown();
            CoreManager::GetFileSystem().Unmount(PoolString::Intern(kMountPoint));
        }

        /// The voice of the music track of slot @p slot
        static uint32_t MusicVoice(uint32_t slot) {
            return kEffectVoices + slot;
        }

        AudioManager manager;
        IntrusivePtr<FakeAudioBackend> backend;
    };

    TEST_F(AudioMusicTest, MusicStreamsOnAVoiceAfterTheEffects) {
        ASSERT_TRUE(manager.PlayMusic(Tone(), 0.5f));
        ASSERT_EQ(backend->streamStarts.size(), 1u);
        const uint32_t voice = backend->streamStarts.front();
        EXPECT_EQ(voice, MusicVoice(1)) << "the first track takes the second slot: the current one is the first";
        EXPECT_EQ(backend->streamFormats[voice], (AudioFormat{22050, 2}));
        EXPECT_GE(backend->queuedFrames[voice], BufferFrames(22050)) << "the buffer is filled at once, the loop rewinding the short track";
        EXPECT_FLOAT_EQ(backend->gains[voice], 0.5f);
        EXPECT_EQ(manager.GetEffectVoiceCount(), kEffectVoices);

        const AudioMusicState state = manager.GetMusicState();
        EXPECT_EQ(state.clip, Tone());
        EXPECT_TRUE(state.playing);
        EXPECT_TRUE(state.loop);
        EXPECT_FLOAT_EQ(state.volume, 0.5f);
        EXPECT_FALSE(state.fading);

        DecodedAudio audio;
        audio.format = AudioFormat{22050, 1};
        audio.samples.assign(16, 0.5f);
        const auto clip = AudioClip::Create("test/effect.wav"_intern, std::move(audio));
        for (int i = 0; i < 5; ++i) {
            manager.Play(*clip, AudioBus::Sfx);
        }
        for (const uint32_t started : backend->starts) {
            EXPECT_LT(started, kEffectVoices) << "an effect never takes a music voice";
        }
    }

    TEST_F(AudioMusicTest, ALoopingTrackIsFedWithoutEnd) {
        ASSERT_TRUE(manager.PlayMusic(Tone()));
        const uint32_t voice = backend->streamStarts.front();
        for (int frame = 0; frame < 20; ++frame) {
            backend->Drain(voice, 2000);
            manager.Update(1.0f / 60.0f);
        }
        EXPECT_GT(backend->totalFrames[voice], kToneFrames * 6);
        EXPECT_FALSE(backend->ended[voice]);
        EXPECT_TRUE(manager.GetMusicState().playing);
    }

    TEST_F(AudioMusicTest, ATrackThatDoesNotLoopPlaysOutAndStops) {
        ASSERT_TRUE(manager.PlayMusic(Tone(), 1.0f, false));
        const uint32_t voice = backend->streamStarts.front();
        EXPECT_EQ(backend->totalFrames[voice], kToneFrames);
        EXPECT_TRUE(backend->ended[voice]);
        manager.Update(1.0f / 60.0f);
        EXPECT_TRUE(manager.GetMusicState().playing) << "its tail still plays";

        backend->Drain(voice, kToneFrames);
        manager.Update(1.0f / 60.0f);
        EXPECT_FALSE(manager.GetMusicState().playing);
        EXPECT_TRUE(manager.GetMusicState().clip.Empty());
    }

    TEST_F(AudioMusicTest, ANewTrackFadesInWhileTheOldOneFadesOut) {
        ASSERT_TRUE(manager.PlayMusic(Tone(), 1.0f, true, 1.0f));
        const uint32_t first = backend->streamStarts.back();
        EXPECT_NEAR(backend->gains[first], 0.0f, kGainTolerance);
        EXPECT_TRUE(manager.GetMusicState().fading);
        manager.Update(0.5f);
        EXPECT_NEAR(backend->gains[first], 0.5f, kGainTolerance);
        manager.Update(0.5f);
        EXPECT_NEAR(backend->gains[first], 1.0f, kGainTolerance);
        EXPECT_FALSE(manager.GetMusicState().fading);

        ASSERT_TRUE(manager.PlayMusic(Blip(), 1.0f, true, 1.0f));
        const uint32_t second = backend->streamStarts.back();
        EXPECT_NE(second, first);
        EXPECT_EQ(backend->streamFormats[second], (AudioFormat{11025, 1}));
        EXPECT_GE(backend->queuedFrames[second], BufferFrames(11025)) << "a WAVE track loops from memory: " << kBlipFrames << " frames";
        manager.Update(0.25f);
        EXPECT_NEAR(backend->gains[first], 0.75f, kGainTolerance);
        EXPECT_NEAR(backend->gains[second], 0.25f, kGainTolerance);
        EXPECT_EQ(manager.GetMusicState().clip, Blip());

        manager.Update(1.0f);
        EXPECT_FALSE(backend->IsPlaying(first)) << "faded out: stopped";
        EXPECT_NEAR(backend->gains[second], 1.0f, kGainTolerance);
    }

    TEST_F(AudioMusicTest, AThirdTrackStopsTheOldestAtOnce) {
        ASSERT_TRUE(manager.PlayMusic(Tone(), 1.0f, true, 1.0f));
        const uint32_t first = backend->streamStarts.back();
        ASSERT_TRUE(manager.PlayMusic(Blip(), 1.0f, true, 1.0f));
        ASSERT_TRUE(manager.PlayMusic(Tone(), 1.0f, true, 1.0f));
        EXPECT_EQ(backend->streamStarts.back(), first) << "the slot of the oldest";
        EXPECT_EQ(backend->streamStarts.size(), 3u);
        EXPECT_EQ(manager.GetMusicState().clip, Tone());
    }

    TEST_F(AudioMusicTest, TheSamePathKeepsPlayingAndTakesTheNewVolume) {
        ASSERT_TRUE(manager.PlayMusic(Tone(), 1.0f));
        ASSERT_TRUE(manager.PlayMusic(Tone(), 0.25f, false));
        EXPECT_EQ(backend->streamStarts.size(), 1u);
        EXPECT_FLOAT_EQ(backend->gains[backend->streamStarts.front()], 0.25f);
        EXPECT_FALSE(manager.GetMusicState().loop);
    }

    TEST_F(AudioMusicTest, StopMusicStopsAtOnceOrAfterItsFade) {
        ASSERT_TRUE(manager.PlayMusic(Tone()));
        const uint32_t voice = backend->streamStarts.front();
        manager.StopMusic(0.5f);
        EXPECT_TRUE(manager.GetMusicState().fading);
        manager.Update(0.25f);
        EXPECT_NEAR(backend->gains[voice], 0.5f, kGainTolerance);
        manager.Update(0.25f);
        EXPECT_FALSE(manager.GetMusicState().playing);
        EXPECT_FALSE(backend->IsPlaying(voice));

        ASSERT_TRUE(manager.PlayMusic(Tone()));
        manager.StopMusic();
        EXPECT_FALSE(manager.GetMusicState().playing);
        EXPECT_FALSE(backend->IsPlaying(backend->streamStarts.back()));
    }

    TEST_F(AudioMusicTest, AFrameOfNoTimeHoldsTheFadeAndStillFeedsTheMusic) {
        ASSERT_TRUE(manager.PlayMusic(Tone(), 1.0f, true, 1.0f));
        const uint32_t voice = backend->streamStarts.front();
        const uint64_t fed = backend->totalFrames[voice];
        backend->Drain(voice, backend->queuedFrames[voice]);
        manager.Update(0.0f);
        EXPECT_NEAR(backend->gains[voice], 0.0f, kGainTolerance);
        EXPECT_GT(backend->totalFrames[voice], fed);
    }

    TEST_F(AudioMusicTest, TheLevelsReachTheMusic) {
        ASSERT_TRUE(manager.PlayMusic(Tone(), 0.5f));
        const uint32_t voice = backend->streamStarts.front();
        manager.SetBusVolume(AudioBus::Music, 0.5f);
        EXPECT_FLOAT_EQ(backend->gains[voice], 0.25f);
        manager.SetBusVolume(AudioBus::Sfx, 0.0f);
        EXPECT_FLOAT_EQ(backend->gains[voice], 0.25f) << "not the bus of the music";
        manager.SetMasterMuted(true);
        EXPECT_FLOAT_EQ(backend->gains[voice], 0.0f);
    }

    TEST_F(AudioMusicTest, StopAllStopsTheMusicToo) {
        ASSERT_TRUE(manager.PlayMusic(Tone()));
        manager.StopAll();
        EXPECT_FALSE(manager.GetMusicState().playing);
        EXPECT_FALSE(backend->IsPlaying(backend->streamStarts.front()));
    }

    TEST_F(AudioMusicTest, MusicThatCannotPlayIsARequestThatPlayedNothing) {
        EXPECT_FALSE(manager.PlayMusic(PoolString::Intern(Format("{}/missing.ogg", kMountPoint))));
        EXPECT_FALSE(manager.PlayMusic(PoolString()));
        Vector<AudioPlayRecord> records;
        manager.GetRecentPlays(records);
        ASSERT_EQ(records.size(), 2u);
        EXPECT_EQ(records[0].bus, AudioBus::Music);
        EXPECT_FALSE(records[0].played);
        EXPECT_TRUE(backend->streamStarts.empty());
        EXPECT_FALSE(manager.GetMusicState().playing);

        manager.Shutdown();
        EXPECT_FALSE(manager.PlayMusic(Tone())) << "no backend";
        manager.SetBackend(New<NullAudioBackend>());
        EXPECT_FALSE(manager.PlayMusic(Tone())) << "the null backend has no stream";
        manager.Update(1.0f);
    }

    TEST_F(AudioMusicTest, TheMusicEventsReachTheManager) {
        AudioEvents::PlayMusicEvent::Emit(Tone(), 0.5f, false);
        ASSERT_EQ(backend->streamStarts.size(), 1u);
        EXPECT_FALSE(manager.GetMusicState().loop);
        EXPECT_FLOAT_EQ(manager.GetMusicState().volume, 0.5f);
        AudioEvents::StopMusicEvent::Emit();
        EXPECT_FALSE(manager.GetMusicState().playing);
    }

}  // namespace BECore::Tests
