#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UIPanel.h>
#include <BECore/UI/UIScreenContent.h>
#include <BECore/UI/UITextComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/UILayoutMatrix.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 0.51f;

        // A HUD the way a game writes it: a plate in the top left corner with a score and a Reset button, and in a portrait window the plate spans the top.
        // The button is 48 units at the reference size and the canvas has a minScale, so it stays a touch target on a small window (.claude/rules/ui-layout.md, "Design rules").
        constexpr const char* kHudBody = R"(
  <elements>
    <element type="UIPanel" name="Plate" anchor="TopLeft" x="16" y="16" width="260" height="120" color="0,0,0,160">
      <children>
        <element type="UIElement" name="Score" x="12" y="10" width="236" height="36" fontSize="28">
          <components><component type="UITextComponent" text="Score: {score}"/></components>
        </element>
        <element type="UIElement" name="Seed" x="12" y="52" width="100" height="24" fontSize="16">
          <components><component type="UITextComponent" text="seed {seed}" textColor="200,200,200,255"/></components>
        </element>
        <element type="UIElement" name="Reset" anchor="BottomRight" x="-12" y="-10" width="120" height="48" fontSize="24" color="40,90,60,255">
          <components>
            <component type="UITextComponent" text="Reset" align="Center" verticalAlign="Center" wrap="false"/>
            <component type="UIInputClicker" action="ScreenTestReset"/>
          </components>
        </element>
      </children>
    </element>
  </elements>
  <variants>
    <variant when="orientation:portrait">
      <overrides>
        <set element="Plate" anchor="Top" x="0" y="16" width="90%"/>
        <set element="Seed" visible="false"/>
      </overrides>
    </variant>
  </variants>)";

        constexpr const char* kHudAttributes = R"(space="Screen" scaleMode="ScaleWithScreen" referenceWidth="1280" referenceHeight="720" match="0.5" minScale="0.7" useSafeArea="true")";

        std::string HudScenesXml() {
            return std::string("<SceneManager><scenes><scene type=\"Scene\" name=\"Main\"><nodes><node name=\"Hud\" layer=\"10\"><components><component type=\"UICanvasComponent\" ") + kHudAttributes +
                   ">" + kHudBody + "</component></components></node></nodes></scene></scenes></SceneManager>";
        }

        /// A pure canvas layout with the HUD, built through the same reflected path as a scene file
        void BuildHudLayout(UICanvasLayout& layout) {
            const std::string xml = std::string("<root>") + kHudBody + "</root>";
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(FromStd(xml)));
            UIScreenContent content;
            content.Deserialize(deserializer);  // positioned at the document's root element already
            ASSERT_FALSE(deserializer.HasErrors());

            layout.root->children = content.elements;
            layout.variants = content.variants;
            layout.params.space = CanvasSpace::Screen;
            layout.params.scaleMode = CanvasScaleMode::ScaleWithScreen;
            layout.params.referenceWidth = 1280.0f;
            layout.params.referenceHeight = 720.0f;
            layout.params.match = 0.5f;
            layout.params.minScale = 0.7f;
            layout.params.useSafeArea = true;
            layout.Invalidate();
        }

    }  // namespace

    class UIScreenTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetFontAtlasLibrary().Clear();
            CoreManager::GetUIVars().Clear();
            _renderer = New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);
        }

        void TearDown() override {
            _canvas.Reset();
            _manager.Reset();
            CoreManager::GetUIVars().Clear();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        void LoadScene() {
            XmlDeserializer deserializer;
            const std::string xml = HudScenesXml();
            ASSERT_TRUE(deserializer.LoadFromString(FromStd(xml)));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _canvas = _manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);
        }

        void DrawAt(float width, float height) {
            _renderer->width = width;
            _renderer->height = height;
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        /// The first filled rect drawn: the plate
        const DrawCall* PlateFill() const {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && call.color.a == 160) {
                    return &call;
                }
            }
            return nullptr;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
    };

    // ---- the size matrix ----------------------------------------------------------------------------------------------------

    TEST_F(UIScreenTest, TheHudPassesTheSizeMatrix) {
        UICanvasLayout layout;
        ASSERT_NO_FATAL_FAILURE(BuildHudLayout(layout));
        // The longest values the game can show: the validator measures the text that is shown
        CoreManager::GetUIVars().Set("score"_intern, 99999999);
        CoreManager::GetUIVars().Set("seed"_intern, 4294967295u);

        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        const auto failures = ValidateOnMatrix(layout, options);

        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(UIScreenTest, ABrokenHudIsCaughtByTheMatrix) {
        // The same screen with a button of 30 units and no minScale: too small to touch on a small window
        UICanvasLayout layout;
        ASSERT_NO_FATAL_FAILURE(BuildHudLayout(layout));
        UIElement* reset = layout.root->FindByName(PoolString::Intern("Reset"));
        ASSERT_NE(reset, nullptr);
        reset->style.rect.height = UILength::Px(30.0f);
        layout.params.minScale = 0.25f;
        layout.Invalidate();

        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        const auto failures = ValidateOnMatrix(layout, options);

        EXPECT_FALSE(failures.empty());
        bool touch = false;
        for (const auto& failure : failures) {
            for (const UILayoutIssue& issue : failure.issues) {
                touch |= issue.kind == UILayoutIssueKind::TouchTargetTooSmall;
            }
        }
        EXPECT_TRUE(touch);
    }

    // ---- the same scene in two shapes ------------------------------------------------------------------------------------------

    TEST_F(UIScreenTest, TheHudDrawsInTheCornerInLandscape) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        DrawAt(1280.0f, 720.0f);

        const DrawCall* plate = PlateFill();
        ASSERT_NE(plate, nullptr);
        EXPECT_NEAR(plate->rect.x, 16.0f, kTolerance);
        EXPECT_NEAR(plate->rect.y, 16.0f, kTolerance);
        EXPECT_NEAR(plate->rect.w, 260.0f, kTolerance);
        EXPECT_NEAR(plate->rect.h, 120.0f, kTolerance);
    }

    TEST_F(UIScreenTest, TheHudSpansTheTopInPortrait) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        DrawAt(720.0f, 1280.0f);

        const DrawCall* plate = PlateFill();
        ASSERT_NE(plate, nullptr);
        EXPECT_NEAR(plate->rect.w, 648.0f, kTolerance) << "90% of the width";
        EXPECT_NEAR(plate->rect.x, 36.0f, kTolerance) << "centered";
        EXPECT_NEAR(plate->rect.y, 16.0f, kTolerance);
        EXPECT_NEAR(plate->rect.h, 120.0f, kTolerance);
    }

    TEST_F(UIScreenTest, AWindowTwiceAsLargeGivesTheSameScreenTwiceAsLarge) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        DrawAt(1280.0f, 720.0f);
        const DrawCall small = *PlateFill();
        const size_t smallCalls = _renderer->calls.size();

        DrawAt(2560.0f, 1440.0f);
        const DrawCall* large = PlateFill();
        ASSERT_NE(large, nullptr);

        EXPECT_NEAR(large->rect.x, small.rect.x * 2.0f, kTolerance);
        EXPECT_NEAR(large->rect.w, small.rect.w * 2.0f, kTolerance);
        EXPECT_NEAR(large->rect.h, small.rect.h * 2.0f, kTolerance);
        EXPECT_EQ(_renderer->calls.size(), smallCalls) << "the same draws, only larger";
    }

    TEST_F(UIScreenTest, ThePortraitVariantHidesTheSeedAndTheShapeIsRestoredOnTheWayBack) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        const auto glyphsOf = [this](const char* name) {
            const UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            size_t count = 0;
            for (const DrawCall& call : _renderer->calls) {
                const Rect& r = call.rect;
                if (call.kind == DrawKind::Texture && element && r.x >= element->rect.x - 1.0f && r.x + r.w <= element->rect.x + element->rect.w + 1.0f && r.y >= element->rect.y - 1.0f &&
                    r.y + r.h <= element->rect.y + element->rect.h + 1.0f) {
                    ++count;
                }
            }
            return count;
        };
        CoreManager::GetUIVars().Set("seed"_intern, 1337);

        DrawAt(1280.0f, 720.0f);
        EXPECT_GT(glyphsOf("Seed"), 0u);

        DrawAt(720.0f, 1280.0f);
        EXPECT_EQ(glyphsOf("Seed"), 0u) << "the variant hides it";

        DrawAt(1280.0f, 720.0f);
        EXPECT_GT(glyphsOf("Seed"), 0u) << "back in landscape it is there again";
    }

    TEST_F(UIScreenTest, TheScoreFollowsTheGameInBothShapes) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        auto* score = _canvas->GetRoot().FindByName(PoolString::Intern("Score"))->GetComponent<UITextComponent>();
        ASSERT_NE(score, nullptr);

        CoreManager::GetUIVars().Set("score"_intern, 0);
        DrawAt(1280.0f, 720.0f);
        EXPECT_EQ(score->GetDisplayText(), "Score: 0");

        CoreManager::GetUIVars().Set("score"_intern, 250);
        DrawAt(720.0f, 1280.0f);
        EXPECT_EQ(score->GetDisplayText(), "Score: 250");
    }

}  // namespace BECore::Tests
