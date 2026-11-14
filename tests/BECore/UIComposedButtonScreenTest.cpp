#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
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
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * The screen test of composed buttons (`UIElement` with `UIImageComponent`, `UITextComponent`, `UIInputClicker`, `UITooltipComponent`; .claude/rules/ui-controls.md,
     * "Components"), on the validator matrix like `UIIconButtonScreenTest`: the touch rule and the text fit are held on every case of the matrix for the forms a button is written in (icon only,
     * with a tooltip, disabled, captioned, icon-left, from HTML). An icon-left button is a stack holding an image child and a text child (named `Left/Icon`, `Left/Text`: the part
     * before the `/` is the button).
     *
     * The canvas is a SceneView canvas of a 480 x 300 view: the width of the narrowest window of the matrix, so the view is drawn at scale 1 or more and a button of 48 units is 48
     * window units on screen on every case.
     */

    namespace {

        constexpr float kViewWidth = 480.0f;
        constexpr float kViewHeight = 300.0f;
        constexpr float kTouchSize = 48.0f;  // the least side of a touch target on the smallest scale of the matrix (1.0)
        constexpr float kSmallSide = 40.0f;  // a button that is a hair too small
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
            const String sidePx = sideUnits + "px";
            const String source = "<style>#Icon { left: 10px; top: 10px; width: " + sidePx + "; height: " + sidePx +
                                  "; background: #283c5a } #Left { left: 70px; top: 10px; width: 200px; height: " + sidePx + "; background: #283c5a }</style>" +
                                  R"(<button id="Icon" data-icon="BarIcon" data-action="IconBar"></button><button id="Left" data-icon="BarIcon" data-action="LeftBar">Go</button>)";
            UIWebReport report;
            const String compiled = UIWebCompiler::Compile(source, {}, report);
            EXPECT_TRUE(report.issues.empty());
            const String xml(compiled.c_str());
            const size_t begin = xml.find("<elements>") + String("<elements>").size();
            const size_t end = xml.find("</elements>");
            return kSceneHead + xml.substr(begin, end - begin) + kSceneTail;
        }

        // A segmented group written in HTML + CSS: an icon-only segment, an icon-left one and a plain one, in a row (the segments are composed like any button)
        String HtmlGroupXml(float side) {
            const String sidePx = ToString(static_cast<int>(side)) + "px";
            const String source =
                "<style>#Seg { display: flex; gap: 4px; left: 10px; top: 10px; width: 400px; height: " + sidePx + "; background: #1e1e1e } #Seg button { flex: 1; height: " + sidePx +
                "; background: #283c5a }</style>" +
                R"(<div id="Seg" data-widget="segmented" data-bind="seg"><button id="SegA" data-value="a" data-icon="BarIcon"></button><button id="SegB" data-value="b" data-icon="BarIcon">Go</button><button id="SegC" data-value="c">Plain</button></div>)";
            UIWebReport report;
            const String compiled = UIWebCompiler::Compile(source, {}, report);
            EXPECT_TRUE(report.issues.empty());
            const String xml(compiled.c_str());
            const size_t begin = xml.find("<elements>") + String("<elements>").size();
            const size_t end = xml.find("</elements>");
            return kSceneHead + xml.substr(begin, end - begin) + kSceneTail;
        }

        void ReplaceAll(String& text, const String& what, const String& with) {
            for (size_t at = text.find(what); at != String::npos; at = text.find(what, at + with.size())) {
                text.replace(at, what.size(), with);
            }
        }

        String BarXml(float side, bool narrow) {
            String xml = kBarXml;
            ReplaceAll(xml, "@SIDE@", ToString(static_cast<int>(side)));
            ReplaceAll(xml, "@EXTRA@", narrow ? kNarrowXml : "");
            return xml;
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

    class UIComposedButtonScreenTest : public ::testing::Test {
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

        void Load(float side, bool narrow = false) {
            LoadXml(BarXml(side, narrow));
        }

        void LoadXml(const String& xml) {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(xml));
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

        Vector<UIMatrixFailure> OnMatrix() const {
            UILayoutValidatorOptions options;
            options.minTouchSize = kTouchSize;
            UILayoutContext context;
            context.sceneViewWidth = kViewWidth;
            context.sceneViewHeight = kViewHeight;
            return ValidateOnMatrix(_bar->GetLayout(), options, context);
        }

        UIElement& Get(const char* name) const {
            UIElement* element = _bar->GetRoot().FindByName(PoolString::Intern(name));
            if (!element) {
                ADD_FAILURE() << "no element " << name;
                std::abort();
            }
            return *element;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _bar;
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
        const Rect left = Get("Left").rect;
        const float icon = left.h * kIconWithTextShare;
        expectRect({left.x + kIconPadding, left.y + (left.h - icon) * 0.5f, icon, icon}, Get("LeftIcon").rect, "the icon of the icon-left button");
        const float captionX = left.x + kIconPadding + icon + kIconGap;
        expectRect({captionX, left.y, left.x + left.w - captionX, left.h}, Get("LeftCaption").rect, "the caption of the icon-left button");
        // Icon only: a centred square of 60% of the button
        const Rect only = Get("Icon").rect;
        const float side = std::min(only.w, only.h) * kIconOnlyShare;
        expectRect({only.x + (only.w - side) * 0.5f, only.y + (only.h - side) * 0.5f, side, side}, Get("IconIcon").rect, "the icon of the icon-only button");
    }

    TEST_F(UIComposedButtonScreenTest, TheSegmentsOfAnHtmlGroupAreTargetsOfTheMatrixByTheirClickers) {
        ASSERT_NO_FATAL_FAILURE(LoadXml(HtmlGroupXml(kTouchSize)));
        const auto failures = OnMatrix();
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        for (const char* segment : {"SegA", "SegB", "SegC"}) {
            EXPECT_TRUE(Get(segment).IsInteractive()) << segment;
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
        UIElement& icon = Get("Icon");
        ASSERT_TRUE(icon.IsInteractive());
        EXPECT_TRUE(Get("Left").IsInteractive());
        EXPECT_FALSE(Get("Left/Icon").IsInteractive()) << "the image child of a stack button is no target of its own";

        // The rect of the icon-only element is 48 x 48 and the picture is fitted in it: a point at the very corner is outside the picture and is still the button
        const UICanvasLayout& layout = _bar->GetLayout();
        const Rect rect = icon.rect;
        EXPECT_EQ(layout.HitTest(rect.x + kEpsilon, rect.y + kEpsilon), &icon);
        EXPECT_EQ(layout.HitTest(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f), &icon);
        EXPECT_EQ(layout.HitTest(rect.x + rect.w + 1.0f, rect.y + rect.h * 0.5f), nullptr);

        // The image child and the text child of the stack hand the press to the stack
        UIElement& left = Get("Left");
        for (const char* part : {"Left/Icon", "Left/Text"}) {
            const Rect inner = Get(part).rect;
            ASSERT_FALSE(inner.IsEmpty()) << part;
            EXPECT_EQ(layout.HitTest(inner.x + inner.w * 0.5f, inner.y + inner.h * 0.5f), &left) << part;
        }
    }

    TEST_F(UIComposedButtonScreenTest, TheTooltipOfAComposedButtonIsItsComponentsText) {
        ASSERT_NO_FATAL_FAILURE(Load(kTouchSize));
        EXPECT_EQ(Get("Tip").GetTooltipText(), "{#bar.tip}");
        EXPECT_TRUE(Get("Icon").GetTooltipText().empty());
    }

    TEST_F(UIComposedButtonScreenTest, ADisabledComposedButtonIsStillATargetOfTheMatrix) {
        ASSERT_NO_FATAL_FAILURE(Load(kTouchSize));
        EXPECT_TRUE(Get("Off").IsDisabled());
        EXPECT_TRUE(Get("Off").IsInteractive());
    }

}  // namespace BECore::Tests
