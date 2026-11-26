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
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIVars.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Common/SceneTestSupport.h>
#include <Common/UIInputEvents.h>
#include <Demo/DemoNames.h>
#include <Games/GameManager.h>
#include <gtest/gtest.h>

namespace BECore::Demo::Tests {

    /**
     * The shipped scene file of BagiDemo, with its sprite library, in a scene manager on a RecordingRenderer, the DemoGame active. The demo's
     * assets are not mounted in the test environment and the renderer loads no texture from a path: a sprite draws only with a texture a case
     * gives it.
     */
    class DemoSceneFixture : public ::testing::Test {
    protected:
        static constexpr double kFrame = 1.0 / 32.0;  // a power of two: the sums of frame times are exact
        static constexpr uint32_t kAtlasWidth = 256;
        static constexpr uint32_t kAtlasHeight = 128;
        static constexpr size_t kBytesPerPixel = 4;
        /// The output is the scene's view, so a canvas unit and a scene unit are a pixel
        static constexpr float kOutputWidth = 1280.0f;
        static constexpr float kOutputHeight = 720.0f;

        static void SetUpTestSuite() {
            BECore::Tests::EngineEnvironment::EnsureCoreServices();
        }

        /// The demo's folder in the source tree. Not through `__FILE__`, which is relative to the build folder under a ccache base_dir
        static Path DemoRoot() {
            return Path(BagiContentRoots::kEngine) / "tools" / "demo";
        }

        void LoadScene() {
            _renderer = New<BECore::Tests::RecordingRenderer>();
            _renderer->width = kOutputWidth;
            _renderer->height = kOutputHeight;
            CoreManager::GetUIVars().Clear();
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
            CoreManager::GetUIVars().Clear();
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

        UICanvasComponent& Panel() const {
            return Component<UICanvasComponent>("Panel");
        }

        UIElement& Element(const char* name) const {
            UIElement* element = Panel().GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << "no element " << name << " on the panel";
            return element ? *element : Panel().GetRoot();
        }

        /// A left click on the panel's element @p name, @p at of its width from the left, through UIInput as a mouse's; draws first, so the layout is current
        void Click(const char* name, float at = 0.5f) {
            Draw();
            const Rect& rect = Element(name).rect;
            ASSERT_FALSE(rect.IsEmpty()) << name;
            const Rect target = Panel().GetLayout().GetResolved().transform.ToOutput(rect.x, rect.y, rect.w, rect.h);
            // Half a pixel inside the edges, so that `at` of 0 and 1 still hit the element
            const float x = std::clamp(target.x + target.w * at, target.x + 0.5f, target.x + target.w - 0.5f);
            const float y = target.y + target.h * 0.5f;
            BECore::Tests::PressLeftAt(x, y);
            CoreManager::GetUIInput().DispatchMouseButtonUp(BECore::Tests::Button(x, y, false));
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
