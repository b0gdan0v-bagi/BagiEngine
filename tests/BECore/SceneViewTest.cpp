#include "SceneViewTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
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
        if (const IntrusivePtr<IRenderer> renderer = CoreManager::GetRenderer()) {
            seen = renderer->GetViewTransform();
        }
        seenView = GetScene().GetActiveView();
        seenVersion = GetScene().GetViewVersion();
    }

    namespace {

        // Three scenes: one with a view size and a click target at (0, 0, 100, 100), one without a view size, and one with view variants
        // (a portrait and a wide one, in this order) over a default of 1280 x 720.
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
  <scene type="Scene" name="Responsive" viewWidth="1280" viewHeight="720">
    <viewVariants>
      <view name="Portrait" when="orientation:portrait" width="720" height="1280"/>
      <view name="Wide" when="minAspect:2" width="1600" height="600"/>
    </viewVariants>
    <nodes>
      <node name="Probe"><components>
        <component type="ViewProbe"/>
      </components></node>
    </nodes>
  </scene>
</scenes></SceneManager>)";

        /// Collects every ActionEvent that is emitted while it is alive.
        class ActionRecorder : public SubscriptionHolder {
        public:
            ActionRecorder() {
                Subscribe<InputEvents::ActionEvent, &ActionRecorder::OnAction>(this);
            }

            Vector<PoolString> actions;

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

    TEST_F(SceneViewTest, TheViewTransformIsTheIdentityBeforeTheFirstDrawAndTheFitAfterIt) {
        const ViewTransform before = GetScene("Fit"_intern).GetViewTransform();
        EXPECT_FLOAT_EQ(before.scale, 1.0f);
        EXPECT_FLOAT_EQ(before.offsetX, 0.0f);
        EXPECT_FLOAT_EQ(before.offsetY, 0.0f);

        _manager->DrawAll();

        // The 400x200 view at 2x with a 100 px bar above (output 800x600): a scene rect takes the same way to output pixels
        const ViewTransform fit = GetScene("Fit"_intern).GetViewTransform();
        EXPECT_FLOAT_EQ(fit.scale, 2.0f);
        EXPECT_FLOAT_EQ(fit.offsetY, 100.0f);
        const Rect output = fit.ToOutput(50.0f, 50.0f, 10.0f, 20.0f);
        EXPECT_FLOAT_EQ(output.x, 100.0f);
        EXPECT_FLOAT_EQ(output.y, 200.0f);
        EXPECT_FLOAT_EQ(output.w, 20.0f);
        EXPECT_FLOAT_EQ(output.h, 40.0f);
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

    // The view variants of a scene (the "Responsive" scene: default 1280 x 720, Portrait 720 x 1280, Wide 1600 x 600)

    class SceneViewVariantsTest : public SceneViewTest {
    protected:
        void SetUp() override {
            SceneViewTest::SetUp();
            ASSERT_NO_FATAL_FAILURE(UseResponsive());
        }

        void UseResponsive() {
            _manager->SetActiveScene("Responsive"_intern);
            _scene = _manager->GetSceneByName("Responsive"_intern);
            ASSERT_NE(_scene, nullptr);
        }

        void DrawAt(float width, float height) {
            _renderer->width = width;
            _renderer->height = height;
            _manager->DrawAll();
        }

        Scene* _scene = nullptr;
    };

    TEST_F(SceneViewVariantsTest, BeforeTheFirstDrawTheViewIsTheDefault) {
        const ActiveSceneView view = _scene->GetActiveView();
        EXPECT_EQ(view.index, -1);
        EXPECT_TRUE(view.name.Empty());
        EXPECT_FLOAT_EQ(view.width, 1280.0f);
        EXPECT_FLOAT_EQ(view.height, 720.0f);
        EXPECT_FLOAT_EQ(_scene->GetViewWidth(), 1280.0f);
        EXPECT_FLOAT_EQ(_scene->GetViewHeight(), 720.0f);
        EXPECT_EQ(_scene->GetViewVersion(), 0u);
    }

    TEST_F(SceneViewVariantsTest, ALandscapeOutputFitsTheDefaultView) {
        DrawAt(900.0f, 600.0f);

        const ViewTransform expected = ViewTransform::Fit(1280.0f, 720.0f, 900.0f, 600.0f);
        const ViewTransform seen = GetProbe("Responsive"_intern)->seen;
        EXPECT_FLOAT_EQ(seen.scale, expected.scale);
        EXPECT_FLOAT_EQ(seen.offsetX, expected.offsetX);
        EXPECT_FLOAT_EQ(seen.offsetY, expected.offsetY);
        EXPECT_EQ(_scene->GetActiveView().index, -1);
        EXPECT_EQ(_scene->GetViewVersion(), 0u);
    }

    TEST_F(SceneViewVariantsTest, APortraitOutputFitsThePortraitView) {
        DrawAt(600.0f, 900.0f);

        const ViewTransform expected = ViewTransform::Fit(720.0f, 1280.0f, 600.0f, 900.0f);
        const ViewTransform seen = GetProbe("Responsive"_intern)->seen;
        EXPECT_FLOAT_EQ(seen.scale, expected.scale);
        EXPECT_FLOAT_EQ(seen.offsetX, expected.offsetX);
        EXPECT_FLOAT_EQ(seen.offsetY, expected.offsetY);

        const ActiveSceneView view = _scene->GetActiveView();
        EXPECT_EQ(view.index, 0);
        EXPECT_EQ(view.name, "Portrait"_intern);
        EXPECT_FLOAT_EQ(_scene->GetViewWidth(), 720.0f);
        EXPECT_FLOAT_EQ(_scene->GetViewHeight(), 1280.0f);
    }

    TEST_F(SceneViewVariantsTest, TheViewIsRightAlreadyInsideOnDraw) {
        DrawAt(600.0f, 900.0f);

        const IntrusivePtr<ViewProbe> probe = GetProbe("Responsive"_intern);
        EXPECT_EQ(probe->seenView.index, 0);
        EXPECT_EQ(probe->seenView.name, "Portrait"_intern);
        EXPECT_EQ(probe->seenVersion, 1u);
    }

    TEST_F(SceneViewVariantsTest, TheVersionMovesOnlyWhenTheChoiceChanges) {
        DrawAt(900.0f, 600.0f);
        EXPECT_EQ(_scene->GetViewVersion(), 0u);

        DrawAt(600.0f, 900.0f);
        EXPECT_EQ(_scene->GetViewVersion(), 1u);

        // The same variant at another size, and the same size again: the same choice
        DrawAt(500.0f, 800.0f);
        DrawAt(500.0f, 800.0f);
        EXPECT_EQ(_scene->GetViewVersion(), 1u);
        EXPECT_FLOAT_EQ(GetProbe("Responsive"_intern)->seen.scale, ViewTransform::Fit(720.0f, 1280.0f, 500.0f, 800.0f).scale);

        // Portrait to Wide, and back to the default
        DrawAt(1200.0f, 400.0f);
        EXPECT_EQ(_scene->GetViewVersion(), 2u);
        EXPECT_EQ(_scene->GetActiveView().name, "Wide"_intern);
        EXPECT_FLOAT_EQ(_scene->GetViewWidth(), 1600.0f);
        DrawAt(900.0f, 600.0f);
        EXPECT_EQ(_scene->GetViewVersion(), 3u);
        EXPECT_EQ(_scene->GetActiveView().index, -1);
    }

    TEST_F(SceneViewVariantsTest, ScreenToSceneFollowsTheViewOfTheLastDraw) {
        DrawAt(600.0f, 900.0f);
        const ViewTransform fit = ViewTransform::Fit(720.0f, 1280.0f, 600.0f, 900.0f);
        EXPECT_FLOAT_EQ(_scene->ScreenToSceneX(300.0f), (300.0f - fit.offsetX) / fit.scale);
        EXPECT_FLOAT_EQ(_scene->ScreenToSceneY(450.0f), (450.0f - fit.offsetY) / fit.scale);

        DrawAt(900.0f, 600.0f);
        const ViewTransform landscape = ViewTransform::Fit(1280.0f, 720.0f, 900.0f, 600.0f);
        EXPECT_FLOAT_EQ(_scene->ScreenToSceneX(300.0f), (300.0f - landscape.offsetX) / landscape.scale);
        EXPECT_FLOAT_EQ(_scene->ScreenToSceneY(450.0f), (450.0f - landscape.offsetY) / landscape.scale);
    }

    TEST_F(SceneViewVariantsTest, ASceneWithoutVariantsNeverChangesItsView) {
        _manager->SetActiveScene("Fit"_intern);
        const Scene& fit = GetScene("Fit"_intern);

        _manager->DrawAll();
        _renderer->width = 300.0f;
        _renderer->height = 900.0f;
        _manager->DrawAll();

        EXPECT_EQ(fit.GetViewVersion(), 0u);
        EXPECT_EQ(fit.GetActiveView().index, -1);
        EXPECT_FLOAT_EQ(fit.GetViewWidth(), 400.0f);
        EXPECT_FLOAT_EQ(fit.GetViewHeight(), 200.0f);
        EXPECT_FLOAT_EQ(GetProbe("Fit"_intern)->seen.scale, 0.75f);
    }

    // The override a tool (the editor viewport) sets around one pass

    TEST_F(SceneViewVariantsTest, TheVariantsAreListedInTheOrderOfTheFile) {
        const Span<const SceneViewVariant> variants = _scene->GetViewVariants();
        ASSERT_EQ(variants.size(), 2u);
        EXPECT_EQ(variants[0].name, "Portrait"_intern);
        EXPECT_EQ(variants[1].name, "Wide"_intern);
        EXPECT_TRUE(GetScene("Fit"_intern).GetViewVariants().empty());
    }

    TEST_F(SceneViewVariantsTest, AnOverrideForcesAVariantWhereTheOutputWouldPickTheDefault) {
        _scene->SetViewOverride(0);
        EXPECT_EQ(_scene->GetViewVersion(), 0u) << "setting the override does not pick";

        DrawAt(900.0f, 600.0f);
        EXPECT_EQ(_scene->GetActiveView().index, 0);
        EXPECT_EQ(_scene->GetActiveView().name, "Portrait"_intern);
        EXPECT_FLOAT_EQ(_scene->GetViewWidth(), 720.0f);
        const ViewTransform expected = ViewTransform::Fit(720.0f, 1280.0f, 900.0f, 600.0f);
        EXPECT_FLOAT_EQ(GetProbe("Responsive"_intern)->seen.scale, expected.scale);
        EXPECT_EQ(GetProbe("Responsive"_intern)->seenView.index, 0) << "right already inside OnDraw()";
    }

    TEST_F(SceneViewVariantsTest, AnOverrideOfTheDefaultViewBeatsAVariantTheOutputWouldPick) {
        _scene->SetViewOverride(-1);
        DrawAt(600.0f, 900.0f);

        EXPECT_EQ(_scene->GetActiveView().index, -1);
        EXPECT_FLOAT_EQ(_scene->GetViewWidth(), 1280.0f);
        EXPECT_FLOAT_EQ(GetProbe("Responsive"_intern)->seen.scale, ViewTransform::Fit(1280.0f, 720.0f, 600.0f, 900.0f).scale);
    }

    TEST_F(SceneViewVariantsTest, TheVersionMovesOnceForAForcedChangeAndOnceWhenItIsCleared) {
        DrawAt(900.0f, 600.0f);
        ASSERT_EQ(_scene->GetViewVersion(), 0u);

        _scene->SetViewOverride(1);
        DrawAt(900.0f, 600.0f);
        DrawAt(900.0f, 600.0f);
        EXPECT_EQ(_scene->GetViewVersion(), 1u);
        EXPECT_EQ(_scene->GetActiveView().name, "Wide"_intern);

        _scene->SetViewOverride(std::nullopt);
        DrawAt(900.0f, 600.0f);
        DrawAt(900.0f, 600.0f);
        EXPECT_EQ(_scene->GetViewVersion(), 2u);
        EXPECT_EQ(_scene->GetActiveView().index, -1);
    }

    TEST_F(SceneViewVariantsTest, NulloptAndAnIndexOutOfRangeBehaveAsAuto) {
        for (const Optional<int32_t> override : {Optional<int32_t>{}, Optional<int32_t>{2}, Optional<int32_t>{-2}, Optional<int32_t>{100}}) {
            _scene->SetViewOverride(override);
            DrawAt(900.0f, 600.0f);
            EXPECT_EQ(_scene->GetActiveView().index, -1);
            DrawAt(600.0f, 900.0f);
            EXPECT_EQ(_scene->GetActiveView().index, 0);
        }
        EXPECT_EQ(_scene->GetViewVersion(), 7u) << "only the switches of the output moved it, the overrides themselves never did";
    }

    TEST_F(SceneViewVariantsTest, ScreenToSceneFollowsTheForcedView) {
        _scene->SetViewOverride(0);
        DrawAt(900.0f, 600.0f);

        const ViewTransform fit = ViewTransform::Fit(720.0f, 1280.0f, 900.0f, 600.0f);
        EXPECT_FLOAT_EQ(_scene->ScreenToSceneX(300.0f), (300.0f - fit.offsetX) / fit.scale);
        EXPECT_FLOAT_EQ(_scene->ScreenToSceneY(450.0f), (450.0f - fit.offsetY) / fit.scale);
    }

    TEST_F(SceneViewVariantsTest, ASceneWithoutVariantsIgnoresAVariantIndex) {
        _manager->SetActiveScene("Fit"_intern);
        Scene& fit = *_manager->GetSceneByName("Fit"_intern);

        fit.SetViewOverride(0);
        _manager->DrawAll();

        EXPECT_EQ(fit.GetActiveView().index, -1);
        EXPECT_EQ(fit.GetViewVersion(), 0u);
        EXPECT_FLOAT_EQ(fit.GetViewWidth(), 400.0f);
    }

    TEST_F(SceneViewVariantsTest, TheOverrideIsNotSaved) {
        XmlSerializer plain;
        _scene->Serialize(plain);
        const String before = plain.SaveToString();

        _scene->SetViewOverride(1);
        DrawAt(900.0f, 600.0f);
        XmlSerializer forced;
        _scene->Serialize(forced);
        EXPECT_EQ(forced.SaveToString(), before);
    }

    TEST_F(SceneViewVariantsTest, WithoutARendererTheViewIsTheDefault) {
        DrawAt(600.0f, 900.0f);
        ASSERT_EQ(_scene->GetActiveView().index, 0);

        CoreManager::GetRendererManager().SetRenderer({});
        _manager->DrawAll();

        EXPECT_EQ(_scene->GetActiveView().index, -1);
        EXPECT_FLOAT_EQ(_scene->GetViewWidth(), 1280.0f);
    }

}  // namespace BECore::Tests
