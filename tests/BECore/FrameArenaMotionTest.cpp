#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UILayoutHud.h>
#include <Common/AllocCheck.h>
#include <Common/UITestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kWarmUpFrames = 30;
        constexpr int kMeasuredFrames = 30;
        constexpr size_t kStackChildren = 40;
        constexpr size_t kBoxElements = 24;
        constexpr float kChildHeight = 20.0f;
        constexpr float kChildBaseWidth = 100.0f;
        constexpr int kChildWidthSteps = 7;
        constexpr uint64_t kTextAllocationsPerFrame = 4;  ///< measured after the temporaries moved; what is left is the heap result a layout hands to its cache
        constexpr float kOutputWidth = 800.0f;
        constexpr float kOutputHeight = 1200.0f;

        /// Runs @p frame (given the frame number) through the warm-up and the measured frames and gives the allocations of the measured ones on this thread, 0 without the tracker
        template <typename TFrame>
        uint64_t CountMeasuredAllocations(TFrame&& frame) {
            int number = 0;
            for (; number < kWarmUpFrames; ++number) {
                frame(number);
            }
            const uint64_t before = AllocTracker::GetThreadCounters().count;
            for (; number < kWarmUpFrames + kMeasuredFrames; ++number) {
                frame(number);
            }
            return AllocTracker::GetThreadCounters().count - before;
        }

        /// What a run of this file reads out of the log: the allocations per frame of one scenario
        void Report(const char* scenario, uint64_t allocations) {
            if constexpr (!AllocTracker::kEnabled) {
                GTEST_SKIP() << kNoAllocTracker;
            }
            const double perFrame = static_cast<double>(allocations) / kMeasuredFrames;
            GTEST_LOG_(INFO) << scenario << ": " << perFrame << " allocations per frame (" << allocations << " in " << kMeasuredFrames << " frames)";
            ::testing::Test::RecordProperty(scenario, Format("{}", perFrame).c_str());
        }

        UILayoutContext Output() {
            UILayoutContext context;
            context.outputWidth = kOutputWidth;
            context.outputHeight = kOutputHeight;
            return context;
        }

        IntrusivePtr<UIElement> AddColumn(UIElement& parent, const char* name) {
            auto column = AddBox(parent, name, 0.0f, 0.0f, kChildBaseWidth + 40.0f, kChildHeight * kStackChildren);
            column->layout = UILayoutKind::Stack;
            return column;
        }

        String BoxElementsXml() {
            String elements;
            for (size_t i = 0; i < kBoxElements; ++i) {
                elements += Format(R"(<element type="UIElement" name="Box{}" x="{}" y="{}" width="90" height="48" color="200,60,60,255" cornerRadius="10" borderWidth="2"
                                      borderColor="255,255,255,255" shadow="0,3,8,0,#000000A0"/>)",
                                   i, 10 + (i % 4) * 100, 10 + (i / 4) * 60);
            }
            return Format(R"(<SceneManager><scenes><scene type="Scene" name="Main"><nodes>
  <node name="Hud" layer="10"><components>
    <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"><elements>{}</elements></component>
  </components></node>
</nodes></scene></scenes></SceneManager>)",
                          elements);
        }

        String TextXml() {
            return R"(<SceneManager><scenes><scene type="Scene" name="Main"><nodes>
  <node name="Hud" layer="10"><components>
    <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"><elements>
      <element type="UIElement" name="Score" x="10" y="10" width="300" height="48" fontSize="32">
        <components><component type="UITextComponent" text="0" align="Center" verticalAlign="Center"/></components>
      </element>
    </elements></component>
  </components></node>
</nodes></scene></scenes></SceneManager>)";
        }

    }  // namespace

    class FrameArenaMotionTest : public UISceneFixture {};

    // The frames of a screen in motion, before and after the frame arena and the scratch arena take the temporaries (the numbers go to the log and the XML report)

    TEST(FrameArenaMotionLayoutTest, ARelayoutInMotionAllocationsPerFrame) {
        EngineEnvironment::EnsureCoreServices();
        UICanvasLayout canvas;
        auto outer = AddColumn(*canvas.root, "Outer");
        auto inner = AddColumn(*outer, "Inner");
        Vector<IntrusivePtr<UIElement>> children;
        for (size_t i = 0; i < kStackChildren; ++i) {
            children.push_back(AddBox(*inner, "Row", 0.0f, 0.0f, kChildBaseWidth, kChildHeight));
        }
        const UILayoutContext context = Output();
        ASSERT_TRUE(canvas.Update(context));

        const uint64_t allocations = CountMeasuredAllocations([&](int number) {
            children[kStackChildren / 2]->style.rect.width = UILength::Px(kChildBaseWidth + static_cast<float>(number % kChildWidthSteps));
            canvas.Invalidate();
            canvas.Update(context);
        });

        EXPECT_EQ(canvas.GetLayoutRuns(), 1u + kWarmUpFrames + kMeasuredFrames) << "every frame lays the tree out";
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(allocations, 0u) << "the layout temporaries are on the scratch arena";
        }
        Report("RelayoutInMotion", allocations);
    }

    TEST_F(FrameArenaMotionTest, ABoxEffectDrawAllocationsPerFrame) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(BoxElementsXml()));
        ASSERT_TRUE(_canvas);

        size_t meshBytes = 0;
        const uint64_t allocations = CountMeasuredAllocations([&](int) {
            Draw();
            meshBytes = 0;
            size_t geometryCalls = 0;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Geometry) {
                    ++geometryCalls;
                    meshBytes += call.vertices.size() * sizeof(GeometryVertex) + call.indices.size() * sizeof(int);
                }
            }
            ASSERT_EQ(geometryCalls, kBoxElements) << "each element draws its box as one geometry call";
        });

        Report("BoxEffectDraw", allocations);
        GTEST_LOG_(INFO) << "BoxEffectDraw: the box meshes of a frame are " << meshBytes << " bytes; " << kBoxElements << " elements keep two vectors of " << sizeof(Vector<GeometryVertex>)
                         << " bytes each, sizeof(UIElement) is " << sizeof(UIElement);
    }

    TEST_F(FrameArenaMotionTest, ATextInMotionAllocationsPerFrame) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(TextXml()));
        ASSERT_TRUE(_canvas);
        UITextComponent& score = ComponentOf<UITextComponent>("Score");

        const uint64_t allocations = CountMeasuredAllocations([&](int number) {
            score.text = Format("{}", number * 37);
            Draw();
        });

        EXPECT_FALSE(_renderer->calls.empty()) << "the counter is drawn";
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_LE(allocations, kTextAllocationsPerFrame * kMeasuredFrames);
        }
        Report("TextInMotion", allocations);
    }

}  // namespace BECore::Tests
