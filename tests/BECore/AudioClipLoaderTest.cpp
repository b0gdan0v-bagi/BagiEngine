#include <BECore/Audio/AudioClip.h>
#include <BECore/AudioTestSupport.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kMountPoint = "test_audio_clips";

    }  // namespace

    /// Needs FileSystem and ResourceManager; the fixture folder of the source tree is mounted read-only
    class AudioClipLoaderTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            ASSERT_TRUE(CoreManager::GetFileSystem().Mount(PoolString::Intern(kMountPoint), AudioFixtureFolder()));
        }

        void TearDown() override {
            CoreManager::GetResourceManager().ClearCache();
            CoreManager::GetFileSystem().Unmount(PoolString::Intern(kMountPoint));
        }
    };

    TEST_F(AudioClipLoaderTest, AnOggFileLoadsAsAClip) {
        const auto clip = CoreManager::GetResourceManager().Load<AudioClip>(Format("{}/stereo-tone.ogg", kMountPoint));
        ASSERT_TRUE(clip);
        ASSERT_EQ(clip->GetState(), ResourceState::Loaded);
        EXPECT_EQ(clip->GetFormat(), (AudioFormat{22050, 2}));
        EXPECT_NEAR(clip->GetDurationSeconds(), 0.25, 0.01);
        EXPECT_EQ(clip->GetSamples().size(), clip->GetFrameCount() * 2);
        EXPECT_GE(clip->GetMemoryUsage(), clip->GetSamples().size_bytes());
        EXPECT_EQ(clip->GetTypeName(), "AudioClip"_intern);
    }

    TEST_F(AudioClipLoaderTest, AFlacFileLoadsAsAClip) {
        const auto clip = CoreManager::GetResourceManager().Load<AudioClip>(Format("{}/stereo-tone.flac", kMountPoint));
        ASSERT_TRUE(clip);
        ASSERT_EQ(clip->GetState(), ResourceState::Loaded);
        EXPECT_EQ(clip->GetFormat(), (AudioFormat{44100, 2}));
        EXPECT_EQ(clip->GetFrameCount(), 11025u);
    }

    TEST_F(AudioClipLoaderTest, AMissingFileIsAFailedClip) {
        const auto clip = CoreManager::GetResourceManager().Load<AudioClip>(Format("{}/missing.wav", kMountPoint));
        ASSERT_TRUE(clip);
        EXPECT_EQ(clip->GetState(), ResourceState::Failed);
        EXPECT_TRUE(clip->GetSamples().empty());
    }

}  // namespace BECore::Tests
