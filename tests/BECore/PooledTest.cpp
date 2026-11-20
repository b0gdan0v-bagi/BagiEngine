#include <BECore/Memory/Pooled.h>
#include <BECore/RefCounted/New.h>
#include <Common/Wait.h>
#include <atomic>
#include <cstring>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>
#include <new>
#include <thread>

namespace BECore::Tests {

    namespace {

        class PooledNonAtomic : public RefCountedNonAtomic {
        public:
            BE_POOLED(PooledNonAtomic)

            explicit PooledNonAtomic(int value = 0) : _value(value) {}
            int _value;
        };

        class PooledAtomic : public RefCountedAtomic {
        public:
            BE_POOLED(PooledAtomic)

            explicit PooledAtomic(int value = 0) : _value(value) {}
            int _value;
        };

        /// A larger class derived from a pooled one: another size, so the heap
        class BigPooledChild : public PooledAtomic {
        public:
            BigPooledChild() : PooledAtomic(9) {}
            char _extra[96] = {};
        };

        /// Its own opt-in: a pool of its own
        class OptedInChild : public PooledAtomic {
        public:
            BE_POOLED(OptedInChild)

            OptedInChild() : PooledAtomic(1) {}
            char _extra[32] = {};
        };

        class SingleThreadObject : public RefCountedNonAtomic {
        public:
            BE_POOLED_SINGLE_THREAD(SingleThreadObject)

            explicit SingleThreadObject(int value = 0) : _value(value) {}
            int _value;
        };

        struct Found {
            bool found = false;
            PoolStatsEntry entry;
        };

        Found Find(const char* name) {
            for (const PoolStatsEntry& entry : GetPoolStats()) {
                if (std::strcmp(entry.name, name) == 0) {
                    return {true, entry};
                }
            }
            return {};
        }

    }  // namespace

    TEST(PooledTest, ANonAtomicObjectTakesItsBlockFromThePoolAndGivesItBackAtTheLastRelease) {
        const size_t before = PooledNonAtomic::PooledPool().GetLiveCount();
        const PooledNonAtomic* address = nullptr;
        {
            auto first = New<PooledNonAtomic>(7);
            address = first.Get();
            EXPECT_EQ(first->_value, 7);
            EXPECT_EQ(PooledNonAtomic::PooledPool().GetLiveCount(), before + 1);
            auto second = first;
            first = nullptr;
            EXPECT_EQ(PooledNonAtomic::PooledPool().GetLiveCount(), before + 1) << "one reference is left";
        }
        EXPECT_EQ(PooledNonAtomic::PooledPool().GetLiveCount(), before);
        auto again = New<PooledNonAtomic>(8);
        EXPECT_EQ(again.Get(), address);
    }

    TEST(PooledTest, AnAtomicObjectMadeWithMakeIntrusiveUsesThePoolToo) {
        const size_t before = PooledAtomic::PooledPool().GetLiveCount();
        const PooledAtomic* address = nullptr;
        {
            auto object = MakeIntrusiveAtomic<PooledAtomic>(3);
            address = object.Get();
            EXPECT_EQ(PooledAtomic::PooledPool().GetLiveCount(), before + 1);
        }
        EXPECT_EQ(PooledAtomic::PooledPool().GetLiveCount(), before);
        EXPECT_EQ(New<PooledAtomic>(4).Get(), address);
    }

    TEST(PooledTest, ADeleteThroughABaseWithAVirtualDestructorReachesThePool) {
        const size_t before = PooledAtomic::PooledPool().GetLiveCount();
        RefCountedAtomic* base = new PooledAtomic(5);
        EXPECT_EQ(PooledAtomic::PooledPool().GetLiveCount(), before + 1);
        delete base;
        EXPECT_EQ(PooledAtomic::PooledPool().GetLiveCount(), before);
    }

    TEST(PooledTest, ADerivedClassOfAnotherSizeLivesOnTheHeapAndLeavesThePoolAlone) {
        const Found beforeStats = Find("PooledAtomic");
        const size_t before = PooledAtomic::PooledPool().GetLiveCount();
        {
            auto child = New<BigPooledChild>();
            EXPECT_EQ(child->_value, 9);
            EXPECT_EQ(PooledAtomic::PooledPool().GetLiveCount(), before);
            IntrusivePtrAtomic<PooledAtomic> asBase = child;
            child = nullptr;
            EXPECT_EQ(asBase->_value, 9);
        }
        PooledAtomic* viaBase = new BigPooledChild();
        delete viaBase;
        EXPECT_EQ(PooledAtomic::PooledPool().GetLiveCount(), before);
        const Found afterStats = Find("PooledAtomic");
        EXPECT_EQ(afterStats.found, beforeStats.found);
        if (beforeStats.found) {
            EXPECT_EQ(afterStats.entry.allocations, beforeStats.entry.allocations);
        }
    }

