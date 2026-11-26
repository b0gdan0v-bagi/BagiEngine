#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Renderer/RenderBatch.h>
#include <Common/AllocCheck.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr uint8_t kOpaque = 255;
        constexpr uint32_t kQuadVertices = 4;
        constexpr uint32_t kQuadIndices = 6;
        constexpr int kWarmFrames = 3;
        constexpr int kQuadsPerFrame = 40;
        const Color kRed{kOpaque, 0, 0, kOpaque};
        const Color kTint{200, 100, 50, 180};
        const Color kWhiteTint{kOpaque, kOpaque, kOpaque, kOpaque};

        /// A texture the recording renderer did not make, so it cannot draw it
        class ForeignTexture : public ITexture {
        public:
            ResourceState GetState() const override {
                return ResourceState::Loaded;
            }
            PoolString GetPath() const override {
                return "<foreign>"_intern;
            }
            uint64_t GetMemoryUsage() const override {
                return 0;
            }
            PoolString GetTypeName() const override {
                return "ForeignTexture"_intern;
            }
            float GetWidth() const override {
                return 4.0f;
            }
            float GetHeight() const override {
                return 4.0f;
            }
            void* GetNativeHandle() const override {
                return nullptr;
            }
        };

        constexpr uint64_t kSubmitNs = 2'000'000;
        constexpr double kSubmitMs = 2.0;

        uint64_t g_fakeNowNs = 0;

        uint64_t FakeNowNs() {
            return g_fakeNowNs;
        }

        /// A batching backend whose every submission takes kSubmitNs of the fake clock
        class SlowSubmitRenderer : public RecordingRenderer {
        protected:
            void SubmitBatch(const RenderBatch& batch) override {
                g_fakeNowNs += kSubmitNs;
                RecordingRenderer::SubmitBatch(batch);
            }
        };

        IntrusivePtr<RecordingRenderer> MakeBatching() {
            auto renderer = New<RecordingRenderer>();
            renderer->SetBatchingEnabled(true);
            return renderer;
        }

        void ExpectSameVertex(const GeometryVertex& a, const GeometryVertex& b) {
            EXPECT_FLOAT_EQ(a.x, b.x);
            EXPECT_FLOAT_EQ(a.y, b.y);
            EXPECT_FLOAT_EQ(a.u, b.u);
            EXPECT_FLOAT_EQ(a.v, b.v);
            EXPECT_EQ(a.color.r, b.color.r);
            EXPECT_EQ(a.color.g, b.color.g);
            EXPECT_EQ(a.color.b, b.color.b);
            EXPECT_EQ(a.color.a, b.color.a);
        }

    }  // namespace

    TEST(RendererBatchTest, QuadsOfOneTextureAreOneBatchWithOneSubmission) {
        auto renderer = MakeBatching();
        const IntrusivePtr<ITexture> texture = MakeTexture(*renderer, 4, 4);
        constexpr uint32_t kCount = 5;
        for (uint32_t i = 0; i < kCount; ++i) {
            renderer->DrawTexture(*texture, nullptr, static_cast<float>(i) * 10.0f, 0, 8, 8, kTint);
        }
        EXPECT_TRUE(renderer->calls.empty()) << "nothing is submitted before the flush";

        renderer->Flush();
        ASSERT_EQ(renderer->calls.size(), 1u);
        const DrawCall& batch = renderer->calls[0];
        EXPECT_EQ(batch.kind, DrawKind::Batch);
        EXPECT_EQ(batch.quads, kCount);
        EXPECT_EQ(batch.texture, texture.Get());
        EXPECT_EQ(batch.vertices.size(), kCount * kQuadVertices);
        EXPECT_EQ(batch.indices.size(), kCount * kQuadIndices);

        const RenderCounters counters = renderer->TakeRenderCounters();
        EXPECT_EQ(counters.primitives, kCount);
        EXPECT_EQ(counters.drawCalls, 1u);
        EXPECT_EQ(counters.triangles, kCount * 2u);
        EXPECT_EQ(counters.textureSwitches, 1u);
    }

    TEST(RendererBatchTest, AnotherTextureOrBlendModeOrNoTextureSplitsInOrder) {
        auto renderer = MakeBatching();
        const IntrusivePtr<ITexture> a = MakeTexture(*renderer, 4, 4);
        const IntrusivePtr<ITexture> b = MakeTexture(*renderer, 4, 4);
        renderer->DrawTexture(*a, nullptr, 0, 0, 1, 1);
        renderer->DrawTexture(*a, nullptr, 0, 0, 1, 1);
        renderer->DrawTexture(*b, nullptr, 0, 0, 1, 1);
        renderer->DrawFilledRect(0, 0, 1, 1, kRed);  // an untextured run between textured ones
        renderer->DrawTexture(*b, nullptr, 0, 0, 1, 1);
        {
            const ScopedBlendMode add(*renderer, BlendMode::Add);
            renderer->DrawTexture(*b, nullptr, 0, 0, 1, 1);
        }
        renderer->DrawTexture(*b, nullptr, 0, 0, 1, 1);
        renderer->Flush();

        ASSERT_EQ(renderer->calls.size(), 6u);
        const uint32_t expectedQuads[] = {2, 1, 1, 1, 1, 1};
        const ITexture* expectedTextures[] = {a.Get(), b.Get(), nullptr, b.Get(), b.Get(), b.Get()};
        const BlendMode expectedBlends[] = {BlendMode::Alpha, BlendMode::Alpha, BlendMode::Alpha, BlendMode::Alpha, BlendMode::Add, BlendMode::Alpha};
        for (size_t i = 0; i < 6; ++i) {
            EXPECT_EQ(At(renderer->calls, i).quads, expectedQuads[i]) << i;
            EXPECT_EQ(At(renderer->calls, i).texture, expectedTextures[i]) << i;
            EXPECT_EQ(At(renderer->calls, i).blend, expectedBlends[i]) << i;
        }
        const RenderCounters counters = renderer->TakeRenderCounters();
        EXPECT_EQ(counters.primitives, 7u);
        EXPECT_EQ(counters.drawCalls, 6u);
    }

    TEST(RendererBatchTest, AGeometryDrawBetweenQuadsKeepsTheOrder) {
        auto renderer = MakeBatching();
        const GeometryVertex triangle[3] = {};
        const int indices[3] = {0, 1, 2};
        renderer->DrawFilledRect(0, 0, 1, 1, kRed);
        renderer->DrawFilledRect(2, 0, 1, 1, kRed);
        renderer->DrawGeometry(nullptr, triangle, indices, 0.0f, 0.0f);
        renderer->DrawFilledRect(4, 0, 1, 1, kRed);
        renderer->Flush();

        ASSERT_EQ(renderer->calls.size(), 3u);
        EXPECT_EQ(renderer->calls[0].kind, DrawKind::Batch);
        EXPECT_EQ(renderer->calls[0].quads, 2u);
        EXPECT_EQ(renderer->calls[1].kind, DrawKind::Geometry);
        EXPECT_EQ(renderer->calls[2].kind, DrawKind::Batch);
        EXPECT_EQ(renderer->calls[2].quads, 1u);

        const RenderCounters counters = renderer->TakeRenderCounters();
        EXPECT_EQ(counters.primitives, 4u);
        EXPECT_EQ(counters.drawCalls, 3u);
    }

    TEST(RendererBatchTest, AClipChangeSplitsAndEachBatchKeepsTheClipItWasDrawnUnder) {
        auto renderer = MakeBatching();
        const Rect clip{10, 10, 50, 50};
        renderer->DrawFilledRect(0, 0, 1, 1, kRed);
        renderer->PushClipRect(clip);
        renderer->DrawFilledRect(0, 0, 1, 1, kRed);
        renderer->DrawFilledRect(0, 0, 1, 1, kRed);
        renderer->PopClipRect();
        renderer->DrawFilledRect(0, 0, 1, 1, kRed);
        renderer->Flush();

        ASSERT_EQ(renderer->calls.size(), 3u);
        EXPECT_FALSE(renderer->calls[0].clip.has_value());
        ASSERT_TRUE(renderer->calls[1].clip.has_value());
        EXPECT_FLOAT_EQ(renderer->calls[1].clip->x, clip.x);
        EXPECT_EQ(renderer->calls[1].quads, 2u);
        EXPECT_FALSE(renderer->calls[2].clip.has_value());
    }

    TEST(RendererBatchTest, AnEmptyClipDrawsNothing) {
        auto renderer = MakeBatching();
        renderer->PushClipRect(Rect{0, 0, 0, 0});
        renderer->DrawFilledRect(0, 0, 1, 1, kRed);
        renderer->Flush();
        EXPECT_TRUE(renderer->calls.empty());
        EXPECT_EQ(renderer->PeekRenderCounters().primitives, 0u);
        renderer->PopClipRect();
    }

    TEST(RendererBatchTest, APauseCountsTheDrawsInsideItAndOutsideItSeparately) {
        auto renderer = MakeBatching();
        renderer->DrawFilledRect(0, 0, 1, 1, kRed);
        {
            const ScopedRenderCountersPause pause(*renderer);
            renderer->DrawFilledRect(0, 0, 1, 1, kRed);
            renderer->DrawFilledRect(0, 0, 1, 1, kRed);
        }
        renderer->DrawFilledRect(0, 0, 1, 1, kRed);
        renderer->Flush();

        ASSERT_EQ(renderer->calls.size(), 3u) << "the pause and the resume each end the batch";
        const RenderCounters counters = renderer->TakeRenderCounters();
        EXPECT_EQ(counters.primitives, 2u);
        EXPECT_EQ(counters.drawCalls, 2u);
    }

    /// Times the process's frame statistics, which Flush() writes to, with the fake clock
    class RendererBatchSubmitStageTest : public ::testing::Test {
    protected:
        void SetUp() override {
            CoreManager::GetFrameStats().SetClock(FakeNowNs);
            _renderer->SetBatchingEnabled(true);
        }

        void TearDown() override {
            CoreManager::GetFrameStats().SetClock(nullptr);
        }

        static double EndFrameAndGetSubmitMs() {
            FrameStatsCollector& stats = CoreManager::GetFrameStats();
            stats.EndFrame({}, AllocTotals{});
            return stats.GetLast()->GetStageMs(FrameStage::Submit);
        }

        IntrusivePtr<SlowSubmitRenderer> _renderer = New<SlowSubmitRenderer>();
    };

    TEST_F(RendererBatchSubmitStageTest, EverySubmissionOutsideACountersPauseAddsItsTime) {
        const IntrusivePtr<ITexture> texture = MakeTexture(*_renderer, 4, 4);
        CoreManager::GetFrameStats().BeginFrame();
        _renderer->DrawFilledRect(0, 0, 1, 1, kRed);
        _renderer->DrawTexture(*texture, nullptr, 0, 0, 1, 1);  // submits the rect
        {
            const ScopedRenderCountersPause pause(*_renderer);  // submits the texture, still outside the pause
            _renderer->DrawFilledRect(0, 0, 1, 1, kRed);
            _renderer->Flush();
            _renderer->DrawFilledRect(0, 0, 1, 1, kRed);
        }
        _renderer->DrawFilledRect(0, 0, 1, 1, kRed);
        _renderer->Flush();

        EXPECT_DOUBLE_EQ(EndFrameAndGetSubmitMs(), 3 * kSubmitMs);
        EXPECT_EQ(_renderer->calls.size(), 5u) << "the paused batches reached the backend too";
    }

    TEST_F(RendererBatchSubmitStageTest, ASubmissionOutsideAnOpenFrameRecordsNothing) {
        CoreManager::GetFrameStats().BeginFrame();
        EXPECT_DOUBLE_EQ(EndFrameAndGetSubmitMs(), 0.0);
        const uint64_t frames = CoreManager::GetFrameStats().GetFrameCount();

        _renderer->DrawFilledRect(0, 0, 1, 1, kRed);
        _renderer->Flush();

        EXPECT_EQ(_renderer->calls.size(), 1u);
        EXPECT_EQ(CoreManager::GetFrameStats().GetFrameCount(), frames);
        EXPECT_DOUBLE_EQ(CoreManager::GetFrameStats().GetLast()->GetStageMs(FrameStage::Submit), 0.0);
        CoreManager::GetFrameStats().BeginFrame();
        EXPECT_DOUBLE_EQ(EndFrameAndGetSubmitMs(), 0.0);
    }

    TEST(RendererBatchTest, TheViewTransformAndTheOpacityAreAppliedOnceAndMatchTheGeometryDefault) {
        const ViewTransform view{2.0f, 5.0f, 7.0f};
        const DrawTransform transform{30.0f, 12.0f, 34.0f, true, true};
        const Rect source{1, 1, 2, 2};

        auto batched = MakeBatching();
        auto immediate = New<RecordingRenderer>();
        const IntrusivePtr<ITexture> batchedTexture = MakeTexture(*batched, 4, 4);
        const IntrusivePtr<ITexture> immediateTexture = MakeTexture(*immediate, 4, 4);
        for (RecordingRenderer* renderer : {batched.Get(), immediate.Get()}) {
            renderer->SetViewTransform(view);
            renderer->SetOpacity(0.5f);
        }
        batched->DrawTransformedTexture(*batchedTexture, &source, 10, 20, 30, 40, kTint, transform);
        batched->Flush();
        immediate->DrawTransformedTexture(*immediateTexture, &source, 10, 20, 30, 40, kTint, transform);

        const DrawCall& batch = Back(batched->calls);
        const DrawCall& geometry = Back(immediate->calls);
        ASSERT_EQ(geometry.kind, DrawKind::Geometry);
        ASSERT_EQ(batch.vertices.size(), geometry.vertices.size());
        for (size_t i = 0; i < batch.vertices.size(); ++i) {
            ExpectSameVertex(batch.vertices[i], geometry.vertices[i]);
        }
        EXPECT_EQ(batch.indices, geometry.indices);
        EXPECT_EQ(batch.vertices[0].color.a, 90) << "180 at half opacity, once";
    }

    TEST(RendererBatchTest, AnUntransformedQuadIsTheDestinationRectOfDrawTexture) {
        auto renderer = MakeBatching();
        const IntrusivePtr<ITexture> texture = MakeTexture(*renderer, 4, 4);
        renderer->SetViewTransform(ViewTransform{2.0f, 5.0f, 7.0f});
        const Rect source{0, 0, 2, 4};
        renderer->DrawTexture(*texture, &source, 10, 20, 30, 40);
        renderer->DrawTransformedTexture(*texture, &source, 10, 20, 30, 40, kWhiteTint, DrawTransform{});
        renderer->Flush();

        const DrawCall& batch = Back(renderer->calls);
        ASSERT_EQ(batch.quads, 2u);
        for (size_t quad = 0; quad < 2; ++quad) {
            const GeometryVertex& topLeft = At(batch.vertices, quad * kQuadVertices);
            const GeometryVertex& bottomRight = At(batch.vertices, quad * kQuadVertices + 3);
            EXPECT_FLOAT_EQ(topLeft.x, 25.0f);
            EXPECT_FLOAT_EQ(topLeft.y, 47.0f);
            EXPECT_FLOAT_EQ(bottomRight.x, 85.0f);
            EXPECT_FLOAT_EQ(bottomRight.y, 127.0f);
            EXPECT_FLOAT_EQ(bottomRight.u, 0.5f);
            EXPECT_FLOAT_EQ(bottomRight.v, 1.0f);
        }
    }

    TEST(RendererBatchTest, TheCapSplitsABatch) {
        auto renderer = MakeBatching();
        for (uint32_t i = 0; i <= kMaxBatchQuads; ++i) {
            renderer->DrawFilledRect(0, 0, 1, 1, kRed);
        }
        renderer->Flush();
        ASSERT_EQ(renderer->calls.size(), 2u);
        EXPECT_EQ(renderer->calls[0].quads, kMaxBatchQuads);
        EXPECT_EQ(renderer->calls[1].quads, 1u);
    }

    TEST(RendererBatchTest, TurningBatchingOffSubmitsTheOpenBatchAndDrawsImmediatelyAfter) {
        auto renderer = MakeBatching();
        renderer->DrawFilledRect(0, 0, 1, 1, kRed);
        renderer->SetBatchingEnabled(false);
        ASSERT_EQ(renderer->calls.size(), 1u);
        EXPECT_EQ(renderer->calls[0].kind, DrawKind::Batch);

        renderer->DrawFilledRect(0, 0, 1, 1, kRed);
        ASSERT_EQ(renderer->calls.size(), 2u);
        EXPECT_EQ(renderer->calls[1].kind, DrawKind::Rect);
    }

    TEST(RendererBatchTest, ATextureTheRendererCannotDrawAddsNothingAndCountsNothing) {
        auto renderer = MakeBatching();
        const IntrusivePtr<ITexture> own = MakeTexture(*renderer, 4, 4);
        const auto foreign = New<ForeignTexture>();
        renderer->DrawTexture(*own, nullptr, 0, 0, 1, 1);
        renderer->DrawTexture(*foreign, nullptr, 0, 0, 1, 1);
        renderer->DrawTransformedTexture(*foreign, nullptr, 0, 0, 1, 1, kWhiteTint, DrawTransform{45.0f, 0.0f, 0.0f});
        renderer->DrawTexture(*own, nullptr, 0, 0, 1, 1);
        renderer->Flush();

        ASSERT_EQ(renderer->calls.size(), 1u) << "the refused quads do not end the open batch";
        EXPECT_EQ(renderer->calls[0].quads, 2u);
        const RenderCounters counters = renderer->TakeRenderCounters();
        EXPECT_EQ(counters.primitives, 2u);
        EXPECT_EQ(counters.drawCalls, 1u);
    }

    TEST(RendererBatchTest, TheBatchKeepsItsTextureAliveUntilTheSubmission) {
        auto renderer = MakeBatching();
        IntrusivePtr<ITexture> texture = MakeTexture(*renderer, 4, 4);
        const ITexture* raw = texture.Get();
        renderer->DrawTexture(*texture, nullptr, 0, 0, 1, 1);
        texture.Reset();
        renderer->Flush();
        ASSERT_EQ(renderer->calls.size(), 1u);
        EXPECT_EQ(renderer->calls[0].texture, raw);
    }

    TEST(RendererBatchTest, ASteadyFrameAllocatesNothing) {
        auto renderer = MakeBatching();
        const IntrusivePtr<ITexture> a = MakeTexture(*renderer, 4, 4);
        const IntrusivePtr<ITexture> b = MakeTexture(*renderer, 4, 4);
        const DrawTransform transform{30.0f, 5.0f, 5.0f, true, false};
        auto frame = [&] {
            renderer->ClearCalls();
            for (int i = 0; i < kQuadsPerFrame; ++i) {
                renderer->DrawTexture(i < kQuadsPerFrame / 2 ? *a : *b, nullptr, static_cast<float>(i), 0, 8, 8, kTint);
                renderer->DrawTransformedTexture(*b, nullptr, 0, 0, 8, 8, kTint, transform);
                renderer->DrawFilledRect(0, 0, 4, 4, kRed);
            }
            renderer->Flush();
            renderer->TakeRenderCounters();
        };
        for (int i = 0; i < kWarmFrames; ++i) {
            frame();
        }
        ExpectNoAllocations(frame);
    }

}  // namespace BECore::Tests
