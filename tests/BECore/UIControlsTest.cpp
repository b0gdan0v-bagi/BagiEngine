#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIImage.h>
#include <BECore/UI/UILabel.h>
#include <BECore/UI/UIPanel.h>
#include <BECore/UI/UIProgressBar.h>
#include <BECore/UI/UISegmented.h>
#include <BECore/UI/UIStack.h>
#include <BECore/UI/UIToggle.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/UITreeCompare.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kScaleTolerance = 3.0f;  // glyphs are snapped to whole pixels at each size, so a doubled text is only doubled to a few pixels

        // One screen of every control, on a Screen canvas in window pixels (output 800 x 600, scale 1):
        //   Plate (10, 10, 200 x 100) holds the Title label and the Ok button in its bottom right corner, which is (122, 62, 80 x 40)
        //   Icon  (300, 10, 64 x 64) is an image, Frame (300, 100, 100 x 60) a panel with a nine-slice sprite, Off (300, 200, 80 x 40) a disabled button
        constexpr const char* kControlsXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIPanel" name="Plate" x="10" y="10" width="200" height="100" color="10,20,30,200">
            <children>
              <element type="UILabel" name="Title" x="8" y="8" width="184" height="30" text="Score" fontSize="24"/>
              <element type="UIButton" name="Ok" anchor="BottomRight" x="-8" y="-8" width="80" height="40" text="OK" action="UIControlsTestOk" color="40,90,60,255"/>
            </children>
          </element>
          <element type="UIImage" name="Icon" x="300" y="10" width="64" height="64"/>
          <element type="UIPanel" name="Frame" x="300" y="100" width="100" height="60" sliceBorder="4" blocksInput="false"/>
          <element type="UIButton" name="Off" x="300" y="200" width="80" height="40" text="Off" action="UIControlsTestOff" color="40,90,60,255" disabled="true"/>
          <element type="UIStack" name="Row" x="500" y="10" width="100" height="40" direction="Horizontal" spacing="4"/>
          <element type="UISegmented" name="Choice" bind="choice" x="500" y="100" width="204" height="48" direction="Horizontal" spacing="4" disabled="true"/>
          <element type="UIToggle" name="Switch" bind="switchOn" action="UIControlsTestSwitch" text="Switch" onValue="yes" offValue="no" x="500" y="200" width="204" height="56" disabled="true"/>
          <element type="UIProgressBar" name="Meter" bind="meter" minValue="10" maxValue="90" x="500" y="300" width="204" height="24"/>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        const Color kButtonColor{40, 90, 60, 255};

        SDL_MouseButtonEvent Button(float x, float y, bool down, uint8_t button = SDL_BUTTON_LEFT) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = button;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

        SDL_MouseMotionEvent Motion(float x, float y, SDL_MouseButtonFlags buttonsHeld = 0) {
            SDL_MouseMotionEvent event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.x = x;
            event.y = y;
            event.state = buttonsHeld;
            return event;
        }

        /// Counts the actions the buttons emit, by name
        class ActionRecorder : public SubscriptionHolder {
        public:
            ActionRecorder() {
                Subscribe<InputEvents::ActionEvent, &ActionRecorder::OnAction>(this);
            }

            int Count(const char* action) const {
                return static_cast<int>(std::count(_actions.begin(), _actions.end(), PoolString::Intern(action)));
            }
            size_t Total() const {
                return _actions.size();
            }
            /// The value of the last action, empty when there was none
            PoolString LastValue() const {
                return _values.empty() ? PoolString{} : _values.back();
            }

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                _actions.push_back(event.action);
                _values.push_back(event.value);
            }

            eastl::vector<PoolString> _actions;
            eastl::vector<PoolString> _values;
        };

        eastl::string ElementToXml(const UIElement& element) {
            XmlSerializer serializer;
            EXPECT_TRUE(serializer.BeginObject("element"));
            element.Serialize(serializer);
            serializer.EndObject();
            return serializer.SaveToString();
        }

        bool Near(const Rect& a, const Rect& b, float tolerance = 1e-2f) {
            return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.w - b.w) <= tolerance && std::abs(a.h - b.h) <= tolerance;
        }

        eastl::vector<uint8_t> GreyPixels(uint32_t width, uint32_t height) {
            return eastl::vector<uint8_t>(static_cast<size_t>(width) * height * 4, 128);
        }

    }  // namespace

    class UIControlsTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetFontAtlasLibrary().Clear();
            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kControlsXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();

            _canvas = _manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);
        }

        void TearDown() override {
            CoreManager::GetUIVars().Clear();
            _canvas.Reset();
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        void Draw() {
            _manager->SetActiveScene(PoolString::Intern("Main"));
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        template <typename T>
        T& Get(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            T* typed = element ? element->Cast<T>() : nullptr;
            EXPECT_NE(typed, nullptr) << name << " is not the expected control";
            return *typed;
        }

        /// The color of the filled rect that was drawn exactly on the element's rect, in the last draw
        std::optional<Color> FillOf(const UIElement& element) const {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && Near(call.rect, element.rect)) {
                    return call.color;
                }
            }
            return std::nullopt;
        }

        /// The textured draws that sit inside the rect of an element (its glyphs); the calls are in output pixels, so @p scale is the scale of the canvas
        eastl::vector<DrawCall> GlyphsIn(const UIElement& element, float scale = 1.0f) const {
            const Rect box{element.rect.x * scale, element.rect.y * scale, element.rect.w * scale, element.rect.h * scale};
            eastl::vector<DrawCall> glyphs;
            for (const DrawCall& call : _renderer->calls) {
                const Rect& r = call.rect;
                if (call.kind == DrawKind::Texture && r.x >= box.x - 1.0f && r.y >= box.y - 1.0f && r.x + r.w <= box.x + box.w + 1.0f && r.y + r.h <= box.y + box.h + 1.0f) {
                    glyphs.push_back(call);
                }
            }
            return glyphs;
        }

        Rect Center(const UIElement& element) const {
            return {element.rect.x + element.rect.w * 0.5f, element.rect.y + element.rect.h * 0.5f, 0.0f, 0.0f};
        }

        bool Press(const UIElement& element) {
            const Rect c = Center(element);
            return CoreManager::GetUIInput().DispatchMouseButtonDown(Button(c.x, c.y, true));
        }
        bool Release(float x, float y) {
            return CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false));
        }
        void MoveTo(float x, float y, SDL_MouseButtonFlags held = 0) {
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(x, y, held));
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
    };

    // ---- from a file ------------------------------------------------------------------------------------------------------

    TEST_F(UIControlsTest, EveryControlIsMadeByItsTypeName) {
        const UIElement& root = _canvas->GetRoot();
        ASSERT_EQ(root.children.size(), 8u);

        EXPECT_TRUE(root.children[0]->Is<UIPanel>());
        EXPECT_TRUE(root.children[1]->Is<UIImage>());
        EXPECT_TRUE(root.children[2]->Is<UIPanel>());
        EXPECT_TRUE(root.children[3]->Is<UIButton>());
        EXPECT_TRUE(root.children[4]->Is<UIStack>());
        EXPECT_TRUE(root.children[5]->Is<UISegmented>());
        EXPECT_TRUE(root.children[6]->Is<UIToggle>());
        EXPECT_TRUE(root.children[7]->Is<UIProgressBar>());
        EXPECT_TRUE(root.children[0]->children[0]->Is<UILabel>());
        EXPECT_TRUE(root.children[0]->children[1]->Is<UIButton>());
    }

    TEST_F(UIControlsTest, TheFieldsComeFromTheFile) {
        const auto& title = Get<UILabel>("Title");
        EXPECT_EQ(title.text, "Score");
        EXPECT_FLOAT_EQ(title.style.fontSize, 24.0f);
        EXPECT_EQ(title.overflow, TextOverflow::Ellipsis);  // the default

        const auto& ok = Get<UIButton>("Ok");
        EXPECT_EQ(ok.action, PoolString::Intern("UIControlsTestOk"));
        EXPECT_TRUE(ok.interactive);
        EXPECT_EQ(ok.align, TextAlign::Center);
        EXPECT_EQ(ok.verticalAlign, UIVerticalAlign::Center);
        EXPECT_FALSE(ok.disabled);
        EXPECT_TRUE(Get<UIButton>("Off").disabled);

        EXPECT_EQ(Get<UIPanel>("Frame").sliceBorder, (UIEdges{4.0f, 4.0f, 4.0f, 4.0f}));
        EXPECT_TRUE(Get<UIPanel>("Plate").blocksInput);  // a panel takes clicks unless it says otherwise
        EXPECT_FALSE(Get<UIPanel>("Frame").blocksInput);
        EXPECT_EQ(Get<UIStack>("Row").layout, UILayoutKind::Stack);
        EXPECT_EQ(Get<UISegmented>("Choice").layout, UILayoutKind::Stack);
        EXPECT_EQ(Get<UISegmented>("Choice").bind, PoolString::Intern("choice"));
        EXPECT_TRUE(Get<UISegmented>("Choice").disabled);
        EXPECT_EQ(Get<UIToggle>("Switch").bind, PoolString::Intern("switchOn"));
        EXPECT_EQ(Get<UIToggle>("Switch").onValue, PoolString::Intern("yes"));
        EXPECT_EQ(Get<UIToggle>("Switch").offValue, PoolString::Intern("no"));
        EXPECT_TRUE(Get<UIToggle>("Switch").disabled);
        EXPECT_TRUE(Get<UIToggle>("Switch").interactive);
        EXPECT_EQ(Get<UIProgressBar>("Meter").bind, PoolString::Intern("meter"));
        EXPECT_FLOAT_EQ(Get<UIProgressBar>("Meter").minValue, 10.0f);
        EXPECT_FLOAT_EQ(Get<UIProgressBar>("Meter").maxValue, 90.0f);
        EXPECT_FALSE(Get<UIProgressBar>("Meter").interactive);
    }

    TEST_F(UIControlsTest, TheControlsSaveAndLoadBack) {
        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("component"));
        _canvas->Serialize(serializer);
        serializer.EndObject();
        const eastl::string xml = serializer.SaveToString();

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
        auto loaded = New<UICanvasComponent>();
        ASSERT_TRUE(deserializer.BeginObject("component"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        EXPECT_FALSE(deserializer.HasErrors());
        loaded->Initialize();

        ASSERT_EQ(loaded->_elements.size(), _canvas->_elements.size());
        for (size_t i = 0; i < loaded->_elements.size(); ++i) {
            // The text of an element holds every reflected field, so this compares the controls' own fields too
            EXPECT_STREQ(ElementToXml(*_canvas->_elements[i]).c_str(), ElementToXml(*loaded->_elements[i]).c_str()) << i;
            EXPECT_STREQ(_canvas->_elements[i]->GetTypeMeta().typeName.data(), loaded->_elements[i]->GetTypeMeta().typeName.data());
        }
    }

    namespace {

        /// Every property of a style set to something other than its default
        void SetEveryStyleProperty(UIElementStyle& style) {
            style.visible = false;
            style.rect.anchor = UIAnchor::Bottom;
            style.rect.x = 3.0f;
            style.rect.y = 4.0f;
            style.rect.width = UILength::Percent(50.0f);
            style.rect.height = UILength::Px(20.0f);
            style.rect.minWidth = UILength::Px(5.0f);
            style.rect.maxWidth = UILength::Percent(60.0f);
            style.rect.minHeight = UILength::Px(6.0f);
            style.rect.maxHeight = UILength::Px(70.0f);
            style.rect.aspectRatio = 1.5f;
            style.rect.aspectMode = UIAspectMode::Fill;
            style.rect.margin = {1.0f, 2.0f, 3.0f, 4.0f};
            style.rect.flex = 2.0f;
            style.stack.direction = UIStackDirection::Horizontal;
            style.stack.spacing = 7.0f;
            style.stack.padding = {8.0f, 9.0f, 10.0f, 11.0f};
            style.stack.wrap = true;
            style.stack.crossAlign = UIStackAlign::End;
            style.fontSize = 13.0f;
        }

        void SetEveryControlField(UIElement& element) {
            element.name = PoolString::Intern("Busy");
            element.layout = UILayoutKind::Stack;
            element.color = Color{1, 2, 3, 4};
            element.blocksInput = true;
            element.interactive = true;
            element.ignoreSafeArea = true;
            if (auto* panel = element.Cast<UIPanel>()) {
                panel->sprite = PoolString::Intern("PanelSprite");
                panel->sliceBorder = {1.0f, 2.0f, 3.0f, 4.0f};
                panel->spriteTint = Color{5, 6, 7, 8};
            } else if (auto* image = element.Cast<UIImage>()) {
                image->sprite = PoolString::Intern("ImageSprite");
                image->tint = Color{5, 6, 7, 8};
                image->stretch = true;
            } else if (auto* button = element.Cast<UIButton>()) {
                button->action = PoolString::Intern("BusyAction");
                button->hoverColor = Color{9, 10, 11, 12};
                button->pressedColor = Color{13, 14, 15, 16};
                button->disabledColor = Color{17, 18, 19, 20};
                button->selectedColor = Color{25, 26, 27, 28};
                button->value = PoolString::Intern("BusyValue");
                button->disabled = true;
            } else if (auto* group = element.Cast<UISegmented>()) {
                group->bind = PoolString::Intern("busyChoice");
                group->disabled = true;
            } else if (auto* toggle = element.Cast<UIToggle>()) {
                toggle->bind = PoolString::Intern("busyOn");
                toggle->action = PoolString::Intern("BusyToggleAction");
                toggle->onValue = PoolString::Intern("BusyOn");
                toggle->offValue = PoolString::Intern("BusyOff");
                toggle->disabled = true;
                toggle->trackColor = Color{31, 32, 33, 34};
                toggle->fillColor = Color{35, 36, 37, 38};
                toggle->thumbColor = Color{39, 40, 41, 42};
            } else if (auto* bar = element.Cast<UIProgressBar>()) {
                bar->bind = PoolString::Intern("busyProgress");
                bar->minValue = -2.5f;
                bar->maxValue = 12.5f;
                bar->trackColor = Color{43, 44, 45, 46};
                bar->fillColor = Color{47, 48, 49, 50};
            }
            if (auto* label = element.Cast<UILabel>(); label || element.Cast<UIButton>() || element.Cast<UIToggle>()) {
                auto* text = label ? label : element.Cast<UIButton>() ? static_cast<UILabel*>(element.Cast<UIButton>()) : static_cast<UILabel*>(element.Cast<UIToggle>());
                text->text = "Some text";
                text->font = PoolString::Intern("assets/fonts/Other.ttf");
                text->textColor = Color{21, 22, 23, 24};
                text->align = TextAlign::Right;
                text->verticalAlign = UIVerticalAlign::Bottom;
                text->overflow = TextOverflow::AutoFit;
                text->wrap = false;  // the text one: the stack one is style.stack.wrap, and the two must stay apart
                text->lineSpacing = 1.5f;
                text->minFontSize = 10.0f;
            }
        }

    }  // namespace

    TEST_F(UIControlsTest, EveryPropertyOfEveryControlSurvivesARoundTrip) {
        for (const char* type : {"UIElement", "UIPanel", "UIImage", "UILabel", "UIButton", "UIStack", "UISegmented", "UIToggle", "UIProgressBar"}) {
            auto original = AbstractFactory<UIElement>::GetInstance().Create(type);
            ASSERT_TRUE(original) << type;
            SetEveryStyleProperty(original->style);
            SetEveryControlField(*original);

            auto canvas = New<UICanvasComponent>();
            canvas->_elements.push_back(original);
            XmlSerializer serializer;
            ASSERT_TRUE(serializer.BeginObject("component"));
            canvas->Serialize(serializer);
            serializer.EndObject();
            const eastl::string xml = serializer.SaveToString();

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
            auto loaded = New<UICanvasComponent>();
            ASSERT_TRUE(deserializer.BeginObject("component"));
            loaded->Deserialize(deserializer);
            deserializer.EndObject();
            ASSERT_FALSE(deserializer.HasErrors()) << type;
            ASSERT_EQ(loaded->_elements.size(), 1u) << type;

            EXPECT_TRUE(original->style == loaded->_elements[0]->style) << type << ": the style changed on the way";
            EXPECT_STREQ(ElementToXml(*original).c_str(), ElementToXml(*loaded->_elements[0]).c_str()) << type;
        }
    }

    TEST_F(UIControlsTest, TheTextWrapAndTheStackWrapAreTwoThings) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString("<root><component><elements><element type=\"UILabel\" name=\"A\" wrap=\"false\"/>"
                                                "<element type=\"UIStack\" name=\"B\" wrapChildren=\"true\"/></elements></component></root>"));
        auto canvas = New<UICanvasComponent>();
        ASSERT_TRUE(deserializer.BeginObject("component"));
        canvas->Deserialize(deserializer);
        deserializer.EndObject();
        ASSERT_EQ(canvas->_elements.size(), 2u);

        auto* label = canvas->_elements[0]->Cast<UILabel>();
        ASSERT_NE(label, nullptr);
        EXPECT_FALSE(label->wrap);
        EXPECT_FALSE(label->style.stack.wrap);
        EXPECT_TRUE(canvas->_elements[1]->style.stack.wrap);
    }

    // ---- variables and show / hide by name --------------------------------------------------------------------------------

    TEST_F(UIControlsTest, ALabelFollowsItsVariables) {
        auto& title = Get<UILabel>("Title");
        title.text = "Score: {score}";
        UIVars& vars = CoreManager::GetUIVars();

        Draw();
        EXPECT_EQ(title.GetDisplayText(), "Score: ") << "a variable nobody set is empty";
        EXPECT_EQ(GlyphsIn(title).size(), 6u);  // S c o r e :

        vars.Set("score"_intern, 5);
        Draw();
        EXPECT_EQ(title.GetDisplayText(), "Score: 5");
        EXPECT_EQ(GlyphsIn(title).size(), 7u);

        vars.Set("score"_intern, 100);
        Draw();
        EXPECT_EQ(GlyphsIn(title).size(), 9u);
    }

    TEST_F(UIControlsTest, ALabelIsLaidOutAgainOnlyWhenItsTextChanged) {
        auto& title = Get<UILabel>("Title");
        title.text = "Score: {score}";
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("score"_intern, 5);

        Draw();
        const uint32_t afterFirst = title.GetLayoutCount();
        Draw();
        Draw();
        EXPECT_EQ(title.GetLayoutCount(), afterFirst) << "an unchanged screen costs no layout";

        vars.Set("other"_intern, 1);  // a variable the label does not show still moves the version, but the shown text stays
        Draw();
        EXPECT_EQ(title.GetLayoutCount(), afterFirst);

        vars.Set("score"_intern, 6);
        Draw();
        EXPECT_EQ(title.GetLayoutCount(), afterFirst + 1);
    }

    TEST_F(UIControlsTest, ABracedTextWithoutAVariableIsShownWithoutBraces) {
        auto& title = Get<UILabel>("Title");
        title.text = "{{x}}";
        Draw();
        EXPECT_EQ(title.GetDisplayText(), "{x}");
    }

    TEST_F(UIControlsTest, TheValidatorSeesTheTextThatIsShown) {
        auto& title = Get<UILabel>("Title");
        title.text = "{line}";
        title.wrap = false;
        title.overflow = TextOverflow::Clip;
        title.style.rect.height = UILength::Px(40.0f);  // a line of 24 units is a little over 30 high, so the fixture's 30 would not hold even "Short"
        _canvas->Invalidate();
        const auto textIssues = [this]() {
            Draw();
            size_t count = 0;
            for (const UILayoutIssue& issue : _canvas->GetLayout().Validate({})) {
                count += issue.kind == UILayoutIssueKind::TextDoesNotFit ? 1u : 0u;
            }
            return count;
        };

        CoreManager::GetUIVars().Set("line"_intern, "Short");
        EXPECT_EQ(textIssues(), 0u);
        CoreManager::GetUIVars().Set("line"_intern, "A value that is much too long for a label of this width");
        EXPECT_EQ(textIssues(), 1u);
    }

    TEST_F(UIControlsTest, AnElementIsHiddenAndShownByName) {
        Draw();
        auto& title = Get<UILabel>("Title");
        ASSERT_FALSE(GlyphsIn(title).empty());

        EXPECT_TRUE(_canvas->SetElementVisible(PoolString::Intern("Title"), false));
        Draw();
        EXPECT_TRUE(title.rect.IsEmpty());
        EXPECT_TRUE(_renderer->calls.size() > 0);
        for (const DrawCall& call : _renderer->calls) {
            EXPECT_FALSE(call.kind == DrawKind::Texture && call.rect.x < 200.0f && call.rect.y < 50.0f) << "the hidden label draws nothing";
        }

        EXPECT_TRUE(_canvas->SetElementVisible(PoolString::Intern("Title"), true));
        Draw();
        EXPECT_FALSE(GlyphsIn(title).empty());

        EXPECT_FALSE(_canvas->SetElementVisible(PoolString::Intern("NoSuchElement"), false));
    }

    TEST_F(UIControlsTest, AHiddenButtonTakesNoClicks) {
        ActionRecorder actions;
        Draw();
        auto& ok = Get<UIButton>("Ok");
        const Rect c = Center(ok);

        Scene& scene = *_manager->GetSceneByName(PoolString::Intern("Main"));
        EXPECT_TRUE(UICanvasComponent::SetElementVisibleByName(scene, PoolString::Intern("Ok"), false));
        Draw();

        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(c.x, c.y, true));
        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(c.x, c.y, false));
        EXPECT_EQ(actions.Total(), 0u);
        EXPECT_EQ(_canvas->GetLayout().HitTest(c.x, c.y), &Get<UIPanel>("Plate")) << "the click lands on the plate below";

        EXPECT_FALSE(UICanvasComponent::SetElementVisibleByName(scene, PoolString::Intern("NoSuchElement"), true));
    }

    // ---- hit testing ------------------------------------------------------------------------------------------------------

    TEST_F(UIControlsTest, ATopmostInteractiveElementIsFound) {
        _canvas->UpdateLayout(800.0f, 600.0f);
        const UICanvasLayout& layout = _canvas->GetLayout();

        EXPECT_EQ(layout.HitTest(162.0f, 82.0f), &Get<UIButton>("Ok"));    // the button is a child of the plate and drawn above it
        EXPECT_EQ(layout.HitTest(30.0f, 25.0f), &Get<UIPanel>("Plate"));   // the label above the plate takes no clicks, the plate does
        EXPECT_EQ(layout.HitTest(330.0f, 40.0f), nullptr);                 // an image is decoration
        EXPECT_EQ(layout.HitTest(350.0f, 130.0f), nullptr);                // a panel with blocksInput="false"
        EXPECT_EQ(layout.HitTest(340.0f, 220.0f), &Get<UIButton>("Off"));  // a disabled button still takes the click
        EXPECT_EQ(layout.HitTest(700.0f, 500.0f), nullptr);
    }

    // ---- drawing ----------------------------------------------------------------------------------------------------------

    TEST_F(UIControlsTest, APanelFillsThenTheLabelDrawsItsText) {
        Draw();

        ASSERT_FALSE(_renderer->calls.empty());
        EXPECT_EQ(_renderer->calls[0].kind, DrawKind::Rect);  // the plate is the first thing drawn
        EXPECT_EQ(_renderer->calls[0].color.a, 200);
        EXPECT_TRUE(Near(_renderer->calls[0].rect, Get<UIPanel>("Plate").rect));

        const auto glyphs = GlyphsIn(Get<UILabel>("Title"));
        EXPECT_EQ(glyphs.size(), 5u) << "one quad per glyph of 'Score'";
        for (const DrawCall& glyph : glyphs) {
            EXPECT_EQ(glyph.color.r, 255);  // the default text color is the tint
            EXPECT_EQ(glyph.color.a, 255);
        }
    }

    TEST_F(UIControlsTest, ALabelIsLaidOutOnlyWhenSomethingChanged) {
        auto& title = Get<UILabel>("Title");
        Draw();
        Draw();
        EXPECT_EQ(title.GetLayoutCount(), 1u);

        title.text = "Score: 100";
        Draw();
        EXPECT_EQ(title.GetLayoutCount(), 2u);
        EXPECT_EQ(GlyphsIn(title).size(), 9u);  // the space has no ink
    }

    TEST_F(UIControlsTest, ATopLabelStaysAtTheTopOfItsRect) {
        Draw();
        const auto& title = Get<UILabel>("Title");
        const auto glyphs = GlyphsIn(title);
        ASSERT_FALSE(glyphs.empty());

        float topmost = glyphs[0].rect.y;
        for (const DrawCall& glyph : glyphs) {
            topmost = std::min(topmost, glyph.rect.y);
        }
        EXPECT_LT(topmost - title.rect.y, title.rect.h * 0.5f);
    }

    TEST_F(UIControlsTest, AButtonCentersItsTextInItsRect) {
        Draw();
        const auto& ok = Get<UIButton>("Ok");
        const auto glyphs = GlyphsIn(ok);
        ASSERT_EQ(glyphs.size(), 2u);

        float top = glyphs[0].rect.y;
        float bottom = glyphs[0].rect.y + glyphs[0].rect.h;
        float left = glyphs[0].rect.x;
        float right = glyphs[0].rect.x + glyphs[0].rect.w;
        for (const DrawCall& glyph : glyphs) {
            top = std::min(top, glyph.rect.y);
            bottom = std::max(bottom, glyph.rect.y + glyph.rect.h);
            left = std::min(left, glyph.rect.x);
            right = std::max(right, glyph.rect.x + glyph.rect.w);
        }
        const Rect c = Center(ok);
        constexpr float kInkTolerance = 8.0f;  // the line box is centered, the ink of capitals sits a little off its middle
        EXPECT_NEAR((top + bottom) * 0.5f, c.y, kInkTolerance);
        EXPECT_NEAR((left + right) * 0.5f, c.x, kInkTolerance);
    }

    TEST_F(UIControlsTest, TextFollowsTheScaleOfTheCanvas) {
        _canvas->_scaleMode = CanvasScaleMode::ScaleWithScreen;
        _canvas->_referenceWidth = 800.0f;
        _canvas->_referenceHeight = 600.0f;

        Draw();
        const auto small = GlyphsIn(Get<UILabel>("Title"));
        ASSERT_FALSE(small.empty());

        _renderer->width = 1600.0f;
        _renderer->height = 1200.0f;
        Draw();
        const auto large = GlyphsIn(Get<UILabel>("Title"), 2.0f);
        ASSERT_EQ(large.size(), small.size());

        // The same text in a twice as large output: every quad is twice as large and twice as far from the corner
        for (size_t i = 0; i < small.size(); ++i) {
            EXPECT_NEAR(large[i].rect.x, small[i].rect.x * 2.0f, kScaleTolerance) << i;
            EXPECT_NEAR(large[i].rect.y, small[i].rect.y * 2.0f, kScaleTolerance) << i;
            EXPECT_NEAR(large[i].rect.h, small[i].rect.h * 2.0f, kScaleTolerance) << i;
        }
    }

    TEST_F(UIControlsTest, AVariantCanChangeTheFontSize) {
        auto& title = Get<UILabel>("Title");
        title.style.rect.height = UILength::Px(80.0f);  // room for a line of 48 units
        _canvas->Invalidate();
        Draw();
        ASSERT_FALSE(GlyphsIn(title).empty());
        const float before = GlyphsIn(title)[0].rect.h;

        UIElementOverride bigger;
        bigger.element = PoolString::Intern("Title");
        bigger.fontSize = 48.0f;
        UIVariant variant;
        variant.overrides.push_back(bigger);  // no condition: always on
        _canvas->GetLayout().variants.push_back(variant);
        _canvas->Invalidate();

        Draw();
        const auto glyphs = GlyphsIn(title);
        ASSERT_FALSE(glyphs.empty());
        EXPECT_GT(glyphs[0].rect.h, before * 1.5f);
    }

    TEST_F(UIControlsTest, ClipCutsTheTextAtTheRectAndEllipsisEndsIt) {
        auto& title = Get<UILabel>("Title");
        title.text = "A text that is much too long for a label of this width";
        title.wrap = false;

        title.overflow = TextOverflow::Clip;
        Draw();
        ASSERT_FALSE(GlyphsIn(title).empty());
        bool clipped = false;
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Texture && call.clip && Near(*call.clip, title.rect)) {
                clipped = true;
            }
        }
        EXPECT_TRUE(clipped) << "the glyphs are drawn inside a clip of the label's rect";
        EXPECT_TRUE(title.GetLastLayout().overflowed);

        title.overflow = TextOverflow::Ellipsis;
        Draw();
        EXPECT_TRUE(title.GetLastLayout().truncated);
    }

    TEST_F(UIControlsTest, TheValidatorKnowsWhenATextCannotFit) {
        auto& title = Get<UILabel>("Title");
        title.text = "A text that is much too long for a label of this width";
        title.wrap = false;
        const auto countTextIssues = [this]() {
            Draw();
            size_t count = 0;
            for (const UILayoutIssue& issue : _canvas->GetLayout().Validate({})) {
                count += issue.kind == UILayoutIssueKind::TextDoesNotFit ? 1u : 0u;
            }
            return count;
        };

        title.overflow = TextOverflow::Clip;
        EXPECT_EQ(countTextIssues(), 1u);
        title.overflow = TextOverflow::Ellipsis;
        EXPECT_EQ(countTextIssues(), 0u) << "a text that ends with an ellipsis is a decision, not a defect";
        title.overflow = TextOverflow::AutoFit;
        EXPECT_EQ(countTextIssues(), 0u);
    }

    TEST_F(UIControlsTest, ANineSlicePanelDrawsNinePieces) {
        auto& frame = Get<UIPanel>("Frame");
        frame.GetSprite().SetTexture(_renderer->CreateTexture(GreyPixels(16, 16).data(), 16, 16), Rect{0, 0, 16, 16});
        Draw();

        eastl::vector<DrawCall> pieces;
        for (const DrawCall& call : _renderer->calls) {
            // The glyphs of the other controls are textured too, but they are elsewhere
            if (call.kind == DrawKind::Texture && call.rect.x >= 299.0f && call.rect.x <= 400.0f && call.rect.y >= 99.0f && call.rect.y <= 160.0f) {
                pieces.push_back(call);
            }
        }
        ASSERT_EQ(pieces.size(), 9u);
        EXPECT_TRUE(Near(pieces[0].rect, {300, 100, 4, 4}));
        EXPECT_TRUE(Near(pieces[4].rect, {304, 104, 92, 52}));
        EXPECT_TRUE(Near(pieces[8].rect, {396, 156, 4, 4}));
        ASSERT_TRUE(pieces[4].source.has_value());
        EXPECT_TRUE(Near(*pieces[4].source, {4, 4, 8, 8}));
    }

    TEST_F(UIControlsTest, APanelWithoutABorderStretchesTheWholeSprite) {
        auto& frame = Get<UIPanel>("Frame");
        frame.sliceBorder = {};
        frame.GetSprite().SetTexture(_renderer->CreateTexture(GreyPixels(16, 16).data(), 16, 16), Rect{0, 0, 16, 16});
        frame.spriteTint = Color{10, 20, 30, 255};
        Draw();

        size_t drawn = 0;
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Texture && Near(call.rect, frame.rect)) {
                ++drawn;
                EXPECT_EQ(call.color.g, 20);
                ASSERT_TRUE(call.source.has_value());
                EXPECT_TRUE(Near(*call.source, {0, 0, 16, 16}));
            }
        }
        EXPECT_EQ(drawn, 1u);
    }

    TEST_F(UIControlsTest, AnImageKeepsItsProportionsUnlessItStretches) {
        auto& icon = Get<UIImage>("Icon");
        icon.GetSprite().SetTexture(_renderer->CreateTexture(GreyPixels(32, 16).data(), 32, 16));  // 2 : 1, in a square 64 x 64 rect
        icon.tint = Color{10, 20, 30, 40};

        Draw();
        ASSERT_FALSE(_renderer->calls.empty());
        const DrawCall* fitted = nullptr;
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Texture && call.rect.x >= 299.0f && call.rect.x <= 365.0f && call.rect.y < 80.0f) {
                fitted = &call;
            }
        }
        ASSERT_NE(fitted, nullptr);
        EXPECT_TRUE(Near(fitted->rect, {300, 26, 64, 32})) << "as wide as the rect, half as tall, centered";
        EXPECT_EQ(fitted->color.a, 40);
        ASSERT_TRUE(fitted->source.has_value());
        EXPECT_TRUE(Near(*fitted->source, {0, 0, 32, 16})) << "the whole texture when no rect is known";

        icon.stretch = true;
        Draw();
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Texture && call.rect.x >= 299.0f && call.rect.x <= 365.0f && call.rect.y < 80.0f) {
                EXPECT_TRUE(Near(call.rect, {300, 10, 64, 64}));
            }
        }
    }

    TEST_F(UIControlsTest, AnImageWithoutASpriteDrawsOnlyItsFill) {
        auto& icon = Get<UIImage>("Icon");
        icon.color = Color{1, 2, 3, 255};
        Draw();

        EXPECT_EQ(FillOf(icon).value_or(Color{}).b, 3);
        for (const DrawCall& call : _renderer->calls) {
            EXPECT_FALSE(call.kind == DrawKind::Texture && Near(call.rect, icon.rect));
        }
    }

    // ---- buttons ----------------------------------------------------------------------------------------------------------

    TEST_F(UIControlsTest, AButtonFollowsThePointer) {
        Draw();
        auto& ok = Get<UIButton>("Ok");
        const Rect c = Center(ok);
        EXPECT_EQ(FillOf(ok)->r, kButtonColor.r);
        EXPECT_FALSE(ok.hovered);

        MoveTo(c.x, c.y);
        Draw();
        EXPECT_TRUE(ok.hovered);
        EXPECT_GT(FillOf(ok)->r, kButtonColor.r) << "lighter under the pointer";

        ASSERT_TRUE(Press(ok));
        Draw();
        EXPECT_TRUE(ok.pressed);
        EXPECT_LT(FillOf(ok)->r, kButtonColor.r) << "darker while held";

        MoveTo(700.0f, 500.0f, SDL_BUTTON_LMASK);  // dragging off the button
        Draw();
        EXPECT_FALSE(ok.hovered);
        EXPECT_TRUE(ok.pressed);
        EXPECT_EQ(FillOf(ok)->r, kButtonColor.r) << "a press that moved off is not shown as pressed";

        Release(700.0f, 500.0f);
        Draw();
        EXPECT_FALSE(ok.pressed);
        EXPECT_EQ(FillOf(ok)->r, kButtonColor.r);
    }

    TEST_F(UIControlsTest, AClickIsAPressAndItsReleaseOnTheButton) {
        ActionRecorder actions;
        Draw();
        auto& ok = Get<UIButton>("Ok");
        const Rect c = Center(ok);

        ASSERT_TRUE(Press(ok));
        EXPECT_EQ(actions.Total(), 0u) << "nothing happens on the press";
        EXPECT_TRUE(Release(c.x, c.y));
        EXPECT_EQ(actions.Count("UIControlsTestOk"), 1);
    }

    TEST_F(UIControlsTest, ReleasingElsewhereCancelsTheClick) {
        ActionRecorder actions;
        Draw();
        auto& ok = Get<UIButton>("Ok");

        ASSERT_TRUE(Press(ok));
        Release(700.0f, 500.0f);
        EXPECT_EQ(actions.Total(), 0u);

        // Pressing outside and releasing on the button is not a click either
        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(700.0f, 500.0f, true));
        const Rect c = Center(ok);
        Release(c.x, c.y);
        EXPECT_EQ(actions.Total(), 0u);
    }

    TEST_F(UIControlsTest, OnlyTheLeftButtonClicks) {
        ActionRecorder actions;
        Draw();
        const Rect c = Center(Get<UIButton>("Ok"));

        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(c.x, c.y, true, SDL_BUTTON_RIGHT));
        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(c.x, c.y, false, SDL_BUTTON_RIGHT));
        EXPECT_EQ(actions.Total(), 0u);
        EXPECT_FALSE(Get<UIButton>("Ok").pressed);
    }

    TEST_F(UIControlsTest, ADisabledButtonTakesTheClickAndDoesNothing) {
        ActionRecorder actions;
        Draw();
        auto& off = Get<UIButton>("Off");
        const Rect c = Center(off);

        EXPECT_TRUE(Press(off)) << "the click does not fall through to what is below";
        EXPECT_TRUE(Release(c.x, c.y));
        EXPECT_EQ(actions.Total(), 0u);

        EXPECT_EQ(FillOf(off)->a, kButtonColor.a / 2) << "half transparent unless disabledColor says otherwise";
        off.disabledColor = Color{50, 50, 50, 255};
        Draw();
        EXPECT_EQ(FillOf(off)->r, 50);
    }

    TEST_F(UIControlsTest, ExplicitStateColorsWin) {
        auto& ok = Get<UIButton>("Ok");
        ok.hoverColor = Color{1, 2, 3, 255};
        ok.pressedColor = Color{4, 5, 6, 255};
        Draw();
        const Rect c = Center(ok);

        MoveTo(c.x, c.y);
        Draw();
        EXPECT_EQ(FillOf(ok)->r, 1);
        Press(ok);
        Draw();
        EXPECT_EQ(FillOf(ok)->r, 4);
    }

    TEST_F(UIControlsTest, AGameDragDoesNotLightTheButton) {
        Draw();
        auto& ok = Get<UIButton>("Ok");
        const Rect c = Center(ok);

        // A button is held that the canvas did not take (the pointer went down on the game): passing over the UI is the game's drag
        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(700.0f, 500.0f, true));
        MoveTo(c.x, c.y, SDL_BUTTON_LMASK);
        EXPECT_FALSE(ok.hovered);
    }

    TEST_F(UIControlsTest, AHiddenCanvasForgetsThePointer) {
        Draw();
        auto& ok = Get<UIButton>("Ok");
        const Rect c = Center(ok);
        MoveTo(c.x, c.y);
        Press(ok);
        ASSERT_TRUE(ok.hovered);
        ASSERT_TRUE(ok.pressed);

        EXPECT_TRUE(UICanvasComponent::SetVisibleByName(*_manager->GetSceneByName(PoolString::Intern("Main")), PoolString::Intern("Hud"), false));
        Draw();

        EXPECT_FALSE(ok.hovered);
        EXPECT_FALSE(ok.pressed);
    }

    TEST_F(UIControlsTest, TheClickOfAButtonCarriesItsValue) {
        ActionRecorder actions;
        Draw();
        auto& ok = Get<UIButton>("Ok");
        const Rect c = Center(ok);

        ASSERT_TRUE(Press(ok));
        ASSERT_TRUE(Release(c.x, c.y));
        EXPECT_EQ(actions.Count("UIControlsTestOk"), 1);
        EXPECT_TRUE(actions.LastValue().Empty()) << "a plain button emits an empty value";

        ok.value = PoolString::Intern("computer");
        ASSERT_TRUE(Press(ok));
        ASSERT_TRUE(Release(c.x, c.y));
        EXPECT_EQ(actions.Count("UIControlsTestOk"), 2);
        EXPECT_EQ(actions.LastValue(), PoolString::Intern("computer"));
    }

    TEST_F(UIControlsTest, AValueAndASelectedColorAreReadFromTheFile) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString("<root><component><elements><element type=\"UIButton\" name=\"B\" action=\"A\" value=\"two\" selectedColor=\"1,2,3,255\"/>"
                                                "</elements></component></root>"));
        auto canvas = New<UICanvasComponent>();
        ASSERT_TRUE(deserializer.BeginObject("component"));
        canvas->Deserialize(deserializer);
        deserializer.EndObject();
        ASSERT_FALSE(deserializer.HasErrors());
        ASSERT_EQ(canvas->_elements.size(), 1u);

        auto* button = canvas->_elements[0]->Cast<UIButton>();
        ASSERT_NE(button, nullptr);
        EXPECT_EQ(button->value, PoolString::Intern("two"));
        EXPECT_EQ(button->selectedColor.g, 2);
        EXPECT_FALSE(button->selected) << "the selection is a runtime state, never read from a file";
        EXPECT_FALSE(button->groupDisabled);
    }

    TEST_F(UIControlsTest, ASelectedButtonIsFilledLighterAndHasABar) {
        auto& ok = Get<UIButton>("Ok");
        const auto barDrawn = [&] {
            const Rect bar{ok.rect.x, ok.rect.y + ok.rect.h - UIButton::kSelectedBarHeight, ok.rect.w, UIButton::kSelectedBarHeight};  // the rect is laid out by the first draw
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && Near(call.rect, bar)) {
                    return std::optional<Color>(call.color);
                }
            }
            return std::optional<Color>();
        };

        Draw();
        EXPECT_FALSE(barDrawn()) << "no bar on a button that is not selected";
        const Color plain = *FillOf(ok);
        const Color hovered = [&] {
            const Rect c = Center(ok);
            MoveTo(c.x, c.y);
            Draw();
            const Color result = *FillOf(ok);
            MoveTo(700.0f, 500.0f);
            return result;
        }();

        ok.selected = true;
        Draw();
        ASSERT_TRUE(FillOf(ok));
        EXPECT_GT(FillOf(ok)->r, hovered.r) << "the derived selected fill is lighter than the hover shift";
        EXPECT_GT(FillOf(ok)->r, plain.r);
        ASSERT_TRUE(barDrawn());
        EXPECT_EQ(barDrawn()->r, ok.textColor.r);
        EXPECT_EQ(barDrawn()->a, ok.textColor.a);

        ok.selected = false;
        Draw();
        EXPECT_FALSE(barDrawn());
        EXPECT_EQ(FillOf(ok)->r, plain.r);
    }

    TEST_F(UIControlsTest, ASelectedColorWinsOverTheDerivedOne) {
        auto& ok = Get<UIButton>("Ok");
        ok.selected = true;
        ok.selectedColor = Color{7, 8, 9, 255};
        Draw();
        EXPECT_EQ(FillOf(ok)->r, 7);
        EXPECT_EQ(FillOf(ok)->b, 9);
    }

    TEST_F(UIControlsTest, HoverAndPressShiftFromTheSelectedFill) {
        auto& ok = Get<UIButton>("Ok");
        ok.selected = true;
        ok.selectedColor = Color{100, 100, 100, 255};
        Draw();
        const Rect c = Center(ok);

        MoveTo(c.x, c.y);
        Draw();
        EXPECT_GT(FillOf(ok)->r, 100) << "lighter than the selected fill under the pointer";

        ASSERT_TRUE(Press(ok));
        Draw();
        EXPECT_LT(FillOf(ok)->r, 100) << "darker than the selected fill while held";

        ok.hoverColor = Color{1, 2, 3, 255};
        ok.pressedColor = Color{4, 5, 6, 255};
        Draw();
        EXPECT_EQ(FillOf(ok)->r, 4) << "an explicit pressed color wins";
        Release(c.x, c.y);
        Draw();
        EXPECT_EQ(FillOf(ok)->r, 1) << "an explicit hover color wins";
    }

    TEST_F(UIControlsTest, DisabledWinsOverSelected) {
        auto& off = Get<UIButton>("Off");
        off.selected = true;
        off.selectedColor = Color{200, 200, 200, 255};
        Draw();
        EXPECT_EQ(FillOf(off)->r, kButtonColor.r) << "the grey of a disabled button, not the selected fill";
        EXPECT_EQ(FillOf(off)->a, kButtonColor.a / 2);
    }

    TEST_F(UIControlsTest, AGroupDisabledButtonIsGreyAndSwallowsTheClick) {
        ActionRecorder actions;
        auto& ok = Get<UIButton>("Ok");
        ok.groupDisabled = true;
        Draw();
        EXPECT_EQ(FillOf(ok)->a, kButtonColor.a / 2);

        const Rect c = Center(ok);
        EXPECT_TRUE(Press(ok)) << "the click does not fall through to what is below";
        EXPECT_TRUE(Release(c.x, c.y));
        EXPECT_EQ(actions.Total(), 0u);

        ok.groupDisabled = false;
        Draw();
        EXPECT_EQ(FillOf(ok)->a, kButtonColor.a);
        ASSERT_TRUE(Press(ok));
        Release(c.x, c.y);
        EXPECT_EQ(actions.Count("UIControlsTestOk"), 1);
    }

    TEST_F(UIControlsTest, TheValidatorChecksTheTouchSizeOfButtons) {
        // The validator checks interactive elements against the minimum touch size; Ok is 80 x 40 units at scale 1
        Draw();
        UILayoutValidatorOptions options;
        options.minTouchSize = 48.0f;
        size_t small = 0;
        for (const UILayoutIssue& issue : _canvas->GetLayout().Validate(options)) {
            small += issue.kind == UILayoutIssueKind::TouchTargetTooSmall ? 1u : 0u;
        }
        EXPECT_EQ(small, 2u) << "Ok and Off are 40 high, below a touch target of 48";
    }

}  // namespace BECore::Tests
