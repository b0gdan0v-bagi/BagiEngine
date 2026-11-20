#include <BECore/GameManager/CoreManager.h>
#include <BECore/Memory/FrameArena.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIProgressBar.h>
#include <BECore/UI/UISliderComponent.h>
#include <BECore/UI/UISwitchComponent.h>
#include <BECore/UI/UIVars.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kWarmUpFrames = 2;
        constexpr int kMeasuredFrames = 16;
        constexpr Rect kArea{10.0f, 10.0f, 200.0f, 32.0f};

        template <typename TDraw>
        uint64_t CountSteadyFrameAllocations(RecordingRenderer& renderer, TDraw&& draw) {
            const auto frame = [&] {
                renderer.ClearCalls();
                draw();
                FrameArena::Advance();  // the box mesh is on the frame arena, which a real frame resets after the present
            };
            for (int warmUp = 0; warmUp < kWarmUpFrames; ++warmUp) {
                frame();
            }
            const uint64_t before = AllocTracker::GetThreadCounters().count;
            for (int measured = 0; measured < kMeasuredFrames; ++measured) {
                frame();
            }
            return AllocTracker::GetThreadCounters().count - before;
        }

        size_t GeometryCallsOfTheLastFrame(const RecordingRenderer& renderer) {
            size_t count = 0;
            for (const DrawCall& call : renderer.calls) {
                count += call.kind == DrawKind::Geometry ? 1 : 0;
            }
            return count;
        }

    }  // namespace

    class UIPartShapeAllocTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        RecordingRenderer _renderer;
    };

    TEST_F(UIPartShapeAllocTest, ARoundSliderAllocatesNothingInASteadyFrame) {
        CoreManager::GetUIVars().Set("part_alloc_slider"_intern, "0.5");
        UIElement owner;
        owner.rect = kArea;
        UISliderComponent slider;
        slider.bind = "part_alloc_slider"_intern;
        slider.partShape = UIPartShape::Round;

        const uint64_t allocations = CountSteadyFrameAllocations(_renderer, [&] { slider.DrawSelf(_renderer, owner); });

        EXPECT_EQ(GeometryCallsOfTheLastFrame(_renderer), 3u) << "the track, the filled part and the thumb are geometry";
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(allocations, 0u);
        }
    }

    TEST_F(UIPartShapeAllocTest, ARoundSwitchAllocatesNothingInASteadyFrame) {
        UIElement owner;
        owner.rect = kArea;
        UISwitchComponent toggle;
        toggle.partShape = UIPartShape::Round;

        const uint64_t allocations = CountSteadyFrameAllocations(_renderer, [&] { toggle.DrawSelf(_renderer, owner); });

        EXPECT_EQ(GeometryCallsOfTheLastFrame(_renderer), 2u) << "the track and the thumb are geometry";
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(allocations, 0u);
        }
    }

    TEST_F(UIPartShapeAllocTest, ARoundProgressBarAllocatesNothingInASteadyFrame) {
        CoreManager::GetUIVars().Set("part_alloc_bar"_intern, "0.5");
        UIProgressBar bar;
        bar.rect = kArea;
        bar.bind = "part_alloc_bar"_intern;
        bar.partShape = UIPartShape::Round;

        const uint64_t allocations = CountSteadyFrameAllocations(_renderer, [&] { bar.DrawSelf(_renderer); });

        EXPECT_EQ(GeometryCallsOfTheLastFrame(_renderer), 2u) << "the track and the filled part are geometry";
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(allocations, 0u);
        }
    }

}  // namespace BECore::Tests
