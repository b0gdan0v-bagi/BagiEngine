#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Time/Time.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UITooltip.h>
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

        constexpr const char* kTooltipSceneXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Icon" x="100" y="100" width="48" height="48" color="30,30,30,255">
            <components>
              <component type="UIInputClicker" action="OpenIcon"/>
              <component type="UITooltipComponent" text="Settings of the game: the sound, the language and the size of everything"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        // Every part of the draw list in one screen: nested opacity, an owner of the pointer state with children, a dropdown (opened by the test) and an element with a tooltip
        constexpr const char* kWholeScreenXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Fade" x="300" y="20" width="200" height="120" color="40,40,70,255" opacity="0.5">
            <children>
              <element type="UIElement" name="Inner" x="10" y="10" width="100" height="60" color="70,70,100,255" opacity="0.5"/>
            </children>
          </element>
          <element type="UIElement" name="Owner" x="300" y="200" width="200" height="60" color="40,40,40,255" hoverColor="60,70,80,255">
            <components><component type="UIInputClicker" action="WholeScreenOwner"/></components>
            <children>
              <element type="UIElement" name="Caption" x="0" y="0" width="120" height="60" fontSize="24">
                <components><component type="UITextComponent" text="Owner" align="Center" verticalAlign="Center" wrap="false"/></components>
              </element>
            </children>
          </element>
          <element type="UIDropdown" name="Pick" x="20" y="20" width="240" height="56" optionHeight="56" color="40,40,70,255">
            <components><component type="UITextComponent" text="Pick one" verticalAlign="Center" wrap="false"/></components>
            <children>
              <element type="UIElement" name="First" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="First" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="first"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Icon" x="20" y="400" width="48" height="48" color="30,30,30,255">
            <components>
              <component type="UIInputClicker" action="OpenIcon"/>
              <component type="UITooltipComponent" text="Settings of the game: the sound, the language and the size of everything"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr double kFrameStep = 0.125;  // a binary fraction; kHoverDelay passes in a few frames

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

    TEST_F(UISteadyFrameAllocTest, AVisibleTooltipAllocatesNothingInASteadyFrame) {
        CoreManager::GetTime().Reset();
        ASSERT_NO_FATAL_FAILURE(LoadScene(kTooltipSceneXml));
        ASSERT_TRUE(_canvas);
        Draw();
        const UIElement& icon = El("Icon");
        const float x = icon.rect.x + icon.rect.w * 0.5f;
        const float y = icon.rect.y + icon.rect.h * 0.5f;
        SDL_MouseMotionEvent motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.x = x;
        motion.y = y;
        ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseMotion(motion));
        for (double passed = 0.0; passed <= UITooltip::kHoverDelay + kFrameStep; passed += kFrameStep) {
            CoreManager::GetTime().Advance(kFrameStep);
            Draw();
        }
        const size_t shown = _renderer->calls.size();

        const uint64_t allocations = CountSteadyFrameAllocations([&](int) {
            CoreManager::GetTime().Advance(kFrameStep);
            Draw();
        });

        EXPECT_GT(shown, 2u) << "the plate and its glyphs are drawn over the icon";
        EXPECT_EQ(_renderer->calls.size(), shown);
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(allocations, 0u);
        }
        CoreManager::GetTime().Reset();
    }

    TEST_F(UISteadyFrameAllocTest, EveryPartOfTheDrawListAllocatesNothingInASteadyFrame) {
        CoreManager::GetTime().Reset();
        ASSERT_NO_FATAL_FAILURE(LoadScene(kWholeScreenXml));
        ASSERT_TRUE(_canvas);
        Draw();
        UIDropdown& dropdown = Get<UIDropdown>("Pick");
        const float headerX = dropdown.rect.x + dropdown.rect.w * 0.5f;
        const float headerY = dropdown.rect.y + dropdown.rect.h * 0.5f;
        ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(headerX, headerY, true)));
        ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(headerX, headerY, false)));
        Draw();
        ASSERT_TRUE(dropdown.IsOpen());
        const size_t withoutTooltip = _renderer->calls.size();
        const UIElement& icon = El("Icon");
        SDL_MouseMotionEvent motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.x = icon.rect.x + icon.rect.w * 0.5f;
        motion.y = icon.rect.y + icon.rect.h * 0.5f;
        ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseMotion(motion));
        for (double passed = 0.0; passed <= UITooltip::kHoverDelay + kFrameStep; passed += kFrameStep) {
            CoreManager::GetTime().Advance(kFrameStep);
            Draw();
        }
        const size_t shown = _renderer->calls.size();

        const uint64_t allocations = CountSteadyFrameAllocations([&](int) {
            CoreManager::GetTime().Advance(kFrameStep);
            Draw();
        });

        EXPECT_GT(shown, withoutTooltip) << "the tooltip plate is drawn over the open list";
        EXPECT_TRUE(dropdown.IsOpen());
        EXPECT_EQ(_renderer->calls.size(), shown);
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(allocations, 0u);
        }
        CoreManager::GetTime().Reset();
    }

}  // namespace BECore::Tests
