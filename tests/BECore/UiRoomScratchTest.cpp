// The heap cost of the small UI and room sites that keep their working lists on the scratch arena.

#include <BECore/Memory/ScratchArena.h>
#include <BECore/Rooms/TestClient.h>
#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UIInputClicker.h>
#include <Common/AllocCheck.h>
#include <Common/Require.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kDropdownOptions = 12;
        constexpr float kOptionHeight = 40.0f;
        constexpr Rect kHeader{20.0f, 20.0f, 200.0f, 40.0f};
        constexpr size_t kPublicRooms = 50;
        /// What one list of 50 public rooms allocates through the hub and its test client (the 20 listings, the texts, the client's copy). The candidate list grew by doubling
        /// on the heap before (seven blocks: 257 allocations, 250 after)
        constexpr uint64_t kListHeapBlocks = 250;
        constexpr Rect kBounds{0.0f, 0.0f, 800.0f, 600.0f};

        IntrusivePtr<UIDropdown> MakeDropdown() {
            auto dropdown = New<UIDropdown>();
            dropdown->rect = kHeader;
            dropdown->optionHeight = kOptionHeight;
            for (size_t i = 0; i < kDropdownOptions; ++i) {
                IntrusivePtr<UIElement> option = New<UIElement>();
                option->AddComponent<UIInputClicker>();
                dropdown->children.push_back(option);
            }
            return dropdown;
        }

    }  // namespace

    TEST(UiRoomScratchTest, ThePopupLayoutOfADropdownAllocatesNothingOnceTheArenaIsWarm) {
        const IntrusivePtr<UIDropdown> dropdown = MakeDropdown();
        dropdown->LayoutPopup(kBounds);  // the first call may grow the thread's scratch arena

        ExpectNoAllocations([&] { dropdown->LayoutPopup(kBounds); });

        float top = kHeader.y + kHeader.h;
        for (const auto& option : dropdown->children) {
            EXPECT_FLOAT_EQ(option->rect.y, top);
            EXPECT_FLOAT_EQ(option->rect.h, kOptionHeight);
            EXPECT_FLOAT_EQ(option->rect.w, kHeader.w) << "one column as wide as the header";
            top += kOptionHeight;
        }
    }

    TEST(UiRoomScratchTest, ThePopupLayoutLeavesTheScratchArenaWhereItFoundIt) {
        const IntrusivePtr<UIDropdown> dropdown = MakeDropdown();
        const size_t before = ScratchArena::Get().GetBytesUsed();
        dropdown->LayoutPopup(kBounds);
        EXPECT_EQ(ScratchArena::Get().GetBytesUsed(), before);
    }

    TEST(UiRoomScratchTest, AListOfAHubWithManyPublicRoomsKeepsItsCandidatesOffTheHeap) {
        HubHarness harness;
        for (size_t i = 0; i < kPublicRooms; ++i) {
            harness.Create(harness.Connect(), "Host", "fake", Json::object(), true);
        }
        const ConnectionId warmUp = harness.Connect();
        harness.List(warmUp);  // the first list may grow the thread's scratch arena
        const ConnectionId browser = harness.Connect();

        const size_t scratchBefore = ScratchArena::Get().GetBytesUsed();
        const AllocCount counted = CountAllocations([&] { harness.List(browser); });
        EXPECT_EQ(ScratchArena::Get().GetBytesUsed(), scratchBefore);

        const ReceivedMessage* answer = harness.Client(browser).Last(RoomMessageTypes::kRooms);
        ASSERT_NE(answer, nullptr);
        EXPECT_EQ(harness.Client(browser).Last(RoomMessageTypes::kRooms)->text, harness.Client(warmUp).Last(RoomMessageTypes::kRooms)->text) << "the same answer for the same rooms";
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        EXPECT_LE(counted.count, kListHeapBlocks);
    }

}  // namespace BECore::Tests
