#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Widgets/SceneEditor/ScenePick.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;

        // "Main" has no view size: a scene unit is an output pixel. Back and Front overlap (tree order); High is on layer 1 and overlaps Low, declared later on layer 0;
        // Under (layer 1), the canvas Hud (layer 3) and Over (layer 5) overlap at the Panel of the canvas; Turned is a strip turned by a quarter; Plain has a transform only;
        // Both is a node with a transform and a canvas that has no elements. "Fit" shows a 400 x 200 view in the 800 x 600 output: scale 2, a bar of 100 pixels above and below.
        constexpr const char* kPickScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Back"><components>
      <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
      <component type="QuadRendererComponent"/>
    </components></node>
    <node name="Front"><components>
      <component type="TransformComponent" x="50" y="50" width="100" height="100"/>
      <component type="QuadRendererComponent"/>
    </components></node>
    <node name="High" layer="1"><components>
      <component type="TransformComponent" x="500" y="0" width="100" height="100"/>
      <component type="QuadRendererComponent"/>
    </components></node>
    <node name="Low" layer="0"><components>
      <component type="TransformComponent" x="520" y="20" width="100" height="100"/>
      <component type="QuadRendererComponent"/>
    </components></node>
    <node name="Under" layer="1"><components>
      <component type="TransformComponent" x="0" y="300" width="200" height="100"/>
      <component type="QuadRendererComponent"/>
    </components></node>
    <node name="Hud" layer="3"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
    </components></node>
    <node name="Over" layer="5"><components>
      <component type="TransformComponent" x="150" y="300" width="100" height="100"/>
      <component type="QuadRendererComponent"/>
    </components></node>
    <node name="Turned"><components>
      <component type="TransformComponent" x="700" y="200" width="200" height="20" rotation="90"/>
      <component type="QuadRendererComponent"/>
    </components></node>
    <node name="Plain"><components>
      <component type="TransformComponent" x="0" y="500" width="100" height="100"/>
    </components></node>
    <node name="Both"><components>
      <component type="TransformComponent" x="600" y="400" width="100" height="100"/>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
    </components></node>
    <node name="Ghost"><components>
      <component type="TransformComponent" x="300" y="500" width="50" height="50"/>
      <component type="QuadRendererComponent"/>
    </components><children>
      <node name="GhostChild"><components>
        <component type="TransformComponent" x="350" y="500" width="50" height="50"/>
        <component type="QuadRendererComponent"/>
      </components></node>
    </children></node>
  </nodes></scene>
  <scene type="Scene" name="Fit" viewWidth="400" viewHeight="200"><nodes>
    <node name="Box"><components>
      <component type="TransformComponent" x="50" y="50" width="100" height="100"/>
      <component type="QuadRendererComponent"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        SceneNode* Find(SceneNode& from, const char* name) {
            const PoolString wanted = PoolString::Intern(name);
            for (const auto& child : from.GetChildren()) {
                if (child->GetName() == wanted) {
                    return child.Get();
                }
                if (SceneNode* found = Find(*child, name)) {
                    return found;
                }
            }
            return nullptr;
        }

        void ExpectCorner(const Vec2& corner, float x, float y) {
            EXPECT_NEAR(corner.x, x, kTolerance);
            EXPECT_NEAR(corner.y, y, kTolerance);
        }

    }  // namespace

    /// The pick of the editor on scenes loaded from a string, drawn once so that the canvases have a layout and the scene its fit
    class ScenePickTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _renderer = New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kPickScenesXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();

            _hud = Node("Main", "Hud").GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_hud);
            _panel = _hud->GetRoot().AddChild("Panel"_intern);  // under Hud's rank: in front of Under, behind Over
            _panel->style.rect.x = UILength::Px(0.0f);
            _panel->style.rect.y = UILength::Px(300.0f);
            _panel->style.rect.width = UILength::Px(200.0f);
            _panel->style.rect.height = UILength::Px(100.0f);
            _panel->color = Color{10, 20, 30, 255};
            _bare = _hud->GetRoot().AddChild("Bare"_intern);  // transparent and without components: not pickable
            _bare->style.rect.x = UILength::Px(300.0f);
            _bare->style.rect.y = UILength::Px(300.0f);
            _bare->style.rect.width = UILength::Px(100.0f);
            _bare->style.rect.height = UILength::Px(100.0f);
            _hud->Invalidate();

            Draw("Main");
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        void Draw(const char* sceneName) {
            _manager->SetActiveScene(PoolString::Intern(sceneName));
            _manager->DrawAll();
        }

        Scene& GetScene(const char* name) const {
            return *_manager->GetSceneByName(PoolString::Intern(name));
        }

        SceneNode& Node(const char* sceneName, const char* name) const {
            SceneNode* node = Find(GetScene(sceneName).GetRootNode(), name);
            EXPECT_NE(node, nullptr) << name;
            return *node;
        }

        ScenePick::Hit Pick(float x, float y) {
            return ScenePick::PickAt(GetScene("Main"), x, y);
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _hud;
        IntrusivePtr<UIElement> _panel;
        IntrusivePtr<UIElement> _bare;
    };

    // ---- PointInRotatedRect -----------------------------------------------------------------------------------------------

    TEST(ScenePickRotatedRectTest, AnUnturnedRectHasItsLeftAndTopEdgesOnly) {
        const Rect rect{10.0f, 20.0f, 30.0f, 40.0f};

        EXPECT_TRUE(ScenePick::PointInRotatedRect(rect, 0.0f, 0.5f, 0.5f, 10.0f, 20.0f));
        EXPECT_TRUE(ScenePick::PointInRotatedRect(rect, 0.0f, 0.5f, 0.5f, 39.9f, 59.9f));
        EXPECT_FALSE(ScenePick::PointInRotatedRect(rect, 0.0f, 0.5f, 0.5f, 40.0f, 30.0f));
        EXPECT_FALSE(ScenePick::PointInRotatedRect(rect, 0.0f, 0.5f, 0.5f, 25.0f, 60.0f));
        EXPECT_FALSE(ScenePick::PointInRotatedRect(rect, 0.0f, 0.5f, 0.5f, 9.9f, 30.0f));
    }

    TEST(ScenePickRotatedRectTest, AQuarterTurnAboutTheCenterSwapsTheSides) {
        const Rect strip{0.0f, 0.0f, 200.0f, 20.0f};  // the center is (100, 10); turned, it stands 20 wide and 200 high

        EXPECT_TRUE(ScenePick::PointInRotatedRect(strip, 90.0f, 0.5f, 0.5f, 100.0f, 100.0f));
        EXPECT_TRUE(ScenePick::PointInRotatedRect(strip, 90.0f, 0.5f, 0.5f, 100.0f, -85.0f));
        EXPECT_FALSE(ScenePick::PointInRotatedRect(strip, 90.0f, 0.5f, 0.5f, 190.0f, 10.0f));  // inside the strip before the turn
        EXPECT_FALSE(ScenePick::PointInRotatedRect(strip, 90.0f, 0.5f, 0.5f, 100.0f, 120.0f));
    }

    TEST(ScenePickRotatedRectTest, TheTurnIsClockwiseAboutThePivot) {
        const Rect rect{0.0f, 0.0f, 100.0f, 20.0f};

        // About the left end, clockwise on a screen with y down: the strip points down
        EXPECT_TRUE(ScenePick::PointInRotatedRect(rect, 90.0f, 0.0f, 0.0f, -10.0f, 80.0f));
        EXPECT_FALSE(ScenePick::PointInRotatedRect(rect, 90.0f, 0.0f, 0.0f, -10.0f, -80.0f));
        EXPECT_FALSE(ScenePick::PointInRotatedRect(rect, 90.0f, 0.0f, 0.0f, 80.0f, 10.0f));
    }

    // ---- PickAt -----------------------------------------------------------------------------------------------------------

    TEST_F(ScenePickTest, ALaterNodeOfTheSameLayerIsOnTop) {
        EXPECT_EQ(Pick(75.0f, 75.0f).node, &Node("Main", "Front"));
        EXPECT_EQ(Pick(20.0f, 20.0f).node, &Node("Main", "Back"));
        EXPECT_EQ(Pick(140.0f, 140.0f).node, &Node("Main", "Front"));
        EXPECT_EQ(Pick(75.0f, 75.0f).element, nullptr);
    }

    TEST_F(ScenePickTest, AHigherLayerIsOnTopWhateverTheTreeOrder) {
        EXPECT_EQ(Pick(530.0f, 30.0f).node, &Node("Main", "High"));
        EXPECT_EQ(Pick(610.0f, 110.0f).node, &Node("Main", "Low"));
    }

    TEST_F(ScenePickTest, ACanvasNodeIsAboveAQuadOfALowerLayerAndBelowOneOfAHigherLayer) {
        const ScenePick::Hit panel = Pick(100.0f, 350.0f);
        EXPECT_EQ(panel.node, &Node("Main", "Hud"));  // Under is on layer 1, Hud on 3
        EXPECT_EQ(panel.element, _panel.Get());

        const ScenePick::Hit over = Pick(175.0f, 350.0f);
        EXPECT_EQ(over.node, &Node("Main", "Over"));  // layer 5, over the panel too
        EXPECT_EQ(over.element, nullptr);

        EXPECT_EQ(Pick(220.0f, 350.0f).node, &Node("Main", "Over"));
    }

    TEST_F(ScenePickTest, AnElementThatIsTransparentAndWithoutComponentsIsNotPicked) {
        EXPECT_EQ(Pick(350.0f, 350.0f).node, nullptr);
        EXPECT_EQ(Pick(350.0f, 350.0f).element, nullptr);
    }

    TEST_F(ScenePickTest, ANodeWithACanvasWithoutAnElementThereFallsBackToItsOwnRect) {
        const ScenePick::Hit hit = Pick(650.0f, 450.0f);

        EXPECT_EQ(hit.node, &Node("Main", "Both"));
        EXPECT_EQ(hit.element, nullptr);
    }

    TEST_F(ScenePickTest, AClickOnNothingGivesAnEmptyHit) {
        const ScenePick::Hit hit = Pick(790.0f, 590.0f);

        EXPECT_EQ(hit.node, nullptr);
        EXPECT_EQ(hit.element, nullptr);
    }

    TEST_F(ScenePickTest, ANodeWithATransformOnlyIsNotPicked) {
        EXPECT_EQ(Pick(50.0f, 550.0f).node, nullptr);
    }

    TEST_F(ScenePickTest, AHiddenNodeAndTheNodesUnderItAreSkipped) {
        ASSERT_EQ(Pick(320.0f, 520.0f).node, &Node("Main", "Ghost"));
        ASSERT_EQ(Pick(370.0f, 520.0f).node, &Node("Main", "GhostChild"));

        Node("Main", "Ghost").SetEditorFlags(EditorFlags::Hidden);

        EXPECT_EQ(Pick(320.0f, 520.0f).node, nullptr);
        EXPECT_EQ(Pick(370.0f, 520.0f).node, nullptr);
    }

    TEST_F(ScenePickTest, ALockedNodeCanBePicked) {
        Node("Main", "Back").SetEditorFlags(EditorFlags::Locked);

        EXPECT_EQ(Pick(20.0f, 20.0f).node, &Node("Main", "Back"));
    }

    TEST_F(ScenePickTest, AnInvisibleCanvasAndAHiddenCanvasNodeAreNotPicked) {
        _hud->_visible = false;
        EXPECT_EQ(Pick(100.0f, 350.0f).node, &Node("Main", "Under"));  // the canvas draws nothing, so the quad below shows

        _hud->_visible = true;
        Node("Main", "Hud").SetEditorFlags(EditorFlags::Hidden);
        EXPECT_EQ(Pick(100.0f, 350.0f).node, &Node("Main", "Under"));
    }

    TEST_F(ScenePickTest, ARotatedNodeIsHitInsideItsTurnedRectAndMissedWhereItLeft) {
        EXPECT_EQ(Pick(800.0f, 120.0f).node, &Node("Main", "Turned"));  // 790..810 wide, 110..310 high after the turn
        EXPECT_EQ(Pick(800.0f, 300.0f).node, &Node("Main", "Turned"));
        EXPECT_EQ(Pick(890.0f, 210.0f).node, nullptr);  // the end of the strip before the turn
        EXPECT_EQ(Pick(710.0f, 210.0f).node, nullptr);
    }

    TEST_F(ScenePickTest, ThePointMapsThroughTheViewTransformOfTheLastDraw) {
        Draw("Fit");
        Scene& fit = GetScene("Fit");
        ASSERT_NEAR(fit.GetViewTransform().scale, 2.0f, kTolerance);  // the box is at scene 50..150 x 50..150, output 100..300 x 200..400

        EXPECT_EQ(ScenePick::PickAt(fit, 150.0f, 250.0f).node, &Node("Fit", "Box"));
        EXPECT_EQ(ScenePick::PickAt(fit, 299.0f, 399.0f).node, &Node("Fit", "Box"));
        EXPECT_EQ(ScenePick::PickAt(fit, 60.0f, 60.0f).node, nullptr);  // the scene point (30, -20), in the bar
        EXPECT_EQ(ScenePick::PickAt(fit, 310.0f, 250.0f).node, nullptr);
    }

    // ---- SelectionOutlines ------------------------------------------------------------------------------------------------

    TEST_F(ScenePickTest, ATransformNodeIsOutlinedByItsRectInOutputPixels) {
        Draw("Fit");

        const ScenePick::Outlines outlines = ScenePick::SelectionOutlines(GetScene("Fit"), Node("Fit", "Box"));

        ASSERT_EQ(outlines.count, 1u);
        ExpectCorner(outlines.quads[0][0], 100.0f, 200.0f);
        ExpectCorner(outlines.quads[0][1], 300.0f, 200.0f);
        ExpectCorner(outlines.quads[0][2], 300.0f, 400.0f);
        ExpectCorner(outlines.quads[0][3], 100.0f, 400.0f);
    }

    TEST_F(ScenePickTest, ARotatedNodeIsOutlinedByItsTurnedCorners) {
        const ScenePick::Outlines outlines = ScenePick::SelectionOutlines(GetScene("Main"), Node("Main", "Turned"));

        ASSERT_EQ(outlines.count, 1u);
        ExpectCorner(outlines.quads[0][0], 810.0f, 110.0f);
        ExpectCorner(outlines.quads[0][1], 810.0f, 310.0f);
        ExpectCorner(outlines.quads[0][2], 790.0f, 310.0f);
        ExpectCorner(outlines.quads[0][3], 790.0f, 110.0f);
    }

    TEST_F(ScenePickTest, ACanvasNodeIsOutlinedByTheBoundsOfItsCanvas) {
        const ScenePick::Outlines outlines = ScenePick::SelectionOutlines(GetScene("Main"), Node("Main", "Hud"));

        ASSERT_EQ(outlines.count, 1u);  // Hud has no transform
        ExpectCorner(outlines.quads[0][0], 0.0f, 0.0f);
        ExpectCorner(outlines.quads[0][2], _renderer->width, _renderer->height);
    }

    TEST_F(ScenePickTest, ANodeWithATransformAndACanvasHasBothOutlines) {
        const ScenePick::Outlines outlines = ScenePick::SelectionOutlines(GetScene("Main"), Node("Main", "Both"));

        ASSERT_EQ(outlines.count, 2u);
        ExpectCorner(outlines.quads[0][0], 600.0f, 400.0f);
        ExpectCorner(outlines.quads[0][2], 700.0f, 500.0f);
        ExpectCorner(outlines.quads[1][0], 0.0f, 0.0f);
    }

    TEST_F(ScenePickTest, ANodeWithNeitherTransformNorCanvasHasNoOutline) {
        EXPECT_EQ(ScenePick::SelectionOutlines(GetScene("Main"), GetScene("Main").GetRootNode()).count, 0u);
    }

    // ---- FindElement ------------------------------------------------------------------------------------------------------

    TEST_F(ScenePickTest, FindElementFindsALiveElementOfTheTree) {
        EXPECT_EQ(ScenePick::FindElement(*_hud, _panel.Get()), _panel.Get());
        EXPECT_EQ(ScenePick::FindElement(*_hud, _bare.Get()), _bare.Get());
        EXPECT_EQ(ScenePick::FindElement(*_hud, &_hud->GetRoot()), &_hud->GetRoot());
    }

    TEST_F(ScenePickTest, FindElementIgnoresAPointerFromAnotherTreeAndNull) {
        auto other = Node("Main", "Both").GetComponent<UICanvasComponent>();
        ASSERT_TRUE(other);
        auto stranger = other->GetRoot().AddChild("Stranger"_intern);

        EXPECT_EQ(ScenePick::FindElement(*_hud, stranger.Get()), nullptr);
        EXPECT_EQ(ScenePick::FindElement(*_hud, nullptr), nullptr);
        EXPECT_EQ(ScenePick::FindElement(*other, _panel.Get()), nullptr);
    }

}  // namespace BECore::Tests
