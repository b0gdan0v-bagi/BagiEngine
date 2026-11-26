#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Widgets/Feedback/EditorProgress.h>

namespace BECore::Tests {

    namespace {

        constexpr int kSteadyFrames = 10;

        /// A source of a fixed label that runs while the test says so, and counts how often it is asked
        struct FakeOperation {
            StringView label;
            bool running = false;
            int asked = 0;
            EditorProgressHandle handle;

            explicit FakeOperation(StringView name) : label(name) {
                handle = EditorProgressRegistry::GetInstance().Register([this]() -> Optional<EditorProgress> {
                    ++asked;
                    return running ? Optional<EditorProgress>(EditorProgress{label, {}}) : Optional<EditorProgress>();
                });
            }
        };

    }  // namespace

    TEST(EditorProgressTest, TheFirstRegisteredOfTheRunningOnesIsShownAndTheRestAreCounted) {
        FakeOperation first("first");
        FakeOperation second("second");
        FakeOperation third("third");
        EXPECT_FALSE(EditorProgressRegistry::GetInstance().Collect().first);

        third.running = true;
        second.running = true;
        const EditorProgressSummary two = EditorProgressRegistry::GetInstance().Collect();

        ASSERT_TRUE(two.first);
        EXPECT_EQ(two.first->label, "second");
        EXPECT_EQ(two.others, 1u);
    }

    TEST(EditorProgressTest, EverySourceIsAskedEachTimeAlsoBehindARunningOne) {
        FakeOperation first("first");
        FakeOperation second("second");
        first.running = true;

        (void)EditorProgressRegistry::GetInstance().Collect();
        (void)EditorProgressRegistry::GetInstance().Collect();

        EXPECT_EQ(second.asked, 2) << "a source takes the end of its operation when it is asked";
    }

    TEST(EditorProgressTest, ASourceLeavesWithItsHandleAndAMovedHandleKeepsIt) {
        FakeOperation operation("work");
        operation.running = true;

        EditorProgressHandle moved = std::move(operation.handle);
        operation.handle.Reset();
        EXPECT_TRUE(EditorProgressRegistry::GetInstance().Collect().first);

        moved = EditorProgressHandle();
        EXPECT_FALSE(EditorProgressRegistry::GetInstance().Collect().first);
    }

    TEST(EditorProgressTest, AskingAllocatesNothing) {
        FakeOperation first("first");
        FakeOperation second("second");
        first.running = true;
        second.running = true;
        (void)EditorProgressRegistry::GetInstance().Collect();

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                (void)EditorProgressRegistry::GetInstance().Collect();
            }
        });
    }

}  // namespace BECore::Tests
