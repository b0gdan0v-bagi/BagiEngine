#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/PropertyVisitorDispatch.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UIThemes.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr Color kLightPanel{240, 240, 230, 255};
        constexpr Color kDarkPanel{30, 32, 40, 255};
        constexpr Color kLightHover{200, 220, 255, 255};
        constexpr Color kDarkHover{60, 70, 110, 255};
        constexpr Color kLightLine{10, 20, 30, 255};
        constexpr Color kDarkLine{210, 220, 230, 255};
        constexpr Color kLightShadow{0, 0, 0, 120};
        constexpr Color kDarkShadow{0, 0, 0, 220};

        // A Screen canvas in window pixels (output 800 x 600, scale 1). Plate (10, 10, 100 x 40) is filled by a token; Hovered and Twin (10, 60 and 10, 110) are buttons whose hover
        // fill is a token and the same colour written out; Menu (10, 160) is a dropdown whose options (its popup) have a token fill; Hidden is not shown; Ghost has a token the table lacks
        constexpr const char* kThemeScreenXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Plate" x="10" y="10" width="100" height="40" color="@panel"/>
          <element type="UIButton" name="Hovered" x="10" y="60" width="100" height="40" color="@panel" hoverColor="@hover"/>
          <element type="UIButton" name="Twin" x="10" y="110" width="100" height="40" color="240,240,230,255" hoverColor="200,220,255,255"/>
          <element type="UIDropdown" name="Menu" bind="menu" x="10" y="160" width="100" height="40" optionHeight="40" color="@panel">
            <children>
              <element type="UIButton" name="OptionA" value="a" color="@panel"/>
            </children>
          </element>
          <element type="UIElement" name="Hidden" x="150" y="10" width="40" height="40" color="@panel" visible="false"/>
          <element type="UIElement" name="Ghost" x="150" y="60" width="40" height="40" color="@nowhere"/>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        /// An inspector that edits the green channel of every colour it meets (the field "g" of a compound) and nothing else
        class GreenEditor : public IPropertyVisitor {
        public:
            explicit GreenEditor(bool editing) : _editing(editing) {}

            bool Visit(eastl::string_view name, uint8_t& value) override {
                if (_editing && name == "g") {
                    value = kEditedGreen;
                    return true;
                }
                return false;
            }
            bool Visit(eastl::string_view, bool&) override {
                return false;
            }
            bool Visit(eastl::string_view, int8_t&) override {
                return false;
            }
            bool Visit(eastl::string_view, int16_t&) override {
                return false;
            }
            bool Visit(eastl::string_view, uint16_t&) override {
                return false;
            }
            bool Visit(eastl::string_view, int32_t&) override {
                return false;
            }
            bool Visit(eastl::string_view, uint32_t&) override {
                return false;
            }
            bool Visit(eastl::string_view, int64_t&) override {
                return false;
            }
            bool Visit(eastl::string_view, uint64_t&) override {
                return false;
            }
            bool Visit(eastl::string_view, float&) override {
                return false;
            }
            bool Visit(eastl::string_view, double&) override {
                return false;
            }
            bool Visit(eastl::string_view, PoolString&) override {
                return false;
            }
            bool Visit(eastl::string_view, eastl::string&) override {
                return false;
            }

            static constexpr uint8_t kEditedGreen = 77;

        private:
            bool _editing;
        };

        /// An element that counts how often its colours are resolved
        class CountingElement : public UIElement {
        public:
            int applied = 0;

        protected:
            void ApplyTheme(UIThemes& themes) override {
                ++applied;
                UIElement::ApplyTheme(themes);
            }
        };

    }  // namespace

    class UIThemeResolveTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            UIThemes& themes = CoreManager::GetUIThemes();
            themes.Set("light"_intern, "panel", kLightPanel);
            themes.Set("light"_intern, "hover", kLightHover);
            themes.Set("light"_intern, "line", kLightLine);
            themes.Set("light"_intern, "shadow", kLightShadow);
            themes.Set("dark"_intern, "panel", kDarkPanel);
            themes.Set("dark"_intern, "hover", kDarkHover);
            themes.Set("dark"_intern, "line", kDarkLine);
            themes.Set("dark"_intern, "shadow", kDarkShadow);

            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kThemeScreenXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _canvas = _manager->GetSceneByName("Main"_intern)->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);
        }

        void TearDown() override {
            CoreManager::GetUIThemes().Clear();
            _canvas.Reset();
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        void Draw() {
            _manager->SetActiveScene("Main"_intern);
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        template <typename T = UIElement>
        T& Get(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            T* typed = element ? element->Cast<T>() : nullptr;
            EXPECT_NE(typed, nullptr) << name << " is not the expected type";
            return *typed;
        }

        /// The colour of the rect the last draw filled at (@p x, @p y); the alpha 0 colour when none was
        [[nodiscard]] Color FillAt(float x, float y) const {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && call.rect.x == x && call.rect.y == y) {
                    return call.color;
                }
            }
            return {};
        }

        static void ExpectColor(const Color& actual, const Color& expected) {
            EXPECT_TRUE(ColorsEqual(actual, expected)) << "got " << int(actual.r) << "," << int(actual.g) << "," << int(actual.b) << "," << int(actual.a);
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
    };

    TEST_F(UIThemeResolveTest, ATokenFillIsDrawnInTheCurrentThemeAndFollowsASwitch) {
        Draw();
        ExpectColor(FillAt(10.0f, 10.0f), kLightPanel);

        ASSERT_TRUE(CoreManager::GetUIThemes().SetTheme("dark"_intern));
        Draw();  // no tree edit, no Invalidate()
        ExpectColor(FillAt(10.0f, 10.0f), kDarkPanel);

        ASSERT_TRUE(CoreManager::GetUIThemes().SetTheme("light"_intern));
        Draw();
        ExpectColor(FillAt(10.0f, 10.0f), kLightPanel);
    }

    TEST_F(UIThemeResolveTest, ASteadyFrameResolvesNothing) {
        auto counter = New<CountingElement>();
        counter->name = "Counter"_intern;
        counter->color = UIColor::FromToken("panel"_intern);
        _canvas->GetRoot().children.push_back(counter);
        _canvas->Invalidate();

        Draw();
        EXPECT_EQ(counter->applied, 1);
        Draw();
        Draw();
        EXPECT_EQ(counter->applied, 1) << "the table did not move";

        CoreManager::GetUIThemes().SetTheme("dark"_intern);
        Draw();
        EXPECT_EQ(counter->applied, 2);
        Draw();
        EXPECT_EQ(counter->applied, 2);

        // A colour that changes while the active theme stays the same moves the version too
        CoreManager::GetUIThemes().Set("dark"_intern, "panel", {1, 2, 3, 255});
        Draw();
        EXPECT_EQ(counter->applied, 3);
        ExpectColor(counter->color, {1, 2, 3, 255});
    }

    TEST_F(UIThemeResolveTest, AHoverTokenShiftsLikeALiteralOne) {
        Get<UIButton>("Hovered").hovered = true;
        Get<UIButton>("Twin").hovered = true;
        Draw();
        const Color token = FillAt(10.0f, 60.0f);
        const Color literal = FillAt(10.0f, 110.0f);
        ASSERT_GT(token.a, 0);
        ExpectColor(token, literal);
        ExpectColor(token, kLightHover);

        CoreManager::GetUIThemes().SetTheme("dark"_intern);
        Draw();
        ExpectColor(FillAt(10.0f, 60.0f), kDarkHover);
    }

    TEST_F(UIThemeResolveTest, AnUnknownTokenIsMagentaAndRecordedOnce) {
        Draw();
        ExpectColor(FillAt(150.0f, 60.0f), kMissingTokenColor);
        ASSERT_EQ(CoreManager::GetUIThemes().GetMissing().size(), 1u);
        EXPECT_EQ(CoreManager::GetUIThemes().GetMissing().front(), "nowhere"_intern);

        CoreManager::GetUIThemes().SetTheme("dark"_intern);
        Draw();
        ExpectColor(FillAt(150.0f, 60.0f), kMissingTokenColor);
        EXPECT_EQ(CoreManager::GetUIThemes().GetMissing().size(), 1u) << "once per token, not once per resolve";
    }

    TEST_F(UIThemeResolveTest, ATokenAssignedInCodeNeedsResetThemeAndThenShows) {
        UIElement& plate = Get("Plate");
        Draw();
        ExpectColor(FillAt(10.0f, 10.0f), kLightPanel);

        plate.color = UIColor::FromToken("line"_intern);
        Draw();
        EXPECT_FALSE(plate.color.a > 0) << "the channels of the new token are not resolved until the stamp is reset";

        plate.ResetTheme();
        Draw();
        ExpectColor(FillAt(10.0f, 10.0f), kLightLine);
    }

    TEST_F(UIThemeResolveTest, AnElementAddedAfterTheSwitchIsDrawnRightOnItsFirstDraw) {
        Draw();
        CoreManager::GetUIThemes().SetTheme("dark"_intern);

        auto late = _canvas->GetRoot().AddChild("Late"_intern);
        late->style.rect.x = UILength::Px(300.0f);
        late->style.rect.y = UILength::Px(10.0f);
        late->style.rect.width = UILength::Px(40.0f);
        late->style.rect.height = UILength::Px(40.0f);
        late->color = UIColor::FromToken("panel"_intern);
        _canvas->Invalidate();

        Draw();
        ExpectColor(FillAt(300.0f, 10.0f), kDarkPanel);
    }

    TEST_F(UIThemeResolveTest, AHiddenElementResolvesWhenItIsShown) {
        Draw();
        EXPECT_EQ(FillAt(150.0f, 10.0f).a, 0) << "hidden, not drawn";
        ASSERT_TRUE(CoreManager::GetUIThemes().SetTheme("dark"_intern));
        Draw();
        EXPECT_EQ(Get("Hidden").color.a, 0) << "a hidden element is not resolved: nothing draws it";

        ASSERT_TRUE(_canvas->SetElementVisible("Hidden"_intern, true));
        Draw();
        ExpectColor(FillAt(150.0f, 10.0f), kDarkPanel);
    }

    TEST_F(UIThemeResolveTest, ThePopupChildrenOfAnOpenDropdownResolveToo) {
        Draw();
        auto& menu = Get<UIDropdown>("Menu");
        UIElement& option = Get<UIButton>("OptionA");
        menu.Open();
        CoreManager::GetUIThemes().SetTheme("dark"_intern);
        Draw();
        ASSERT_TRUE(menu.IsOpen());
        ExpectColor(option.color, kDarkPanel);
        ExpectColor(FillAt(option.rect.x, option.rect.y), kDarkPanel);
    }

    TEST_F(UIThemeResolveTest, TheColoursOfTheBoxAreResolvedIncludingShadowAndGlow) {
        UIElement element;
        element.box.borderColor = UIColor::FromToken("line"_intern);
        element.box.gradientColor = UIColor::FromToken("panel"_intern);
        element.box.highlight = UIColor::FromToken("hover"_intern);
        element.box.shadowColorToken = "shadow"_intern;
        element.box.glowColorToken = "line"_intern;
        element.hoverColor = UIColor::FromToken("hover"_intern);
        element.pressedColor = UIColor::FromToken("line"_intern);
        element.disabledColor = UIColor::FromToken("panel"_intern);
        element.selectedColor = UIColor::FromToken("shadow"_intern);

        UIThemes& themes = CoreManager::GetUIThemes();
        element.ResolveTheme(themes);
        ExpectColor(element.box.borderColor, kLightLine);
        ExpectColor(element.box.gradientColor, kLightPanel);
        ExpectColor(element.box.highlight, kLightHover);
        ExpectColor(element.box.shadow.color, kLightShadow);
        ExpectColor(element.box.glow.color, kLightLine);
        ExpectColor(element.hoverColor, kLightHover);
        ExpectColor(element.pressedColor, kLightLine);
        ExpectColor(element.disabledColor, kLightPanel);
        ExpectColor(element.selectedColor, kLightShadow);

        themes.SetTheme("dark"_intern);
        element.ResolveTheme(themes);
        ExpectColor(element.box.borderColor, kDarkLine);
        ExpectColor(element.box.shadow.color, kDarkShadow);
        ExpectColor(element.box.glow.color, kDarkLine);
        EXPECT_TRUE(element.box.shadowColorToken == "shadow"_intern) << "the token stays, so the next switch resolves again";
    }

    TEST_F(UIThemeResolveTest, ALiteralColourIsNeverTouchedByAResolve) {
        UIElement element;
        element.color = Color{1, 2, 3, 4};
        element.box.shadow.color = Color{5, 6, 7, 8};
        element.ResolveTheme(CoreManager::GetUIThemes());
        ExpectColor(element.color, {1, 2, 3, 4});
        ExpectColor(element.box.shadow.color, {5, 6, 7, 8});
        EXPECT_TRUE(CoreManager::GetUIThemes().GetMissing().empty());
    }

    TEST_F(UIThemeResolveTest, AnInspectorEditOfAColourDropsItsTokenAndALookDoesNot) {
        UIColor color = UIColor::FromToken("panel"_intern);
        GreenEditor looking(false);
        EXPECT_FALSE(PropVisit::Visit(looking, color, "color"));
        EXPECT_TRUE(color.HasToken());

        GreenEditor editing(true);
        EXPECT_TRUE(PropVisit::Visit(editing, color, "color"));
        EXPECT_FALSE(color.HasToken()) << "an edit makes it a literal colour: the token would overwrite it at the next switch";
        EXPECT_EQ(color.g, GreenEditor::kEditedGreen);
    }

    TEST_F(UIThemeResolveTest, TheInspectorOfAnElementDropsTheTokensItEdits) {
        UIElement element;
        element.color = UIColor::FromToken("panel"_intern);
        element.box.shadowColorToken = "shadow"_intern;
        element.box.borderColor = UIColor::FromToken("line"_intern);

        GreenEditor looking(false);
        EXPECT_FALSE(element.AcceptPropertyVisitor(looking));
        EXPECT_TRUE(element.color.HasToken());
        EXPECT_FALSE(element.box.shadowColorToken.Empty());

        GreenEditor editing(true);
        EXPECT_TRUE(element.AcceptPropertyVisitor(editing));
        EXPECT_FALSE(element.color.HasToken());
        EXPECT_FALSE(element.box.borderColor.HasToken());
        EXPECT_TRUE(element.box.shadowColorToken.Empty());

        element.ResolveTheme(CoreManager::GetUIThemes());
        EXPECT_EQ(element.color.g, GreenEditor::kEditedGreen) << "the edit survives the resolve";
    }

}  // namespace BECore::Tests
