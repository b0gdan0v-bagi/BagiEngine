#include "UIComponentTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UILayoutValidator.h>
#include <BECore/UI/UIPreviewOverlay.h>
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

}  // namespace BECore::Tests
