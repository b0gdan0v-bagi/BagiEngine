#include <BECore/GameManager/CoreManager.h>
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

        // RecordingRenderer copies every vertex of a geometry call into a new list, which a measurement of the caller must not count.
        class GeometryCountingRenderer : public RecordingRenderer {
        public:
            void DrawGeometry(ITexture* /*texture*/, Span<const GeometryVertex> vertices, Span<const int> /*indices*/, float /*translateX*/, float /*translateY*/) override {
                geometryCalls += vertices.empty() ? 0 : 1;
            }

            int geometryCalls = 0;
        };

        template <typename TDraw>
        uint64_t CountSteadyFrameAllocations(TDraw&& draw) {
            for (int frame = 0; frame < kWarmUpFrames; ++frame) {
                draw();
            }
            const uint64_t before = AllocTracker::GetThreadCounters().count;
            for (int frame = 0; frame < kMeasuredFrames; ++frame) {
                draw();
            }
            return AllocTracker::GetThreadCounters().count - before;
        }

    }  // namespace

    class UIPartShapeAllocTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        GeometryCountingRenderer _renderer;
    };

    TEST_F(UIPartShapeAllocTest, ARoundSliderAllocatesNothingInASteadyFrame) {
        CoreManager::GetUIVars().Set("part_alloc_slider"_intern, "0.5");
        UIElement owner;
        owner.rect = kArea;
        UISliderComponent slider;
        slider.bind = "part_alloc_slider"_intern;
        slider.partShape = UIPartShape::Round;

        const uint64_t allocations = CountSteadyFrameAllocations([&] { slider.DrawSelf(_renderer, owner); });

        EXPECT_EQ(_renderer.geometryCalls, (kWarmUpFrames + kMeasuredFrames) * 3) << "the track, the filled part and the thumb are geometry";
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(allocations, 0u);
        }
    }

    TEST_F(UIPartShapeAllocTest, ARoundSwitchAllocatesNothingInASteadyFrame) {
        UIElement owner;
        owner.rect = kArea;
        UISwitchComponent toggle;
        toggle.partShape = UIPartShape::Round;

        const uint64_t allocations = CountSteadyFrameAllocations([&] { toggle.DrawSelf(_renderer, owner); });

        EXPECT_EQ(_renderer.geometryCalls, (kWarmUpFrames + kMeasuredFrames) * 2) << "the track and the thumb are geometry";
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

        const uint64_t allocations = CountSteadyFrameAllocations([&] { bar.DrawSelf(_renderer); });

        EXPECT_EQ(_renderer.geometryCalls, (kWarmUpFrames + kMeasuredFrames) * 2) << "the track and the filled part are geometry";
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(allocations, 0u);
        }
    }

}  // namespace BECore::Tests
