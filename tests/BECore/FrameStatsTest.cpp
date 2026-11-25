#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Memory/BlockPool.h>
#include <BECore/Memory/LinearArena.h>
#include <BECore/Memory/MemTag.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr uint64_t kNanosecondsPerMs = 1000000;

        /// A clock the test moves by hand
        uint64_t g_fakeNow = 0;
        uint64_t FakeNow() {
            return g_fakeNow;
        }
        void AdvanceMs(uint64_t ms) {
            g_fakeNow += ms * kNanosecondsPerMs;
        }

        // Three quads and two sprites (the sprites get their textures from the test)
        constexpr const char* kCountedSceneXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Counted"><nodes>
    <node name="Quad1"><components><component type="TransformComponent" x="0" y="0" width="10" height="10"/><component type="QuadRendererComponent"/></components></node>
    <node name="Quad2"><components><component type="TransformComponent" x="20" y="0" width="10" height="10"/><component type="QuadRendererComponent"/></components></node>
    <node name="Quad3"><components><component type="TransformComponent" x="40" y="0" width="10" height="10"/><component type="QuadRendererComponent"/></components></node>
    <node name="SpriteA"><components><component type="TransformComponent" x="0" y="20" width="10" height="10"/><component type="SpriteRendererComponent"/></components></node>
    <node name="SpriteB"><components><component type="TransformComponent" x="20" y="20" width="10" height="10"/><component type="SpriteRendererComponent"/></components></node>
  </nodes></scene>
