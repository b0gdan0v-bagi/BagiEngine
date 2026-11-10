#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kPopupScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr float kOptionWidth = 200.0f;
        constexpr float kOptionHeight = 60.0f;

        // Colors that tell the drawn elements apart in the RecordingRenderer
        constexpr uint8_t kHeaderRed = 10;
        constexpr uint8_t kCoverRed = 20;
        constexpr uint8_t kFirstOptionRed = 30;
        constexpr uint8_t kSecondOptionRed = 40;

        SDL_MouseButtonEvent Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

        /// A test popup: a header that toggles its list on a click; the list is a column of children under the header, as wide as it, `optionHeight` high each
        class TestPopup : public UIElement {
        public:
            [[nodiscard]] bool HasPopupChildren() const override {
                return true;
            }
            [[nodiscard]] bool IsPopupOpen() const override {
                return open;
            }
            void LayoutPopup(const Rect& contentBounds) override {
                lastBounds = contentBounds;
                float y = rect.y + rect.h;
                for (const auto& child : children) {
                    child->rect = Rect{rect.x, y, rect.w, optionHeight};
                    y += optionHeight;
                }
            }
            void ClosePopup() override {
                open = false;
                ++closes;
            }
            void OnClick() override {
                ++clicks;
                open = !open;
            }
            void OnPopupChoice(UIElement& child) override {
                chosen = child.name;
                ++choices;
                open = false;
            }

            bool open = false;
            float optionHeight = kOptionHeight;
            Rect lastBounds;
            PoolString chosen;
            int clicks = 0;
            int choices = 0;
            int closes = 0;
        };

        /// An element that counts the clicks it gets
        class ClickCounter : public UIElement {
        public:
            void OnClick() override {
                ++clicks;
            }
            int clicks = 0;
        };

        void Place(UIElement& element, float x, float y, float width, float height) {
            element.style.rect.x = x;
            element.style.rect.y = y;
            element.style.rect.width = UILength::Px(width);
            element.style.rect.height = UILength::Px(height);
        }

        template <typename TElement>
        IntrusivePtr<TElement> AddTo(UIElement& parent, const char* name, float x, float y, float width, float height, uint8_t red) {
            auto element = New<TElement>();
            element->name = PoolString::Intern(name);
            Place(*element, x, y, width, height);
            element->color = Color{red, 0, 0, 255};
            parent.children.push_back(element);
            return element;
        }

        size_t CountOf(const eastl::vector<UILayoutIssue>& issues, UILayoutIssueKind kind) {
            return static_cast<size_t>(std::count_if(issues.begin(), issues.end(), [kind](const UILayoutIssue& issue) { return issue.kind == kind; }));
        }

    }  // namespace

    class UIPopupTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        // Screen of 800 x 600 units at scale 1: Pop is the header at (100, 100); its list would be at (100, 160) and (100, 220). Cover is a later sibling over the list,
        // Below a clickable element on the right.
        void SetUp() override {
            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kPopupScenesXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _manager->SetActiveScene("Main"_intern);

            _canvas = _manager->GetSceneByName("Main"_intern)->GetRootNode().GetChildren()[0]->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);
            UIElement& root = _canvas->GetRoot();
            _pop = AddTo<TestPopup>(root, "Pop", 100.0f, 100.0f, kOptionWidth, kOptionHeight, kHeaderRed);
            _pop->interactive = true;
            _first = AddTo<ClickCounter>(*_pop, "First", 0.0f, 0.0f, 10.0f, 10.0f, kFirstOptionRed);
            _first->interactive = true;
            _second = AddTo<ClickCounter>(*_pop, "Second", 0.0f, 0.0f, 10.0f, 10.0f, kSecondOptionRed);
            _second->interactive = true;
            _cover = AddTo<UIElement>(root, "Cover", 100.0f, 160.0f, 300.0f, 300.0f, kCoverRed);
            _cover->blocksInput = true;
            _below = AddTo<ClickCounter>(root, "Below", 500.0f, 100.0f, 100.0f, 100.0f, 0);
            _below->interactive = true;
            _canvas->Invalidate();
            _canvas->UpdateLayout(800.0f, 600.0f);
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        void Draw() {
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        /// The red channels of the draw calls, in order
        eastl::vector<uint8_t> DrawnReds() const {
            eastl::vector<uint8_t> reds;
            for (const DrawCall& call : _renderer->calls) {
                reds.push_back(call.color.r);
            }
            return reds;
        }

        bool Press(float x, float y) {
            return CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true));
        }
        bool Release(float x, float y) {
            return CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false));
        }
        /// A press and a release at a point; false when either was not consumed
        bool Click(float x, float y) {
            const bool down = Press(x, y);
            const bool up = Release(x, y);
            return down && up;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
        IntrusivePtr<TestPopup> _pop;
        IntrusivePtr<ClickCounter> _first;
        IntrusivePtr<ClickCounter> _second;
        IntrusivePtr<UIElement> _cover;
        IntrusivePtr<ClickCounter> _below;
    };

    // ---- closed ----------------------------------------------------------------------------------------------------------

    TEST_F(UIPopupTest, TheChildrenOfAClosedPopupHaveNoRectAndAreNeitherHitNorDrawn) {
        EXPECT_TRUE(_first->rect.IsEmpty());
        EXPECT_TRUE(_second->rect.IsEmpty());
        EXPECT_EQ(_canvas->GetLayout().HitTest(150.0f, 180.0f), _cover.Get());  // where the first option would be
        EXPECT_EQ(_canvas->GetLayout().HitTest(150.0f, 120.0f), _pop.Get());
        EXPECT_TRUE(_canvas->GetLayout().OpenPopups().empty());

        Draw();

        EXPECT_EQ(DrawnReds(), (eastl::vector<uint8_t>{kHeaderRed, kCoverRed, 0}));
    }

    // ---- open ------------------------------------------------------------------------------------------------------------

    TEST_F(UIPopupTest, AnOpenPopupIsPlacedAgainstTheContentBoundsWithoutAnInvalidateByHand) {
        _pop->open = true;
        Draw();  // no Invalidate() by hand: the canvas notices the opened popup

        EXPECT_FLOAT_EQ(_first->rect.y, 160.0f);
        EXPECT_FLOAT_EQ(_second->rect.y, 220.0f);
        EXPECT_FLOAT_EQ(_pop->lastBounds.w, 800.0f);
        EXPECT_FLOAT_EQ(_pop->lastBounds.h, 600.0f);
    }

    TEST_F(UIPopupTest, TheChildrenOfAnOpenPopupAreDrawnAfterALaterSibling) {
        _pop->open = true;
        Draw();

        // Header, then the Cover and Below that come later in the tree, and only then the list
        EXPECT_EQ(DrawnReds(), (eastl::vector<uint8_t>{kHeaderRed, kCoverRed, 0, kFirstOptionRed, kSecondOptionRed}));
    }

    TEST_F(UIPopupTest, TheChildrenOfAnOpenPopupAreHitBeforeALaterSibling) {
        _pop->open = true;
        Draw();

        EXPECT_EQ(_canvas->GetLayout().HitTest(150.0f, 180.0f), _first.Get());
        EXPECT_EQ(_canvas->GetLayout().HitTest(150.0f, 240.0f), _second.Get());
        EXPECT_EQ(_canvas->GetLayout().HitTest(150.0f, 300.0f), _cover.Get());  // below the list
        ASSERT_EQ(_canvas->GetLayout().OpenPopups().size(), 1u);
    }

    TEST_F(UIPopupTest, ACloseHidesTheListAgain) {
        _pop->open = true;
        Draw();
        _pop->open = false;
        Draw();

        EXPECT_TRUE(_first->rect.IsEmpty());
        EXPECT_EQ(_canvas->GetLayout().HitTest(150.0f, 180.0f), _cover.Get());
    }

    // ---- input -----------------------------------------------------------------------------------------------------------

    TEST_F(UIPopupTest, AClickOnTheHeaderOpensTheListAndAnotherClosesIt) {
        EXPECT_TRUE(Click(150.0f, 120.0f));
        EXPECT_TRUE(_pop->open);
        _canvas->UpdateLayout(800.0f, 600.0f);
        EXPECT_FALSE(_first->rect.IsEmpty()) << "the canvas invalidated the layout after the open";

        EXPECT_TRUE(Click(150.0f, 120.0f));
        EXPECT_FALSE(_pop->open);
        EXPECT_EQ(_pop->clicks, 2);
        EXPECT_EQ(_pop->closes, 0) << "a click on the open element is its own: it is not an outside press";
    }

    TEST_F(UIPopupTest, APressOutsideClosesThePopupAndIsConsumedBeforeAnythingBelowSeesIt) {
        _pop->open = true;
        Draw();

        EXPECT_TRUE(Press(550.0f, 150.0f));  // on Below
        EXPECT_FALSE(_pop->open);
        EXPECT_EQ(_pop->closes, 1);
        EXPECT_FALSE(_below->pressed);
        EXPECT_TRUE(Release(550.0f, 150.0f)) << "the release belongs to the press that was taken";
        EXPECT_EQ(_below->clicks, 0);

        _canvas->UpdateLayout(800.0f, 600.0f);
        EXPECT_TRUE(_first->rect.IsEmpty());
        EXPECT_TRUE(Click(550.0f, 150.0f));  // the next one is an ordinary click
        EXPECT_EQ(_below->clicks, 1);
    }

    TEST_F(UIPopupTest, APressOnNothingAtAllClosesThePopupToo) {
        _pop->open = true;
        Draw();

        EXPECT_TRUE(Press(700.0f, 550.0f));  // empty canvas
        EXPECT_FALSE(_pop->open);
        EXPECT_TRUE(Release(700.0f, 550.0f));
    }

    TEST_F(UIPopupTest, AClickOnAChildCallsOnPopupChoiceInsteadOfItsOnClick) {
        _pop->open = true;
        Draw();

        EXPECT_TRUE(Click(150.0f, 240.0f));  // the second option, above the Cover

        EXPECT_EQ(_pop->choices, 1);
        EXPECT_EQ(_pop->chosen, "Second"_intern);
        EXPECT_EQ(_second->clicks, 0);
        EXPECT_FALSE(_pop->open);
        _canvas->UpdateLayout(800.0f, 600.0f);
        EXPECT_TRUE(_second->rect.IsEmpty()) << "the layout follows the close";
    }

    TEST_F(UIPopupTest, APressOnAChildThatIsLetGoElsewhereChoosesNothing) {
        _pop->open = true;
        Draw();

        EXPECT_TRUE(Press(150.0f, 180.0f));
        Release(700.0f, 550.0f);

        EXPECT_EQ(_pop->choices, 0);
        EXPECT_TRUE(_pop->open);
    }

    TEST_F(UIPopupTest, AnOtherButtonDoesNotClosePopups) {
        _pop->open = true;
        Draw();
        SDL_MouseButtonEvent event = Button(550.0f, 150.0f, true);
        event.button = SDL_BUTTON_RIGHT;

        CoreManager::GetUIInput().DispatchMouseButtonDown(event);

        EXPECT_TRUE(_pop->open);
    }

    // ---- the canvas cannot take input / a second popup ---------------------------------------------------------------------

    TEST_F(UIPopupTest, AHiddenCanvasClosesItsPopup) {
        _pop->open = true;
        Draw();

        _canvas->_visible = false;
        Draw();

        EXPECT_FALSE(_pop->open);
        _canvas->_visible = true;
        Draw();
        EXPECT_TRUE(_first->rect.IsEmpty());
    }

    TEST_F(UIPopupTest, ACanvasWithInputOffClosesItsPopupOnTheNextEvent) {
        _pop->open = true;
        Draw();

        _canvas->_inputEnabled = false;
        EXPECT_FALSE(Press(550.0f, 150.0f));

        EXPECT_FALSE(_pop->open);
    }

    TEST_F(UIPopupTest, OpeningASecondPopupClosesTheFirst) {
        auto other = AddTo<TestPopup>(_canvas->GetRoot(), "Other", 300.0f, 20.0f, kOptionWidth, kOptionHeight, kHeaderRed);
        other->interactive = true;
        auto child = AddTo<ClickCounter>(*other, "Entry", 0.0f, 0.0f, 10.0f, 10.0f, kFirstOptionRed);
        child->interactive = true;
        _canvas->Invalidate();

        _pop->open = true;
        Draw();
        ASSERT_TRUE(_pop->open);

        other->open = true;
        Draw();

        EXPECT_FALSE(_pop->open);
        EXPECT_TRUE(other->open);
        EXPECT_EQ(_canvas->GetLayout().OpenPopups().size(), 1u);
        EXPECT_TRUE(_first->rect.IsEmpty());
        EXPECT_FALSE(child->rect.IsEmpty());
    }

    // ---- the validator ----------------------------------------------------------------------------------------------------

    TEST_F(UIPopupTest, TheValidatorSkipsTheChildrenOfAClosedPopupAndChecksAnOpenOne) {
        // The options are interactive and their rects empty while closed: not a touch-target defect
        EXPECT_EQ(CountOf(_canvas->GetLayout().Validate({}), UILayoutIssueKind::TouchTargetTooSmall), 0u);

        _pop->optionHeight = 20.0f;  // smaller than a finger
        _pop->open = true;
        Draw();

        const auto issues = _canvas->GetLayout().Validate({});
        EXPECT_EQ(CountOf(issues, UILayoutIssueKind::TouchTargetTooSmall), 2u);
        EXPECT_EQ(CountOf(issues, UILayoutIssueKind::SiblingsOverlap), 0u);
    }

    TEST_F(UIPopupTest, AnOpenListThatLeavesTheCanvasIsReported) {
        _pop->style.rect.y = 500.0f;
        _canvas->Invalidate();
        _pop->open = true;
        Draw();

        EXPECT_GE(CountOf(_canvas->GetLayout().Validate({}), UILayoutIssueKind::OutsideCanvas), 1u);
    }

    TEST_F(UIPopupTest, CompileTimeChecks) {
        static_assert(!std::is_abstract_v<TestPopup>);
    }

}  // namespace BECore::Tests
