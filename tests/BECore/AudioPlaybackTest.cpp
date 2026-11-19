#include <BECore/Audio/AudioEvents.h>
#include <BECore/Audio/AudioManager.h>
#include <BECore/AudioTestSupport.h>
#include <BECore/FakeAudioBackend.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kMountPoint = "test_audio_playback";

        PoolString FixturePath(const char* file) {
            return PoolString::Intern(Format("{}/{}", kMountPoint, file));
        }

    }  // namespace

    /// The audio of the core (CoreManager::GetAudio()) over a fake backend, with the clip fixtures mounted
    class AudioPlaybackTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            ASSERT_TRUE(CoreManager::GetFileSystem().Mount(PoolString::Intern(kMountPoint), AudioFixtureFolder()));
            backend = New<FakeAudioBackend>();
            CoreManager::GetAudio().SetBackend(backend);
            ASSERT_TRUE(CoreManager::GetAudio().GetBackend());
        }

        void TearDown() override {
            CoreManager::GetAudio().Shutdown();
            CoreManager::GetResourceManager().ClearCache();
            CoreManager::GetFileSystem().Unmount(PoolString::Intern(kMountPoint));
        }

        IntrusivePtr<FakeAudioBackend> backend;
    };

    TEST_F(AudioPlaybackTest, APathIsLoadedThroughTheResourceManagerAndPlayed) {
        AudioManager& audio = CoreManager::GetAudio();
        const AudioHandle handle = audio.Play(FixturePath("stereo-tone.ogg"), AudioBus::Ui, 0.5f);
        ASSERT_TRUE(handle.IsValid());
        EXPECT_TRUE(audio.IsPlaying(handle));
        EXPECT_EQ(backend->starts.size(), 1u);
        const AudioPlayRecord record = LastRecord(audio);
        EXPECT_EQ(record.clip, FixturePath("stereo-tone.ogg"));
        EXPECT_EQ(record.bus, AudioBus::Ui);
        EXPECT_TRUE(record.played);

        audio.Play(FixturePath("stereo-tone.ogg"), AudioBus::Ui);
        EXPECT_EQ(backend->starts.size(), 2u) << "the cached clip plays again";
    }

    TEST_F(AudioPlaybackTest, APathThatLoadsNothingIsARequestThatPlayedNothing) {
        AudioManager& audio = CoreManager::GetAudio();
        const uint64_t requests = audio.GetRequestCount();
        EXPECT_FALSE(audio.Play(FixturePath("missing.wav"), AudioBus::Sfx).IsValid());
        EXPECT_FALSE(audio.Play(PoolString(), AudioBus::Sfx).IsValid());
        EXPECT_FALSE(audio.Play(FixturePath("missing.mp3"), AudioBus::Sfx).IsValid()) << "no loader takes it";
        EXPECT_EQ(audio.GetRequestCount(), requests + 3);
        EXPECT_FALSE(LastRecord(audio).played);
        EXPECT_TRUE(backend->starts.empty());
    }

    TEST_F(AudioPlaybackTest, ThePlaySoundEventPlaysOnTheAudioOfTheCore) {
        AudioEvents::PlaySoundEvent::Emit(FixturePath("stereo-tone.ogg"), AudioBus::Music, 0.25f);
        ASSERT_EQ(backend->starts.size(), 1u);
        const AudioPlayRecord record = LastRecord(CoreManager::GetAudio());
        EXPECT_EQ(record.bus, AudioBus::Music);
        EXPECT_FLOAT_EQ(record.volume, 0.25f);
        EXPECT_TRUE(record.played);
    }

    TEST_F(AudioPlaybackTest, TheStopAllSoundsEventStopsEverySound) {
        AudioManager& audio = CoreManager::GetAudio();
        const AudioHandle first = audio.Play(FixturePath("stereo-tone.ogg"), AudioBus::Sfx);
        const AudioHandle second = audio.Play(FixturePath("stereo-tone.ogg"), AudioBus::Sfx);
        AudioEvents::StopAllSoundsEvent::Emit();
        EXPECT_FALSE(audio.IsPlaying(first));
        EXPECT_FALSE(audio.IsPlaying(second));
        EXPECT_EQ(audio.GetPlayingCount(), 0u);
    }

}  // namespace BECore::Tests
