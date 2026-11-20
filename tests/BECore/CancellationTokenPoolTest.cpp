#include <BECore/Memory/ConcurrentBlockPool.h>
#include <BECore/Memory/PoolStats.h>
#include <Common/Wait.h>
#include <TaskSystem/CancellationToken.h>
#include <TaskSystem/ThreadPool.h>
#include <atomic>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kPoolName = "CancellationToken::State";
        constexpr size_t kTokens = 200;
        constexpr size_t kWorkers = 4;

        /// The registry's counters of the token pool, exact: the registry of a concurrent pool lags until a thread publishes, and a drain publishes
        /// the pools its thread keeps a list for, so one probe state is made and dropped first (one more allocation per read)
        PoolStatsEntry ReadPool() {
            {
                const CancellationToken probe = CancellationToken::Create();
            }
            ConcurrentBlockPool::DrainThreadCache();
            for (const PoolStatsEntry& entry : GetPoolStats()) {
                if (std::strcmp(entry.name, kPoolName) == 0) {
                    return entry;
                }
            }
            return {};
        }

    }  // namespace

    TEST(CancellationTokenPoolTest, TheStatePoolIsInTheRegistryAndAnswersForItsBlocks) {
        // The pool claims its slot at its first block
        CancellationToken first = CancellationToken::Create();
        const PoolStatsEntry before = ReadPool();
        ASSERT_STREQ(before.name, kPoolName);
        EXPECT_GT(before.blockSize, 0u);

        Vector<CancellationToken> tokens;
        for (size_t i = 0; i < kTokens; ++i) {
            tokens.push_back(CancellationToken::Create());
        }
        const PoolStatsEntry during = ReadPool();
        EXPECT_EQ(during.live - before.live, kTokens);
        EXPECT_EQ(during.allocations - before.allocations, kTokens + 1);

        tokens.clear();
        const PoolStatsEntry after = ReadPool();
        EXPECT_EQ(after.live, before.live);
        EXPECT_EQ(after.pages, during.pages);
    }

    TEST(CancellationTokenPoolTest, ALinkedChildKeepsTheBlockOfItsParent) {
        CancellationToken anchor = CancellationToken::Create();
        const PoolStatsEntry before = ReadPool();

        CancellationToken parent = CancellationToken::Create();
        CancellationToken child = parent.CreateLinked();
        EXPECT_EQ(ReadPool().live - before.live, 2u);

        parent = CancellationToken();
        EXPECT_EQ(ReadPool().live - before.live, 2u) << "the child holds its own count of the parent's state";
        parent = CancellationToken::Create();
        parent.Cancel();
        EXPECT_FALSE(child.IsCancelled());

        child = CancellationToken();
        parent = CancellationToken();
        EXPECT_EQ(ReadPool().live, before.live);
    }

    TEST(CancellationTokenPoolTest, TokensCopiedIntoPoolTasksAreGivenBackAndTheirPagesStayPut) {
        CancellationToken anchor = CancellationToken::Create();
        const PoolStatsEntry before = ReadPool();

        // A worker keeps the captures of the job it ran last until the next job or its end, so each round ends with the pool's shutdown
        auto round = [&before]() {
            ThreadPool pool(kWorkers);
            std::atomic<size_t> ran{0};
            {
                Vector<CancellationToken> tokens;
                for (size_t i = 0; i < kTokens; ++i) {
                    tokens.push_back(CancellationToken::Create());
                }
                for (const CancellationToken& token : tokens) {
                    pool.Submit([token, &ran]() {
                        if (!token.IsCancelled()) {
                            ran.fetch_add(1, std::memory_order_relaxed);
                        }
                    });
                }
                EXPECT_TRUE(WaitUntil([&ran]() { return ran.load(std::memory_order_relaxed) == kTokens; }));
            }
            pool.Shutdown();
            const PoolStatsEntry done = ReadPool();
            EXPECT_EQ(done.live, before.live) << "every state is given back";
            return done;
        };

        const PoolStatsEntry firstRound = round();
        const PoolStatsEntry secondRound = round();
        EXPECT_EQ(secondRound.pages, firstRound.pages);
        EXPECT_GE(secondRound.allocations - firstRound.allocations, kTokens);
    }

}  // namespace BECore::Tests
