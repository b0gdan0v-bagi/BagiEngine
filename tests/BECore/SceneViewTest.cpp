#include "SceneViewTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/IRenderTarget.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Generated/SceneViewTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    void ViewProbe::OnDraw() {
        ++drawCount;
        seen = CoreManager::GetRenderer()->GetViewTransform();
    }

    namespace {

        // Two scenes: one with a view size and a click target at (0, 0, 100, 100), one without a view size.
        constexpr const char* kViewScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Fit" viewWidth="400" viewHeight="200"><nodes>
    <node name="Button"><components>
      <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
      <component type="ViewProbe"/>
      <component type="ClickableComponent" actionName="Pressed"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Pixels"><nodes>
    <node name="Probe"><components>
      <component type="ViewProbe"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        /// Collects every ActionEvent that is emitted while it is alive.
        class ActionRecorder : public SubscriptionHolder {
        public:
            ActionRecorder() {
                Subscribe<InputEvents::ActionEvent, &ActionRecorder::OnAction>(this);
            }

            eastl::vector<PoolString> actions;

        private:
            void OnAction(const InputEvents::ActionEvent& e) {
                actions.push_back(e.action);
            }
        };

        void EmitLeftClick(float x, float y) {
            SDL_MouseButtonEvent button{};
            button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            button.button = SDL_BUTTON_LEFT;
            button.down = true;
            button.x = x;
            button.y = y;
            SDLEvents::MouseButtonDownEvent::Emit(button);
        }

    }  // namespace

    class SceneViewTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _renderer = BECore::New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kViewScenesXml));
            _manager = BECore::New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        void TearDown() override {
            // Destroying the scenes drops the components' event connections
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        Scene& GetScene(PoolString name) const {
            return *_manager->GetSceneByName(name);
        }

        IntrusivePtr<ViewProbe> GetProbe(PoolString sceneName) const {
            return GetScene(sceneName).GetRootNode().GetChildren().front()->GetComponent<ViewProbe>();
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(SceneViewTest, ViewSizeComesFromTheSceneConfig) {
        EXPECT_FLOAT_EQ(GetScene("Fit"_intern).GetViewWidth(), 400.0f);
        EXPECT_FLOAT_EQ(GetScene("Fit"_intern).GetViewHeight(), 200.0f);
        EXPECT_FLOAT_EQ(GetScene("Pixels"_intern).GetViewWidth(), 0.0f);
        EXPECT_FLOAT_EQ(GetScene("Pixels"_intern).GetViewHeight(), 0.0f);
    }

    TEST_F(SceneViewTest, DrawFitsTheViewToTheOutput) {
        _manager->DrawAll();

        const ViewTransform seen = GetProbe("Fit"_intern)->seen;
        EXPECT_EQ(GetProbe("Fit"_intern)->drawCount, 1);
        EXPECT_FLOAT_EQ(seen.scale, 2.0f);
        EXPECT_FLOAT_EQ(seen.offsetX, 0.0f);
        EXPECT_FLOAT_EQ(seen.offsetY, 100.0f);
    }

    TEST_F(SceneViewTest, TheFitDoesNotOutliveTheDrawPass) {
        _manager->DrawAll();

        EXPECT_FLOAT_EQ(_renderer->GetViewTransform().scale, 1.0f);
        EXPECT_FLOAT_EQ(_renderer->GetViewTransform().offsetX, 0.0f);
        EXPECT_FLOAT_EQ(_renderer->GetViewTransform().offsetY, 0.0f);
    }

    TEST_F(SceneViewTest, TheFitFollowsTheOutputSizeOfEveryFrame) {
        _manager->DrawAll();
        _renderer->width = 1600.0f;
        _renderer->height = 400.0f;
        _manager->DrawAll();

        const ViewTransform seen = GetProbe("Fit"_intern)->seen;
        EXPECT_FLOAT_EQ(seen.scale, 2.0f);
        EXPECT_FLOAT_EQ(seen.offsetX, 400.0f);
        EXPECT_FLOAT_EQ(seen.offsetY, 0.0f);
    }

    TEST_F(SceneViewTest, ASceneWithoutAViewSizeKeepsOneUnitPerPixel) {
        _manager->SetActiveScene("Pixels"_intern);
        _manager->DrawAll();

        const ViewTransform seen = GetProbe("Pixels"_intern)->seen;
        EXPECT_EQ(GetProbe("Pixels"_intern)->drawCount, 1);
        EXPECT_FLOAT_EQ(seen.scale, 1.0f);
        EXPECT_FLOAT_EQ(seen.offsetX, 0.0f);
        EXPECT_FLOAT_EQ(seen.offsetY, 0.0f);
    }

    TEST_F(SceneViewTest, ScreenToSceneIsTheIdentityBeforeTheFirstDraw) {
        EXPECT_FLOAT_EQ(GetScene("Fit"_intern).ScreenToSceneX(123.0f), 123.0f);
        EXPECT_FLOAT_EQ(GetScene("Fit"_intern).ScreenToSceneY(45.0f), 45.0f);
    }

    TEST_F(SceneViewTest, ScreenToSceneUndoesTheFitOfTheLastDraw) {
        _manager->DrawAll();

        // Output 800x600 shows the 400x200 view at 2x with a 100 px bar above: output (100, 200) is scene (50, 50).
        EXPECT_FLOAT_EQ(GetScene("Fit"_intern).ScreenToSceneX(100.0f), 50.0f);
        EXPECT_FLOAT_EQ(GetScene("Fit"_intern).ScreenToSceneY(200.0f), 50.0f);
    }

    TEST_F(SceneViewTest, ClickIsHitTestedInSceneUnits) {
        _manager->DrawAll();
        ActionRecorder recorder;

        // Output (10, 10) is scene (5, -45), above the button even though the raw position is inside (0, 0, 100, 100).
        EmitLeftClick(10.0f, 10.0f);
        EXPECT_TRUE(recorder.actions.empty());

        // Output (100, 150) is scene (50, 25), on the button even though the raw y is past its bottom edge.
        EmitLeftClick(100.0f, 150.0f);
        ASSERT_EQ(recorder.actions.size(), 1u);
        EXPECT_EQ(recorder.actions[0], "Pressed"_intern);
    }

}  // namespace BECore::Tests
