#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Widgets/Viewport/ViewportGizmos.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;

        // "Fit" shows a 400 x 200 view in the 800 x 600 output (scale 2, a bar of 100 pixels above and below), so a scene rect is not its output rect.
        constexpr const char* kScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="A"><components>
      <component type="TransformComponent" x="10" y="20" width="100" height="50" rotation="30" pivotX="0.25" pivotY="0.75"/>
      <component type="QuadRendererComponent"/>
    </components></node>
    <node name="B"><components>
      <component type="TransformComponent" x="200" y="40" width="60" height="60"/>
      <component type="QuadRendererComponent"/>
    </components></node>
    <node name="Locked"><components>
      <component type="TransformComponent" x="300" y="40" width="60" height="60"/>
      <component type="QuadRendererComponent"/>
    </components></node>
    <node name="Hud" layer="3"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Other"><nodes>
    <node name="Elsewhere"><components>
      <component type="TransformComponent" x="1" y="2" width="3" height="4"/>
      <component type="QuadRendererComponent"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Fit" viewWidth="400" viewHeight="200"><nodes>
    <node name="Box"><components>
      <component type="TransformComponent" x="50" y="50" width="100" height="100"/>
      <component type="QuadRendererComponent"/>
    </components></node>
    <node name="Box2"><components>
      <component type="TransformComponent" x="200" y="60" width="50" height="40"/>
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
            }
            return nullptr;
        }

    }  // namespace

    class ViewportGizmosTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _renderer = New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kScenesXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _manager->SetActiveScene("Fit"_intern);
            _manager->DrawAll();
            _manager->SetActiveScene("Main"_intern);
            _manager->DrawAll();
            Node("Main", "Locked").SetEditorFlags(EditorFlags::Locked);
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        Scene& GetScene(const char* name) const {
            return *_manager->GetSceneByName(PoolString::Intern(name));
        }

        SceneNode& Node(const char* sceneName, const char* name) const {
            SceneNode* node = Find(GetScene(sceneName).GetRootNode(), name);
            EXPECT_NE(node, nullptr) << name;
            return *node;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    // ---- the frame ------------------------------------------------------------------------------------------------------

    TEST(ViewportFrameTest, AnIdentityCameraShowsTheTargetExactlyWhereTheFitPutsIt) {
        const ViewportFrame frame = MakeViewportFrame(ViewportCamera{}, {100.0f, 50.0f}, {400.0f, 300.0f}, {800.0f, 600.0f}, 0.5f);

        EXPECT_NEAR(frame.origin.x, 100.0f, kTolerance);
        EXPECT_NEAR(frame.origin.y, 50.0f, kTolerance);
        EXPECT_NEAR(frame.end.x, 500.0f, kTolerance);
        EXPECT_NEAR(frame.end.y, 350.0f, kTolerance);
        EXPECT_NEAR(frame.scale, 0.5f, kTolerance);
        EXPECT_NEAR(frame.imageMax.x, 500.0f, kTolerance);
        EXPECT_NEAR(frame.imageMax.y, 350.0f, kTolerance);
    }

    TEST(ViewportFrameTest, ACameraMovesAndScalesTheTargetAndTheFrameRoundTrips) {
        ViewportCamera camera;
        camera.ZoomAt(2.0f, {300.0f, 200.0f}, {100.0f, 50.0f, 400.0f, 300.0f}, 0.5f);
        camera.PanBy({30.0f, -10.0f}, 0.5f);

        const ViewportFrame frame = MakeViewportFrame(camera, {100.0f, 50.0f}, {400.0f, 300.0f}, {800.0f, 600.0f}, 0.5f);

        EXPECT_NEAR(frame.scale, 1.0f, kTolerance);
        EXPECT_NEAR(frame.end.x - frame.origin.x, 800.0f, kTolerance);
        const Vec2 target = frame.ToTarget(frame.ToScreen({123.0f, 456.0f}));
        EXPECT_NEAR(target.x, 123.0f, kTolerance);
        EXPECT_NEAR(target.y, 456.0f, kTolerance);
        // The camera agrees with the frame about where a target pixel is
        const Vec2 byCamera = camera.ToScreen({123.0f, 456.0f}, {100.0f, 50.0f, 400.0f, 300.0f}, 0.5f);
        const ImVec2 byFrame = frame.ToScreen({123.0f, 456.0f});
        EXPECT_NEAR(byFrame.x, byCamera.x, kTolerance);
        EXPECT_NEAR(byFrame.y, byCamera.y, kTolerance);
    }

    // ---- framing the selection ------------------------------------------------------------------------------------------

    TEST_F(ViewportGizmosTest, TheBoundsOfASelectionAreTheUnionOfItsOutlinesInOutputPixels) {
        _manager->SetActiveScene("Fit"_intern);
        _manager->DrawAll();
        const eastl::vector<SceneNode*> selected{&Node("Fit", "Box"), &Node("Fit", "Box2")};
        Rect bounds;

        ASSERT_TRUE(SelectionBounds(GetScene("Fit"), {selected.data(), selected.size()}, bounds));

        // Box: (100, 200)-(300, 400) in the output; Box2: (400, 220)-(500, 300)
        EXPECT_NEAR(bounds.x, 100.0f, kTolerance);
        EXPECT_NEAR(bounds.y, 200.0f, kTolerance);
        EXPECT_NEAR(bounds.w, 400.0f, kTolerance);
        EXPECT_NEAR(bounds.h, 200.0f, kTolerance);
    }

    TEST_F(ViewportGizmosTest, NoBoundsForAnEmptySelectionOrNodesOfAnotherScene) {
        Rect bounds{1.0f, 2.0f, 3.0f, 4.0f};
        const eastl::vector<SceneNode*> stranger{&Node("Other", "Elsewhere")};

        EXPECT_FALSE(SelectionBounds(GetScene("Main"), {}, bounds));
        EXPECT_FALSE(SelectionBounds(GetScene("Main"), {stranger.data(), stranger.size()}, bounds));
        EXPECT_FLOAT_EQ(bounds.x, 1.0f);  // untouched
    }

}  // namespace BECore::Tests
