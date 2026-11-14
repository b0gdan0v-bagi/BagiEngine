#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/UILayoutMatrix.h>
#include <CoreSDL/SDLEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // A scene laid out the way a game with two layouts does it: the default view is landscape, a Portrait view stands for the tall outputs, one SceneView canvas
        // extends to the output. "Board" lies inside both views, "Bar" is anchored to the bottom of the output and is shown by a canvas variant only where a bar exists
        // (a very tall output); "NoVariant" forgets to hide it, so it reaches into the view of the wide outputs.
        constexpr const char* kTwoLayoutsXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Table" viewWidth="1280" viewHeight="720">
    <viewVariants><view name="Portrait" when="orientation:portrait" width="720" height="1280"/></viewVariants>
    <nodes>
      <node name="Layout"><components>
        <component type="UICanvasComponent" space="SceneView" extendToOutput="true">
          <elements>
            <element type="UIPanel" name="Board" x="20" y="20" width="200" height="200" color="9,9,9,255" blocksInput="false"/>
            <element type="UIElement" name="Bar" anchor="Bottom" x="0" y="-20" width="300" height="100" visible="false" blocksInput="true" color="40,90,60,255">
              <components>
                <component type="UIInputClicker"/>
              </components>
            </element>
          </elements>
          <variants>
            <variant when="maxAspect:0.5"><overrides><set element="Bar" visible="true"/></overrides></variant>
          </variants>
        </component>
      </components></node>
    </nodes>
  </scene>
  <scene type="Scene" name="NoVariant" viewWidth="1280" viewHeight="720">
    <viewVariants><view name="Portrait" when="orientation:portrait" width="720" height="1280"/></viewVariants>
    <nodes>
      <node name="Layout"><components>
        <component type="UICanvasComponent" space="SceneView" extendToOutput="true">
          <elements>
            <element type="UIPanel" name="Board" x="20" y="20" width="200" height="200" color="9,9,9,255" blocksInput="false"/>
            <element type="UIElement" name="Bar" anchor="Bottom" x="0" y="-20" width="300" height="100" blocksInput="true" color="40,90,60,255">
              <components>
                <component type="UIInputClicker"/>
              </components>
            </element>
          </elements>
        </component>
      </components></node>
    </nodes>
  </scene>
</scenes></SceneManager>)";

        constexpr float kViewWidth = 1280.0f;
        constexpr float kViewHeight = 720.0f;

        SDL_MouseButtonEvent Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

    }  // namespace

    class SceneViewEndToEndTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _renderer = New<RecordingRenderer>();
            _renderer->width = 1280.0f;
            _renderer->height = 720.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kTwoLayoutsXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        Scene& GetScene(const char* name) const {
            return *_manager->GetSceneByName(PoolString::Intern(name));
        }

        IntrusivePtr<UICanvasComponent> Canvas(const char* sceneName) const {
            return GetScene(sceneName).GetRootNode().GetChildren()[0]->GetComponent<UICanvasComponent>();
        }

        void Draw(const char* sceneName) {
            _manager->SetActiveScene(PoolString::Intern(sceneName));
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        /// The matrix over the views the scene declares, with the bar row kept off the view
        Vector<UIMatrixFailure> RunMatrix(const char* sceneName) {
            Draw(sceneName);  // the canvas takes its parameters from the component on a draw
            const Scene& scene = GetScene(sceneName);
            UIMatrixViews views{kViewWidth, kViewHeight, scene.GetViewVariants()};
            UILayoutValidatorOptions options;
            options.keepOffView.push_back("Bar"_intern);
            UILayoutContext base;
            base.sceneViewWidth = kViewWidth;
            base.sceneViewHeight = kViewHeight;
            return ValidateOnMatrix(Canvas(sceneName)->GetLayout(), options, base, views);
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(SceneViewEndToEndTest, ATwoLayoutSceneKeepsItsBarRowOffTheViewOnTheWholeMatrix) {
        const auto failures = RunMatrix("Table");

        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(SceneViewEndToEndTest, TheSameSceneWithoutTheVariantThatHidesTheBarRowOverlapsTheView) {
        const auto failures = RunMatrix("NoVariant");

        ASSERT_FALSE(failures.empty());
        for (const UIMatrixFailure& failure : failures) {
            ASSERT_FALSE(failure.issues.empty());
            EXPECT_EQ(failure.issues[0].kind, UILayoutIssueKind::OverlapsSceneView) << failure.label.c_str();
            EXPECT_EQ(failure.issues[0].element, "Bar"_intern);
        }
    }

    TEST_F(SceneViewEndToEndTest, ThePortraitViewIsPickedAndTheBarRowIsClickableOnTheBar) {
        _renderer->width = 720.0f;  // 720 x 1280 fits the width at scale 1: bars of 160 pixels above and below
        _renderer->height = 1600.0f;
        Draw("Table");

        const Scene& scene = GetScene("Table");
        EXPECT_NEAR(scene.GetActiveView().width, 720.0f, 1e-3f);
        EXPECT_NEAR(scene.GetActiveView().height, 1280.0f, 1e-3f);

        // The bar row: y 1320..1420 in the canvas (bounds start at -160), so pixels 1480..1580, below the view that ends at pixel 1440
        UIInput& input = CoreManager::GetUIInput();
        EXPECT_TRUE(input.DispatchMouseButtonDown(Button(360.0f, 1530.0f, true)));
        input.DispatchMouseButtonUp(Button(360.0f, 1530.0f, false));
        EXPECT_FALSE(input.DispatchMouseButtonDown(Button(100.0f, 1530.0f, true))) << "beside the row the bar is empty";
        input.DispatchMouseButtonUp(Button(100.0f, 1530.0f, false));
    }

}  // namespace BECore::Tests
