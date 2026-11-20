#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIDropdown.h>
#include <Common/RecordingRenderer.h>
#include <Common/UIInputEvents.h>
#include <Common/UITestSupport.h>
#include <gtest/gtest.h>

// The allocations are counted only in a build with ENGINE_PROFILER_ALLOC (macos-profile); elsewhere the counts are 0 and the cases pass with no meaning.

namespace BECore::Tests {

    namespace {

        constexpr int kWarmUpFrames = 3;
        constexpr int kMeasuredFrames = 16;

        /// Runs @p frame a few times to fill the caches, then counts the allocations of the next frames on this thread
        template <typename TFrame>
        uint64_t CountSteadyFrameAllocations(TFrame&& frame) {
            for (int warmUp = 0; warmUp < kWarmUpFrames; ++warmUp) {
                frame(warmUp);
            }
            const uint64_t before = AllocTracker::GetThreadCounters().count;
            for (int measured = 0; measured < kMeasuredFrames; ++measured) {
                frame(kWarmUpFrames + measured);
            }
            return AllocTracker::GetThreadCounters().count - before;
        }

        constexpr const char* kDropdownSceneXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIDropdown" name="Pick" x="20" y="20" width="240" height="56" optionHeight="56" color="40,40,70,255">
            <components><component type="UITextComponent" text="Pick one" verticalAlign="Center" wrap="false"/></components>
            <children>
              <element type="UIElement" name="First" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="First" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="first"/>
                </components>
              </element>
              <element type="UIElement" name="Second" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Second" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="second"/>
                </components>
              </element>
            </children>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr float kMovingStep = 7.0f;

    }  // namespace

    class UISteadyFrameAllocTest : public UISceneFixture {};

    TEST_F(UISteadyFrameAllocTest, AnOpenDropdownAllocatesNothingInASteadyFrame) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kDropdownSceneXml));
        ASSERT_TRUE(_canvas);
        Draw();
        UIDropdown& dropdown = Get<UIDropdown>("Pick");
        const float headerX = dropdown.rect.x + dropdown.rect.w * 0.5f;
        const float headerY = dropdown.rect.y + dropdown.rect.h * 0.5f;
        ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(headerX, headerY, true)));
        ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(headerX, headerY, false)));
        Draw();
        ASSERT_TRUE(dropdown.IsOpen());

        const uint64_t allocations = CountSteadyFrameAllocations([&](int frame) {
            _renderer->ClearCalls();
            _manager->DrawAll();
            UIElement* hit = _canvas->GetLayout().HitTest(headerX + kMovingStep * static_cast<float>(frame % 8), headerY + 2.0f * dropdown.rect.h);
            EXPECT_NE(hit, nullptr);
            _manager->UpdateAll();
        });

        EXPECT_TRUE(dropdown.IsOpen());
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(allocations, 0u);
        }
    }

}  // namespace BECore::Tests