    TEST(PooledTest, ADerivedClassThatOptsInHasAPoolOfItsOwn) {
        const size_t baseBefore = PooledAtomic::PooledPool().GetLiveCount();
        const size_t childBefore = OptedInChild::PooledPool().GetLiveCount();
        {
            auto child = New<OptedInChild>();
            EXPECT_EQ(OptedInChild::PooledPool().GetLiveCount(), childBefore + 1);
            EXPECT_EQ(PooledAtomic::PooledPool().GetLiveCount(), baseBefore);
            RefCountedAtomic* asBase = child.Get();
            EXPECT_NE(asBase, nullptr);
        }
        EXPECT_EQ(OptedInChild::PooledPool().GetLiveCount(), childBefore);
    }

    TEST(PooledTest, PlacementNewStillWorksAndTouchesNoPool) {
        alignas(PooledNonAtomic) std::byte storage[sizeof(PooledNonAtomic)];
        const size_t before = PooledNonAtomic::PooledPool().GetLiveCount();
        auto* object = new (storage) PooledNonAtomic(11);
        EXPECT_EQ(static_cast<void*>(object), static_cast<void*>(storage));
        EXPECT_EQ(object->_value, 11);
        EXPECT_EQ(PooledNonAtomic::PooledPool().GetLiveCount(), before);
        object->~PooledNonAtomic();
    }

    TEST(PooledTest, AnAtomicObjectReleasedOnAnotherThreadReturnsToThePool) {
        const size_t before = PooledAtomic::PooledPool().GetLiveCount();
        auto object = New<PooledAtomic>(1);
        EXPECT_EQ(PooledAtomic::PooledPool().GetLiveCount(), before + 1);
        std::thread other([moved = std::move(object)]() mutable { moved = nullptr; });
        other.join();
        EXPECT_EQ(PooledAtomic::PooledPool().GetLiveCount(), before);
    }

    TEST(PooledTest, ThePoolShowsInTheRegistryUnderTheTypesName) {
        auto object = New<PooledAtomic>(1);
        ConcurrentBlockPool::DrainThreadCache();
        const Found found = Find("PooledAtomic");
        ASSERT_TRUE(found.found);
        EXPECT_GE(found.entry.blockSize, sizeof(PooledAtomic));
        EXPECT_GE(found.entry.live, 1u);
        EXPECT_GE(found.entry.pages, 1u);
    }

    TEST(PooledTest, ASingleThreadObjectReturnsItsBlockAtTheLastRelease) {
        const size_t before = SingleThreadObject::PooledPool().GetLiveCount();
        const SingleThreadObject* address = nullptr;
        {
            auto object = New<SingleThreadObject>(2);
            address = object.Get();
            EXPECT_EQ(SingleThreadObject::PooledPool().GetLiveCount(), before + 1);
        }
        EXPECT_EQ(SingleThreadObject::PooledPool().GetLiveCount(), before);
        EXPECT_EQ(New<SingleThreadObject>(3).Get(), address);
        const Found found = Find("SingleThreadObject");
        ASSERT_TRUE(found.found);
        EXPECT_EQ(found.entry.blockSize, SingleThreadObject::PooledPool().GetBlockSize());
    }

#if ENGINE_ENABLE_ASSERTS
    namespace {

        /// Its own type: the first thread to use a single-thread pool is its owner for good
        class OwnedByTheFirstThread : public RefCountedNonAtomic {
        public:
            BE_POOLED_SINGLE_THREAD(OwnedByTheFirstThread)
        };

    }  // namespace

    TEST(PooledTest, ASingleThreadPoolUsedFromASecondThreadAssertsInDebug) {
        auto first = New<OwnedByTheFirstThread>();
        IntrusivePtr<OwnedByTheFirstThread> second;
        EXPECT_NONFATAL_FAILURE_ON_ALL_THREADS(
            {
                std::thread other([&] { second = New<OwnedByTheFirstThread>(); });
                other.join();
            },
            "second thread");
        ASSERT_NE(second.Get(), nullptr);
    }
#endif

}  // namespace BECore::Tests
