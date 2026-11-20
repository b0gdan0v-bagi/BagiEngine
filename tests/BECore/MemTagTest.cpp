#include <BECore/Memory/MemTag.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UILayoutHud.h>
#include <gtest/gtest.h>
#include <thread>
#include <type_traits>

namespace BECore::Tests {

#if BE_PROFILER_ALLOC

    TEST(MemTagTest, ScopesNestAndRestore) {
        ASSERT_EQ(AllocTracker::GetCurrentTag(), MemTag::Untagged);
        {
            BE_MEM_TAG(Scene);
            EXPECT_EQ(AllocTracker::GetCurrentTag(), MemTag::Scene);
            {
                BE_MEM_TAG(UI);
                EXPECT_EQ(AllocTracker::GetCurrentTag(), MemTag::UI);
            }
            EXPECT_EQ(AllocTracker::GetCurrentTag(), MemTag::Scene);
        }
        EXPECT_EQ(AllocTracker::GetCurrentTag(), MemTag::Untagged);
    }

    TEST(MemTagTest, SequentialScopesDoNotLeakIntoEachOther) {
        {
            BE_MEM_TAG(Text);
        }
        EXPECT_EQ(AllocTracker::GetCurrentTag(), MemTag::Untagged);
        {
            BE_MEM_TAG(Resource);
            EXPECT_EQ(AllocTracker::GetCurrentTag(), MemTag::Resource);
        }
        EXPECT_EQ(AllocTracker::GetCurrentTag(), MemTag::Untagged);
    }

    TEST(MemTagTest, TheTagBelongsToTheThread) {
        BE_MEM_TAG(Game);
        MemTag seenByOther = MemTag::Game;
        std::thread other([&seenByOther] {
            seenByOther = AllocTracker::GetCurrentTag();
            BE_MEM_TAG(Audio);
        });
        other.join();
        EXPECT_EQ(seenByOther, MemTag::Untagged);
        EXPECT_EQ(AllocTracker::GetCurrentTag(), MemTag::Game);
    }

    TEST(MemTagTest, SwapTagReturnsThePreviousOne) {
        const MemTag before = AllocTracker::SwapTag(MemTag::Network);
        EXPECT_EQ(before, MemTag::Untagged);
        EXPECT_EQ(AllocTracker::SwapTag(before), MemTag::Network);
    }

    // The entry point sets its tag itself: the layout allocates under no scope of the test and still counts in `UI`
    TEST(MemTagTest, ALayoutIsCountedUnderTheUITag) {
        UICanvasLayout canvas;
        BuildHud(canvas);
        const AllocTagTotals uiBefore = AllocTracker::GetTagTotals(MemTag::UI);

        UILayoutContext context;
        context.outputWidth = 1280.0f;
        context.outputHeight = 720.0f;
        ASSERT_TRUE(canvas.Update(context));

        const AllocTagTotals uiAfter = AllocTracker::GetTagTotals(MemTag::UI);
        EXPECT_GT(uiAfter.count, uiBefore.count);
        EXPECT_EQ(AllocTracker::GetCurrentTag(), MemTag::Untagged);
    }

#else

    TEST(MemTagTest, ScopeDoesNothingWithoutAllocationTracking) {
        BE_MEM_TAG(UI);
        EXPECT_EQ(AllocTracker::GetCurrentTag(), MemTag::Untagged);
        EXPECT_TRUE(std::is_empty_v<MemTagScope>);
    }

#endif

}  // namespace BECore::Tests
