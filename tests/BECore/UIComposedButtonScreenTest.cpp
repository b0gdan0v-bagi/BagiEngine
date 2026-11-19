#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIImageComponent.h>
#include <BECore/UI/UIWebCompiler.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/UILayoutMatrix.h>
#include <Common/UITestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * The screen test of composed buttons (`UIElement` with `UIImageComponent`, `UITextComponent`, `UIInputClicker`, `UITooltipComponent`; .claude/rules/ui-controls.md,
     * "Components"), on the validator matrix: the touch rule and the text fit are held on every case of the matrix for the forms a button is written in (icon only,
     * with a tooltip, disabled, captioned, icon-left, from HTML). An icon-left button is a stack holding an image child and a text child (named `Left/Icon`, `Left/Text`: the part
     * before the `/` is the button).
     *
     * The touch-size rule is the validator's and does not know icons: the target is the rect of the button, never the icon drawn in it, so an icon button passes or fails by its
     * rect alone, and the icon and the tooltip add no finding of their own (the icon bar below, four round icon-only buttons and an icon-left one).
     *
     * The canvas is a SceneView canvas of a 480 x 300 view: the width of the narrowest window of the matrix, so the view is drawn at scale 1 or more and a button of 48 units is 48
     * window units on screen on every case.
     */

    namespace {

        constexpr float kViewWidth = 480.0f;
        constexpr float kViewHeight = 300.0f;
        constexpr float kTouchSize = 48.0f;  // the least side of a touch target on the smallest scale of the matrix (1.0)
        constexpr float kSmallSide = 40.0f;  // a button that is a hair too small
        constexpr size_t kRoundButtons = 4;
        constexpr float kEpsilon = 1e-3f;
        // What the HTML compiler and the migration step write for an icon button: a picture child of this share of the button, or of this share of its height next to a caption
        constexpr float kIconOnlyShare = UIImageComponent::kIconOnlyShare;
        constexpr float kIconWithTextShare = UIImageComponent::kIconWithTextShare;
        constexpr float kIconPadding = UIImageComponent::kIconPadding;
        constexpr float kIconGap = UIImageComponent::kIconGap;

        // Row one (y 10): an icon-only button (@SIDE@ units, `aspectRatio` 1), one with a tooltip, a disabled one.
        // Row two (y 100): a captioned button with rounded corners (120 x @SIDE@). Row three (y 160): the icon-left button, a stack (200 x @SIDE@).
        // @EXTRA@ is empty or a captioned button of 80 units whose clipped text does not fit.
        constexpr const char* kBarXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main" viewWidth="480" viewHeight="300"><nodes>
    <node name="Bar" layer="10"><components>
      <component type="UICanvasComponent" space="SceneView">
        <elements>
          <element type="UIElement" name="Icon" aspectRatio="1" x="10" y="10" width="@SIDE@" height="@SIDE@" color="40,60,90,255">
            <components><component type="UIImageComponent" sprite="BarIcon"/><component type="UIInputClicker" action="IconBar"/></components>
          </element>

          <element type="UIElement" name="Tip" aspectRatio="1" x="130" y="10" width="@SIDE@" height="@SIDE@" color="40,60,90,255">
            <components>
              <component type="UIImageComponent" sprite="BarIcon"/><component type="UIInputClicker" action="TipBar"/><component type="UITooltipComponent" text="{#bar.tip}"/>
            </components>
          </element>

          <element type="UIElement" name="Off" aspectRatio="1" x="250" y="10" width="@SIDE@" height="@SIDE@" color="40,60,90,255">
            <components><component type="UIImageComponent" sprite="BarIcon"/><component type="UIInputClicker" action="OffBar" disabled="true"/></components>
          </element>

          <element type="UIElement" name="Caption" x="10" y="100" width="120" height="@SIDE@" color="40,60,90,255" cornerRadius="8" borderWidth="2" borderColor="200,200,220,255">
            <components>
              <component type="UITextComponent" text="Go" align="Center" verticalAlign="Center"/><component type="UIInputClicker" action="CaptionBar"/>
            </components>
          </element>

          <element type="UIStack" name="Left" direction="Horizontal" spacing="8" crossAlign="Center" x="10" y="160" width="200" height="@SIDE@" color="40,60,90,255">
            <components><component type="UIInputClicker" action="LeftBar"/></components>
            <children>
              <element type="UIElement" name="Left/Icon" width="32" height="32"><components><component type="UIImageComponent" sprite="BarIcon"/></components></element>
              <element type="UIElement" name="Left/Text" width="100" height="32">
                <components><component type="UITextComponent" text="Go" verticalAlign="Center"/></components>
              </element>
            </children>
          </element>
@EXTRA@
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr const char* kNarrowXml = R"(
          <element type="UIElement" name="Narrow" x="10" y="220" width="80" height="48" color="40,60,90,255">
            <components>
              <component type="UITextComponent" text="A caption far too long for its rect" wrap="false" overflow="Clip"/><component type="UIInputClicker" action="NarrowBar"/>
            </components>
          </element>
)";

        // The bar in the exact forms the HTML compiler and the migration write (ui-controls.md, "Components"). Row one: four icon-only buttons (@SIDE@ units, `aspectRatio` 1, a picture child at 60%);
        // row two: an icon-left button (200 x 48), the tooltips are plain text and a `{#key}`.
        constexpr const char* kIconBarXml = R"(
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

        // The scene around a list of elements, for the screens that are not written in XML here
        constexpr const char* kSceneHead = R"(<SceneManager><scenes>
  <scene type="Scene" name="Main" viewWidth="480" viewHeight="300"><nodes>
    <node name="Bar" layer="10"><components>
      <component type="UICanvasComponent" space="SceneView"><elements>)";
        constexpr const char* kSceneTail = R"(</elements></component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        // Two buttons written in HTML + CSS: an icon-only one and an icon-left one
        String HtmlBarXml(float side) {
            const String sideUnits = ToString(static_cast<int>(side));
            const String sidePx = Format("{}px", sideUnits);
            const String source = Format("<style>#Icon {{ left: 10px; top: 10px; width: {0}; height: {0}; background: #283c5a }} #Left {{ left: 70px; top: 10px; width: 200px; height: {0}; "
                                         "background: #283c5a }}</style>"
                                         R"(<button id="Icon" data-icon="BarIcon" data-action="IconBar"></button><button id="Left" data-icon="BarIcon" data-action="LeftBar">Go</button>)",
                                         sidePx);
            UIWebReport report;
            const String compiled = UIWebCompiler::Compile(source, {}, report);
            EXPECT_TRUE(report.issues.empty());
            const String xml(compiled.c_str());
            const size_t begin = xml.find("<elements>") + String("<elements>").size();
            const size_t end = xml.find("</elements>");
            return Format("{}{}{}", kSceneHead, xml.substr(begin, end - begin), kSceneTail);
        }

        // A segmented group written in HTML + CSS: an icon-only segment, an icon-left one and a plain one, in a row (the segments are composed like any button)
        String HtmlGroupXml(float side) {
            const String sidePx = Format("{}px", static_cast<int>(side));
            const String source = Format(
                "<style>#Seg {{ display: flex; gap: 4px; left: 10px; top: 10px; width: 400px; height: {0}; background: #1e1e1e }} #Seg button {{ flex: 1; height: {0}; "
                "background: #283c5a }}</style>"
                R"(<div id="Seg" data-widget="segmented" data-bind="seg"><button id="SegA" data-value="a" data-icon="BarIcon"></button><button id="SegB" data-value="b" data-icon="BarIcon">Go</button><button id="SegC" data-value="c">Plain</button></div>)",
                sidePx);
            UIWebReport report;
            const String compiled = UIWebCompiler::Compile(source, {}, report);
            EXPECT_TRUE(report.issues.empty());
            const String xml(compiled.c_str());
            const size_t begin = xml.find("<elements>") + String("<elements>").size();
            const size_t end = xml.find("</elements>");
            return Format("{}{}{}", kSceneHead, xml.substr(begin, end - begin), kSceneTail);
        }

        String BarXml(float side, bool narrow) {
            String xml = kBarXml;
            ReplaceAll(xml, "@SIDE@", ToString(static_cast<int>(side)));
            ReplaceAll(xml, "@EXTRA@", narrow ? kNarrowXml : "");
            return xml;
        }

        String IconBarXml(float side) {
            String xml = kIconBarXml;
            ReplaceAll(xml, "@SIDE@", ToString(static_cast<int>(side)));
            return xml;
        }

        // One of the four round buttons themselves ("Round0" .. "Round3"), not a part of one
        bool IsRound(const UILayoutIssue& issue) {
            const StringView name = issue.element.ToStringView();
            return name.size() == StringView("Round0").size() && name.substr(0, StringView("Round").size()) == "Round";
        }

        // The button an element name belongs to: "Left/Text" is a part of "Left"
        String ButtonOf(const PoolString& element) {
            String name(element.CStr());
            const size_t part = name.find('/');
            if (part != String::npos) {
                name.resize(part);
            }
            return name;
        }

        bool Reports(const Vector<UIMatrixFailure>& failures, UILayoutIssueKind kind, const char* button) {
            for (const UIMatrixFailure& failure : failures) {
                for (const UILayoutIssue& issue : failure.issues) {
                    if (issue.kind == kind && ButtonOf(issue.element) == button) {
                        return true;
                    }
                }
            }
            return false;
        }

    }  // namespace

    class UIComposedButtonScreenTest : public UISceneFixture {
    protected:
        void SetUp() override {
            UISceneFixture::SetUp();
            _renderer->width = kViewWidth;
            _renderer->height = kViewHeight;
        }

        void Load(float side, bool narrow = false) {
            LoadXml(BarXml(side, narrow));
        }

        void LoadXml(const String& xml) {
            ASSERT_NO_FATAL_FAILURE(LoadScene(xml));
            ASSERT_NO_FATAL_FAILURE(UseCanvas("Bar"_intern));
            _canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        }

        Vector<UIMatrixFailure> OnMatrix() const {
            UILayoutValidatorOptions options;
            options.minTouchSize = kTouchSize;
            UILayoutContext context;
            context.sceneViewWidth = kViewWidth;
            context.sceneViewHeight = kViewHeight;
            return ValidateOnMatrix(_canvas->GetLayout(), options, context);
        }
    };

    TEST_F(UIComposedButtonScreenTest, ABarOfComposedButtonsOfTheTouchSizeIsCleanOnTheWholeMatrix) {
        ASSERT_NO_FATAL_FAILURE(Load(kTouchSize));
        const auto failures = OnMatrix();
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(UIComposedButtonScreenTest, TheTouchRuleReportsTheComposedButtonsThatAreTooSmall) {
        ASSERT_NO_FATAL_FAILURE(Load(kSmallSide));
        const auto failures = OnMatrix();

        EXPECT_FALSE(failures.empty());
        for (const char* button : {"Icon", "Tip", "Off", "Caption", "Left"}) {
            EXPECT_TRUE(Reports(failures, UILayoutIssueKind::TouchTargetTooSmall, button)) << button << " is under the touch size of 48 and is a target by its clicker";
        }
        for (const UIMatrixFailure& failure : failures) {
            for (const UILayoutIssue& issue : failure.issues) {
                EXPECT_EQ(issue.kind, UILayoutIssueKind::TouchTargetTooSmall) << failure.label.c_str() << " " << issue.element.CStr() << ": the image and the text add no finding";
            }
        }
    }

    TEST_F(UIComposedButtonScreenTest, TheButtonsOfAnHtmlDocumentAreReportedByTheMatrixWhenTooSmall) {
        for (const float side : {kTouchSize, kSmallSide}) {
            SCOPED_TRACE(side);
            ASSERT_NO_FATAL_FAILURE(LoadXml(HtmlBarXml(side)));
            const auto failures = OnMatrix();
            EXPECT_EQ(failures.empty(), side >= kTouchSize) << DescribeMatrixFailures(failures).c_str();
            if (side < kTouchSize) {
                EXPECT_TRUE(Reports(failures, UILayoutIssueKind::TouchTargetTooSmall, "Icon"));
                EXPECT_TRUE(Reports(failures, UILayoutIssueKind::TouchTargetTooSmall, "Left"));
            }
        }
    }

    TEST_F(UIComposedButtonScreenTest, TheIconAndTheCaptionOfAnHtmlButtonLieByTheShares) {
        ASSERT_NO_FATAL_FAILURE(LoadXml(HtmlBarXml(kTouchSize)));
        const auto expectRect = [](const Rect& expected, const Rect& actual, const char* what) {
            EXPECT_NEAR(expected.x, actual.x, 1.0f) << what;
            EXPECT_NEAR(expected.y, actual.y, 1.0f) << what;
            EXPECT_NEAR(expected.w, actual.w, 1.0f) << what;
            EXPECT_NEAR(expected.h, actual.h, 1.0f) << what;
        };
        // Icon left of a caption: padding on the left, the icon a square of half the height centred on the cross axis, a gap, then the caption takes the rest of the width
        const Rect left = El("Left").rect;
        const float icon = left.h * kIconWithTextShare;
        expectRect({left.x + kIconPadding, left.y + (left.h - icon) * 0.5f, icon, icon}, El("LeftIcon").rect, "the icon of the icon-left button");
        const float captionX = left.x + kIconPadding + icon + kIconGap;
        expectRect({captionX, left.y, left.x + left.w - captionX, left.h}, El("LeftCaption").rect, "the caption of the icon-left button");
        // Icon only: a centred square of 60% of the button
        const Rect only = El("Icon").rect;
        const float side = std::min(only.w, only.h) * kIconOnlyShare;
        expectRect({only.x + (only.w - side) * 0.5f, only.y + (only.h - side) * 0.5f, side, side}, El("IconIcon").rect, "the icon of the icon-only button");
    }

    TEST_F(UIComposedButtonScreenTest, TheSegmentsOfAnHtmlGroupAreTargetsOfTheMatrixByTheirClickers) {
        ASSERT_NO_FATAL_FAILURE(LoadXml(HtmlGroupXml(kTouchSize)));
        const auto failures = OnMatrix();
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        for (const char* segment : {"SegA", "SegB", "SegC"}) {
            EXPECT_TRUE(El(segment).IsInteractive()) << segment;
        }

        ASSERT_NO_FATAL_FAILURE(LoadXml(HtmlGroupXml(kSmallSide)));
        const auto small = OnMatrix();
        for (const char* segment : {"SegA", "SegB", "SegC"}) {
            EXPECT_TRUE(Reports(small, UILayoutIssueKind::TouchTargetTooSmall, segment)) << segment << " is under the touch size of 48";
        }
    }

    TEST_F(UIComposedButtonScreenTest, ACaptionThatDoesNotFitIsReported) {
        ASSERT_NO_FATAL_FAILURE(Load(kTouchSize, true));
        const auto failures = OnMatrix();
        EXPECT_TRUE(Reports(failures, UILayoutIssueKind::TextDoesNotFit, "Narrow"));
    }

    TEST_F(UIComposedButtonScreenTest, TheTargetOfAClickerIsTheRectOfItsElementNotTheImage) {
        ASSERT_NO_FATAL_FAILURE(Load(kTouchSize));
        UIElement& icon = El("Icon");
        ASSERT_TRUE(icon.IsInteractive());
        EXPECT_TRUE(El("Left").IsInteractive());
        EXPECT_FALSE(El("Left/Icon").IsInteractive()) << "the image child of a stack button is no target of its own";

        // The rect of the icon-only element is 48 x 48 and the picture is fitted in it: a point at the very corner is outside the picture and is still the button
        const UICanvasLayout& layout = _canvas->GetLayout();
        const Rect rect = icon.rect;
        EXPECT_EQ(layout.HitTest(rect.x + kEpsilon, rect.y + kEpsilon), &icon);
        EXPECT_EQ(layout.HitTest(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f), &icon);
        EXPECT_EQ(layout.HitTest(rect.x + rect.w + 1.0f, rect.y + rect.h * 0.5f), nullptr);

        // The image child and the text child of the stack hand the press to the stack
        UIElement& left = El("Left");
        for (const char* part : {"Left/Icon", "Left/Text"}) {
            const Rect inner = El(part).rect;
            ASSERT_FALSE(inner.IsEmpty()) << part;
            EXPECT_EQ(layout.HitTest(inner.x + inner.w * 0.5f, inner.y + inner.h * 0.5f), &left) << part;
        }
    }

    TEST_F(UIComposedButtonScreenTest, TheTooltipOfAComposedButtonIsItsComponentsText) {
        ASSERT_NO_FATAL_FAILURE(Load(kTouchSize));
        EXPECT_EQ(El("Tip").GetTooltipText(), "{#bar.tip}");
        EXPECT_TRUE(El("Icon").GetTooltipText().empty());
    }

    TEST_F(UIComposedButtonScreenTest, ADisabledComposedButtonIsStillATargetOfTheMatrix) {
        ASSERT_NO_FATAL_FAILURE(Load(kTouchSize));
        EXPECT_TRUE(El("Off").IsDisabled());
        EXPECT_TRUE(El("Off").IsInteractive());
    }

    // ---- the icon bar: the rect of the button is the target ----------------------------------------------------------------

    TEST_F(UIComposedButtonScreenTest, TheRectOfTheButtonIsTheTargetNotTheIconInIt) {
        ASSERT_NO_FATAL_FAILURE(LoadXml(IconBarXml(kTouchSize)));
        const auto clean = OnMatrix();
        EXPECT_TRUE(clean.empty()) << DescribeMatrixFailures(clean).c_str();

        ASSERT_NO_FATAL_FAILURE(LoadXml(IconBarXml(kSmallSide)));
        const auto failures = OnMatrix();
        ASSERT_FALSE(failures.empty()) << "a round button of 40 units is under the touch size of 48 however big its icon is";

        Vector<PoolString> reported;
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

    TEST_F(UIComposedButtonScreenTest, AnIconBiggerThanTheRectDoesNotMakeTheTargetBigger) {
        ASSERT_NO_FATAL_FAILURE(LoadXml(IconBarXml(kSmallSide)));
        UIElement& round = El("Round0");
        UIElement& icon = El("Round0Icon");
        ASSERT_FALSE(HasFatalFailure());
        icon.style.rect.width = UILength::Px(200.0f);  // the picture asks for far more than the button has
        icon.style.rect.height = UILength::Px(200.0f);
        _canvas->Invalidate();
        EXPECT_FALSE(OnMatrix().empty()) << "the target is still the 40 units of the button";
        EXPECT_FLOAT_EQ(round.rect.w, kSmallSide);
        EXPECT_FALSE(icon.IsInteractive()) << "the picture is not a target";
    }

}  // namespace BECore::Tests
