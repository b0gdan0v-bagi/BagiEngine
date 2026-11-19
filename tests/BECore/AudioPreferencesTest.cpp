#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/AudioManager.h>
#include <BECore/Audio/AudioPreferences.h>
#include <BECore/AudioTestSupport.h>
#include <BECore/FakeAudioBackend.h>
#include <BECore/FileSystem/FileSystem.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// A local FileSystem and a temp save folder per test, a manager of its own: no global services
    class AudioPreferencesTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _base = TestTempDirectory("BagiAudioPreferencesTest");
            std::filesystem::remove_all(_base);
            _service.emplace(_fileSystem);
            ASSERT_TRUE(_service->Initialize(_base / "saves"));
            manager.Initialize(AudioSettings{});
        }

        void TearDown() override {
            _service.reset();
            std::error_code ec;
            std::filesystem::remove_all(_base, ec);
        }

        SaveGameService& Service() {
            return *_service;
        }

        AudioManager manager;
        std::filesystem::path _base;
        FileSystem _fileSystem;
        Optional<SaveGameService> _service;
    };

    TEST_F(AudioPreferencesTest, TheLevelsComeBackInAnotherRun) {
        manager.SetMasterVolume(0.75f);
        manager.SetBusVolume(AudioBus::Music, 0.25f);
        manager.SetBusMuted(AudioBus::Sfx, true);
        manager.SetBusMuted(AudioBus::Ui, true);
        ASSERT_TRUE(SaveAudioPreferences(Service(), manager).has_value());

        AudioManager next;
        AudioSettings config;
        config._voices = 4;
        config._backend = "SomeBackend";
        next.Initialize(config);
        const auto info = LoadAudioPreferences(Service(), next);
        ASSERT_TRUE(info.has_value()) << info.error().detail.c_str();
        EXPECT_EQ(info->version, kAudioPreferencesSchema.version);

        const AudioSettings& loaded = next.GetSettings();
        EXPECT_FLOAT_EQ(loaded._master._volume, 0.75f);
        EXPECT_FALSE(loaded._master._muted);
        EXPECT_FLOAT_EQ(loaded._music._volume, 0.25f);
        EXPECT_TRUE(loaded._sfx._muted);
        EXPECT_TRUE(loaded._ui._muted);
        EXPECT_EQ(loaded._voices, 4u) << "the voice count is the config's, not the player's";
        EXPECT_EQ(loaded._backend, "SomeBackend");
    }

    TEST_F(AudioPreferencesTest, ALoadReachesTheSoundsThatPlay) {
        auto backend = New<FakeAudioBackend>();
        manager.SetBackend(backend);
        const AudioHandle handle = manager.Play(*MakeTestClip(), AudioBus::Sfx);
        ASSERT_TRUE(handle.IsValid());

        AudioManager quiet;
        quiet.Initialize(AudioSettings{});
        quiet.SetMasterMuted(true);
        ASSERT_TRUE(SaveAudioPreferences(Service(), quiet).has_value());
        ASSERT_TRUE(LoadAudioPreferences(Service(), manager).has_value());
        EXPECT_FLOAT_EQ(backend->gains[handle.voice], 0.0f);
    }

    TEST_F(AudioPreferencesTest, NoFileLeavesTheLevelsAsTheyWere) {
        manager.SetBusVolume(AudioBus::Ui, 0.5f);
        const uint64_t revision = manager.GetLevelsRevision();
        const auto info = LoadAudioPreferences(Service(), manager);
        ASSERT_FALSE(info.has_value());
        EXPECT_EQ(info.error().kind, SaveErrorKind::NotFound);
        EXPECT_FLOAT_EQ(manager.GetSettings()._ui._volume, 0.5f);
        EXPECT_EQ(manager.GetLevelsRevision(), revision);
    }

    TEST_F(AudioPreferencesTest, AFileOfAnotherKindIsRefusedAndChangesNothing) {
        AudioPreferences other;
        other._master._volume = 0.1f;
        ASSERT_TRUE(Service().Save(kAudioPreferencesSlot, SaveSchema{"SomeGame", 1, 1}, other).has_value());
        const auto wrongGame = LoadAudioPreferences(Service(), manager);
        ASSERT_FALSE(wrongGame.has_value());
        EXPECT_EQ(wrongGame.error().kind, SaveErrorKind::WrongGame);

        ASSERT_TRUE(Service().WriteSlot(kAudioPreferencesSlot, Vector<uint8_t>(32, 0xAB)).has_value());
        const auto corrupt = LoadAudioPreferences(Service(), manager);
        ASSERT_FALSE(corrupt.has_value());
        EXPECT_EQ(corrupt.error().kind, SaveErrorKind::Corrupt);

        EXPECT_FLOAT_EQ(manager.GetSettings()._master._volume, 1.0f);
    }

    TEST_F(AudioPreferencesTest, AVolumeOutOfRangeInTheFileIsClamped) {
        AudioPreferences edited;
        edited._sfx._volume = 7.0f;
        edited._music._volume = -1.0f;
        ASSERT_TRUE(Service().Save(kAudioPreferencesSlot, kAudioPreferencesSchema, edited).has_value());
        ASSERT_TRUE(LoadAudioPreferences(Service(), manager).has_value());
        EXPECT_FLOAT_EQ(manager.GetSettings()._sfx._volume, 1.0f);
        EXPECT_FLOAT_EQ(manager.GetSettings()._music._volume, 0.0f);
    }

}  // namespace BECore::Tests
