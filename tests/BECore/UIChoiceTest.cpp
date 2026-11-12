#include <BECore/UI/UIChoice.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UITextComponent.h>
#include <Common/EngineEnvironment.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        IntrusivePtr<UIElement> MakeClickerElement(const char* value, const char* text = nullptr) {
            auto element = New<UIElement>();
            auto& clicker = element->AddComponent<UIInputClicker>();
            clicker.value = PoolString::Intern(value);
            if (text) {
                element->AddComponent<UITextComponent>().text = text;
            }
            return element;
        }

        IntrusivePtr<UIElement> MakeCaptionChild(const char* text) {
            auto child = New<UIElement>();
            child->AddComponent<UITextComponent>().text = text;
            return child;
        }

    }  // namespace

    class UIChoiceTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }
    };

    TEST_F(UIChoiceTest, AnElementWithAClickerIsAnOptionWithTheValueOfTheClicker) {
        const auto element = MakeClickerElement("hard", "Hard");

        EXPECT_TRUE(UIChoice::IsOption(*element));
        EXPECT_EQ(UIChoice::GetValue(*element), PoolString::Intern("hard"));
        EXPECT_EQ(UIChoice::GetText(*element), "Hard");
        EXPECT_EQ(&UIChoice::GetText(*element), &element->GetComponent<UITextComponent>()->text) << "a view of the string, not a copy";
    }

    TEST_F(UIChoiceTest, APlainElementIsNoOption) {
        auto plain = New<UIElement>();
        plain->AddComponent<UITextComponent>().text = "Caption";

        EXPECT_FALSE(UIChoice::IsOption(*plain));
        EXPECT_TRUE(UIChoice::GetValue(*plain).Empty());
    }

    TEST_F(UIChoiceTest, TheCaptionIsTheTextOfTheFirstDescendantThatHasOne) {
        const auto element = MakeClickerElement("a");
        auto icon = New<UIElement>();
        auto wrap = New<UIElement>();
        wrap->children.push_back(MakeCaptionChild("Deep"));
        element->children.push_back(icon);
        element->children.push_back(wrap);
        element->children.push_back(MakeCaptionChild("Second"));

        EXPECT_EQ(UIChoice::GetText(*element), "Deep") << "depth first, in order";
    }

    TEST_F(UIChoiceTest, TheTextOfTheElementItselfWinsOverItsChildren) {
        const auto element = MakeClickerElement("a", "Own");
        element->children.push_back(MakeCaptionChild("Child"));

        EXPECT_EQ(UIChoice::GetText(*element), "Own");
    }

    TEST_F(UIChoiceTest, ANestedClickerIsAnotherButtonAndItsTextIsNotTaken) {
        const auto outer = MakeClickerElement("outer");
        auto inner = MakeClickerElement("inner", "Inner");
        inner->children.push_back(MakeCaptionChild("InnerChild"));
        outer->children.push_back(inner);

        EXPECT_TRUE(UIChoice::GetText(*outer).empty());

        outer->children.push_back(MakeCaptionChild("Mine"));
        EXPECT_EQ(UIChoice::GetText(*outer), "Mine") << "the sibling after the nested button is still the caption";
    }

    TEST_F(UIChoiceTest, AnOptionWithoutAnyTextHasAnEmptyCaption) {
        const auto element = MakeClickerElement("a");
        element->children.push_back(New<UIElement>());

        EXPECT_TRUE(UIChoice::GetText(*element).empty());
    }

    TEST_F(UIChoiceTest, DisabledIsTheAnswerOfTheElement) {
        const auto element = MakeClickerElement("a");
        EXPECT_FALSE(element->IsDisabled());
        element->GetComponent<UIInputClicker>()->disabled = true;
        EXPECT_TRUE(element->IsDisabled());
    }

}  // namespace BECore::Tests
