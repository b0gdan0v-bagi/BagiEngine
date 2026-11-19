#include "ClipRectComponentTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/PointerTargetComponentTest.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Components/ClickableComponent.h>
#include <BECore/Scene/Components/ClipRectComponent.h>
#include <BECore/Scene/SceneDrawSystem.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <Common/UIInputEvents.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Generated/ClipRectComponentTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    void ClipProbeTestComponent::OnDraw() {
        if (samples != nullptr) {
            const IRenderer& renderer = *CoreManager::GetRenderer();
            samples->push_back(ClipSample{renderer.GetClipDepth(), renderer.GetClipRect()});
        }
    }

    namespace {

        constexpr float kOutputWidth = 800.0f;
        constexpr float kOutputHeight = 600.0f;
        constexpr float kClipSize = 100.0f;
        constexpr float kWide = 300.0f;
        constexpr float kInside = 10.0f;
        constexpr float kBeside = 150.0f;
        constexpr float kFitScale = 2.0f;      // viewWidth 400, viewHeight 200 in the 800 x 600 output
        constexpr float kFitOffsetY = 100.0f;  // the bar above the fitted view
        constexpr uint32_t kMouse = 1;
        constexpr const char* kFitView = R"(viewWidth="400" viewHeight="200")";

        /// A node of @p width x @p height at (@p x, @p y) with the components @p extra after the transform and the nodes @p children below it
        String Node(const char* name, float x, float y, float width, float height, int layer, const char* extra, const String& children = {}) {
            return Format(R"(<node name="{}" layer="{}"><components>
  <component type="TransformComponent" x="{}" y="{}" width="{}" height="{}"/>{}
</components><children>{}</children></node>)",
                          name, layer, x, y, width, height, extra, children);
        }

        constexpr const char* kQuad = R"(<component type="QuadRendererComponent"/>)";
        constexpr const char* kClip = R"(<component type="ClipRectComponent"/>)";

        String SceneXml(const String& nodes, const char* sceneAttributes = "") {
            return Format(R"(<SceneManager><scenes><scene type="Scene" name="S" {}><nodes>{}</nodes></scene></scenes></SceneManager>)", sceneAttributes, nodes);
        }

        bool Press(float x, float y) {
            SDL_MouseMotionEvent motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.which = kMouse;
            motion.x = x;
            motion.y = y;
            CoreManager::GetUIInput().DispatchMouseMotion(motion);
            SDL_MouseButtonEvent down{};
            down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            down.which = kMouse;
            down.button = SDL_BUTTON_LEFT;
            down.down = true;
            down.x = x;
            down.y = y;
            const bool taken = CoreManager::GetUIInput().DispatchMouseButtonDown(down);
            SDL_MouseButtonEvent up = down;
            up.type = SDL_EVENT_MOUSE_BUTTON_UP;
            up.down = false;
            CoreManager::GetUIInput().DispatchMouseButtonUp(up);
            return taken;
        }

        void ExpectClip(const DrawCall& call, const Rect& expected) {
            ASSERT_TRUE(call.clip.has_value());
            EXPECT_FLOAT_EQ(call.clip->x, expected.x);
            EXPECT_FLOAT_EQ(call.clip->y, expected.y);
            EXPECT_FLOAT_EQ(call.clip->w, expected.w);
            EXPECT_FLOAT_EQ(call.clip->h, expected.h);
        }

    }  // namespace

    class ClipRectComponentTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _renderer = New<RecordingRenderer>();
            _renderer->width = kOutputWidth;
            _renderer->height = kOutputHeight;
            CoreManager::GetRendererManager().SetRenderer(_renderer);
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        void LoadNodes(const String& nodes, const char* sceneAttributes = "") {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(SceneXml(nodes, sceneAttributes).c_str()));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        SceneNode& At(const char* name) {
            SceneNode* node = FindNodeBelow(_manager->GetActiveScene()->GetRootNode(), name);
            EXPECT_NE(node, nullptr) << name;
            return node != nullptr ? *node : _manager->GetActiveScene()->GetRootNode();
        }

        /// Draws once and returns the recorded rect draws, in draw order, with the clip each had
        const Vector<DrawCall>& Draw() {
            _renderer->calls.clear();
            _manager->DrawAll();
            return _renderer->calls;
        }

        /// Binds a sample list to the probe of the node @p name
        void Probe(const char* name, Vector<ClipSample>& samples) {
            const auto probe = At(name).GetComponent<ClipProbeTestComponent>();
            ASSERT_TRUE(probe) << name;
            probe->samples = &samples;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(ClipRectComponentTest, TheSubtreeIsClippedWhateverItsLayerAndTheSiblingAfterItIsNot) {
        // Drawn in the order Parent (layer 0), Sibling (layer 3), Child (layer 5): the clip is pushed, popped and pushed again
        const String child = Node("Child", 0, 0, kWide, kWide, 5, kQuad);
        const String parent = Node("Parent", 0, 0, kClipSize, kClipSize, 0, Format("{}{}", kClip, kQuad).c_str(), child);
        ASSERT_NO_FATAL_FAILURE(LoadNodes(parent + Node("Sibling", 0, 0, kWide, kWide, 3, kQuad)));

        const Vector<DrawCall>& calls = Draw();

        ASSERT_EQ(calls.size(), 3u);
        const Rect clip{0.0f, 0.0f, kClipSize, kClipSize};
        ExpectClip(calls[0], clip);
        EXPECT_FALSE(calls[1].clip.has_value()) << "the sibling is outside the subtree";
        ExpectClip(calls[2], clip);  // the child on another layer is still clipped
        EXPECT_EQ(_renderer->GetClipDepth(), 0u);
    }

    TEST_F(ClipRectComponentTest, NestedClipsIntersect) {
        const String inner = Node("Inner", 50, 50, kClipSize, kClipSize, 0, Format("{}{}", kClip, kQuad).c_str(), Node("Leaf", 0, 0, kWide, kWide, 0, kQuad));
        ASSERT_NO_FATAL_FAILURE(LoadNodes(Node("Outer", 0, 0, 120, 120, 0, kClip, inner)));

        const Vector<DrawCall>& calls = Draw();

        ASSERT_EQ(calls.size(), 2u);
        const Rect both{50.0f, 50.0f, 70.0f, 70.0f};  // the inner rect cut by the outer one
        ExpectClip(calls[0], both);
        ExpectClip(calls[1], both);
    }

    TEST_F(ClipRectComponentTest, ASiblingAfterAnInnerClipFallsBackToTheOuterClip) {
        const String inner = Node("Inner", 50, 50, kClipSize, kClipSize, 0, Format("{}{}", kClip, kQuad).c_str());
        const String outer = Node("Outer", 0, 0, 120, 120, 0, kClip, inner + Node("Plain", 0, 0, kWide, kWide, 0, kQuad));
        ASSERT_NO_FATAL_FAILURE(LoadNodes(outer));

        const Vector<DrawCall>& calls = Draw();

        ASSERT_EQ(calls.size(), 2u);
        ExpectClip(calls[1], Rect{0.0f, 0.0f, 120.0f, 120.0f});
    }

    TEST_F(ClipRectComponentTest, ALetterboxedViewMapsTheRectThroughTheViewTransform) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(Node("Clipper", 0, 0, kClipSize, kClipSize, 0, Format("{}{}", kClip, kQuad).c_str()), kFitView));

        const Vector<DrawCall>& calls = Draw();

        ASSERT_EQ(calls.size(), 1u);
        ExpectClip(calls[0], Rect{0.0f, kFitOffsetY, kClipSize * kFitScale, kClipSize * kFitScale});
    }

    TEST_F(ClipRectComponentTest, ANodeWithTheComponentAndNoTransformClipsNothing) {
        const String bare = Format(R"(<node name="Bare"><components><component type="ClipRectComponent"/></components><children>{}</children></node>)", Node("Child", 0, 0, kWide, kWide, 0, kQuad));
        ASSERT_NO_FATAL_FAILURE(LoadNodes(bare));

        const Vector<DrawCall>& calls = Draw();

        ASSERT_EQ(calls.size(), 1u);
        EXPECT_FALSE(calls[0].clip.has_value());
    }

    TEST_F(ClipRectComponentTest, AHiddenSubtreePushesNothingAndTheNextSiblingIsUnclipped) {
        Vector<ClipSample> samples;
        const String probe = R"(<component type="ClipProbeTestComponent"/>)";
        const String hidden = Node("Hidden", 0, 0, kClipSize, kClipSize, 0, Format("{}{}", kClip, kQuad).c_str(), Node("Under", 0, 0, 1, 1, 0, probe.c_str()));
        ASSERT_NO_FATAL_FAILURE(LoadNodes(hidden + Node("After", 0, 0, kWide, kWide, 0, (probe + kQuad).c_str())));
        ASSERT_NO_FATAL_FAILURE(Probe("After", samples));
        ASSERT_NO_FATAL_FAILURE(Probe("Under", samples));
        At("Hidden").SetEditorFlags(EditorFlags::Hidden);

        const Vector<DrawCall>& calls = Draw();

        ASSERT_EQ(calls.size(), 1u) << "only the quad of After is drawn";
        EXPECT_FALSE(calls[0].clip.has_value());
        ASSERT_EQ(samples.size(), 1u) << "OnDraw() of the hidden subtree is skipped";
        EXPECT_EQ(samples[0].depth, 0u);
    }

    TEST_F(ClipRectComponentTest, TheClipOfADeeperChainThanTheLimitStillBalances) {
        String nest = Node("Leaf", 0, 0, kWide, kWide, 0, kQuad);
        for (size_t level = 0; level < SceneDrawSystem::kMaxClipDepth + 3; ++level) {
            nest = Node(Format("Level{}", level).c_str(), 0, 0, kWide, kWide, 0, kClip, nest);
        }
        ASSERT_NO_FATAL_FAILURE(LoadNodes(nest));

        const Vector<DrawCall>& calls = Draw();

        ASSERT_EQ(calls.size(), 1u);
        EXPECT_EQ(_renderer->GetClipDepth(), 0u);
        _manager->DrawAll();
        EXPECT_EQ(_renderer->GetClipDepth(), 0u);
    }

    TEST_F(ClipRectComponentTest, ARemovedComponentTakesEffectOnTheNextDraw) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(Node("Clipper", 0, 0, kClipSize, kClipSize, 0, Format("{}{}", kClip, kQuad).c_str())));
        ASSERT_TRUE(Draw()[0].clip.has_value());

        SceneNode& node = At("Clipper");
        node.RemoveComponent("ClipRectComponent"_intern);
        const Vector<DrawCall>& calls = Draw();

        ASSERT_EQ(calls.size(), 1u);
        EXPECT_FALSE(calls[0].clip.has_value());
    }

    TEST_F(ClipRectComponentTest, AClickableUnderTheClipTakesAPressInsideAndTheOneBelowTakesAPressOutside) {
        const String below = Node("Below", 0, 0, kWide, kWide, 0, R"(<component type="ClickableComponent" actionName="ActBelow"/>)");
        const String inside = Node("Inside", 0, 0, kWide, kWide, 0, R"(<component type="ClickableComponent" actionName="ActInside"/>)");
        ASSERT_NO_FATAL_FAILURE(LoadNodes(below + Node("Clipper", 0, 0, kClipSize, kClipSize, 0, kClip, inside)));
        Draw();
        ActionLog recorder;

        EXPECT_TRUE(Press(kInside, kInside));
        EXPECT_TRUE(Press(kBeside, kInside));

        ASSERT_EQ(recorder.Total(), 2u);
        EXPECT_EQ(recorder.ActionAt(0), "ActInside"_intern);
        EXPECT_EQ(recorder.ActionAt(1), "ActBelow"_intern);
    }

    TEST_F(ClipRectComponentTest, APointerTargetUnderTheClipLeavesAPressOutsideToTheOneBelow) {
        const String target = R"(<component type="PointerTargetTestComponent"/>)";
        ASSERT_NO_FATAL_FAILURE(
            LoadNodes(Node("Below", 0, 0, kWide, kWide, 0, target.c_str()) + Node("Clipper", 0, 0, kClipSize, kClipSize, 0, kClip, Node("Inside", 0, 0, kWide, kWide, 0, target.c_str()))));
        TargetScript below;
        TargetScript inside;
        Require(At("Below").GetComponent<PointerTargetTestComponent>()).script = &below;
        Require(At("Inside").GetComponent<PointerTargetTestComponent>()).script = &inside;
        _manager->DrawAll();

        EXPECT_TRUE(Press(kInside, kInside));
        EXPECT_EQ(inside.Count(TargetCall::Press), 1u);
        EXPECT_EQ(below.Count(TargetCall::Press), 0u);

        EXPECT_TRUE(Press(kBeside, kInside));
        EXPECT_EQ(inside.Count(TargetCall::Press), 1u);
        EXPECT_EQ(below.Count(TargetCall::Press), 1u);
    }

    TEST_F(ClipRectComponentTest, TheHitClipFollowsTheLetterboxedView) {
        const String inside = Node("Inside", 0, 0, kWide, kWide, 0, R"(<component type="ClickableComponent" actionName="ActInside"/>)");
        ASSERT_NO_FATAL_FAILURE(LoadNodes(Node("Clipper", 0, 0, kClipSize, kClipSize, 0, kClip, inside), kFitView));
        Draw();
        ActionLog recorder;

        EXPECT_TRUE(Press(kInside * kFitScale, kFitOffsetY + kInside * kFitScale));
        EXPECT_FALSE(Press(kBeside * kFitScale, kFitOffsetY + kInside * kFitScale));

        ASSERT_EQ(recorder.Total(), 1u);
        EXPECT_EQ(recorder.ActionAt(0), "ActInside"_intern);
    }

}  // namespace BECore::Tests
