#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Time/Time.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UITransitionComponent.h>
#include <Common/RecordingRenderer.h>
#include <Common/UIInputEvents.h>
#include <Common/UITestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kCanvasXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        // Gate is hidden by the portrait variant; Wide is always there
        constexpr const char* kVariantXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Wide" x="0" y="0" width="80" height="80" color="11,0,0,255"/>
          <element type="UIElement" name="Gate" x="100" y="0" width="80" height="80" color="12,0,0,255"/>
        </elements>
        <variants>
          <variant when="orientation:portrait"><overrides><set element="Gate" visible="false"/></overrides></variant>
        </variants>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr float kPortraitWidth = 600.0f;
        constexpr float kPortraitHeight = 800.0f;
        constexpr double kStep = 0.125;  // a binary fraction: the sums of the clock are exact
        constexpr float kTransitionSeconds = 1.0f;
        constexpr float kHalfOpacity = 0.5f;
        constexpr uint8_t kHoverRed = 99;
        constexpr uint8_t kTintedRed = 77;
        constexpr float kOptionHeight = 60.0f;
        constexpr float kElementSize = 80.0f;

        // Colors that tell the drawn elements apart in the RecordingRenderer
        constexpr uint8_t kPlainRed = 11;
        constexpr uint8_t kHeaderRed = 21;
        constexpr uint8_t kOptionRed = 31;
        constexpr uint8_t kOtherHeaderRed = 22;
        constexpr uint8_t kOtherOptionRed = 32;
        constexpr uint8_t kSecondRed = 12;
        constexpr uint8_t kChildRed = 13;
        constexpr uint8_t kThirdRed = 14;

        /// A popup whose list is a column of children under the header; open and closed by code, as a dropdown is
        class TestPopup : public UIElement {
        public:
            [[nodiscard]] bool HasPopupChildren() const override {
                return true;
            }
            [[nodiscard]] bool IsPopupOpen() const override {
                return open;
            }
            void LayoutPopup(const Rect&) override {
                float y = rect.y + rect.h;
                for (const auto& child : children) {
                    child->rect = Rect{rect.x, y, rect.w, kOptionHeight};
                    y += kOptionHeight;
                }
            }
            void ClosePopup() override {
                open = false;
            }
            bool open = false;
        };

        /// An element that counts how often the canvas asks whether it is a popup: a walk of the tree asks every visible element
        class AskCounter : public UIElement {
        public:
            [[nodiscard]] bool HasPopupChildren() const override {
                ++asked;
                return false;
            }
            mutable int asked = 0;
        };

        void Place(UIElement& element, float x, float y, uint8_t red) {
            element.style.rect.x = UILength::Px(x);
            element.style.rect.y = UILength::Px(y);
            element.style.rect.width = UILength::Px(kElementSize);
            element.style.rect.height = UILength::Px(kElementSize);
            element.color = Color{red, 0, 0, 255};
        }

        template <typename TElement>
        IntrusivePtr<TElement> AddTo(UIElement& parent, float x, float y, uint8_t red) {
            auto element = New<TElement>();
            Place(*element, x, y, red);
            parent.children.push_back(element);
            return element;
        }

    }  // namespace

    class UICanvasDrawListTest : public UISceneFixture {
    protected:
        void SetUp() override {
            UISceneFixture::SetUp();
            ASSERT_NO_FATAL_FAILURE(LoadScene(kCanvasXml));
            ASSERT_TRUE(_canvas);
        }

        void TearDown() override {
            CoreManager::GetTime().Reset();
            UISceneFixture::TearDown();
        }

        [[nodiscard]] uint32_t Builds() const {
            return _canvas->GetDrawListBuilds();
        }

        /// How many times the list was made by two frames in a row: a key builds at the first and the second finds the list current
        uint32_t BuildsOfTwoFrames() {
            const uint32_t before = Builds();
            Draw();
            Draw();
            return Builds() - before;
        }

        void Wait(double seconds) {
            for (double passed = 0.0; passed < seconds - 1e-9; passed += kStep) {
                CoreManager::GetTime().Advance(kStep);
                Draw();
            }
        }

        /// Draws the tree of `Tree()` and settles: one build is behind it
        void DrawSettled() {
            Draw();
            Draw();
        }

        /// Three siblings, the second with a child: A, B (B1), C
        struct Tree {
            IntrusivePtr<UIElement> a;
            IntrusivePtr<UIElement> b;
            IntrusivePtr<UIElement> b1;
            IntrusivePtr<UIElement> c;
        };

        Tree MakeTree() {
            Tree tree;
            tree.a = AddTo<UIElement>(_canvas->GetRoot(), 0.0f, 0.0f, kPlainRed);
            tree.b = AddTo<UIElement>(_canvas->GetRoot(), 100.0f, 0.0f, kSecondRed);
            tree.b1 = AddTo<UIElement>(*tree.b, 0.0f, 0.0f, kChildRed);
            tree.c = AddTo<UIElement>(_canvas->GetRoot(), 200.0f, 0.0f, kThirdRed);
            _canvas->Invalidate();
            return tree;
        }

        /// The red channels of the draw calls of the last Draw(), in order
        Vector<uint8_t> DrawnReds() const {
            Vector<uint8_t> reds;
            for (const DrawCall& call : _renderer->calls) {
                reds.push_back(call.color.r);
            }
            return reds;
        }
    };

    // ---- popups are found through the list ----------------------------------------------------------------------------------

    TEST_F(UICanvasDrawListTest, ASteadyFrameOfACanvasWithNoPopupDoesNotWalkTheTreeForOne) {
        auto probe = AddTo<AskCounter>(_canvas->GetRoot(), 0.0f, 0.0f, kPlainRed);
        _canvas->Invalidate();
        Draw();
        Draw();
        probe->asked = 0;

        Draw();
        Draw();

        EXPECT_EQ(probe->asked, 0);
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kPlainRed}));
    }

    TEST_F(UICanvasDrawListTest, APopupOpenedByCodeBetweenTwoDrawsIsDrawnInTheSameFrame) {
        auto popup = AddTo<TestPopup>(_canvas->GetRoot(), 100.0f, 100.0f, kHeaderRed);
        AddTo<UIElement>(*popup, 0.0f, 0.0f, kOptionRed);
        AddTo<UIElement>(_canvas->GetRoot(), 300.0f, 100.0f, kPlainRed);
        _canvas->Invalidate();
        Draw();
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kHeaderRed, kPlainRed}));

        popup->open = true;
        Draw();

        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kHeaderRed, kPlainRed, kOptionRed}));
        popup->open = false;
        Draw();
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kHeaderRed, kPlainRed}));
    }

    TEST_F(UICanvasDrawListTest, ThePopupOpenedLastWinsAndTheOneBeforeItIsClosed) {
        auto first = AddTo<TestPopup>(_canvas->GetRoot(), 100.0f, 100.0f, kHeaderRed);
        AddTo<UIElement>(*first, 0.0f, 0.0f, kOptionRed);
        auto second = AddTo<TestPopup>(_canvas->GetRoot(), 300.0f, 100.0f, kOtherHeaderRed);
        AddTo<UIElement>(*second, 0.0f, 0.0f, kOtherOptionRed);
        _canvas->Invalidate();
        first->open = true;
        Draw();
        ASSERT_TRUE(first->open);

        second->open = true;
        Draw();

        EXPECT_FALSE(first->open);
        EXPECT_TRUE(second->open);
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kHeaderRed, kOtherHeaderRed, kOtherOptionRed}));
    }

    TEST_F(UICanvasDrawListTest, AnOpenPopupAddedByATreeEditIsFoundByTheWalkTheSameFrame) {
        auto first = AddTo<TestPopup>(_canvas->GetRoot(), 100.0f, 100.0f, kHeaderRed);
        AddTo<UIElement>(*first, 0.0f, 0.0f, kOptionRed);
        _canvas->Invalidate();
        first->open = true;
        Draw();
        ASSERT_TRUE(first->open);

        // The list of the last draw does not know this popup: only the walk of the edited tree does
        auto added = AddTo<TestPopup>(_canvas->GetRoot(), 300.0f, 100.0f, kOtherHeaderRed);
        AddTo<UIElement>(*added, 0.0f, 0.0f, kOtherOptionRed);
        added->open = true;
        _canvas->Invalidate();
        Draw();

        EXPECT_FALSE(first->open);
        EXPECT_TRUE(added->open);
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kHeaderRed, kOtherHeaderRed, kOtherOptionRed}));
    }

    // ---- the keys of the list ------------------------------------------------------------------------------------------------

    TEST_F(UICanvasDrawListTest, TwoDrawsInARowBuildTheListOnce) {
        MakeTree();

        EXPECT_EQ(BuildsOfTwoFrames(), 1u);
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kPlainRed, kSecondRed, kChildRed, kThirdRed}));
        Draw();
        EXPECT_EQ(Builds(), 1u);
    }

    TEST_F(UICanvasDrawListTest, WhatIsReadAtDrawTimeBuildsNothingAndShowsInTheNextDraw) {
        Tree tree = MakeTree();
        tree.a->hoverColor = Color{kHoverRed, 0, 0, 255};
        tree.a->AddComponent<UIInputClicker>();
        _canvas->Invalidate();
        DrawSettled();
        const uint32_t builds = Builds();

        tree.c->opacity = kHalfOpacity;
        tree.b->color = Color{kTintedRed, 0, 0, 255};
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(kElementSize * 0.5f, kElementSize * 0.5f));
        Draw();

        const Vector<uint8_t> reds = DrawnReds();
        ASSERT_EQ(reds.size(), 4u);
        EXPECT_EQ(reds[0], kHoverRed) << "the pointer state of the owner";
        EXPECT_EQ(reds[1], kTintedRed) << "a colour";
        EXPECT_LT(_renderer->calls[3].color.a, 255) << "the opacity of the element";
        EXPECT_EQ(Builds(), builds);
    }

    TEST_F(UICanvasDrawListTest, AChildAddedOrRemovedBuildsOnceAtTheNextDrawAfterInvalidate) {
        Tree tree = MakeTree();
        DrawSettled();

        AddTo<UIElement>(*tree.a, 0.0f, 0.0f, kOtherHeaderRed);
        _canvas->Invalidate();
        EXPECT_EQ(BuildsOfTwoFrames(), 1u);
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kPlainRed, kOtherHeaderRed, kSecondRed, kChildRed, kThirdRed}));

        tree.a->children.clear();
        _canvas->Invalidate();
        EXPECT_EQ(BuildsOfTwoFrames(), 1u);
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kPlainRed, kSecondRed, kChildRed, kThirdRed}));
    }

    TEST_F(UICanvasDrawListTest, ShowingAndHidingAnElementBuildsOnceEach) {
        Tree tree = MakeTree();
        tree.b->name = "B"_intern;
        DrawSettled();

        ASSERT_TRUE(_canvas->SetElementVisible("B"_intern, false));
        EXPECT_EQ(BuildsOfTwoFrames(), 1u);
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kPlainRed, kThirdRed})) << "the subtree goes with it";

        ASSERT_TRUE(_canvas->SetElementVisible("B"_intern, true));
        EXPECT_EQ(BuildsOfTwoFrames(), 1u);
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kPlainRed, kSecondRed, kChildRed, kThirdRed}));
    }

    TEST_F(UICanvasDrawListTest, AVariantThatHidesAnElementBuildsWhenTheOutputChangesItsShapeAndASteadyOutputDoesNot) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kVariantXml));
        ASSERT_TRUE(_canvas);
        DrawSettled();
        ASSERT_EQ(DrawnReds(), (Vector<uint8_t>{kPlainRed, kSecondRed}));
        const uint32_t builds = Builds();
        DrawSettled();
        EXPECT_EQ(Builds(), builds) << "a steady output";

        _renderer->width = kPortraitWidth;
        _renderer->height = kPortraitHeight;

        EXPECT_EQ(BuildsOfTwoFrames(), 1u);
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kPlainRed}));
    }

    TEST_F(UICanvasDrawListTest, AnOpeningAndAClosingPopupBuildOnceEach) {
        auto popup = AddTo<TestPopup>(_canvas->GetRoot(), 100.0f, 100.0f, kHeaderRed);
        AddTo<UIElement>(*popup, 0.0f, 0.0f, kOptionRed);
        _canvas->Invalidate();
        DrawSettled();

        popup->open = true;
        EXPECT_EQ(BuildsOfTwoFrames(), 1u);
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kHeaderRed, kOptionRed}));

        popup->open = false;
        EXPECT_EQ(BuildsOfTwoFrames(), 1u);
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kHeaderRed}));
    }

    TEST_F(UICanvasDrawListTest, ATransitionBuildsWhenItStartsAndWhenItEndsAndTheElementStaysInTheListInBetween) {
        Tree tree = MakeTree();
        tree.b->name = "B"_intern;
        UITransitionComponent& transition = tree.b->AddComponent<UITransitionComponent>();
        transition.duration = kTransitionSeconds;
        transition.ease = EaseType::Linear;
        _canvas->Invalidate();
        DrawSettled();

        ASSERT_TRUE(_canvas->SetElementVisible("B"_intern, false));
        const uint32_t beforeStart = Builds();
        Draw();
        EXPECT_EQ(Builds(), beforeStart + 1) << "the run starts";
        Wait(0.5);
        EXPECT_EQ(Builds(), beforeStart + 1) << "no build while it runs";
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kPlainRed, kSecondRed, kChildRed, kThirdRed})) << "the leaving element is still drawn";

        Wait(1.0);
        EXPECT_EQ(Builds(), beforeStart + 2) << "the run ends";
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kPlainRed, kThirdRed}));
    }

    TEST_F(UICanvasDrawListTest, ATransitionComponentAddedToALiveElementWorksAfterInvalidate) {
        Tree tree = MakeTree();
        tree.b->name = "B"_intern;
        DrawSettled();
        UITransitionComponent& transition = tree.b->AddComponent<UITransitionComponent>();
        transition.duration = kTransitionSeconds;
        transition.ease = EaseType::Linear;
        _canvas->Invalidate();
        EXPECT_EQ(BuildsOfTwoFrames(), 1u);

        ASSERT_TRUE(_canvas->SetElementVisible("B"_intern, false));
        Draw();
        CoreManager::GetTime().Advance(kStep);
        Draw();

        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kPlainRed, kSecondRed, kChildRed, kThirdRed})) << "it fades out instead of going at once";
    }

    // ---- an edit without Invalidate() leaves a stale list, never a dangling one ----------------------------------------------

    TEST_F(UICanvasDrawListTest, AChildRemovedWithNoInvalidateIsDrawnOnceMoreAndGoesWithTheNextInvalidate) {
        Tree tree = MakeTree();
        DrawSettled();

        tree.b->children.clear();
        tree.b1.Reset();  // the list is the last owner of it

        Draw();
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kPlainRed, kSecondRed, kChildRed, kThirdRed}));

        _canvas->Invalidate();
        Draw();
        EXPECT_EQ(DrawnReds(), (Vector<uint8_t>{kPlainRed, kSecondRed, kThirdRed}));
    }

}  // namespace BECore::Tests
