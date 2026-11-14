#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIImageComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/UILayoutMatrix.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * The screen test of a bar of icon buttons (.claude/rules/ui-layout.md, "A screen test"): four icon-only buttons of the round style (`aspectRatio` 1, no caption) and an icon-left
     * button (the composed forms of `.claude/rules/ui-controls.md`, "Components"). The touch-size rule is the validator's and does not know icons: the target is the rect of the button, never the icon
     * drawn in it, so the icon buttons pass or fail by their rect alone, and the icon and the tooltip add no finding of their own.
     *
     * The canvas is a SceneView canvas of a 480 x 300 view: the width of the narrowest window of the matrix, so the view is drawn at scale 1 or more and a button of 48 units is 48
     * window units on screen on every case.
     */

    namespace {

        constexpr float kViewWidth = 480.0f;
        constexpr float kViewHeight = 300.0f;
        constexpr float kTouchSize = 48.0f;  // the least side of a touch target on the smallest scale of the matrix (1.0)
        constexpr float kSmallSide = 40.0f;  // a round button that is a hair too small
        constexpr size_t kRoundButtons = 4;

        // Row one: four icon-only buttons (@SIDE@ units, `aspectRatio` 1, a picture child at 60%); row two: an icon-left button (200 x 48), the tooltips are plain text and a `{#key}`.
        constexpr const char* kBarXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main" viewWidth="480" viewHeight="300"><nodes>
    <node name="Bar" layer="10"><components>
      <component type="UICanvasComponent" space="SceneView">
        <elements>
          <element type="UIElement" name="Round0" aspectRatio="1" x="10" y="10" width="@SIDE@" height="@SIDE@" color="40,60,90,255">
            <components><component type="UIInputClicker" action="IconBar0"/><component type="UITooltipComponent" text="First"/></components>
            <children>
              <element type="UIElement" name="Round0Icon" anchor="Center" width="60%" height="60%" aspectRatio="1"><components><component type="UIImageComponent" sprite="BarIcon"/></components></element>
            </children>
          </element>
          <element type="UIElement" name="Round1" aspectRatio="1" x="70" y="10" width="@SIDE@" height="@SIDE@" color="40,60,90,255">
            <components><component type="UIInputClicker" action="IconBar1"/><component type="UITooltipComponent" text="{#bar.second}"/></components>
            <children>
              <element type="UIElement" name="Round1Icon" anchor="Center" width="60%" height="60%" aspectRatio="1"><components><component type="UIImageComponent" sprite="BarIcon"/></components></element>
            </children>
          </element>
          <element type="UIElement" name="Round2" aspectRatio="1" x="130" y="10" width="@SIDE@" height="@SIDE@" color="40,60,90,255">
            <components><component type="UIInputClicker" action="IconBar2"/></components>
            <children>
              <element type="UIElement" name="Round2Icon" anchor="Center" width="60%" height="60%" aspectRatio="1"><components><component type="UIImageComponent" sprite="BarIcon"/></components></element>
            </children>
          </element>
          <element type="UIElement" name="Round3" aspectRatio="1" x="190" y="10" width="@SIDE@" height="@SIDE@" color="40,60,90,255">
            <components><component type="UIInputClicker" action="IconBar3"/><component type="UITooltipComponent" text="Fourth"/></components>
            <children>
              <element type="UIElement" name="Round3Icon" anchor="Center" width="60%" height="60%" aspectRatio="1"><components><component type="UIImageComponent" sprite="BarIcon"/></components></element>
            </children>
          </element>
          <element type="UIElement" name="WithText" x="10" y="100" width="200" height="48" layout="Stack" direction="Horizontal" padding="8,0,0,0" spacing="6" crossAlign="Center" color="40,60,90,255">
            <components><component type="UIInputClicker" action="IconBarText"/></components>
            <children>
              <element type="UIElement" name="WithTextIcon" height="50%" aspectRatio="1"><components><component type="UIImageComponent" sprite="BarIcon"/></components></element>
              <element type="UIElement" name="WithTextCaption" flex="1" height="100%" fontSize="24">
                <components><component type="UITextComponent" text="Go" align="Center" verticalAlign="Center" wrap="false"/></components>
              </element>
            </children>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        std::string BarXml(float side) {
            std::string xml = kBarXml;
            const std::string value = std::to_string(static_cast<int>(side));
            for (size_t at = xml.find("@SIDE@"); at != std::string::npos; at = xml.find("@SIDE@", at)) {
                xml.replace(at, 6, value);
            }
            return xml;
        }

        bool IsRound(const UILayoutIssue& issue) {
            const StringView name = issue.element.ToStringView();
            return name.size() == 6 && name.substr(0, 5) == "Round";
        }

    }  // namespace

    class UIIconButtonScreenTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetFontAtlasLibrary().Clear();
            _renderer = New<RecordingRenderer>();
            _renderer->width = kViewWidth;
            _renderer->height = kViewHeight;
            CoreManager::GetRendererManager().SetRenderer(_renderer);
        }

        void TearDown() override {
            _bar.Reset();
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        void Load(float side) {
            XmlDeserializer deserializer;
            const std::string xml = BarXml(side);
            ASSERT_TRUE(deserializer.LoadFromString(FromStd(xml)));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            for (const auto& node : _manager->GetSceneByName("Main"_intern)->GetRootNode().GetChildren()) {
                if (node->GetName() == "Bar"_intern) {
                    _bar = node->GetComponent<UICanvasComponent>();
                }
            }
            ASSERT_TRUE(_bar);
            _bar->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        }

        eastl::vector<UIMatrixFailure> OnMatrix() const {
            UILayoutValidatorOptions options;
            options.minTouchSize = kTouchSize;
            UILayoutContext context;
            context.sceneViewWidth = kViewWidth;
            context.sceneViewHeight = kViewHeight;
            return ValidateOnMatrix(_bar->GetLayout(), options, context);
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _bar;
    };

    TEST_F(UIIconButtonScreenTest, ABarOfIconButtonsOfTheTouchSizeIsCleanOnTheWholeMatrix) {
        ASSERT_NO_FATAL_FAILURE(Load(kTouchSize));
        const auto failures = OnMatrix();
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(UIIconButtonScreenTest, TheRectOfTheButtonIsTheTargetNotTheIconInIt) {
        ASSERT_NO_FATAL_FAILURE(Load(kSmallSide));
        const auto failures = OnMatrix();
        ASSERT_FALSE(failures.empty()) << "a round button of 40 units is under the touch size of 48 however big its icon is";

        eastl::vector<PoolString> reported;
        for (const UIMatrixFailure& failure : failures) {
            for (const UILayoutIssue& issue : failure.issues) {
                EXPECT_EQ(issue.kind, UILayoutIssueKind::TouchTargetTooSmall) << failure.label.c_str() << " " << issue.element.CStr();
                EXPECT_TRUE(IsRound(issue)) << issue.element.CStr() << " is 48 units or more and fine";
                if (eastl::find(reported.begin(), reported.end(), issue.element) == reported.end()) {
                    reported.push_back(issue.element);
                }
            }
        }
        EXPECT_EQ(reported.size(), kRoundButtons) << "every round button is reported, with a tooltip or without";
    }

    TEST_F(UIIconButtonScreenTest, AnIconBiggerThanTheRectDoesNotMakeTheTargetBigger) {
        ASSERT_NO_FATAL_FAILURE(Load(kSmallSide));
        UIElement* round = _bar->GetRoot().FindByName("Round0"_intern);
        UIElement* icon = _bar->GetRoot().FindByName("Round0Icon"_intern);
        ASSERT_TRUE(round && icon);
        icon->style.rect.width = UILength::Px(200.0f);  // the picture asks for far more than the button has
        icon->style.rect.height = UILength::Px(200.0f);
        _bar->Invalidate();
        EXPECT_FALSE(OnMatrix().empty()) << "the target is still the 40 units of the button";
        EXPECT_FLOAT_EQ(round->rect.w, kSmallSide);
        EXPECT_FALSE(icon->IsInteractive()) << "the picture is not a target";
    }

}  // namespace BECore::Tests
