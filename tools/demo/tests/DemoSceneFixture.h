#pragma once

#include <BECore/Animation/TweenManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/Time/Time.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Common/SceneTestSupport.h>
#include <Demo/DemoNames.h>
#include <Games/GameManager.h>
#include <gtest/gtest.h>

namespace BECore::Demo::Tests {

    /**
     * The shipped scene file of BagiDemo, with its sprite library, in a scene manager on a RecordingRenderer, the DemoGame active. The demo's
     * assets are not mounted in the test environment, so no sprite has a texture until GiveSpritesTexture().
     */
    class DemoSceneFixture : public ::testing::Test {
    protected:
        static constexpr double kFrame = 1.0 / 32.0;  // a power of two: the sums of frame times are exact
        static constexpr uint32_t kAtlasWidth = 256;
        static constexpr uint32_t kAtlasHeight = 128;
        static constexpr size_t kBytesPerPixel = 4;

        static void SetUpTestSuite() {
            BECore::Tests::EngineEnvironment::EnsureCoreServices();
        }

        /// The demo's folder in the source tree. Not through `__FILE__`, which is relative to the build folder under a ccache base_dir
        static Path DemoRoot() {
            return Path(BagiContentRoots::kEngine) / "tools" / "demo";
        }

        void LoadScene() {
            _renderer = New<BECore::Tests::RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);
            LoadSpriteLibrary(CoreManager::GetTextureLibrary());

            XmlDeserializer deserializer;
            const Path file = DemoRoot() / "config" / "SceneConfig.xml";
            ASSERT_TRUE(deserializer.LoadFromFile(file)) << file.CStr();
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            BECore::Tests::EngineEnvironment::UseSceneManager(_manager);

            GameManager::GetInstance().Initialize();
            ASSERT_NE(GameManager::GetInstance().GetActive(), nullptr);
            ASSERT_EQ(GameManager::GetInstance().GetActive()->GetName(), DemoNames::game);
        }

        static void LoadSpriteLibrary(TextureLibrary& library) {
            XmlDeserializer deserializer;
            const Path file = DemoRoot() / "config" / "TextureLibrary.xml";
            ASSERT_TRUE(deserializer.LoadFromFile(file)) << file.CStr();
            library.Load(deserializer);
        }

        void TearDown() override {
            GameManager::GetInstance().Shutdown();
            BECore::Tests::EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            CoreManager::GetFontAtlasLibrary().Clear();
            CoreManager::GetRendererManager().SetRenderer({});
            XmlDeserializer empty;
            if (empty.LoadFromString("<root/>")) {
                CoreManager::GetTextureLibrary().Load(empty);
            }
            CoreManager::GetTime().Reset();
        }

        /// One frame of the scene: the clock, the tweens, the update (the order of CoreManager::OnGameCycle)
        void Update() {
            CoreManager::GetTime().Advance(kFrame);
            CoreManager::GetTweens().Advance(CoreManager::GetTime().GetDeltaTime());
            _manager->UpdateAll();
        }

        void Draw() {
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        Scene& GetScene() const {
            return *_manager->GetSceneByName(DemoNames::scene);
        }

        SceneNode& Node(StringView name) const {
            if (SceneNode* found = BECore::Tests::FindNodeBelow(GetScene().GetRootNode(), name)) {
                return *found;
            }
            ADD_FAILURE() << "no node " << String(name).c_str();
            return GetScene().GetRootNode();
        }

        template <typename T>
        T& Component(StringView node) const {
            auto component = Node(node).GetComponent<T>();
            EXPECT_TRUE(component) << "no such component on " << String(node).c_str();
            return *component;
        }

        IntrusivePtr<BECore::Tests::RecordingRenderer> _renderer;
        IntrusivePtr<SceneManager> _manager;
    };

}  // namespace BECore::Demo::Tests
