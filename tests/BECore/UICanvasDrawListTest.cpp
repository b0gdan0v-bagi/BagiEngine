#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIElement.h>
#include <Common/RecordingRenderer.h>
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

        constexpr float kOptionHeight = 60.0f;
        constexpr float kElementSize = 80.0f;

        // Colors that tell the drawn elements apart in the RecordingRenderer
        constexpr uint8_t kPlainRed = 11;
        constexpr uint8_t kHeaderRed = 21;
        constexpr uint8_t kOptionRed = 31;
        constexpr uint8_t kOtherHeaderRed = 22;
        constexpr uint8_t kOtherOptionRed = 32;

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

}  // namespace BECore::Tests
