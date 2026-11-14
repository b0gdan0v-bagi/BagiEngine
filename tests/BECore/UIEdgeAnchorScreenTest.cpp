#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasScaler.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UIScreenContent.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UITextComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/UILayoutMatrix.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * The screen test of a screen written only with edge anchors and percents (.claude/rules/ui-layout.md, "Edge anchors"); the template for a screen that has no pixel
     * position in its file. No `x`, no `y` of a point, no fixed width: the shapes of the window decide.
     *
     *  - `Side`: a panel pinned to `anchorMin=(0.65,0)` `anchorMax=(1,1)`, the right 35% of the canvas, with a title that spans it and an OK button pinned to its bottom edge
     *    (`pivot=(0.5,1)`). In portrait a variant moves its min to `(0,0.6)`: the lower 40%.
     *  - `Bar`: in the region "play" (the part of the canvas the game keeps for the table, published from the content bounds by `PublishPlay`), from 10% to 90% of it, its bottom edge
     *    16 units above the region's bottom. In portrait a variant widens it to 5%..95%.
     *
     * The regions are the game's: the test passes `PublishPlay` to ValidateOnMatrix, so "inside its region" is checked on the whole matrix, in each of the languages of the table.
     */

    namespace {

        constexpr float kTolerance = 0.51f;
        constexpr float kSideStart = 0.65f;     // the left edge of the side panel, a fraction of the canvas width
        constexpr float kPortraitSplit = 0.6f;  // the top of the side panel in portrait, and the height of the play region there
        constexpr float kReferenceWidth = 1280.0f;
        constexpr float kReferenceHeight = 720.0f;

        constexpr const char* kScreenBody = R"(
  <elements>
    <element type="UIPanel" name="Side" anchorMin="0.65,0" anchorMax="1,1" margin="8" color="0,0,0,160">
      <children>
        <element type="UIElement" name="Title" anchorMin="0,0" anchorMax="1,0" height="48" y="12" margin="12,0,12,0" fontSize="28">
          <components><component type="UITextComponent" text="{#edge.title}"/></components>
        </element>
        <element type="UIElement" name="Ok" anchorMin="0,1" anchorMax="1,1" pivot="0.5,1" height="56" y="-12" margin="12,0,12,0" fontSize="26" color="40,90,60,255">
          <components>
            <component type="UITextComponent" text="{#edge.ok}" align="Center" verticalAlign="Center" wrap="false"/>
            <component type="UIInputClicker" action="EdgeScreenOk"/>
          </components>
        </element>
      </children>
    </element>
    <element type="UIPanel" name="Bar" region="play" anchorMin="10%,1" anchorMax="90%,1" pivot="0.5,1" y="-16" height="56" color="0,0,0,160">
      <children>
        <element type="UIElement" name="Status" anchor="Stretch" margin="8" fontSize="24">
          <components><component type="UITextComponent" text="{#edge.status}"/></components>
        </element>
      </children>
    </element>
  </elements>
  <variants>
    <variant when="orientation:portrait">
      <overrides>
        <set element="Side" anchorMin="0,0.6"/>
        <set element="Bar" anchorMin="0.05,1" anchorMax="0.95,1"/>
      </overrides>
    </variant>
  </variants>)";

        // English, Russian and a long test language, so the text of the title, the button and the status has to fit in all of them
        constexpr const char* kStringsXml = R"(
<UIStrings default="en">
  <language id="en">
    <string key="edge.title" text="Settings"/>
    <string key="edge.ok" text="OK"/>
    <string key="edge.status" text="Your turn"/>
  </language>
  <language id="ru">
    <string key="edge.title" text="Настройки"/>
    <string key="edge.ok" text="Готово"/>
    <string key="edge.status" text="Ваш ход"/>
  </language>
  <language id="de">
    <string key="edge.title" text="Einstellungen"/>
    <string key="edge.ok" text="Übernehmen"/>
    <string key="edge.status" text="Du bist am Zug"/>
  </language>
