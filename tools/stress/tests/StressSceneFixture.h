#pragma once

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Games/GameManager.h>
#include <Stress/StressGame.h>
#include <gtest/gtest.h>

namespace BECore::Stress::Tests {

    /**
     * The shipped scene file of BagiStress in a scene manager on a RecordingRenderer, with the StressGame active: the setup of the tests of the
     * scene components. StartGame() takes the flags of the command line (`--stress-scene=entities` opens that scene).
     */
    class StressSceneFixture : public ::testing::Test {
    protected:
        static constexpr double kFrame = 1.0 / 32.0;  // a power of two: the sums of frame times are exact

        static void SetUpTestSuite() {
            BECore::Tests::EngineEnvironment::EnsureCoreServices();
        }

        /// The scenes the app ships, from the file next to the sources: the app's own content is not mounted in the test environment
        static std::filesystem::path ShippedSceneConfig() {
            return std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "SceneConfig.xml";
        }

        void StartGame(const Vector<String>& arguments = {}) {
            CoreManager::GetCommandLine().Set(arguments);
            _renderer = New<BECore::Tests::RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromFile(ShippedSceneConfig())) << ShippedSceneConfig().string();
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            BECore::Tests::EngineEnvironment::UseSceneManager(_manager);

            GameManager::GetInstance().Initialize();
            ASSERT_NE(StressGame::GetActive(), nullptr);
        }

        void TearDown() override {
            GameManager::GetInstance().Shutdown();
            BECore::Tests::EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetCommandLine().Set({});
            CoreManager::GetTime().Reset();
        }

        void SetCount(uint32_t count) {
            StressGame::GetActive()->GetState().SetCount(count);
        }

        /// One frame of the scene: the clock, the update (the tweens are advanced by the test when it wants them)
        void Update() {
            CoreManager::GetTime().Advance(kFrame);
            _manager->UpdateAll();
        }

        void Draw() {
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        Scene& GetScene(const char* name) const {
            return *_manager->GetSceneByName(PoolString::Intern(name));
        }

        /// The node `Field` of the scene @p sceneName
        SceneNode& Field(const char* sceneName) const {
            for (const auto& node : GetScene(sceneName).GetRootNode().GetChildren()) {
                if (node->GetName() == "Field"_intern) {
                    return *node;
                }
            }
            ADD_FAILURE() << "no node Field in " << sceneName;
            return GetScene(sceneName).GetRootNode();
        }

        IntrusivePtr<BECore::Tests::RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

}  // namespace BECore::Stress::Tests
