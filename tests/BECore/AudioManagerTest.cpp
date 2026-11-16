#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/AudioManager.h>
#include <BECore/Audio/NullAudioBackend.h>
#include <BECore/FakeAudioBackend.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    namespace {

        IntrusivePtr<AudioClip> MakeClip() {
            DecodedAudio audio;
            audio.format = AudioFormat{22050, 1};
            audio.samples.assign(64, 0.25f);
            return AudioClip::Create("test/clip.wav"_intern, std::move(audio));
        }

    }  // namespace

    class AudioManagerTest : public ::testing::Test {
    protected:
        /// A manager with default levels (no config is read) over a fake of @p voices voices
        void Open(uint32_t voices) {
            AudioSettings settings;
            settings._voices = voices;
            manager.Initialize(settings);
            backend = New<FakeAudioBackend>();
            manager.SetBackend(backend);
            ASSERT_EQ(manager.GetBackend().Get(), backend.Get());
        }

        AudioManager manager;
        IntrusivePtr<FakeAudioBackend> backend;
        IntrusivePtr<AudioClip> clip = MakeClip();
    };

    TEST_F(AudioManagerTest, WithoutABackendNothingPlays) {
        const AudioHandle handle = manager.Play(*clip, AudioBus::Sfx);
        EXPECT_FALSE(handle.IsValid());
        EXPECT_FALSE(manager.IsPlaying(handle));
        manager.Stop(handle);
        manager.StopAll();
        EXPECT_EQ(manager.GetStartedCount(), 0u);
    }

    TEST_F(AudioManagerTest, ABackendThatDoesNotOpenIsDropped) {
        auto refusing = New<FakeAudioBackend>();
        refusing->opens = false;
        manager.SetBackend(refusing);
        EXPECT_FALSE(manager.GetBackend());
        EXPECT_FALSE(manager.Play(*clip, AudioBus::Sfx).IsValid());
    }

    TEST_F(AudioManagerTest, ASoundGetsAFreeVoiceAndTheGainOfItsBus) {
        Open(4);
        manager.SetMasterVolume(0.5f);
        manager.SetBusVolume(AudioBus::Ui, 0.5f);
        const AudioHandle handle = manager.Play(*clip, AudioBus::Ui, 0.8f);
        ASSERT_TRUE(handle.IsValid());
        EXPECT_TRUE(manager.IsPlaying(handle));
        EXPECT_FLOAT_EQ(backend->gains[handle.voice], 0.8f * 0.5f * 0.5f);
        EXPECT_EQ(manager.GetStartedCount(), 1u);

        backend->playing[handle.voice] = false;  // the sound ended
        EXPECT_FALSE(manager.IsPlaying(handle));
        const AudioHandle next = manager.Play(*clip, AudioBus::Sfx);
        EXPECT_EQ(next.voice, handle.voice) << "a voice that has ended is free again";
    }

    TEST_F(AudioManagerTest, AllVoicesBusyTakesTheOldestAndItsHandleGoesStale) {
        Open(2);
        const AudioHandle first = manager.Play(*clip, AudioBus::Sfx);
        const AudioHandle second = manager.Play(*clip, AudioBus::Sfx);
        ASSERT_NE(first.voice, second.voice);
        const AudioHandle third = manager.Play(*clip, AudioBus::Sfx);
        EXPECT_EQ(third.voice, first.voice);
        EXPECT_FALSE(manager.IsPlaying(first));
        EXPECT_TRUE(manager.IsPlaying(third));

        manager.Stop(first);  // stale: must not stop the third sound
        EXPECT_TRUE(manager.IsPlaying(third));

        const AudioHandle fourth = manager.Play(*clip, AudioBus::Sfx);
        EXPECT_EQ(fourth.voice, second.voice) << "the oldest is now the second";
    }

    TEST_F(AudioManagerTest, StopEndsOneSoundAndStopAllEveryOne) {
        Open(3);
        const AudioHandle a = manager.Play(*clip, AudioBus::Sfx);
        const AudioHandle b = manager.Play(*clip, AudioBus::Music);
        manager.Stop(a);
        EXPECT_FALSE(manager.IsPlaying(a));
        EXPECT_TRUE(manager.IsPlaying(b));
        manager.StopAll();
        EXPECT_FALSE(manager.IsPlaying(b));
        EXPECT_FALSE(backend->IsPlaying(b.voice));
    }

    TEST_F(AudioManagerTest, ABusChangeReachesTheSoundsThatPlay) {
        Open(2);
        const AudioHandle music = manager.Play(*clip, AudioBus::Music, 0.5f);
        const AudioHandle sfx = manager.Play(*clip, AudioBus::Sfx);
        manager.SetBusVolume(AudioBus::Music, 0.5f);
        EXPECT_FLOAT_EQ(backend->gains[music.voice], 0.25f);
        EXPECT_FLOAT_EQ(backend->gains[sfx.voice], 1.0f);

        manager.SetBusMuted(AudioBus::Music, true);
        EXPECT_FLOAT_EQ(backend->gains[music.voice], 0.0f);
        manager.SetBusMuted(AudioBus::Music, false);
        EXPECT_FLOAT_EQ(backend->gains[music.voice], 0.25f) << "unmuting gives the volume back";

        manager.SetMasterMuted(true);
        EXPECT_FLOAT_EQ(backend->gains[sfx.voice], 0.0f);
        EXPECT_FLOAT_EQ(manager.GetGain(AudioBus::Sfx, 1.0f), 0.0f);
    }

    TEST_F(AudioManagerTest, VolumesAreKeptBetweenZeroAndOne) {
        manager.SetBusVolume(AudioBus::Sfx, 3.0f);
        EXPECT_FLOAT_EQ(manager.GetSettings()._sfx._volume, 1.0f);
        manager.SetMasterVolume(std::numeric_limits<float>::quiet_NaN());
        EXPECT_FLOAT_EQ(manager.GetSettings()._master._volume, 0.0f);
        manager.SetMasterVolume(1.0f);
        EXPECT_FLOAT_EQ(manager.GetGain(AudioBus::Sfx, -2.0f), 0.0f);
    }

    TEST_F(AudioManagerTest, ApplySettingsReplacesTheLevels) {
        Open(1);
        const AudioHandle handle = manager.Play(*clip, AudioBus::Sfx);
        AudioSettings settings;
        settings._sfx._volume = 0.25f;
        manager.ApplySettings(settings);
        EXPECT_FLOAT_EQ(backend->gains[handle.voice], 0.25f);
        EXPECT_EQ(manager.GetSettings()._voices, 1u) << "the voice count is not a level";
    }

    TEST_F(AudioManagerTest, AFailedClipDoesNotPlay) {
        Open(1);
        EXPECT_FALSE(manager.Play(*AudioClip::CreateFailed("test/missing.wav"_intern), AudioBus::Sfx).IsValid());
        EXPECT_TRUE(backend->starts.empty());
    }

    TEST_F(AudioManagerTest, ShutdownClosesTheBackend) {
        Open(1);
        const AudioHandle handle = manager.Play(*clip, AudioBus::Sfx);
        manager.Shutdown();
        EXPECT_EQ(backend->closes, 1u);
        EXPECT_FALSE(manager.GetBackend());
        EXPECT_FALSE(manager.IsPlaying(handle));
    }

    TEST_F(AudioManagerTest, TheNullBackendAcceptsAndPlaysNothing) {
        AudioSettings settings;
        settings._voices = 2;
        manager.Initialize(settings);
        manager.SetBackend(New<NullAudioBackend>());
        const AudioHandle handle = manager.Play(*clip, AudioBus::Sfx);
        EXPECT_TRUE(handle.IsValid());
        EXPECT_FALSE(manager.IsPlaying(handle));
        EXPECT_EQ(manager.GetStartedCount(), 1u);
    }

    TEST_F(AudioManagerTest, EveryRequestIsRecordedAlsoTheOnesThatPlayNothing) {
        manager.Play(*clip, AudioBus::Ui, 2.0f);  // no backend yet
        Open(1);
        manager.Play(*clip, AudioBus::Music, 0.5f);
        manager.Play(*AudioClip::CreateFailed("test/missing.wav"_intern), AudioBus::Sfx);

        Vector<AudioPlayRecord> records;
        manager.GetRecentPlays(records);
        ASSERT_EQ(records.size(), 3u);
        EXPECT_EQ(manager.GetRequestCount(), 3u);
        EXPECT_EQ(manager.GetStartedCount(), 1u);

        EXPECT_EQ(records[0].index, 1u);
        EXPECT_EQ(records[0].clip, "test/clip.wav"_intern);
        EXPECT_EQ(records[0].bus, AudioBus::Ui);
        EXPECT_FLOAT_EQ(records[0].volume, 1.0f) << "clamped as the gain is";
        EXPECT_FALSE(records[0].played);

        EXPECT_EQ(records[1].bus, AudioBus::Music);
        EXPECT_FLOAT_EQ(records[1].volume, 0.5f);
        EXPECT_TRUE(records[1].played);

        EXPECT_EQ(records[2].clip, "test/missing.wav"_intern);
        EXPECT_FALSE(records[2].played);
    }

    TEST_F(AudioManagerTest, TheRecordKeepsTheLastRequestsOldestFirst) {
        Open(1);
        const size_t requests = kAudioRecentPlays + 5;
        for (size_t i = 0; i < requests; ++i) {
            manager.Play(*clip, AudioBus::Sfx);
        }
        Vector<AudioPlayRecord> records;
        manager.GetRecentPlays(records);
        ASSERT_EQ(records.size(), kAudioRecentPlays);
        EXPECT_EQ(records.front().index, requests - kAudioRecentPlays + 1);
        EXPECT_EQ(records.back().index, requests);
        for (size_t i = 1; i < records.size(); ++i) {
            EXPECT_EQ(records[i].index, records[i - 1].index + 1);
        }
    }

    TEST_F(AudioManagerTest, TheLevelsRevisionMovesOnEveryChangeOfALevel) {
        Open(1);
        uint64_t revision = manager.GetLevelsRevision();
        const auto expectMoved = [&](const char* what) {
            EXPECT_GT(manager.GetLevelsRevision(), revision) << what;
            revision = manager.GetLevelsRevision();
        };
        manager.SetMasterVolume(0.5f);
        expectMoved("master volume");
        manager.SetMasterMuted(true);
        expectMoved("master mute");
        manager.SetBusVolume(AudioBus::Music, 0.25f);
        expectMoved("bus volume");
        manager.SetBusMuted(AudioBus::Ui, true);
        expectMoved("bus mute");
        manager.ApplySettings(AudioSettings{});
        expectMoved("settings");

        manager.Play(*clip, AudioBus::Sfx);
        manager.StopAll();
        EXPECT_EQ(manager.GetLevelsRevision(), revision) << "a sound is not a level";
    }

    TEST_F(AudioManagerTest, ThePlayingCountIsTheVoicesThatPlay) {
        EXPECT_EQ(manager.GetPlayingCount(), 0u);
        Open(3);
        const AudioHandle first = manager.Play(*clip, AudioBus::Sfx);
        manager.Play(*clip, AudioBus::Sfx);
        EXPECT_EQ(manager.GetPlayingCount(), 2u);
        manager.Stop(first);
        EXPECT_EQ(manager.GetPlayingCount(), 1u);
    }

}  // namespace BECore::Tests
