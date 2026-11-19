#include <BECore/Audio/AudioManager.h>
#include <BECore/Audio/AudioSourceComponent.h>
#include <BECore/AudioTestSupport.h>
#include <BECore/FakeAudioBackend.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Time/Time.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kMountPoint = "test_audio_source";
        constexpr double kFrame = 1.0 / 32.0;

        // Scene "A" is the active one. "OnStart" plays at the first update, "OnAction" on the action "Ding", "Missing" names no file.
        // Scene "B" is loaded and not active, with a source that would do both.
        constexpr const char* kScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="A"><nodes>
    <node name="OnStart"><components>
      <component type="AudioSourceComponent" clip="test_audio_source/stereo-tone.ogg" playOnStart="true"/>
    </components></node>
    <node name="OnAction"><components>
      <component type="AudioSourceComponent" clip="test_audio_source/stereo-tone.ogg" bus="Ui" volume="0.5" playAction="Ding"/>
    </components></node>
    <node name="Missing"><components>
      <component type="AudioSourceComponent" clip="test_audio_source/missing.wav"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="B"><nodes>
    <node name="OnStart"><components>
      <component type="AudioSourceComponent" clip="test_audio_source/stereo-tone.ogg" playOnStart="true" playAction="Ding"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        IntrusivePtr<SceneNode> FindNodeByName(Scene& scene, const char* name) {
            for (const auto& node : scene.GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(name)) {
                    return node;
                }
            }
            return {};
        }

    }  // namespace

    class AudioSourceComponentTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            ASSERT_TRUE(CoreManager::GetFileSystem().Mount(PoolString::Intern(kMountPoint), AudioFixtureFolder()));
            backend = New<FakeAudioBackend>();
            CoreManager::GetAudio().SetBackend(backend);
            ASSERT_TRUE(CoreManager::GetAudio().GetBackend());

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kScenesXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetAudio().Shutdown();
            CoreManager::GetResourceManager().ClearCache();
            CoreManager::GetFileSystem().Unmount(PoolString::Intern(kMountPoint));
        }

        IntrusivePtr<AudioSourceComponent> Source(const char* node, const char* scene = "A") {
            auto found = FindNodeByName(*_manager->GetSceneByName(PoolString::Intern(scene)), node);
            return found ? found->GetComponent<AudioSourceComponent>() : IntrusivePtr<AudioSourceComponent>{};
        }

        void Frame() {
            CoreManager::GetTime().Advance(kFrame);
            _manager->UpdateAll();
        }

        IntrusivePtr<FakeAudioBackend> backend;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(AudioSourceComponentTest, ASceneFileSetsTheFieldsAndTheClipLoadsAtAttach) {
        auto source = Source("OnAction");
        ASSERT_TRUE(source);
        EXPECT_EQ(source->_clip, "test_audio_source/stereo-tone.ogg"_intern);
        EXPECT_EQ(source->_bus, AudioBus::Ui);
        EXPECT_FLOAT_EQ(source->_volume, 0.5f);
        EXPECT_FALSE(source->_playOnStart);
        EXPECT_EQ(source->_playAction, "Ding"_intern);
        ASSERT_NE(source->GetLoadedClip(), nullptr);
        EXPECT_EQ(source->GetLoadedClip()->GetState(), ResourceState::Loaded);
        EXPECT_TRUE(backend->starts.empty()) << "nothing plays at load";
    }

    TEST_F(AudioSourceComponentTest, PlayOnStartPlaysOnceAtTheFirstUpdateOfTheActiveScene) {
        Frame();
        EXPECT_EQ(backend->starts.size(), 1u) << "only the source of the active scene";
        EXPECT_TRUE(Source("OnStart")->IsPlaying());
        Frame();
        EXPECT_EQ(backend->starts.size(), 1u);
    }

    TEST_F(AudioSourceComponentTest, TheActionPlaysOnItsBusAtItsVolumeInTheActiveSceneOnly) {
        InputEvents::ActionEvent::Emit("Ding"_intern);
        ASSERT_EQ(backend->starts.size(), 1u);
        EXPECT_TRUE(Source("OnAction")->IsPlaying());
        const AudioPlayRecord record = LastRecord(CoreManager::GetAudio());
        EXPECT_EQ(record.bus, AudioBus::Ui);
        EXPECT_FLOAT_EQ(record.volume, 0.5f);

        InputEvents::ActionEvent::Emit("Other"_intern);
        EXPECT_EQ(backend->starts.size(), 1u);
    }

    TEST_F(AudioSourceComponentTest, StopStopsItsSound) {
        auto source = Source("OnAction");
        source->Play();
        ASSERT_TRUE(source->IsPlaying());
        source->Stop();
        EXPECT_FALSE(source->IsPlaying());
        EXPECT_EQ(CoreManager::GetAudio().GetPlayingCount(), 0u);
    }

    TEST_F(AudioSourceComponentTest, AClipThatDoesNotLoadPlaysNothingAndIsRecorded) {
        auto source = Source("Missing");
        ASSERT_NE(source->GetLoadedClip(), nullptr);
        EXPECT_EQ(source->GetLoadedClip()->GetState(), ResourceState::Failed);
        source->Play();
        EXPECT_FALSE(source->IsPlaying());
        EXPECT_TRUE(backend->starts.empty());
        const AudioPlayRecord record = LastRecord(CoreManager::GetAudio());
        EXPECT_EQ(record.clip, "test_audio_source/missing.wav"_intern);
        EXPECT_FALSE(record.played);
    }

    TEST_F(AudioSourceComponentTest, AnEditedClipLoadsAgainAtTheNextPlay) {
        auto source = Source("Missing");
        source->_clip = "test_audio_source/stereo-tone.ogg"_intern;
        source->Play();
        EXPECT_TRUE(source->IsPlaying());
        EXPECT_EQ(source->GetLoadedClip()->GetPath(), "test_audio_source/stereo-tone.ogg"_intern);
    }

}  // namespace BECore::Tests
