#include "UIComponentTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIImageComponent.h>
#include <BECore/UI/UILayoutValidator.h>
#include <BECore/UI/UIPreviewOverlay.h>
#include <BECore/UI/UISliderComponent.h>
#include <BECore/UI/UISwitchComponent.h>
#include <BECore/UI/UIToggleComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Generated/UIComponentTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kMarkSize = 4.0f;

        void Record(const PoolString& tag, const char* hook, const UIElement& owner) {
            UIComponentLog().push_back(Format("{} {} {}", tag.ToStringView(), hook, owner.name.ToStringView()));
        }

        void DrawMark(IRenderer& renderer, const UIElement& owner, const Color& mark) {
            renderer.DrawFilledRect(owner.rect.x, owner.rect.y, kMarkSize, kMarkSize, mark);
        }

    }  // namespace

    void UITestProbe::DrawSelf(IRenderer& renderer, UIElement& owner) {
        Record(tag, "draw", owner);
        DrawMark(renderer, owner, mark);
    }

    UIContentFit UITestProbe::CheckContent(const UIElement& /*owner*/, float /*width*/, float /*height*/, float /*scale*/) const {
        return {hasContent, fits, canShrink};
    }

    void UITestProbe::OnPress(UIElement& owner, float /*x*/, float /*y*/) {
        Record(tag, "press", owner);
    }

    void UITestProbe::OnDrag(UIElement& owner, float /*x*/, float /*y*/) {
        Record(tag, "drag", owner);
    }

    void UITestProbe::OnClick(UIElement& owner) {
        Record(tag, "click", owner);
    }

    void UITestOther::DrawSelf(IRenderer& renderer, UIElement& owner) {
        Record(tag, "draw", owner);
        DrawMark(renderer, owner, mark);
    }

    UIContentFit UITestOther::CheckContent(const UIElement& /*owner*/, float /*width*/, float /*height*/, float /*scale*/) const {
        return {hasContent, fits, canShrink};
    }

    void UITestOther::OnClick(UIElement& owner) {
        Record(tag, "click", owner);
    }

    namespace {

        // Host (10, 10, 100 x 60) has a probe that takes clicks and another component, and a child Kid in its corner; Passive has a component that takes none; Small is a clicker 20 x 20;
        // Twice lists two probes, the second of a type is dropped; Plain has no components at all
        constexpr const char* kComponentsXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Host" x="10" y="10" width="100" height="60" color="10,10,10,255">
            <components>
              <component type="UITestProbe" tag="first" mark="1,0,0,255" takesClicks="true"/>
              <component type="UITestOther" tag="second" mark="2,0,0,255"/>
            </components>
            <children>
              <element type="UIElement" name="Kid" x="0" y="0" width="20" height="20" color="30,30,30,255"/>
            </children>
          </element>
          <element type="UIElement" name="Passive" x="200" y="10" width="60" height="60" color="50,50,50,255">
            <components>
              <component type="UITestOther" tag="quiet" mark="3,0,0,255"/>
            </components>
          </element>
          <element type="UIElement" name="Small" x="300" y="10" width="20" height="20">
            <components>
              <component type="UITestProbe" tag="tiny" mark="4,0,0,255" takesClicks="true"/>
            </components>
          </element>
          <element type="UIElement" name="Twice" x="400" y="10" width="60" height="60">
            <components>
              <component type="UITestProbe" tag="one" mark="5,0,0,255"/>
              <component type="UITestOther" tag="between" mark="6,0,0,255"/>
              <component type="UITestProbe" tag="two" mark="7,0,0,255"/>
            </components>
          </element>
          <element type="UIElement" name="Plain" x="500" y="10" width="60" height="60" color="70,70,70,255"/>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        const Color kHostColor{10, 10, 10, 255};
        const Color kFirstMark{1, 0, 0, 255};
        const Color kSecondMark{2, 0, 0, 255};
        const Color kKidColor{30, 30, 30, 255};

        SDL_MouseButtonEvent Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

        SDL_MouseMotionEvent Motion(float x, float y, SDL_MouseButtonFlags held) {
            SDL_MouseMotionEvent event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.x = x;
            event.y = y;
            event.state = held;
            return event;
        }

        eastl::string ElementToXml(const UIElement& element) {
            XmlSerializer serializer;
            EXPECT_TRUE(serializer.BeginObject("element"));
            element.Serialize(serializer);
            serializer.EndObject();
            return serializer.SaveToString();
        }

        eastl::vector<eastl::string> TypeNames(const UIElement& element) {
            eastl::vector<eastl::string> names;
            for (const auto& component : element.components) {
                names.push_back(eastl::string(component->GetTypeMeta().typeName));
            }
            return names;
        }

    }  // namespace

    class UIComponentTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            UIComponentLog().clear();
            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kComponentsXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _canvas = _manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);
        }

        void TearDown() override {
            _canvas.Reset();
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            UIComponentLog().clear();
        }

        void Draw() {
            _manager->SetActiveScene(PoolString::Intern("Main"));
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        UIElement& Get(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            return *element;
        }

        /// The position of the first rect drawn in @p color in the last draw, -1 when there is none
        int IndexOf(const Color& color) const {
            for (size_t i = 0; i < _renderer->calls.size(); ++i) {
                const DrawCall& call = _renderer->calls[i];
                if (call.kind == DrawKind::Rect && call.color.r == color.r && call.color.g == color.g && call.color.b == color.b && call.color.a == color.a) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        static bool Contains(const eastl::vector<eastl::string>& log, const char* line) {
            return eastl::find(log.begin(), log.end(), eastl::string(line)) != log.end();
        }

        bool PressOn(const UIElement& element) {
            return CoreManager::GetUIInput().DispatchMouseButtonDown(Button(element.rect.x + element.rect.w * 0.5f, element.rect.y + element.rect.h * 0.5f, true));
        }

        bool ReleaseOn(const UIElement& element) {
            return CoreManager::GetUIInput().DispatchMouseButtonUp(Button(element.rect.x + element.rect.w * 0.5f, element.rect.y + element.rect.h * 0.5f, false));
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
    };

    // ---- the list in a file -----------------------------------------------------------------------------------------------

    TEST_F(UIComponentTest, TheComponentsComeFromTheFileInListOrder) {
        const UIElement& host = Get("Host");
        ASSERT_EQ(host.components.size(), 2u);
        EXPECT_TRUE(host.components[0]->Is<UITestProbe>());
        EXPECT_TRUE(host.components[1]->Is<UITestOther>());
        EXPECT_EQ(host.components[0]->Cast<UITestProbe>()->tag, PoolString::Intern("first"));
        EXPECT_TRUE(host.components[0]->Cast<UITestProbe>()->takesClicks);
        EXPECT_EQ(host.components[1]->Cast<UITestOther>()->tag, PoolString::Intern("second"));
        EXPECT_TRUE(Get("Plain").components.empty());
    }

    TEST_F(UIComponentTest, TheComponentsSaveAndLoadBackInTheSameOrder) {
        const eastl::string xml = ElementToXml(Get("Host"));
        EXPECT_NE(xml.find("<components>"), eastl::string::npos) << xml.c_str();

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
        auto loaded = New<UIElement>();
        ASSERT_TRUE(deserializer.BeginObject("element"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        EXPECT_FALSE(deserializer.HasErrors());

        EXPECT_EQ(TypeNames(*loaded), TypeNames(Get("Host")));
        EXPECT_STREQ(ElementToXml(*loaded).c_str(), xml.c_str());
        EXPECT_EQ(loaded->components[0]->Cast<UITestProbe>()->mark.g, 0);
        EXPECT_EQ(loaded->components[0]->Cast<UITestProbe>()->mark.r, 1);
    }

    TEST_F(UIComponentTest, AnElementWithNoComponentsSavesNoneAndLoadsNone) {
        const eastl::string xml = ElementToXml(Get("Plain"));
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
        auto loaded = New<UIElement>();
        ASSERT_TRUE(deserializer.BeginObject("element"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        EXPECT_TRUE(loaded->components.empty());
        EXPECT_FALSE(deserializer.HasErrors());
    }

    TEST_F(UIComponentTest, AnUnknownComponentTypeIsReported) {
        constexpr const char* xml = R"(<root><element name="Odd"><components><component type="NoSuchComponent"/></components></element></root>)";
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml));
        auto loaded = New<UIElement>();
        ASSERT_TRUE(deserializer.BeginObject("element"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        ASSERT_TRUE(deserializer.HasErrors());
        EXPECT_NE(deserializer.GetErrors()[0].errorMessage.ToStringView().find("NoSuchComponent"), eastl::string_view::npos);
    }

    // ---- one of each type -------------------------------------------------------------------------------------------------

    TEST_F(UIComponentTest, ASecondComponentOfATypeInAFileIsDroppedAndTheFirstKept) {
        const UIElement& twice = Get("Twice");
        ASSERT_EQ(twice.components.size(), 2u);
        EXPECT_EQ(twice.components[0]->Cast<UITestProbe>()->tag, PoolString::Intern("one"));
        EXPECT_EQ(twice.components[1]->Cast<UITestOther>()->tag, PoolString::Intern("between"));
        EXPECT_EQ(twice.GetComponent<UITestProbe>()->tag, PoolString::Intern("one"));
    }

    TEST_F(UIComponentTest, GetComponentFindsATypeAndAddComponentReturnsTheExistingOne) {
        UIElement& host = Get("Host");
        EXPECT_EQ(host.GetComponent<UITestProbe>(), host.components[0]->Cast<UITestProbe>());
        EXPECT_EQ(&host.AddComponent<UITestOther>(), host.components[1]->Cast<UITestOther>());
        EXPECT_EQ(host.components.size(), 2u);
    }

    TEST_F(UIComponentTest, AddComponentMakesTheMissingOneAtTheEndOfTheList) {
        UIElement& passive = Get("Passive");
        EXPECT_EQ(passive.GetComponent<UITestProbe>(), nullptr);
        UITestProbe& added = passive.AddComponent<UITestProbe>();
        ASSERT_EQ(passive.components.size(), 2u);
        EXPECT_EQ(passive.components[1].Get(), &added);
        EXPECT_EQ(&passive.AddComponent<UITestProbe>(), &added);
        EXPECT_EQ(passive.components.size(), 2u);
    }

    // ---- order of the draw ------------------------------------------------------------------------------------------------

    TEST_F(UIComponentTest, TheBoxIsDrawnThenTheComponentsInListOrderThenTheChildren) {
        Draw();
        const int box = IndexOf(kHostColor);
        const int first = IndexOf(kFirstMark);
        const int second = IndexOf(kSecondMark);
        const int kid = IndexOf(kKidColor);
        ASSERT_GE(box, 0);
        ASSERT_GE(first, 0);
        ASSERT_GE(second, 0);
        ASSERT_GE(kid, 0);
        EXPECT_LT(box, first);
        EXPECT_LT(first, second);
        EXPECT_LT(second, kid);
    }

    TEST_F(UIComponentTest, AHiddenElementDrawsNoComponents) {
        Get("Host").style.visible = false;
        _canvas->Invalidate();
        Draw();
        EXPECT_EQ(IndexOf(kFirstMark), -1);
        EXPECT_EQ(IndexOf(kSecondMark), -1);
    }

    TEST_F(UIComponentTest, TheComponentsGetTheirOwnerToDraw) {
        Draw();
        EXPECT_TRUE(Contains(UIComponentLog(), "first draw Host"));
        EXPECT_TRUE(Contains(UIComponentLog(), "second draw Host"));
        EXPECT_TRUE(Contains(UIComponentLog(), "quiet draw Passive"));
    }

    // ---- the pointer ------------------------------------------------------------------------------------------------------

    TEST_F(UIComponentTest, PressDragAndClickReachTheComponentsInListOrderWithTheirOwner) {
        Draw();
        UIComponentLog().clear();
        const UIElement& host = Get("Host");

        EXPECT_TRUE(PressOn(host));
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(host.rect.x + 70.0f, host.rect.y + 40.0f, SDL_BUTTON_LMASK));
        EXPECT_TRUE(ReleaseOn(host));

        const eastl::vector<eastl::string> expected{"first press Host", "first drag Host", "first click Host", "second click Host"};
        EXPECT_EQ(UIComponentLog(), expected);
    }

    TEST_F(UIComponentTest, LettingGoElsewhereCancelsTheClick) {
        Draw();
        UIComponentLog().clear();
        EXPECT_TRUE(PressOn(Get("Host")));
        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(700.0f, 500.0f, false));

        EXPECT_FALSE(Contains(UIComponentLog(), "first click Host"));
        EXPECT_FALSE(Contains(UIComponentLog(), "second click Host"));
    }

    TEST_F(UIComponentTest, AnElementWhoseComponentsTakeNoClicksIsNotInteractiveAndNotAHitTarget) {
        Draw();
        const UIElement& passive = Get("Passive");
        EXPECT_FALSE(passive.IsInteractive());
        EXPECT_FALSE(passive.IsHitTestable());
        EXPECT_FALSE(Get("Plain").IsInteractive());

        UIComponentLog().clear();
        EXPECT_FALSE(PressOn(passive));
        ReleaseOn(passive);
        EXPECT_FALSE(Contains(UIComponentLog(), "quiet click Passive"));
    }

    TEST_F(UIComponentTest, AComponentThatTakesClicksMakesTheElementInteractiveAndAHitTarget) {
        const UIElement& host = Get("Host");
        EXPECT_FALSE(host.interactive);  // the field stays what the file says
        EXPECT_TRUE(host.IsInteractive());
        EXPECT_TRUE(host.IsHitTestable());
    }

    TEST_F(UIComponentTest, TheFieldInteractiveStillMakesAnElementInteractive) {
        UIElement& plain = Get("Plain");
        plain.interactive = true;
        EXPECT_TRUE(plain.IsInteractive());
    }

    // ---- the state look of the box ----------------------------------------------------------------------------------------

    TEST_F(UIComponentTest, AComponentIsNotCheckedUnlessItSaysSo) {
        UIElement& host = Get("Host");
        const UIElement& passive = Get("Passive");
        EXPECT_FALSE(host.components[0]->IsChecked(host));
        EXPECT_FALSE(host.GetVisualState().checked);
        EXPECT_FALSE(passive.GetVisualState().checked);

        host.GetComponent<UITestProbe>()->checked = true;
        EXPECT_TRUE(host.components[0]->IsChecked(host));
        EXPECT_TRUE(host.GetVisualState().checked) << "any component of the element is enough";
        EXPECT_FALSE(passive.GetVisualState().checked);
    }

    TEST_F(UIComponentTest, TheBoxTakesTheStateLookOnlyWhenAComponentGivesIt) {
        Draw();
        UIElement& host = Get("Host");        // a component that takes clicks gives the state look
        UIElement& passive = Get("Passive");  // one that takes none does not: a hovered panel that merely blocks input must not light up
        EXPECT_TRUE(host.GivesStateLook());
        EXPECT_FALSE(passive.GivesStateLook());

        host.hovered = true;
        passive.hovered = true;
        _renderer->ClearCalls();
        host.DrawSelf(*_renderer);
        passive.DrawSelf(*_renderer);

        ASSERT_EQ(_renderer->calls.size(), 2u);
        EXPECT_TRUE(ColorsEqual(_renderer->calls[0].color, ShiftColor(kHostColor, kHoverBrighten)));
        EXPECT_TRUE(ColorsEqual(_renderer->calls[1].color, Color{50, 50, 50, 255}));
    }

    TEST_F(UIComponentTest, TheStateColorsOfTheElementAreTheFillsOfItsStates) {
        Draw();
        UIElement& host = Get("Host");
        host.pressedColor = Color{90, 91, 92, 255};
        host.disabledColor = Color{80, 81, 82, 255};
        host.hovered = true;
        host.pressed = true;
        _renderer->ClearCalls();
        host.DrawSelf(*_renderer);
        EXPECT_TRUE(ColorsEqual(_renderer->calls[0].color, host.pressedColor));

        host.groupDisabled = true;  // the group's flag disables an element as a `disabled` does a button
        _renderer->ClearCalls();
        host.DrawSelf(*_renderer);
        EXPECT_TRUE(ColorsEqual(_renderer->calls[0].color, host.disabledColor));
    }

    TEST_F(UIComponentTest, ASelectedElementThatShowsItsStateDrawsTheBarAtItsFoot) {
        Draw();
        UIElement& host = Get("Host");
        host.selected = true;
        _renderer->ClearCalls();
        host.DrawSelf(*_renderer);
        ASSERT_EQ(_renderer->calls.size(), 2u);
        EXPECT_FLOAT_EQ(_renderer->calls[1].rect.h, UIStateLook::kSelectedBarHeight);
        EXPECT_FLOAT_EQ(_renderer->calls[1].rect.y + _renderer->calls[1].rect.h, host.rect.y + host.rect.h);
    }

    // ---- touch size and content -------------------------------------------------------------------------------------------

    TEST_F(UIComponentTest, TheValidatorChecksTheTouchSizeOfAnElementWhoseComponentTakesClicks) {
        Draw();
        UILayoutValidatorOptions options;
        options.minTouchSize = 48.0f;
        const auto issues = _canvas->GetLayout().Validate(options);

        eastl::vector<PoolString> small;
        for (const UILayoutIssue& issue : issues) {
            if (issue.kind == UILayoutIssueKind::TouchTargetTooSmall) {
                small.push_back(issue.element);
            }
        }
        ASSERT_EQ(small.size(), 1u) << "Host is 100 x 60, Passive and Plain take no clicks";
        EXPECT_EQ(small[0], PoolString::Intern("Small"));
    }

    TEST_F(UIComponentTest, ThePreviewMarksTheTouchTargetsOfComposedElements) {
        Draw();
        const auto marks = UIPreviewOverlay::TouchTargets(_canvas->GetLayout(), 48.0f, 1.0f);
        eastl::vector<PoolString> names;
        for (const TouchMark& mark : marks) {
            names.push_back(mark.name);
        }
        EXPECT_NE(eastl::find(names.begin(), names.end(), PoolString::Intern("Host")), names.end());
        EXPECT_NE(eastl::find(names.begin(), names.end(), PoolString::Intern("Small")), names.end());
        EXPECT_EQ(eastl::find(names.begin(), names.end(), PoolString::Intern("Passive")), names.end());
    }

    TEST_F(UIComponentTest, ContentIsTheCombinationOfWhatTheComponentsSay) {
        auto element = New<UIElement>();
        EXPECT_FALSE(element->CheckContent(10.0f, 10.0f, 1.0f).hasContent);

        auto& probe = element->AddComponent<UITestProbe>();
        auto& other = element->AddComponent<UITestOther>();
        EXPECT_FALSE(element->CheckContent(10.0f, 10.0f, 1.0f).hasContent) << "components with nothing to show say nothing";

        probe.hasContent = true;
        UIContentFit fit = element->CheckContent(10.0f, 10.0f, 1.0f);
        EXPECT_TRUE(fit.hasContent);
        EXPECT_TRUE(fit.fits);

        other.hasContent = true;
        other.fits = false;
        other.canShrink = true;
        fit = element->CheckContent(10.0f, 10.0f, 1.0f);
        EXPECT_TRUE(fit.hasContent);
        EXPECT_FALSE(fit.fits);
        EXPECT_TRUE(fit.canShrink);

        probe.fits = false;  // a part that cannot shrink is a defect, whatever the other can do
        fit = element->CheckContent(10.0f, 10.0f, 1.0f);
        EXPECT_FALSE(fit.fits);
        EXPECT_FALSE(fit.canShrink);
    }

    TEST_F(UIComponentTest, TheValidatorReportsTextThatDoesNotFitFromAComponent) {
        auto& probe = Get("Twice").AddComponent<UITestProbe>();
        probe.hasContent = true;
        probe.fits = false;
        Draw();
        const auto issues = _canvas->GetLayout().Validate({});
        const bool reported =
            eastl::any_of(issues.begin(), issues.end(), [](const UILayoutIssue& issue) { return issue.kind == UILayoutIssueKind::TextDoesNotFit && issue.element == PoolString::Intern("Twice"); });
        EXPECT_TRUE(reported);

        probe.canShrink = true;
        const auto shrinking = _canvas->GetLayout().Validate({});
        EXPECT_TRUE(eastl::none_of(shrinking.begin(), shrinking.end(), [](const UILayoutIssue& issue) { return issue.kind == UILayoutIssueKind::TextDoesNotFit; }));
    }

    // ---- UIImageComponent -------------------------------------------------------------------------------------------------

    namespace {

        constexpr uint32_t kPictureSide = 32;
        constexpr float kPictureHalfHeight = 16.0f;

        /// Plain is 60 x 60 at (500, 10); the box is drawn under the picture
        constexpr float kPlainX = 500.0f;
        constexpr float kPlainY = 10.0f;
        constexpr float kPlainSide = 60.0f;

        const Color kHoverTint{1, 2, 3, 255};
        const Color kPressedTint{4, 5, 6, 255};
        const Color kDisabledTint{7, 8, 9, 128};

    }  // namespace

    class UIImageComponentTest : public UIComponentTest {
    protected:
        void SetUp() override {
            UIComponentTest::SetUp();
            const eastl::vector<uint8_t> pixels(static_cast<size_t>(kPictureSide) * kPictureSide * 4, 200);
            _texture = _renderer->CreateTexture(pixels.data(), kPictureSide, kPictureSide);
            _image = &Get("Plain").AddComponent<UIImageComponent>();
            _image->GetSprite().SetTexture(_texture);
        }

        void TearDown() override {
            _image = nullptr;
            _texture.Reset();
            UIComponentTest::TearDown();
        }

        /// The one textured draw of the last frame, none when there was no picture
        std::optional<DrawCall> Picture() const {
            std::optional<DrawCall> found;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture) {
                    EXPECT_FALSE(found.has_value()) << "more than one picture";
                    found = call;
                }
            }
            return found;
        }

        IntrusivePtr<ITexture> _texture;
        UIImageComponent* _image = nullptr;
    };

    TEST(UISpriteFitTest, TheLargestRectOfTheProportionsThatFitsIsCentred) {
        const Rect wide = UISprite::FitInto({10.0f, 20.0f, 100.0f, 50.0f}, {0.0f, 0.0f, 32.0f, 32.0f});
        EXPECT_NEAR(wide.x, 35.0f, 1e-3f);
        EXPECT_NEAR(wide.y, 20.0f, 1e-3f);
        EXPECT_NEAR(wide.w, 50.0f, 1e-3f);
        EXPECT_NEAR(wide.h, 50.0f, 1e-3f);

        const Rect tall = UISprite::FitInto({0.0f, 0.0f, 20.0f, 60.0f}, {5.0f, 5.0f, 40.0f, 20.0f});
        EXPECT_NEAR(tall.x, 0.0f, 1e-3f);
        EXPECT_NEAR(tall.y, 25.0f, 1e-3f);
        EXPECT_NEAR(tall.w, 20.0f, 1e-3f);
        EXPECT_NEAR(tall.h, 10.0f, 1e-3f);
    }

    TEST(UISpriteFitTest, AnEmptySourceFillsTheArea) {
        const Rect area{3.0f, 4.0f, 50.0f, 70.0f};
        const Rect fitted = UISprite::FitInto(area, {});
        EXPECT_FLOAT_EQ(fitted.x, area.x);
        EXPECT_FLOAT_EQ(fitted.y, area.y);
        EXPECT_FLOAT_EQ(fitted.w, area.w);
        EXPECT_FLOAT_EQ(fitted.h, area.h);
    }

    TEST_F(UIImageComponentTest, ThePictureIsFittedAndCentredInTheRectOfTheOwner) {
        _image->GetSprite().SetTexture(_texture, {0.0f, 0.0f, static_cast<float>(kPictureSide), kPictureHalfHeight});
        Draw();
        const auto picture = Picture();
        ASSERT_TRUE(picture.has_value());
        EXPECT_NEAR(picture->rect.x, kPlainX, 1e-3f);
        EXPECT_NEAR(picture->rect.w, kPlainSide, 1e-3f);
        EXPECT_NEAR(picture->rect.h, kPlainSide * 0.5f, 1e-3f) << "the proportions of the sprite are kept";
        EXPECT_NEAR(picture->rect.y, kPlainY + kPlainSide * 0.25f, 1e-3f) << "centred in the height";
    }

    TEST_F(UIImageComponentTest, ASquarePictureInAWideRectKeepsItsShape) {
        UIElement& host = Get("Host");
        auto& image = host.AddComponent<UIImageComponent>();
        image.GetSprite().SetTexture(_texture);
        _image->tint = UIColor{0, 0, 0, 0};  // the picture of Plain is not drawn: only Host's is left
        Draw();
        const auto picture = Picture();
        ASSERT_TRUE(picture.has_value());
        EXPECT_NEAR(picture->rect.w, host.rect.h, 1e-3f);
        EXPECT_NEAR(picture->rect.h, host.rect.h, 1e-3f);
        EXPECT_NEAR(picture->rect.x, host.rect.x + (host.rect.w - host.rect.h) * 0.5f, 1e-3f);
    }

    TEST_F(UIImageComponentTest, StretchFillsTheRect) {
        _image->GetSprite().SetTexture(_texture, {0.0f, 0.0f, static_cast<float>(kPictureSide), kPictureHalfHeight});
        _image->stretch = true;
        Draw();
        const auto picture = Picture();
        ASSERT_TRUE(picture.has_value());
        EXPECT_NEAR(picture->rect.x, kPlainX, 1e-3f);
        EXPECT_NEAR(picture->rect.y, kPlainY, 1e-3f);
        EXPECT_NEAR(picture->rect.w, kPlainSide, 1e-3f);
        EXPECT_NEAR(picture->rect.h, kPlainSide, 1e-3f);
    }

    TEST_F(UIImageComponentTest, ThePictureIsDrawnAfterTheBoxOfItsOwner) {
        Draw();
        size_t boxAt = _renderer->calls.size();
        size_t pictureAt = _renderer->calls.size();
        for (size_t i = 0; i < _renderer->calls.size(); ++i) {
            const DrawCall& call = _renderer->calls[i];
            if (call.kind == DrawKind::Rect && call.rect.x == kPlainX) {
                boxAt = i;
            } else if (call.kind == DrawKind::Texture) {
                pictureAt = i;
            }
        }
        ASSERT_LT(boxAt, _renderer->calls.size());
        EXPECT_LT(boxAt, pictureAt);
    }

    TEST_F(UIImageComponentTest, ATransparentTintDrawsNothing) {
        _image->tint = UIColor{255, 255, 255, 0};
        Draw();
        EXPECT_FALSE(Picture().has_value());
    }

    TEST_F(UIImageComponentTest, AnUnknownSpriteDrawsNothing) {
        auto& other = Get("Passive").AddComponent<UIImageComponent>();
        other.sprite = PoolString::Intern("NoSuchSprite");
        _image->tint = UIColor{0, 0, 0, 0};
        Draw();
        EXPECT_FALSE(Picture().has_value());
    }

    TEST_F(UIImageComponentTest, TheTintFollowsTheStateOfTheOwner) {
        UIElement& plain = Get("Plain");
        _image->hoverTint = kHoverTint;
        _image->pressedTint = kPressedTint;
        _image->disabledTint = kDisabledTint;
        const auto drawn = [this]() {
            Draw();
            const auto picture = Picture();
            return picture.has_value() ? picture->color : Color{};
        };

        EXPECT_TRUE(ColorsEqual(drawn(), Color(255, 255, 255, 255)));
        plain.hovered = true;
        EXPECT_TRUE(ColorsEqual(drawn(), kHoverTint));
        plain.pressed = true;
        EXPECT_TRUE(ColorsEqual(drawn(), kPressedTint));
        plain.pressed = false;
        plain.hovered = false;
        plain.selected = true;
        EXPECT_TRUE(ColorsEqual(drawn(), Color(255, 255, 255, 255))) << "a selected picture is left alone";
        plain.groupDisabled = true;
        EXPECT_TRUE(ColorsEqual(drawn(), kDisabledTint));
    }

    TEST_F(UIImageComponentTest, ATintThatIsNotGivenForAStateIsDerivedFromTheTint) {
        UIElement& plain = Get("Plain");
        _image->tint = UIColor{100, 120, 140, 200};
        const Color base = _image->tint;
        const UIStateLook::State hovered{true, false, false, false};
        const UIStateLook::State pressed{true, true, false, false};
        const UIStateLook::State disabled{false, false, true, false};

        plain.hovered = true;
        Draw();
        EXPECT_TRUE(ColorsEqual(Picture()->color, UIStateLook::ResolveTint(base, {}, hovered)));
        EXPECT_TRUE(ColorsEqual(_image->GetStateTint(hovered), UIStateLook::ResolveTint(base, {}, hovered)));
        EXPECT_GT(_image->GetStateTint(hovered).r, base.r) << "lighter under the pointer";
        EXPECT_LT(_image->GetStateTint(pressed).r, base.r) << "darker pressed";
        EXPECT_LT(_image->GetStateTint(disabled).a, base.a) << "fainter disabled";
    }

    TEST_F(UIImageComponentTest, APressThatLeftTheOwnerIsNotShownAsPressed) {
        UIElement& plain = Get("Plain");
        _image->pressedTint = kPressedTint;
        plain.pressed = true;
        plain.hovered = false;
        Draw();
        EXPECT_TRUE(ColorsEqual(Picture()->color, Color(255, 255, 255, 255)));
    }

    TEST_F(UIImageComponentTest, TheFieldsComeFromAFileAndSaveBack) {
        constexpr const char* xml =
            R"(<root><element type="UIElement" name="Icon" width="40" height="40"><components><component type="UIImageComponent" sprite="icons.play" tint="10,20,30,255" hoverTint="1,2,3,255" pressedTint="4,5,6,255" disabledTint="7,8,9,128" stretch="true"/></components></element></root>)";
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml));
        auto loaded = New<UIElement>();
        ASSERT_TRUE(deserializer.BeginObject("element"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        ASSERT_FALSE(deserializer.HasErrors());

        const UIImageComponent* image = loaded->GetComponent<UIImageComponent>();
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->sprite, PoolString::Intern("icons.play"));
        EXPECT_TRUE(ColorsEqual(image->tint, Color(10, 20, 30, 255)));
        EXPECT_TRUE(ColorsEqual(image->hoverTint, kHoverTint));
        EXPECT_TRUE(ColorsEqual(image->pressedTint, kPressedTint));
        EXPECT_TRUE(ColorsEqual(image->disabledTint, kDisabledTint));
        EXPECT_TRUE(image->stretch);

        const eastl::string saved = ElementToXml(*loaded);
        EXPECT_NE(saved.find("hoverTint"), eastl::string::npos) << saved.c_str();
        EXPECT_NE(saved.find("stretch"), eastl::string::npos) << saved.c_str();
    }

    TEST_F(UIComponentTest, TheToggleAndSwitchComponentsComeFromAFileAndSaveBack) {
        constexpr const char* xml =
            R"(<root><element type="UIElement" name="Row"><components><component type="UIToggleComponent" bind="mode" action="SetMode" onValue="1" offValue="0"/></components><children><element type="UIElement" name="RowSwitch"><components><component type="UISwitchComponent" trackColor="1,2,3,255" fillColor="4,5,6,255" thumbColor="7,8,9,255" partShape="Round"/></components></element></children></element></root>)";
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml));
        auto loaded = New<UIElement>();
        ASSERT_TRUE(deserializer.BeginObject("element"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        ASSERT_FALSE(deserializer.HasErrors());

        const UIToggleComponent* toggle = loaded->GetComponent<UIToggleComponent>();
        ASSERT_NE(toggle, nullptr);
        EXPECT_EQ(toggle->bind, PoolString::Intern("mode"));
        EXPECT_EQ(toggle->action, PoolString::Intern("SetMode"));
        EXPECT_EQ(toggle->onValue, PoolString::Intern("1"));
        EXPECT_EQ(toggle->offValue, PoolString::Intern("0"));
        const UISwitchComponent* thumb = loaded->children[0]->GetComponent<UISwitchComponent>();
        ASSERT_NE(thumb, nullptr);
        EXPECT_TRUE(ColorsEqual(thumb->trackColor, Color(1, 2, 3, 255)));
        EXPECT_TRUE(ColorsEqual(thumb->fillColor, Color(4, 5, 6, 255)));
        EXPECT_TRUE(ColorsEqual(thumb->thumbColor, Color(7, 8, 9, 255)));
        EXPECT_EQ(thumb->partShape, UIPartShape::Round);

        const eastl::string saved = ElementToXml(*loaded);
        EXPECT_NE(saved.find("UIToggleComponent"), eastl::string::npos) << saved.c_str();
        EXPECT_NE(saved.find("partShape=\"Round\""), eastl::string::npos) << saved.c_str();

        XmlDeserializer again;
        ASSERT_TRUE(again.LoadFromString(eastl::string_view(saved.data(), saved.size())));
        auto reloaded = New<UIElement>();
        ASSERT_TRUE(again.BeginObject("element"));
        reloaded->Deserialize(again);
        again.EndObject();
        EXPECT_FALSE(again.HasErrors());
        EXPECT_STREQ(ElementToXml(*reloaded).c_str(), saved.c_str());
    }

    TEST_F(UIComponentTest, TheToggleAndSwitchComponentsAreMadeByTheFactoryAndAreNotCheckedOrStateGiving) {
        for (const char* type : {"UIToggleComponent", "UISwitchComponent"}) {
            const auto made = AbstractFactory<UIComponent>::GetInstance().Create(type);
            ASSERT_TRUE(made) << type;
            EXPECT_FALSE(made->GivesStateLook()) << type << ": the clicker beside it gives the state look";
        }
        const auto toggle = New<UIToggleComponent>();
        EXPECT_TRUE(toggle->MakesInteractive());
        EXPECT_FALSE(New<UISwitchComponent>()->MakesInteractive());
    }

    TEST_F(UIComponentTest, TheSliderComponentComesFromAFileAndSavesBack) {
        constexpr const char* xml =
            R"(<root><element type="UIElement" name="Volume"><components><component type="UIInputClicker" stateLook="false"/><component type="UISliderComponent" bind="volume" action="SetVolume" minValue="-1" maxValue="3" step="0.5" trackColor="1,2,3,255" fillColor="4,5,6,255" thumbColor="7,8,9,255" partShape="Round"/></components></element></root>)";
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml));
        auto loaded = New<UIElement>();
        ASSERT_TRUE(deserializer.BeginObject("element"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        ASSERT_FALSE(deserializer.HasErrors());

        const UISliderComponent* slider = loaded->GetComponent<UISliderComponent>();
        ASSERT_NE(slider, nullptr);
        EXPECT_EQ(slider->bind, PoolString::Intern("volume"));
        EXPECT_EQ(slider->action, PoolString::Intern("SetVolume"));
        EXPECT_FLOAT_EQ(slider->minValue, -1.0f);
        EXPECT_FLOAT_EQ(slider->maxValue, 3.0f);
        EXPECT_FLOAT_EQ(slider->step, 0.5f);
        EXPECT_TRUE(ColorsEqual(slider->trackColor, Color(1, 2, 3, 255)));
        EXPECT_TRUE(ColorsEqual(slider->fillColor, Color(4, 5, 6, 255)));
        EXPECT_TRUE(ColorsEqual(slider->thumbColor, Color(7, 8, 9, 255)));
        EXPECT_EQ(slider->partShape, UIPartShape::Round);
        EXPECT_TRUE(loaded->IsInteractive());
        EXPECT_FALSE(loaded->GivesStateLook());

        const eastl::string saved = ElementToXml(*loaded);
        XmlDeserializer again;
        ASSERT_TRUE(again.LoadFromString(eastl::string_view(saved.data(), saved.size())));
        auto reloaded = New<UIElement>();
        ASSERT_TRUE(again.BeginObject("element"));
        reloaded->Deserialize(again);
        again.EndObject();
        EXPECT_FALSE(again.HasErrors());
        EXPECT_STREQ(ElementToXml(*reloaded).c_str(), saved.c_str());
    }

}  // namespace BECore::Tests
