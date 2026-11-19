#pragma once

#include <BECore/Config/XmlConfig.h>
#include <BECore/Format/Format.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIEnvironment.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UIVars.h>
#include <BECore/Utils/FunctionRef.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Common/UIInputEvents.h>
#include <Common/UILayoutMatrix.h>
#include <Common/UITestSupport.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/SubscriptionHolder.h>
#include <gtest/gtest.h>
#include <type_traits>

namespace BECore::Tests {

    /// The height of a control that takes a press in a screen of the screen tests: a touch target at the smallest fit scale of the matrix (0.571)
    inline constexpr float kScreenControlHeight = 56.0f;

    /// The narrowest window of the matrix, in pixels: 9:16 at its smallest width
    inline constexpr float kNarrowestWidth = 480.0f;
    inline constexpr float kNarrowestHeight = 853.0f;

    /// The scene text of @p sceneTemplate, which names its view size `@VIEW_W@` x `@VIEW_H@`, at this size and then changed by @p edit (a deliberately broken copy)
    [[nodiscard]] inline String SceneXml(StringView sceneTemplate, float viewWidth, float viewHeight, FunctionRef<void(String&)> edit = NoTextEdit) {
        String xml(sceneTemplate.data(), sceneTemplate.size());
        ReplaceAll(xml, "@VIEW_W@", ToString(static_cast<int>(viewWidth)));
        ReplaceAll(xml, "@VIEW_H@", ToString(static_cast<int>(viewHeight)));
        edit(xml);
        return xml;
    }

    /// What reaches game code instead of a canvas: the board under a panel
    class BoardClicks : public SubscriptionHolder {
    public:
        BoardClicks() {
            Subscribe<SDLEvents::MouseButtonDownEvent, &BoardClicks::OnDown>(this);
        }

        int downs = 0;

    private:
        void OnDown(const SDLEvents::MouseButtonDownEvent&) {
            ++downs;
        }
    };

    /// What is wrong with a laid-out canvas in the language that is set now, one problem to a line, nothing when it is right: a shown control with an empty rect (a missing
    /// height), text that its control cuts (CutsItsText: the validator does not see it for an ellipsis text) and a control that takes a press and is under @p minControlHeight
    [[nodiscard]] inline String Problems(const UICanvasLayout& layout, float minControlHeight = kScreenControlHeight) {
        String problems;
        const float scale = layout.GetResolved().scale;
        Vector<const UIElement*> controls;
        CollectControls(*layout.root, controls);
        for (const UIElement* control : controls) {
            if (control->rect.IsEmpty()) {
                if (control->active.visible) {
                    problems += Format("{} '{}' has an empty rect (a missing height?)\n", control->name.ToStringView(), ShownText(*control).c_str());
                }
                continue;
            }
            if (CutsItsText(*control, scale)) {
                const float air = IsButton(*control) ? kButtonAir : 0.0f;
                problems +=
                    Format("{} cuts '{}': {:.1f} units wide with {:.1f} of air each side, at scale {:.3f}\n", control->name.ToStringView(), ShownText(*control).c_str(), control->rect.w, air, scale);
            }
            if (control->IsInteractive() && control->rect.h < minControlHeight - kRectTolerance) {
                problems += Format("{} is {:.1f} units high, under {:.0f}\n", control->name.ToStringView(), control->rect.h, minControlHeight);
            }
        }
        return problems;
    }

    /// What a press and its release were taken for; true when both were
    struct UIClick {
        bool pressed = false;
        bool released = false;

        explicit operator bool() const {
            return pressed && released;
        }
    };

