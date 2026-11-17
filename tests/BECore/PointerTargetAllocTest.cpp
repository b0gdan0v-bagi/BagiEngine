#include "PointerTargetAllocTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Generated/PointerTargetAllocTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    AllocTargetCounters AllocTargetComponent::counters;

    bool AllocTargetComponent::OnPointerPress(const PointerEvent&) {
        ++counters.presses;
        return true;
    }

    void AllocTargetComponent::OnPointerRelease(const PointerEvent&) {
        ++counters.releases;
    }

    void AllocTargetComponent::OnPointerEnter(const PointerEvent&) {
        ++counters.enters;
    }

    void AllocTargetComponent::OnPointerExit(const PointerEvent&) {
        ++counters.exits;
    }

    bool AllocTargetComponent::OnPointerMove(const PointerEvent&) {
        ++counters.moves;
        return true;
    }

    void AllocTargetComponent::OnPointerDrag(const PointerEvent&) {
        ++counters.drags;
    }

    bool AllocTargetComponent::OnPointerWheel(const PointerEvent&) {
        ++counters.wheels;
        return true;
    }

    namespace {

        constexpr float kOutputWidth = 800.0f;
        constexpr float kOutputHeight = 600.0f;
        constexpr int kGridSide = 10;
        constexpr float kCell = 40.0f;
        constexpr float kClipX = 420.0f;
        constexpr float kClipSize = 100.0f;
        constexpr float kReach = 300.0f;
        constexpr float kProbeY = 150.0f;
        constexpr float kEmptyX = 700.0f;
        constexpr float kEmptyY = 500.0f;
        constexpr uint32_t kMouse = 1;
        constexpr int kWarmUpRounds = 3;
        constexpr int kMeasuredRounds = 5;
        constexpr float kDragStep = 7.0f;

        String BuildScene() {
            String nodes;
            // Even columns are clickables with an action, odd ones count their callbacks: a hundred targets side by side
            for (int row = 0; row < kGridSide; ++row) {
                for (int column = 0; column < kGridSide; ++column) {
                    const char* const target = column % 2 == 0 ? R"(<component type="ClickableComponent" actionName="AllocAction"/>)" : R"(<component type="AllocTargetComponent"/>)";
                    nodes += Format(R"(<node name="Cell{}_{}"><components>
  <component type="TransformComponent" x="{}" y="{}" width="{}" height="{}"/>
  <component type="QuadRendererComponent"/>{}
</components></node>)",
                                    row, column, static_cast<float>(column) * kCell, static_cast<float>(row) * kCell, kCell, kCell, target);
                }
            }
            nodes += Format(R"(<node name="Clipper" layer="5"><components>
  <component type="TransformComponent" x="{}" y="0" width="{}" height="{}"/>
  <component type="ClipRectComponent"/>
</components><children><node name="Wide" layer="5"><components>
  <component type="TransformComponent" x="{}" y="0" width="{}" height="{}"/>
  <component type="QuadRendererComponent"/>
  <component type="AllocTargetComponent"/>
</components></node></children></node>)",
                            kClipX, kClipSize, kClipSize, kClipX, kReach, kReach);
            nodes += R"(<node name="Canvas" layer="10"><components>
  <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
