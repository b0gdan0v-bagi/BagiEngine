#include <BECore/PoolString/PoolString.h>
#include <BECore/UI/UIDrawList.h>
#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UITransitionComponent.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// A popup element: its children are not part of the flow, and the list says whether it is open
        class TestPopup : public UIElement {
        public:
            [[nodiscard]] bool HasPopupChildren() const override {
                return true;
            }
            [[nodiscard]] bool IsPopupOpen() const override {
                return open;
            }
            [[nodiscard]] bool OwnsState() const override {
                return true;
            }
            bool open = false;
        };

        IntrusivePtr<UIElement> Add(UIElement& parent, const char* name) {
            auto child = parent.AddChild(PoolString::Intern(name));
            return child;
        }

        IntrusivePtr<TestPopup> AddPopup(UIElement& parent, const char* name) {
            auto popup = New<TestPopup>();
            popup->name = PoolString::Intern(name);
            parent.children.push_back(popup);
            return popup;
        }

        void MakeClicker(UIElement& element) {
            element.AddComponent<UIInputClicker>();
        }

        /// The names of the entries in list order
        Vector<String> Names(const UIDrawList& list) {
            Vector<String> names;
            for (const auto& entry : list.Entries()) {
                names.push_back(String(entry.element->name.ToStringView()));
            }
            return names;
        }

        int32_t IndexOf(const UIDrawList& list, const char* name) {
            for (size_t index = 0; index < list.Entries().size(); ++index) {
                if (list.Entries()[index].element->name.ToStringView() == name) {
                    return static_cast<int32_t>(index);
                }
            }
            return -1;
        }

        const UIDrawList::Entry& EntryOf(const UIDrawList& list, const char* name) {
            const int32_t index = IndexOf(list, name);
            EXPECT_GE(index, 0) << name;
            return list.Entries()[static_cast<size_t>(index < 0 ? 0 : index)];
        }

    }  // namespace

    class UIDrawListTest : public ::testing::Test {
    protected:
        IntrusivePtr<UIElement> _root = New<UIElement>();
        UIDrawList _list;
    };

    TEST_F(UIDrawListTest, AnEmptyTreeIsTheRootAlone) {
        _list.Rebuild(*_root);

        ASSERT_EQ(_list.Entries().size(), 1u);
        EXPECT_EQ(_list.Entries()[0].element.Get(), _root.Get());
        EXPECT_EQ(_list.Entries()[0].subtreeEnd, 1u);
        EXPECT_EQ(_list.MainEnd(), 1u);
        EXPECT_EQ(_list.MaxDepth(), 1u);
        EXPECT_TRUE(_list.Sections().empty());
        EXPECT_EQ(_list.OwnerCount(), 0u);
    }

    TEST_F(UIDrawListTest, ElementsComeInPreOrderWithTheEndOfEachSubtree) {
        auto a = Add(*_root, "a");
        Add(*a, "a1");
        auto a2 = Add(*a, "a2");
        Add(*a2, "a2x");
        Add(*_root, "b");
        _list.Rebuild(*_root);

        EXPECT_EQ(Names(_list), (Vector<String>{"", "a", "a1", "a2", "a2x", "b"}));
        EXPECT_EQ(EntryOf(_list, "a").subtreeEnd, 5u);
        EXPECT_EQ(EntryOf(_list, "a1").subtreeEnd, 3u);
        EXPECT_EQ(EntryOf(_list, "a2").subtreeEnd, 5u);
        EXPECT_EQ(EntryOf(_list, "b").subtreeEnd, 6u);
        EXPECT_EQ(_list.Entries()[0].subtreeEnd, 6u);
        EXPECT_EQ(_list.MaxDepth(), 4u);
    }

    TEST_F(UIDrawListTest, AHiddenElementAndItsSubtreeHaveNoEntries) {
        auto hidden = Add(*_root, "hidden");
        hidden->active.visible = false;
        Add(*hidden, "inner");
        auto shown = Add(*_root, "shown");
        auto hiddenChild = Add(*shown, "hiddenChild");
        hiddenChild->active.visible = false;
        Add(*shown, "shownChild");
        _list.Rebuild(*_root);

        EXPECT_EQ(Names(_list), (Vector<String>{"", "shown", "shownChild"}));
        EXPECT_EQ(EntryOf(_list, "shown").subtreeEnd, 3u);
    }

    TEST_F(UIDrawListTest, ATransitionComponentIsCachedAndItsAbsenceIsNull) {
        auto fading = Add(*_root, "fading");
        auto& transition = fading->AddComponent<UITransitionComponent>();
        Add(*_root, "plain");
        _list.Rebuild(*_root);

        EXPECT_EQ(EntryOf(_list, "fading").transition.Get(), &transition);
        EXPECT_EQ(EntryOf(_list, "plain").transition.Get(), nullptr);
    }

    TEST_F(UIDrawListTest, ADescendantIsDrawnInTheStateOfItsNearestOwner) {
        auto outer = Add(*_root, "outer");
        MakeClicker(*outer);
        auto caption = Add(*outer, "caption");
        auto inner = Add(*outer, "inner");
        MakeClicker(*inner);
        Add(*inner, "innerCaption");
        Add(*_root, "free");
        _list.Rebuild(*_root);

        const auto& outerEntry = EntryOf(_list, "outer");
        const auto& innerEntry = EntryOf(_list, "inner");
        EXPECT_EQ(_list.OwnerCount(), 2u);
        EXPECT_EQ(outerEntry.ownSlot, 0);
        EXPECT_EQ(outerEntry.inheritSlot, -1);
        EXPECT_EQ(EntryOf(_list, "caption").ownSlot, -1);
        EXPECT_EQ(EntryOf(_list, "caption").inheritSlot, outerEntry.ownSlot);
        EXPECT_EQ(innerEntry.ownSlot, 1);
        EXPECT_EQ(innerEntry.inheritSlot, outerEntry.ownSlot) << "an owner is itself drawn in the state it was handed, which it ignores";
        EXPECT_EQ(EntryOf(_list, "innerCaption").inheritSlot, innerEntry.ownSlot);
        EXPECT_EQ(EntryOf(_list, "free").inheritSlot, -1);
        EXPECT_EQ(caption->GetComponent<UIInputClicker>(), nullptr);
    }

    TEST_F(UIDrawListTest, TheChildrenOfAPopupAreASectionAfterTheTreeAndStartTheStateAgain) {
        auto owner = Add(*_root, "owner");
        MakeClicker(*owner);
        auto popup = AddPopup(*owner, "popup");
        auto option = Add(*popup, "option");
        Add(*option, "optionText");
        Add(*owner, "after");
        Add(*_root, "later");
        _list.Rebuild(*_root);

        EXPECT_EQ(Names(_list), (Vector<String>{"", "owner", "popup", "after", "later", "option", "optionText"}));
        EXPECT_EQ(_list.MainEnd(), 5u);
        EXPECT_EQ(EntryOf(_list, "popup").subtreeEnd, 3u) << "a popup element is one entry of the main section";
        ASSERT_EQ(_list.Sections().size(), 1u);
        const UIDrawList::PopupSection& section = _list.Sections()[0];
        EXPECT_EQ(section.owner, 2u);
        EXPECT_EQ(section.begin, 5u);
        EXPECT_EQ(section.end, 7u);
        EXPECT_EQ(EntryOf(_list, "option").inheritSlot, -1);
        EXPECT_EQ(EntryOf(_list, "optionText").inheritSlot, -1);
        EXPECT_EQ(EntryOf(_list, "popup").inheritSlot, EntryOf(_list, "owner").ownSlot);
    }

    TEST_F(UIDrawListTest, ADropdownOwnsItsStateAndItsOptionsAreASection) {
        auto dropdown = New<UIDropdown>();
        dropdown->name = "dropdown"_intern;
        _root->children.push_back(dropdown);
        Add(*dropdown, "optionA");
        Add(*dropdown, "optionB");
        _list.Rebuild(*_root);

        EXPECT_EQ(EntryOf(_list, "dropdown").ownSlot, 0);
        ASSERT_EQ(_list.Sections().size(), 1u);
        EXPECT_EQ(_list.Sections()[0].end - _list.Sections()[0].begin, 2u);
        EXPECT_EQ(EntryOf(_list, "optionA").inheritSlot, -1);
    }

    TEST_F(UIDrawListTest, PopupSectionsFollowTheTreeOrderWhetherOpenOrNot) {
        auto first = AddPopup(*_root, "first");
        Add(*first, "firstOption");
        auto group = Add(*_root, "group");
        auto second = AddPopup(*group, "second");
        second->open = true;
        Add(*second, "secondOption");
        _list.Rebuild(*_root);

        ASSERT_EQ(_list.Sections().size(), 2u);
        EXPECT_EQ(_list.Entries()[_list.Sections()[0].owner].element.Get(), first.Get());
        EXPECT_EQ(_list.Entries()[_list.Sections()[1].owner].element.Get(), second.Get());
        EXPECT_EQ(_list.Sections()[0].end, _list.Sections()[1].begin);
        EXPECT_EQ(Names(_list).back(), "secondOption");
    }

    TEST_F(UIDrawListTest, APopupInsideAPopupIsDrawnButHasNoSectionOfItsOwn) {
        auto outer = AddPopup(*_root, "outer");
        auto inner = AddPopup(*outer, "inner");
        Add(*inner, "innerOption");
        _list.Rebuild(*_root);

        EXPECT_EQ(Names(_list), (Vector<String>{"", "outer", "inner"}));
        EXPECT_EQ(_list.Sections().size(), 1u);
    }

    TEST_F(UIDrawListTest, AHiddenPopupHasNoSectionAndHiddenOptionsHaveNoEntry) {
        auto hiddenPopup = AddPopup(*_root, "hiddenPopup");
        hiddenPopup->active.visible = false;
        Add(*hiddenPopup, "lost");
        auto popup = AddPopup(*_root, "popup");
        Add(*popup, "shown");
        auto hiddenOption = Add(*popup, "hiddenOption");
        hiddenOption->active.visible = false;
        _list.Rebuild(*_root);

        ASSERT_EQ(_list.Sections().size(), 1u);
        EXPECT_EQ(Names(_list), (Vector<String>{"", "popup", "shown"}));
        EXPECT_EQ(_list.Sections()[0].end - _list.Sections()[0].begin, 1u);
    }

    TEST_F(UIDrawListTest, ASecondRebuildAfterAnEditGivesTheNewList) {
        auto a = Add(*_root, "a");
        _list.Rebuild(*_root);
        ASSERT_EQ(_list.Entries().size(), 2u);

        Add(*a, "a1");
        Add(*_root, "b");
        a->active.visible = false;
        _list.Rebuild(*_root);

        EXPECT_EQ(Names(_list), (Vector<String>{"", "b"}));
        EXPECT_EQ(_list.MaxDepth(), 2u);
    }

    TEST_F(UIDrawListTest, ARebuildClearsTheInheritedStateOfWhatNothingHandsDownTo) {
        auto owner = Add(*_root, "owner");
        MakeClicker(*owner);
        auto child = Add(*owner, "child");
        _list.Rebuild(*_root);
        const UIStateLook::State hovered{true, false, false, false, false};
        child->SetInheritedState(&hovered, 0.0f);
        ASSERT_TRUE(child->GetVisualState().hovered);

        // The child leaves its owner and the list is made again: it is drawn in no one's state
        _root->children.push_back(child);
        owner->children.clear();
        _list.Rebuild(*_root);

        EXPECT_FALSE(child->GetVisualState().hovered);
    }

    TEST_F(UIDrawListTest, ARebuildOfAnUnchangedTreeAllocatesNothing) {
        for (int group = 0; group < 4; ++group) {
            auto container = Add(*_root, "group");
            MakeClicker(*container);
            for (int item = 0; item < 3; ++item) {
                auto leaf = Add(*container, "leaf");
                leaf->AddComponent<UITransitionComponent>();
            }
            AddPopup(*container, "popup")->AddChild("option"_intern);
        }
        _list.Rebuild(*_root);

        ExpectNoAllocations([&] {
            for (int round = 0; round < 8; ++round) {
                _list.Rebuild(*_root);
            }
        });
    }

}  // namespace BECore::Tests