    /**
     * @brief The fixture of a screen test of a panel (`ui-layout.md`, "A screen test"): a scene `Main` with a canvas `Hud` and the panel's canvas on the node the derived
     * fixture names, loaded from XML into a RecordingRenderer whose output is the view, so a point of the view is a pixel.
     *
     * The derived fixture owns the part of the game (it answers the actions) and makes it after LoadPanelScene(). A table of strings loaded with LoadStrings() makes the
     * matrix helpers run once per language; with none they run once. The lookups El() and Get() never end the process: a missing name is a fatal failure of the case
     * and the answer is a blank stand-in, so a case that reads further through it is `ASSERT_NO_FATAL_FAILURE`.
     */
    class UIPanelScreenTest : public ::testing::Test {
    protected:
        explicit UIPanelScreenTest(PoolString panelNode) : _panelNode(panelNode) {}

        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIStrings().Clear();
            CoreManager::GetFontAtlasLibrary().Clear();
            _renderer = New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);
        }

        void TearDown() override {
            _hud.Reset();
            _panel.Reset();
            _manager.Reset();
            _standIns.clear();
            _standInComponents.clear();
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIStrings().Clear();  // the table is global: the next test starts with none, and with no current language
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        /// The table of the screen, through the reader of the file, so the test covers the file format as well; the first language is the default one and the current one
        static void LoadStrings(StringView xml, size_t languages) {
            UIStrings& strings = CoreManager::GetUIStrings();
            strings.Clear();
            auto config = XmlConfig::Create();
            ASSERT_TRUE(config.LoadFromString(xml));
            const Vector<String> problems = strings.LoadFromXml(config.GetRoot());
            EXPECT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0].c_str());
            ASSERT_EQ(strings.GetLanguages().size(), languages);
        }

        /// The scene @p sceneText with a view of this size; the output has the same size
        void LoadPanelScene(const String& sceneText, float viewWidth, float viewHeight) {
            _hud.Reset();
            _panel.Reset();
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(sceneText));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _renderer->width = viewWidth;
            _renderer->height = viewHeight;

            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                if (node->GetName() == "Hud"_intern) {
                    _hud = node->GetComponent<UICanvasComponent>();
                } else if (node->GetName() == _panelNode) {
                    _panel = node->GetComponent<UICanvasComponent>();
                }
            }
            ASSERT_TRUE(_hud);
            ASSERT_TRUE(_panel);
            _hud->UpdateLayout(kFirstLayoutWidth, kFirstLayoutHeight);  // the reflected fields of the component reach the layout on its first update
            _panel->UpdateLayout(kFirstLayoutWidth, kFirstLayoutHeight);
        }

        Scene& GetScene() const {
            return *_manager->GetSceneByName("Main"_intern);
        }

        void Draw() {
            _manager->SetActiveScene("Main"_intern);
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        void Show() {
            ASSERT_TRUE(UICanvasComponent::SetVisibleByName(GetScene(), _panelNode, true));
            Draw();
        }

        UILayoutContext ViewContext() const {
            UILayoutContext context;
            context.sceneViewWidth = GetScene().GetViewWidth();
            context.sceneViewHeight = GetScene().GetViewHeight();
            return context;
        }

        /// The element @p name of the panel, null when there is none
        UIElement* FindElement(const char* name) const {
            return _panel ? _panel->GetRoot().FindByName(PoolString::Intern(name)) : nullptr;
        }

        UIElement& El(const char* name) const {
            UIElement* element = FindElement(name);
            if (element == nullptr) {
                [&] { FAIL() << name << " is not in the panel"; }();
                return StandIn<UIElement>();
            }
            return *element;
        }

        template <typename T>
        T& Get(const char* name) const {
            UIElement* element = FindElement(name);
            T* typed = element ? element->Cast<T>() : nullptr;
            if (typed == nullptr) {
                [&] { FAIL() << name << (element ? " is not the expected control" : " is not in the panel"); }();
                return StandIn<T>();
            }
            return *typed;
        }

        /// The component @p T of the element @p name
        template <typename T>
        const T& ComponentOf(const char* name) const {
            const T* component = El(name).template GetComponent<T>();
            if (component == nullptr) {
                [&] { FAIL() << name << " lacks the component asked for"; }();
                return StandIn<T>();
            }
            return *component;
        }

        static Pair<float, float> CenterOf(const UIElement& element) {
            return {element.rect.x + element.rect.w * 0.5f, element.rect.y + element.rect.h * 0.5f};
        }

        /// A press and its release at (@p x, @p y), through the input as the window delivers them
        UIClick ClickAt(float x, float y) {
            const bool pressed = CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true));
            const bool released = CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false));
            return {pressed, released};
        }

        /// The same at the center of @p element as it was laid out in the last draw
        UIClick Click(const UIElement& element) {
            const auto [x, y] = CenterOf(element);
            return ClickAt(x, y);
        }

        /// Runs @p check once per language of the table (ForEachLanguage), or once with no language when the table is empty
        template <typename Check>
        static void ForEachShownLanguage(Check&& check) {
            if (CoreManager::GetUIStrings().GetLanguages().empty()) {
                check(PoolString{});
                return;
            }
            ForEachLanguage(check);
        }

        /// "[<language>] " before a label of a case, nothing with no language
        static String LanguageTag(PoolString language) {
            return language.ToStringView().empty() ? String() : Format("[{}] ", language.ToStringView());
        }

        /// The canvas on the whole matrix with the options of the game, in every language: the findings of the validator, each case named with its language
        Vector<UIMatrixFailure> ValidatePanel(UICanvasComponent& canvas) const {
            UILayoutValidatorOptions options;
            options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
            Vector<UIMatrixFailure> all;
            ForEachShownLanguage([&](PoolString language) {
                for (UIMatrixFailure& failure : ValidateOnMatrix(canvas.GetLayout(), options, ViewContext())) {
                    failure.label = Format("{}{}", LanguageTag(language).c_str(), failure.label.c_str());
                    all.push_back(std::move(failure));
                }
            });
            return all;
        }

        /// Problems() on every case of the matrix, in every language: the first lines of the first cases that have any
        String ProblemsOnMatrix(UICanvasComponent& canvas) const {
            constexpr size_t kReported = 4;
            String report;
            size_t reported = 0;
            UICanvasLayout& layout = canvas.GetLayout();
            ForEachShownLanguage([&](PoolString language) {
                for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
                    UILayoutContext context = ViewContext();
                    context.outputWidth = matrixCase.context.outputWidth;
                    context.outputHeight = matrixCase.context.outputHeight;
                    context.safeArea = matrixCase.context.safeArea;
                    layout.Update(context);
                    const String problems = Problems(layout);
                    if (!problems.empty() && reported++ < kReported) {
                        report += Format("{}{}:\n{}", LanguageTag(language).c_str(), matrixCase.label.c_str(), problems.c_str());
                    }
                }
            });
            layout.Invalidate();
            return report;
        }

        /// The canvas in the narrowest window of the matrix, alone: kNarrowestWidth px wide with the side insets of a phone held sideways
        void LayOutInTheNarrowestWindow(UICanvasLayout& layout) const {
            UILayoutContext context = ViewContext();
            context.outputWidth = kNarrowestWidth;
            context.outputHeight = kNarrowestHeight;
            context.safeArea = UIPreview::SimulatedSafeArea(SafeAreaSimulation::SideInsets, kNarrowestWidth, kNarrowestHeight);
            layout.Update(context);
        }

        /// Problems() in the narrowest window, in every language
        String ProblemsInTheNarrowestWindow(UICanvasComponent& canvas) const {
            String report;
            UICanvasLayout& layout = canvas.GetLayout();
            ForEachShownLanguage([&](PoolString language) {
                LayOutInTheNarrowestWindow(layout);
                const String problems = Problems(layout);
                if (!problems.empty()) {
                    report += Format("{}{}", LanguageTag(language).c_str(), problems.c_str());
                }
            });
            layout.Invalidate();
            return report;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _hud;
        IntrusivePtr<UICanvasComponent> _panel;

    private:
        // Any size: the layout is laid out again for the output of every draw and every case
        static constexpr float kFirstLayoutWidth = 1280.0f;
        static constexpr float kFirstLayoutHeight = 720.0f;

        /// A blank object for a lookup that failed, alive until TearDown
        template <typename T>
        T& StandIn() const {
            auto standIn = New<T>();
            T& object = *standIn;
            if constexpr (std::is_base_of_v<UIElement, T>) {
                _standIns.push_back(standIn);
            } else {
                _standInComponents.push_back(standIn);
            }
            return object;
        }

        PoolString _panelNode;
        mutable Vector<IntrusivePtrNonAtomic<UIElement>> _standIns;
        mutable Vector<IntrusivePtrNonAtomic<UIComponent>> _standInComponents;
    };

}  // namespace BECore::Tests
