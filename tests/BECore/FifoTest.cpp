#include <BECore/Types/Fifo.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// Counts how many instances live, to show every element is destroyed exactly once
        struct Counted {
            static inline int alive = 0;

            explicit Counted(int valueIn) : value(valueIn) {
                ++alive;
            }
            Counted(Counted&& other) noexcept : value(other.value) {
                ++alive;
            }
            Counted(const Counted&) = delete;
            Counted& operator=(const Counted&) = delete;
            ~Counted() {
                --alive;
            }

            int value;
        };

        struct Payload {
            String text;
            UniquePtr<int> number;
        };

        /// Drives the queue through a pattern that moves the head far past the first lap
        void Cycle(Fifo<int>& queue, int& next, int& expected, int laps) {
            for (int i = 0; i < laps; ++i) {
                queue.push_back(next++);
                queue.push_back(next++);
                ASSERT_EQ(queue.front(), expected);
                queue.pop_front();
                ++expected;
            }
        }

    }  // namespace

    TEST(FifoTest, KeepsTheOrderOverManyLaps) {
        Fifo<int> queue;
        int next = 0;
        int expected = 0;
        Cycle(queue, next, expected, 1000);
        while (!queue.empty()) {
            ASSERT_EQ(queue.front(), expected++);
            queue.pop_front();
        }
        EXPECT_EQ(expected, next);
    }

    TEST(FifoTest, StaysInOneBlockWhileItDoesNotGrow) {
        Fifo<int> queue;
        queue.reserve(16);
        const size_t capacity = queue.capacity();
        for (int i = 0; i < 10000; ++i) {
            queue.push_back(i);
            if (queue.size() > 8) {
                queue.pop_front();
            }
        }
        EXPECT_EQ(queue.capacity(), capacity);
        EXPECT_EQ(queue.size(), 8U);
    }

    TEST(FifoTest, GrowthWhileWrappedKeepsTheOrder) {
        Fifo<int> queue;
        int next = 0;
        int expected = 0;
        // Fill to the first capacity, pop a few and push again, so the live range wraps over the end of the block
        for (int i = 0; i < 8; ++i) {
            queue.push_back(next++);
        }
        for (int i = 0; i < 5; ++i) {
            ASSERT_EQ(queue.front(), expected++);
            queue.pop_front();
        }
        for (int i = 0; i < 5; ++i) {
            queue.push_back(next++);
        }
        ASSERT_EQ(queue.size(), queue.capacity());
        queue.push_back(next++);
        EXPECT_GT(queue.capacity(), 8U);
        for (size_t i = 0; i < queue.size(); ++i) {
            EXPECT_EQ(queue[i], expected + static_cast<int>(i));
        }
        while (!queue.empty()) {
            ASSERT_EQ(queue.front(), expected++);
            queue.pop_front();
        }
        EXPECT_EQ(expected, next);
    }

    TEST(FifoTest, PushingAnElementOfTheQueueSurvivesGrowth) {
        Fifo<String> queue;
        queue.push_back(String("a long enough text to leave the small string buffer"));
        while (queue.size() < queue.capacity()) {
            queue.push_back(String("filler"));
        }
        queue.push_back(queue.front());
        EXPECT_EQ(queue[queue.size() - 1], queue.front());
        EXPECT_EQ(queue.front(), "a long enough text to leave the small string buffer");
    }

    TEST(FifoTest, HoldsMoveOnlyElements) {
        Fifo<Payload> queue;
        for (int i = 0; i < 100; ++i) {
            queue.push_back(Payload{String("text"), MakeUnique<int>(i)});
        }
        for (int i = 0; i < 100; ++i) {
            ASSERT_NE(queue.front().number, nullptr);
            EXPECT_EQ(*queue.front().number, i);
            EXPECT_EQ(queue.front().text, "text");
            queue.pop_front();
        }
        EXPECT_TRUE(queue.empty());
    }

    TEST(FifoTest, EveryElementIsDestroyedOnce) {
        ASSERT_EQ(Counted::alive, 0);
        {
            Fifo<Counted> queue;
            for (int i = 0; i < 40; ++i) {
                queue.emplace_back(i);
            }
            EXPECT_EQ(Counted::alive, 40);
            for (int i = 0; i < 15; ++i) {
                queue.pop_front();
            }
            EXPECT_EQ(Counted::alive, 25);
            queue.clear();
            EXPECT_EQ(Counted::alive, 0);
            queue.emplace_back(1);
            queue.emplace_back(2);
        }
        EXPECT_EQ(Counted::alive, 0);
    }

    TEST(FifoTest, ClearKeepsTheCapacity) {
        Fifo<int> queue;
        for (int i = 0; i < 100; ++i) {
            queue.push_back(i);
        }
        const size_t capacity = queue.capacity();
        queue.clear();
        EXPECT_TRUE(queue.empty());
        EXPECT_EQ(queue.capacity(), capacity);
        queue.push_back(7);
        EXPECT_EQ(queue.front(), 7);
    }

    TEST(FifoTest, MoveTransfersTheElements) {
        Fifo<Counted> source;
        for (int i = 0; i < 20; ++i) {
            source.emplace_back(i);
        }
        Fifo<Counted> moved(std::move(source));
        EXPECT_TRUE(source.empty());  // NOLINT(bugprone-use-after-move): the moved-from state is part of the contract
        EXPECT_EQ(moved.size(), 20U);
        EXPECT_EQ(Counted::alive, 20);

        Fifo<Counted> assigned;
        assigned.emplace_back(99);
        assigned = std::move(moved);
        EXPECT_EQ(assigned.size(), 20U);
        EXPECT_EQ(assigned.front().value, 0);
        EXPECT_EQ(Counted::alive, 20);

        assigned.clear();
        EXPECT_EQ(Counted::alive, 0);
    }

    TEST(FifoTest, SwapExchangesTheContents) {
        Fifo<int> a;
        Fifo<int> b;
        a.push_back(1);
        a.push_back(2);
        b.push_back(10);
        a.swap(b);
        EXPECT_EQ(a.size(), 1U);
        EXPECT_EQ(a.front(), 10);
        ASSERT_EQ(b.size(), 2U);
        EXPECT_EQ(b[0], 1);
        EXPECT_EQ(b[1], 2);
    }

}  // namespace BECore::Tests
