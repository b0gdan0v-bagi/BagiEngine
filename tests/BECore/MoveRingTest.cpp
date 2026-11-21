#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <Common/AllocCheck.h>
#include <TaskSystem/MoveRing.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using IntRing = Detail::MoveRing<int>;
        using Job = MoveOnlyFunction<void()>;

        constexpr int kBeyondInitialCapacity = 50;

        /// Counts its destructions while it owns the count: a moved-from tracker counts nothing
        struct Tracker {
            explicit Tracker(int* destroyed) : destroyed(destroyed) {}
            Tracker(Tracker&& other) noexcept : destroyed(std::exchange(other.destroyed, nullptr)) {}
            Tracker& operator=(Tracker&&) = delete;
            ~Tracker() {
                if (destroyed != nullptr) {
                    ++*destroyed;
                }
            }

            int* destroyed;
        };

        Job TrackedJob(int* destroyed) {
            return [tracker = Tracker(destroyed)] {};
        }

    }  // namespace

    TEST(MoveRingTest, HandsOutTheOldestFromTheFrontAndTheNewestFromTheBack) {
        IntRing ring;
        for (int i = 1; i <= 4; ++i) {
            ring.PushBack(i);
        }

        EXPECT_EQ(ring.Front(), 1);
        EXPECT_EQ(ring.PopFront(), 1);
        EXPECT_EQ(ring.PopBack(), 4);
        EXPECT_EQ(ring.PopBack(), 3);
        EXPECT_EQ(ring.PopFront(), 2);
        EXPECT_TRUE(ring.Empty());
    }

    TEST(MoveRingTest, GrowthWithAWrappedHeadKeepsTheOrder) {
        IntRing ring;
        // Moves the head into the middle of the first array, so the live elements wrap when it fills
        for (int i = 0; i < 6; ++i) {
            ring.PushBack(i);
        }
        for (int i = 0; i < 6; ++i) {
            ASSERT_EQ(ring.PopFront(), i);
        }

        for (int i = 0; i < kBeyondInitialCapacity; ++i) {
            ring.PushBack(i);
        }

        ASSERT_EQ(ring.Size(), static_cast<size_t>(kBeyondInitialCapacity));
        for (int i = 0; i < kBeyondInitialCapacity; ++i) {
            ASSERT_EQ(ring.PopFront(), i);
        }
    }

    TEST(MoveRingTest, HoldsAMoveOnlyElement) {
        Detail::MoveRing<UniquePtr<int>> ring;
        ring.PushBack(MakeUnique<int>(7));
        ring.PushBack(MakeUnique<int>(8));

        const UniquePtr<int> first = ring.PopFront();
        const UniquePtr<int> last = ring.PopBack();
        ASSERT_TRUE(first && last);
        EXPECT_EQ(*first, 7);
        EXPECT_EQ(*last, 8);
    }

    TEST(MoveRingTest, ClearDestroysEachElementOnceAndKeepsWorking) {
        int destroyed = 0;
        Detail::MoveRing<Job> ring;
        for (int i = 0; i < kBeyondInitialCapacity; ++i) {
            ring.PushBack(TrackedJob(&destroyed));
        }
        EXPECT_EQ(destroyed, 0);

        ring.Clear();
        EXPECT_EQ(destroyed, kBeyondInitialCapacity);
        EXPECT_TRUE(ring.Empty());

        ring.PushBack(TrackedJob(&destroyed));
        EXPECT_EQ(ring.Size(), 1u);
        ring.Clear();
        EXPECT_EQ(destroyed, kBeyondInitialCapacity + 1);
    }

    TEST(MoveRingTest, ThePoppedCaptureDiesWithTheJobNotWhenTheSlotIsReused) {
        int destroyed = 0;
        Detail::MoveRing<Job> ring;
        ring.PushBack(TrackedJob(&destroyed));
        ring.PushBack(TrackedJob(&destroyed));

        (void)ring.PopFront();
        EXPECT_EQ(destroyed, 1);
        (void)ring.PopBack();
        EXPECT_EQ(destroyed, 2);

        // The slots are reused: the ring does not destroy a capture a second time
        ring.PushBack(TrackedJob(&destroyed));
        ring.PushBack(TrackedJob(&destroyed));
        EXPECT_EQ(destroyed, 2);
    }

    TEST(MoveRingTest, KeepsItsCapacityAfterADrain) {
        IntRing ring;
        for (int i = 0; i < kBeyondInitialCapacity; ++i) {
            ring.PushBack(i);
        }
        while (!ring.Empty()) {
            (void)ring.PopFront();
        }

        ExpectNoAllocations([&] {
            for (int round = 0; round < 3; ++round) {
                for (int i = 0; i < kBeyondInitialCapacity; ++i) {
                    ring.PushBack(i);
                }
                for (int i = 0; i < kBeyondInitialCapacity; ++i) {
                    (void)ring.PopFront();
                }
            }
        });
    }

}  // namespace BECore::Tests