</scenes></SceneManager>)";

    }  // namespace

    // ---- Draw counters ----

    TEST(RenderCountersTest, CountsDrawsTrianglesAndTextureSwitches) {
        auto renderer = New<RecordingRenderer>();
        const IntrusivePtr<ITexture> a = MakeTexture(*renderer);
        const IntrusivePtr<ITexture> b = MakeTexture(*renderer);
        renderer->DrawFilledRect(0, 0, 1, 1, Color{});
        renderer->DrawFilledRect(0, 0, 1, 1, Color{});
        renderer->DrawTexture(*a, nullptr, 0, 0, 1, 1);
        renderer->DrawTexture(*a, nullptr, 0, 0, 1, 1);
        renderer->DrawTexture(*b, nullptr, 0, 0, 1, 1);

        const RenderCounters counters = renderer->TakeRenderCounters();
        EXPECT_EQ(counters.drawCalls, 5u);
        EXPECT_EQ(counters.primitives, counters.drawCalls);  // nothing batches here: a primitive is a submission
        EXPECT_EQ(counters.triangles, 10u);
        EXPECT_EQ(counters.textureSwitches, 2u);
    }

    TEST(RenderCountersTest, GeometryCountsItsWholeTriangles) {
        auto renderer = New<RecordingRenderer>();
        const GeometryVertex vertices[3] = {};
        const int indices[7] = {0, 1, 2, 0, 1, 2, 0};
        renderer->DrawGeometry(nullptr, vertices, indices, 0.0f, 0.0f);
        const RenderCounters counters = renderer->TakeRenderCounters();
        EXPECT_EQ(counters.drawCalls, 1u);
        EXPECT_EQ(counters.triangles, 2u);
        EXPECT_EQ(counters.textureSwitches, 0u);
    }

    TEST(RenderCountersTest, APauseHidesDrawsAndTakingStartsAfresh) {
        auto renderer = New<RecordingRenderer>();
        const IntrusivePtr<ITexture> a = MakeTexture(*renderer);
        {
            const ScopedRenderCountersPause pause(*renderer);
            renderer->DrawFilledRect(0, 0, 1, 1, Color{});
        }
        EXPECT_EQ(renderer->PeekRenderCounters().drawCalls, 0u);
        EXPECT_EQ(renderer->PeekRenderCounters().primitives, 0u);

        renderer->DrawTexture(*a, nullptr, 0, 0, 1, 1);
        EXPECT_EQ(renderer->TakeRenderCounters().textureSwitches, 1u);
        EXPECT_EQ(renderer->PeekRenderCounters().drawCalls, 0u);
        // A new frame: the same texture is a switch again
        renderer->DrawTexture(*a, nullptr, 0, 0, 1, 1);
        EXPECT_EQ(renderer->TakeRenderCounters().textureSwitches, 1u);
    }

    class SceneDrawCountTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _renderer = New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kCountedSceneXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        IntrusivePtr<SpriteRendererComponent> Sprite(const char* nodeName) const {
            Scene& scene = *_manager->GetSceneByName("Counted"_intern);
            for (const auto& node : scene.GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(nodeName)) {
                    return node->GetComponent<SpriteRendererComponent>();
                }
            }
            return {};
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtr<SceneManager> _manager;
    };

    TEST_F(SceneDrawCountTest, AKnownSceneMakesTheExpectedDraws) {
        ASSERT_TRUE(Sprite("SpriteA"));
        Sprite("SpriteA")->SetTexture(MakeTexture(*_renderer));
        Sprite("SpriteB")->SetTexture(MakeTexture(*_renderer));
        _manager->SetActiveScene("Counted"_intern);
        _renderer->TakeRenderCounters();

        _manager->DrawAll();

        const RenderCounters counters = _renderer->TakeRenderCounters();
        EXPECT_EQ(counters.drawCalls, 5u);
        EXPECT_EQ(counters.triangles, 10u);
        EXPECT_EQ(counters.textureSwitches, 2u);
    }

    // ---- The collector ----

    TEST(FrameStatsCollectorTest, TimesTheStagesOfAFrame) {
        FrameStatsCollector collector;
        collector.SetClock(FakeNow);
        g_fakeNow = 0;

        collector.BeginFrame();
        collector.BeginStage(FrameStage::Update);
        AdvanceMs(1);
        collector.BeginStage(FrameStage::SceneUpdate);
        AdvanceMs(2);
        collector.EndStage(FrameStage::SceneUpdate);
        collector.EndStage(FrameStage::Update);
        collector.BeginStage(FrameStage::Render);
        AdvanceMs(4);
        collector.EndStage(FrameStage::Render);
        collector.SetCounter("Things"_intern, 3.0);
        collector.SetCounter("Things"_intern, 5.0);
        collector.EndFrame(RenderCounters{.drawCalls = 7, .triangles = 14, .textureSwitches = 1}, AllocTotals{});

        const FrameStats* last = collector.GetLast();
        ASSERT_NE(last, nullptr);
        EXPECT_EQ(last->frame, 1u);
        EXPECT_DOUBLE_EQ(last->cpuMs, 7.0);
        EXPECT_DOUBLE_EQ(last->frameMs, 7.0);
        EXPECT_DOUBLE_EQ(last->GetStageMs(FrameStage::Update), 3.0);
        EXPECT_DOUBLE_EQ(last->GetStageMs(FrameStage::SceneUpdate), 2.0);
        EXPECT_DOUBLE_EQ(last->GetStageMs(FrameStage::Render), 4.0);
        EXPECT_DOUBLE_EQ(last->GetStageMs(FrameStage::Present), 0.0);
        EXPECT_EQ(last->draws.drawCalls, 7u);
        EXPECT_EQ(last->FindCounter("Things"_intern), 5.0);
        EXPECT_FALSE(last->FindCounter("Other"_intern).has_value());

        // The second frame: frameMs runs from the end of the first
        AdvanceMs(10);
        collector.BeginFrame();
        AdvanceMs(6);
        collector.EndFrame({}, AllocTotals{});
        EXPECT_DOUBLE_EQ(collector.GetLast()->cpuMs, 6.0);
        EXPECT_DOUBLE_EQ(collector.GetLast()->frameMs, 16.0);
        EXPECT_TRUE(collector.GetLast()->counters.empty());
    }

    TEST(FrameStatsCollectorTest, TheWaitStageAccumulatesOverTheScopesOfAFrame) {
        FrameStatsCollector collector;
        collector.SetClock(FakeNow);
        g_fakeNow = 0;

        collector.BeginFrame();
        {
            FrameStatsCollector::StageScope scope(collector, FrameStage::Wait);
            AdvanceMs(3);
        }
        AdvanceMs(1);
        {
            FrameStatsCollector::StageScope scope(collector, FrameStage::Wait);
            AdvanceMs(2);
        }
        collector.EndFrame({}, AllocTotals{});

        EXPECT_DOUBLE_EQ(collector.GetLast()->GetStageMs(FrameStage::Wait), 5.0);
        EXPECT_DOUBLE_EQ(collector.GetLast()->cpuMs, 6.0);
    }

    TEST(FrameStatsCollectorTest, TheAllocationsOfAFrameAreTheDifference) {
        FrameStatsCollector collector;
        collector.SetClock(FakeNow);
        collector.BeginFrame();
        const AllocTotals atBegin = AllocTracker::GetTotals();
        AllocTotals atEnd = atBegin;
        atEnd.count += 3;
        atEnd.bytes += 300;
        atEnd.liveBytes = 1234;
        collector.EndFrame({}, atEnd);
        EXPECT_EQ(collector.GetLast()->allocCount, 3u);
        EXPECT_EQ(collector.GetLast()->allocBytes, 300u);
        EXPECT_EQ(collector.GetLast()->liveBytes, 1234u);
    }

    TEST(FrameStatsCollectorTest, AFrameCountsItsAllocationsPerTagAndKeepsTheLiveBytesOfTheTagAtItsEnd) {
        constexpr size_t kBlockBytes = 4096;
        FrameStatsCollector collector;
        collector.SetClock(FakeNow);
        collector.BeginFrame();
        void* block = nullptr;
        {
            BE_MEM_TAG(UI);
            block = ::operator new(kBlockBytes);
        }
        collector.EndFrame({}, AllocTracker::GetTotals());
        const FrameStats& frame = *collector.GetLast();
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_GE(frame.tagAllocCount[+MemTag::UI], 1u);
            EXPECT_GE(frame.tagLiveBytes[+MemTag::UI], kBlockBytes);
        } else {
            for (size_t i = 0; i < kMemTagCount; ++i) {
                EXPECT_EQ(frame.tagAllocCount[i], 0u);
                EXPECT_EQ(frame.tagLiveBytes[i], 0u);
            }
        }

        // the next frame allocates nothing under UI, yet the block is still alive at its end
        collector.BeginFrame();
        collector.EndFrame({}, AllocTracker::GetTotals());
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_GE(collector.GetLast()->tagLiveBytes[+MemTag::UI], kBlockBytes);
        }
        ::operator delete(block);
    }

    TEST(FrameStatsCollectorTest, AFrameSeesTheTotalsOfTheArenasAliveAtItsEnd) {
        constexpr size_t kFromHeap = 100;
        constexpr size_t kFromBuffer = 200;
        constexpr size_t kBufferSize = 64;  // smaller than the request: one overflow
        constexpr size_t kNoAlignment = 1;
        FrameStatsCollector collector;
        collector.SetClock(FakeNow);
        collector.BeginFrame();
        collector.EndFrame({}, AllocTotals{});
        const FrameStats outside = *collector.GetLast();

        {
            std::byte buffer[kBufferSize];
            LinearArena heapArena("FrameStatsHeap");
            LinearArena bufferArena("FrameStatsBuffer", buffer, sizeof(buffer));
            ASSERT_NE(heapArena.Allocate(kFromHeap, kNoAlignment), nullptr);
            ASSERT_NE(bufferArena.Allocate(kFromBuffer, kNoAlignment), nullptr);

            collector.BeginFrame();
            collector.EndFrame({}, AllocTotals{});
            const FrameStats& inside = *collector.GetLast();
            const ArenaTotals totals = GetArenaTotals();
            EXPECT_EQ(inside.arenaUsedBytes, totals.used);
            EXPECT_EQ(inside.arenaPeakBytes, totals.peak);
            EXPECT_EQ(inside.arenaOverflows, totals.overflows);
            EXPECT_EQ(inside.arenaUsedBytes - outside.arenaUsedBytes, kFromHeap + kFromBuffer);
            EXPECT_EQ(inside.arenaPeakBytes - outside.arenaPeakBytes, kFromHeap + kFromBuffer);
            EXPECT_EQ(inside.arenaOverflows - outside.arenaOverflows, 1u);
        }

        collector.BeginFrame();
        collector.EndFrame({}, AllocTotals{});
        EXPECT_EQ(collector.GetLast()->arenaUsedBytes, outside.arenaUsedBytes);
        EXPECT_EQ(collector.GetLast()->arenaPeakBytes, outside.arenaPeakBytes);
        EXPECT_EQ(collector.GetLast()->arenaOverflows, outside.arenaOverflows);
    }

    TEST(FrameStatsCollectorTest, AFrameSeesTheLiveBlocksOfThePoolsAliveAtItsEnd) {
        constexpr size_t kBlockSize = 24;
        constexpr size_t kFirstBlocks = 3;
        constexpr size_t kSecondBlocks = 2;
        FrameStatsCollector collector;
        collector.SetClock(FakeNow);
        collector.BeginFrame();
        collector.EndFrame({}, AllocTotals{});
        const FrameStats outside = *collector.GetLast();

        {
            BlockPool first("FrameStatsPoolFirst", kBlockSize, alignof(void*));
            BlockPool second("FrameStatsPoolSecond", kBlockSize, alignof(void*));
            void* blocks[kFirstBlocks + kSecondBlocks];
            for (size_t i = 0; i < kFirstBlocks; ++i) {
                blocks[i] = first.Allocate();
            }
            for (size_t i = 0; i < kSecondBlocks; ++i) {
                blocks[kFirstBlocks + i] = second.Allocate();
            }

            collector.BeginFrame();
            collector.EndFrame({}, AllocTotals{});
            const FrameStats& inside = *collector.GetLast();
            const PoolTotals totals = GetPoolTotals();
            EXPECT_EQ(inside.poolLiveBlocks, totals.liveBlocks);
            EXPECT_EQ(inside.poolPageBytes, totals.pageBytes);
            EXPECT_EQ(inside.poolLiveBlocks - outside.poolLiveBlocks, kFirstBlocks + kSecondBlocks);
            EXPECT_EQ(inside.poolPageBytes - outside.poolPageBytes, first.GetPageBytes() + second.GetPageBytes());

            for (size_t i = 0; i < kFirstBlocks; ++i) {
                first.Deallocate(blocks[i]);
            }
            for (size_t i = 0; i < kSecondBlocks; ++i) {
                second.Deallocate(blocks[kFirstBlocks + i]);
            }
        }

        collector.BeginFrame();
        collector.EndFrame({}, AllocTotals{});
        EXPECT_EQ(collector.GetLast()->poolLiveBlocks, outside.poolLiveBlocks);
        EXPECT_EQ(collector.GetLast()->poolPageBytes, outside.poolPageBytes);
    }

    TEST(FrameStatsCollectorTest, TheHistoryKeepsTheLastFramesOldestFirst) {
        FrameStatsCollector collector;
        collector.SetClock(FakeNow);
        EXPECT_EQ(collector.GetLast(), nullptr);
        EXPECT_TRUE(collector.GetHistory(5).empty());

        const size_t frames = FrameStatsCollector::kFrameHistory + 10;
        for (size_t i = 0; i < frames; ++i) {
            collector.BeginFrame();
            collector.EndFrame(RenderCounters{.drawCalls = static_cast<uint32_t>(i)}, AllocTotals{});
        }
        const Vector<FrameStats> lastThree = collector.GetHistory(3);
        ASSERT_EQ(lastThree.size(), 3u);
        EXPECT_EQ(lastThree[0].frame, frames - 2);
        EXPECT_EQ(lastThree[2].frame, frames);
        EXPECT_EQ(lastThree[2].draws.drawCalls, frames - 1);
        EXPECT_EQ(collector.GetHistory(10000).size(), FrameStatsCollector::kFrameHistory);
        EXPECT_EQ(collector.GetHistory(10000).front().frame, frames - FrameStatsCollector::kFrameHistory + 1);
    }

    TEST(FrameStatsCollectorTest, TheHistoryIntoAndTheVisitorGiveTheFramesOfGetHistory) {
        FrameStatsCollector collector;
        collector.SetClock(FakeNow);
        const size_t counts[] = {0, 3, FrameStatsCollector::kFrameHistory + 10};
        size_t seen = 0;
        Vector<FrameStats> into;
        for (const size_t frames : counts) {
            for (; seen < frames; ++seen) {
                collector.BeginFrame();
                collector.EndFrame(RenderCounters{.drawCalls = static_cast<uint32_t>(seen)}, AllocTotals{});
            }
            for (const size_t asked : {size_t{0}, size_t{2}, size_t{5}, FrameStatsCollector::kFrameHistory + 5}) {
                const Vector<FrameStats> expected = collector.GetHistory(asked);
                collector.GetHistoryInto(asked, into);
                ASSERT_EQ(into.size(), expected.size());
                EXPECT_EQ(collector.RecentCount(asked), expected.size());
                size_t index = 0;
                collector.ForEachRecent(asked, [&](const FrameStats& stats) {
                    ASSERT_LT(index, expected.size());
                    EXPECT_EQ(stats.frame, expected[index].frame);
                    EXPECT_EQ(stats.draws.drawCalls, expected[index].draws.drawCalls);
                    EXPECT_EQ(into[index].frame, expected[index].frame);
                    ++index;
                });
                EXPECT_EQ(index, expected.size());
            }
        }
    }

    TEST(FrameStatsCollectorTest, TheHistoryIntoAReusedVectorAllocatesNothing) {
        FrameStatsCollector collector;
        collector.SetClock(FakeNow);
        for (size_t i = 0; i < FrameStatsCollector::kFrameHistory + 3; ++i) {
            collector.BeginFrame();
            collector.EndFrame({}, AllocTotals{});
        }
        Vector<FrameStats> history;
        collector.GetHistoryInto(FrameStatsCollector::kFrameHistory, history);
        ExpectNoAllocations([&] {
            collector.GetHistoryInto(FrameStatsCollector::kFrameHistory, history);
            collector.ForEachRecent(FrameStatsCollector::kFrameHistory, [](const FrameStats&) {});
        });
    }

    TEST(FrameStatsCollectorTest, AHeldFrameIsMarkedPausedAndTheHistoryKeepsIt) {
        FrameStatsCollector collector;
        collector.SetClock(FakeNow);

        collector.BeginFrame();
        collector.EndFrame({}, AllocTotals{});
        EXPECT_FALSE(collector.GetLast()->paused) << "a frame is a game frame by default";

        collector.BeginFrame(true);
        collector.EndFrame({}, AllocTotals{});
        EXPECT_TRUE(collector.GetLast()->paused);

        collector.BeginFrame(false);  // a stepped frame is a game frame
        collector.EndFrame({}, AllocTotals{});
        EXPECT_FALSE(collector.GetLast()->paused) << "the flag of one frame does not leak into the next";

        const Vector<FrameStats> history = collector.GetHistory(3);
        ASSERT_EQ(history.size(), 3u);
        EXPECT_FALSE(history[0].paused);
        EXPECT_TRUE(history[1].paused);
        EXPECT_FALSE(history[2].paused);
    }

    TEST(FrameStatsCollectorTest, StagesAndCountersOutsideAFrameAreIgnored) {
        FrameStatsCollector collector;
        collector.SetClock(FakeNow);
        collector.BeginStage(FrameStage::Update);
        collector.EndStage(FrameStage::Update);
        collector.SetCounter("Lost"_intern, 1.0);
        collector.EndFrame({}, AllocTotals{});
        EXPECT_EQ(collector.GetFrameCount(), 0u);
    }

}  // namespace BECore::Tests