</components></node>)";
            return Format(R"(<SceneManager><scenes><scene type="Scene" name="S"><nodes>{}</nodes></scene></scenes></SceneManager>)", nodes);
        }

        /// A square of the canvas that takes the pointer
        class AllocProbe : public UIElement {
        public:
            void OnPress(float, float) override {
                ++presses;
            }

            uint32_t presses = 0;
        };

        bool Move(float x, float y, SDL_MouseButtonFlags held = 0, float dx = 0.0f, float dy = 0.0f) {
            SDL_MouseMotionEvent event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.which = kMouse;
            event.state = held;
            event.x = x;
            event.y = y;
            event.xrel = dx;
            event.yrel = dy;
            return CoreManager::GetUIInput().DispatchMouseMotion(event);
        }

        bool Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.which = kMouse;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.clicks = 1;
            event.x = x;
            event.y = y;
            return down ? CoreManager::GetUIInput().DispatchMouseButtonDown(event) : CoreManager::GetUIInput().DispatchMouseButtonUp(event);
        }

        bool Wheel(float x, float y) {
            SDL_MouseWheelEvent event{};
            event.type = SDL_EVENT_MOUSE_WHEEL;
            event.which = kMouse;
            event.y = 1.0f;
            event.mouse_x = x;
            event.mouse_y = y;
            return CoreManager::GetUIInput().DispatchMouseWheel(event);
        }

        /// A press and its release at one point; the answer of the press
        bool Click(float x, float y) {
            Move(x, y);
            const bool taken = Button(x, y, true);
            Button(x, y, false);
            return taken;
        }

        /// The middle of the cell @p index of a grid row or column
        float CellCentre(int index) {
            return (static_cast<float>(index) + 0.5f) * kCell;
        }

    }  // namespace

    class PointerTargetAllocTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            AllocTargetComponent::counters = {};
            _renderer = New<RecordingRenderer>();
            _renderer->width = kOutputWidth;
            _renderer->height = kOutputHeight;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(BuildScene().c_str()));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();

            for (const auto& node : _manager->GetActiveScene()->GetRootNode().GetChildren()) {
                if (node->GetName() == "Canvas"_intern) {
                    const auto canvas = node->GetComponent<UICanvasComponent>();
                    ASSERT_TRUE(canvas);
                    _probe = New<AllocProbe>();
                    _probe->style.rect.x = UILength::Px(kClipX);
                    _probe->style.rect.y = UILength::Px(kProbeY);
                    _probe->style.rect.width = UILength::Px(kClipSize);
                    _probe->style.rect.height = UILength::Px(kClipSize);
                    _probe->interactive = true;
                    _probe->blocksInput = true;
                    canvas->GetRoot().children.push_back(_probe);
                    canvas->Invalidate();
                }
            }
            ASSERT_TRUE(_probe);
        }

        void TearDown() override {
            _probe.Reset();
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        /// One frame as the game loop runs it. The recorded calls are dropped first, so the vector keeps its capacity
        void Frame() {
            _renderer->calls.clear();
            _manager->DrawAll();
        }

        /// Hover over the grid, the clip and the canvas, a drag, presses that a clickable, a target, the canvas, nothing and the area
        /// outside a clip get, and the wheel over a target and over nothing
        void Pointer() {
            for (int index = 0; index < kGridSide; ++index) {
                Move(CellCentre(index), CellCentre(index));  // the diagonal: a different target every step
                Move(CellCentre(index), CellCentre(0));      // the first row: clickables and targets alternate
            }
            Move(kClipX + kClipSize * 0.5f, kClipSize * 0.5f);            // into the clip
            Move(kClipX + kClipSize * 1.5f, kClipSize * 0.5f);            // out of it, over nothing
            Move(kClipX + kClipSize * 0.5f, kProbeY + kClipSize * 0.5f);  // over the canvas element
            Move(kEmptyX, kEmptyY);

            Click(CellCentre(1), CellCentre(0));                           // a target (odd column)
            Click(CellCentre(0), CellCentre(1));                           // a clickable: emits its action
            Click(kClipX + kClipSize * 0.5f, kClipSize * 0.5f);            // the target under the clip
            Click(kClipX + kClipSize * 1.5f, kClipSize * 0.5f);            // outside the clip: no one takes it
            Click(kClipX + kClipSize * 0.5f, kProbeY + kClipSize * 0.5f);  // the canvas
            Click(kEmptyX, kEmptyY);                                       // no one

            Move(CellCentre(1), CellCentre(0));
            Button(CellCentre(1), CellCentre(0), true);
            for (int step = 1; step <= kGridSide; ++step) {  // a drag that leaves the target and comes back
                Move(CellCentre(1) + kDragStep * static_cast<float>(step), CellCentre(0) + kDragStep * static_cast<float>(step), SDL_BUTTON_LMASK, kDragStep, kDragStep);
            }
            Button(CellCentre(1), CellCentre(0), false);

            Wheel(CellCentre(1), CellCentre(0));
            Wheel(kEmptyX, kEmptyY);
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<AllocProbe> _probe;
    };

    TEST_F(PointerTargetAllocTest, ASteadyFrameOfManyTargetsAndAClipAllocatesNothing) {
        if (!AllocTracker::kEnabled) {
            GTEST_SKIP() << "the allocation counters are off in this preset (ENGINE_PROFILER_ALLOC)";
        }
        for (int round = 0; round < kWarmUpRounds; ++round) {
            Frame();
        }

        const AllocTracker::ThreadCounters before = AllocTracker::GetThreadCounters();
        for (int round = 0; round < kMeasuredRounds; ++round) {
            Frame();
        }
        const AllocTracker::ThreadCounters after = AllocTracker::GetThreadCounters();

        EXPECT_EQ(after.count, before.count);
        EXPECT_GE(_renderer->calls.size(), static_cast<size_t>(kGridSide * kGridSide)) << "the targets were drawn";
    }

    TEST_F(PointerTargetAllocTest, MotionPressReleaseAndWheelOverManyTargetsAllocateNothing) {
        if (!AllocTracker::kEnabled) {
            GTEST_SKIP() << "the allocation counters are off in this preset (ENGINE_PROFILER_ALLOC)";
        }
        Frame();
        for (int round = 0; round < kWarmUpRounds; ++round) {
            Pointer();
            Frame();
        }
        AllocTargetComponent::counters = {};

        const AllocTracker::ThreadCounters before = AllocTracker::GetThreadCounters();
        for (int round = 0; round < kMeasuredRounds; ++round) {
            Pointer();
            Frame();
        }
        const AllocTracker::ThreadCounters after = AllocTracker::GetThreadCounters();

        EXPECT_EQ(after.count, before.count);
        const AllocTargetCounters& seen = AllocTargetComponent::counters;
        EXPECT_GT(seen.presses, 0u);
        EXPECT_EQ(seen.presses, seen.releases);
        EXPECT_GT(seen.enters, 0u);
        EXPECT_GT(seen.exits, 0u);
        EXPECT_GT(seen.moves, 0u);
        EXPECT_GT(seen.drags, 0u);
        EXPECT_GT(seen.wheels, 0u);
        EXPECT_GT(_probe->presses, 0u);
    }

    TEST_F(PointerTargetAllocTest, TheSequenceReachesTheTargetsWhenTheCountersAreOff) {
        // Runs in every preset: the other two tests skip without the counters, and a sequence that touches nothing would pass them there
        Frame();
        Pointer();
        Frame();

        const AllocTargetCounters& seen = AllocTargetComponent::counters;
        EXPECT_GE(seen.presses, 2u) << "a target in the grid and the one under the clip";
        EXPECT_EQ(seen.presses, seen.releases);
        EXPECT_GT(seen.enters, 0u);
        EXPECT_GT(seen.exits, 0u);
        EXPECT_GT(seen.drags, 0u);
        EXPECT_GT(seen.wheels, 0u);
        EXPECT_EQ(_probe->presses, 1u);
    }

}  // namespace BECore::Tests