</UIStrings>)";

        /// What the game computes from the bounds of the case: the region "play", the left part of the content bounds (the upper part in portrait)
        void PublishPlay(UIRegionSet& regions, const UICanvasResolved& resolved) {
            const Rect& area = resolved.contentBounds;
            const bool portrait = area.h > area.w;
            regions.Set("play"_intern, portrait ? Rect{area.x, area.y, area.w, area.h * kPortraitSplit} : Rect{area.x, area.y, area.w * kSideStart, area.h});
        }

        /// A pure canvas layout with the screen, built through the same reflected path as a `.ui.xml` file; @p edit changes the text of the file first
        void BuildLayout(UICanvasLayout& layout, const std::function<void(std::string&)>& edit = {}) {
            std::string body = kScreenBody;
            if (edit) {
                edit(body);
            }
            const std::string xml = "<root>" + body + "</root>";
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(FromStd(xml)));
            UIScreenContent content;
            content.Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());

            layout.root->children = content.elements;
            layout.variants = content.variants;
            layout.params.space = CanvasSpace::Screen;
            layout.params.scaleMode = CanvasScaleMode::ScaleWithScreen;
            layout.params.referenceWidth = kReferenceWidth;
            layout.params.referenceHeight = kReferenceHeight;
            layout.params.match = 0.5f;
            layout.params.minScale = 0.7f;
            layout.params.useSafeArea = true;
            layout.Invalidate();
        }

        /// Replaces the first @p what in @p text, and fails the test when it is not there (a broken copy that changed nothing would prove nothing)
        void Replace(std::string& text, const std::string& what, const std::string& with) {
            const size_t at = text.find(what);
            ASSERT_NE(at, std::string::npos) << what;
            text.replace(at, what.size(), with);
        }

        void LayOutAt(UICanvasLayout& layout, float width, float height) {
            UILayoutContext context;
            context.outputWidth = width;
            context.outputHeight = height;
            layout.regions.Clear();
            PublishPlay(layout.regions, UICanvasScaler::Resolve(layout.params, context));
            layout.Update(context);
        }

        const Rect& RectOf(const UICanvasLayout& layout, const char* name) {
            static const Rect none;
            const UIElement* element = layout.root->FindByName(PoolString::Intern(name));
            if (element == nullptr) {
                ADD_FAILURE() << name << " is not in the screen";
                return none;
            }
            return element->rect;
        }

        void ExpectRect(const Rect& rect, float x, float y, float w, float h) {
            EXPECT_NEAR(rect.x, x, kTolerance);
            EXPECT_NEAR(rect.y, y, kTolerance);
            EXPECT_NEAR(rect.w, w, kTolerance);
            EXPECT_NEAR(rect.h, h, kTolerance);
        }

        bool HasIssue(const eastl::vector<UIMatrixFailure>& failures, UILayoutIssueKind kind) {
            for (const auto& failure : failures) {
                for (const UILayoutIssue& issue : failure.issues) {
                    if (issue.kind == kind) {
                        return true;
                    }
                }
            }
            return false;
        }

        constexpr float kButtonAir = 8.0f;  // the text of a button keeps this much from each edge of its fill

        /// The names of the labels and buttons of the laid-out screen whose control cuts its text in the language that is set now (an ellipsis label "can shrink", so the
        /// validator does not see it)
        String CutTexts(const UIElement& element, float scale) {
            String cut;
            const bool text = element.GetComponent<UITextComponent>() != nullptr;
            if (text && !element.rect.IsEmpty()) {
                const float air = element.GetComponent<UIInputClicker>() ? kButtonAir : 0.0f;  // a button is a text with a clicker
                const UIContentFit fit = element.CheckContent(element.rect.w - 2.0f * air, element.rect.h, scale);
                if (fit.hasContent && !fit.fits) {
                    cut += Format("{} ", element.name.ToStringView());
                }
            }
            for (const auto& child : element.children) {
                cut += CutTexts(*child, scale);
            }
            return cut;
        }

        /// Runs @p check once for every language of the table and puts the language that was current back
        template <typename Check>
        void ForEachLanguage(Check&& check) {
            UIStrings& strings = CoreManager::GetUIStrings();
            const PoolString saved = strings.GetLanguage();
            const eastl::vector<PoolString> languages = strings.GetLanguages();
            for (const PoolString language : languages) {
                ASSERT_TRUE(strings.SetLanguage(language)) << language.ToStringView().data();
                check(language);
            }
            strings.SetLanguage(saved);
        }

    }  // namespace

    class UIEdgeAnchorScreenTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetFontAtlasLibrary().Clear();
            CoreManager::GetUIStrings().Clear();
            _renderer = New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            auto config = XmlConfig::Create();
            ASSERT_TRUE(config.LoadFromString(kStringsXml));
            const eastl::vector<String> problems = CoreManager::GetUIStrings().LoadFromXml(config.GetRoot());
            EXPECT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0].c_str());
            ASSERT_EQ(CoreManager::GetUIStrings().GetLanguages().size(), 3u);
        }

        void TearDown() override {
            CoreManager::GetUIStrings().Clear();  // the table is global: the next test starts with none
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        static UILayoutValidatorOptions Options() {
            UILayoutValidatorOptions options;
            options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
            return options;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
    };

    // ---- the size matrix ----------------------------------------------------------------------------------------------------

    TEST_F(UIEdgeAnchorScreenTest, TheScreenPassesTheSizeMatrixInEveryLanguage) {
        UICanvasLayout layout;
        ASSERT_NO_FATAL_FAILURE(BuildLayout(layout));

        ForEachLanguage([&](PoolString language) {
            const auto failures = ValidateOnMatrix(layout, Options(), {}, {}, PublishPlay);
            EXPECT_TRUE(failures.empty()) << language.ToStringView().data() << ": " << DescribeMatrixFailures(failures).c_str();
        });
    }

    TEST_F(UIEdgeAnchorScreenTest, NoControlCutsItsTextInAnyLanguageAtTheSmallAndTheLargeSizes) {
        UICanvasLayout layout;
        ASSERT_NO_FATAL_FAILURE(BuildLayout(layout));

        ForEachLanguage([&](PoolString language) {
            for (const Rect& size : {Rect{0, 0, 480, 480}, Rect{0, 0, 640, 480}, Rect{0, 0, 480, 640}, Rect{0, 0, 1280, 720}, Rect{0, 0, 2560, 1440}}) {
                LayOutAt(layout, size.w, size.h);
                EXPECT_TRUE(CutTexts(*layout.root, layout.GetResolved().scale).empty())
                    << language.ToStringView().data() << " " << size.w << "x" << size.h << ": " << CutTexts(*layout.root, layout.GetResolved().scale).c_str();
            }
        });
    }

    TEST_F(UIEdgeAnchorScreenTest, ALongWordInTheStatusIsCutAndTheCheckSeesIt) {
        // A broken table: the status says more than the bar can hold at the smallest window
        CoreManager::GetUIStrings().Clear();
        auto config = XmlConfig::Create();
        ASSERT_TRUE(config.LoadFromString("<UIStrings default=\"en\"><language id=\"en\"><string key=\"edge.title\" text=\"Settings\"/><string key=\"edge.ok\" text=\"OK\"/>"
                                          "<string key=\"edge.status\" text=\"It is your turn now, please pick a tile to throw away or declare a win\"/></language></UIStrings>"));
        ASSERT_TRUE(CoreManager::GetUIStrings().LoadFromXml(config.GetRoot()).empty());
        UICanvasLayout layout;
        ASSERT_NO_FATAL_FAILURE(BuildLayout(layout));

        LayOutAt(layout, 480.0f, 480.0f);

        EXPECT_NE(CutTexts(*layout.root, layout.GetResolved().scale).find("Status"), String::npos);
    }

    TEST_F(UIEdgeAnchorScreenTest, AnOffsetThatLeavesTheRegionIsCaughtOnTheMatrix) {
        // The same screen with the bar pushed 40 units down from the bottom of its region instead of 16 up
        UICanvasLayout layout;
        ASSERT_NO_FATAL_FAILURE(BuildLayout(layout, [](std::string& text) { Replace(text, "y=\"-16\"", "y=\"40\""); }));

        const auto failures = ValidateOnMatrix(layout, Options(), {}, {}, PublishPlay);

        EXPECT_FALSE(failures.empty());
        EXPECT_TRUE(HasIssue(failures, UILayoutIssueKind::OutsideRegion)) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(UIEdgeAnchorScreenTest, ABarThatNamesARegionNobodyPublishesIsCaught) {
        UICanvasLayout layout;
        ASSERT_NO_FATAL_FAILURE(BuildLayout(layout));

        const auto failures = ValidateOnMatrix(layout, Options(), {}, {}, [](UIRegionSet&, const UICanvasResolved&) {});

        EXPECT_TRUE(HasIssue(failures, UILayoutIssueKind::MissingRegion));
    }

    TEST_F(UIEdgeAnchorScreenTest, AnEdgeAnchoredButtonThatTheWindowMakesTooSmallIsCaught) {
        // The OK button 20 units high instead of 56, with no minScale: too small to touch on a small window
        UICanvasLayout layout;
        ASSERT_NO_FATAL_FAILURE(BuildLayout(layout, [](std::string& text) { Replace(text, "height=\"56\" y=\"-12\"", "height=\"20\" y=\"-12\""); }));
        layout.params.minScale = 0.25f;
        layout.Invalidate();

        const auto failures = ValidateOnMatrix(layout, Options(), {}, {}, PublishPlay);

        EXPECT_TRUE(HasIssue(failures, UILayoutIssueKind::TouchTargetTooSmall));
    }

    // ---- the same screen in two shapes --------------------------------------------------------------------------------------

    TEST_F(UIEdgeAnchorScreenTest, InLandscapeThePanelIsTheRightThirdAndTheBarSitsInThePlayRegion) {
        UICanvasLayout layout;
        ASSERT_NO_FATAL_FAILURE(BuildLayout(layout));

        LayOutAt(layout, kReferenceWidth, kReferenceHeight);

        // 65% of 1280 is 832; the margin of 8 is taken from the span between the anchors
        ExpectRect(RectOf(layout, "Side"), 840.0f, 8.0f, 432.0f, 704.0f);
        // The title spans the panel minus 12 each side, 12 below its top; the button hangs on the bottom edge of the panel, 12 above it
        ExpectRect(RectOf(layout, "Title"), 852.0f, 20.0f, 408.0f, 48.0f);
        ExpectRect(RectOf(layout, "Ok"), 852.0f, 644.0f, 408.0f, 56.0f);
        // The region is (0, 0, 832, 720): 10% to 90% of it, the bottom edge 16 above its bottom
        ExpectRect(RectOf(layout, "Bar"), 83.2f, 648.0f, 665.6f, 56.0f);
        ExpectRect(RectOf(layout, "Status"), 91.2f, 656.0f, 649.6f, 40.0f);
    }

    TEST_F(UIEdgeAnchorScreenTest, InPortraitTheVariantMovesThePanelDownAndWidensTheBar) {
        UICanvasLayout layout;
        ASSERT_NO_FATAL_FAILURE(BuildLayout(layout));

        LayOutAt(layout, kReferenceHeight, kReferenceWidth);

        // anchorMin (0, 0.6) and the max of the file (1, 1): the lower 40% of 1280, inset by 8
        ExpectRect(RectOf(layout, "Side"), 8.0f, 776.0f, 704.0f, 496.0f);
        // The region is (0, 0, 720, 768); the variant sets 5%..95% of it
        ExpectRect(RectOf(layout, "Bar"), 36.0f, 696.0f, 648.0f, 56.0f);
    }

    TEST_F(UIEdgeAnchorScreenTest, AWindowTwiceAsLargeGivesTheSameScreenTwiceAsLarge) {
        UICanvasLayout layout;
        ASSERT_NO_FATAL_FAILURE(BuildLayout(layout));

        LayOutAt(layout, kReferenceWidth, kReferenceHeight);
        const Rect small = RectOf(layout, "Bar");
        const Rect side = RectOf(layout, "Side");
        // The canvas is in units: the same units on a window twice as large (scale 2), so the rects keep their values
        LayOutAt(layout, 2.0f * kReferenceWidth, 2.0f * kReferenceHeight);

        ExpectRect(RectOf(layout, "Bar"), small.x, small.y, small.w, small.h);
        ExpectRect(RectOf(layout, "Side"), side.x, side.y, side.w, side.h);
        EXPECT_NEAR(layout.GetResolved().scale, 2.0f, 0.001f);
    }

}  // namespace BECore::Tests
