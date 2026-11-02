#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr uint8_t kOpaque = 255;
        const Color kWhite{kOpaque, kOpaque, kOpaque, kOpaque};
        const Color kRed{kOpaque, 0, 0, kOpaque};
        constexpr uint32_t kBytesPerPixel = 4;

        IntrusivePtr<ITexture> MakeTexture(RecordingRenderer& renderer, uint32_t width, uint32_t height) {
            const eastl::vector<uint8_t> pixels(static_cast<size_t>(width) * height * kBytesPerPixel, kOpaque);
            return renderer.CreateTexture(pixels.data(), width, height);
        }

    }  // namespace

    TEST(RendererClipTest, NoClipByDefault) {
        RecordingRenderer renderer;

        EXPECT_FALSE(renderer.GetClipRect().has_value());
        EXPECT_FALSE(renderer.IsClipEmpty());
        EXPECT_EQ(renderer.GetClipDepth(), 0u);
    }

    TEST(RendererClipTest, PushSetsTheClipAndPopRestoresIt) {
        RecordingRenderer renderer;

        renderer.PushClipRect({10.0f, 20.0f, 100.0f, 50.0f});
        ASSERT_TRUE(renderer.GetClipRect().has_value());
        EXPECT_FLOAT_EQ(renderer.GetClipRect()->x, 10.0f);
        EXPECT_FLOAT_EQ(renderer.GetClipRect()->w, 100.0f);
        EXPECT_EQ(renderer.GetClipDepth(), 1u);

        renderer.PopClipRect();
        EXPECT_FALSE(renderer.GetClipRect().has_value());
        EXPECT_EQ(renderer.GetClipDepth(), 0u);
    }

    TEST(RendererClipTest, NestedRectsIntersect) {
        RecordingRenderer renderer;

        renderer.PushClipRect({0.0f, 0.0f, 100.0f, 100.0f});
        renderer.PushClipRect({60.0f, 40.0f, 100.0f, 100.0f});

        const Rect inner = *renderer.GetClipRect();
        EXPECT_FLOAT_EQ(inner.x, 60.0f);
        EXPECT_FLOAT_EQ(inner.y, 40.0f);
        EXPECT_FLOAT_EQ(inner.w, 40.0f);
        EXPECT_FLOAT_EQ(inner.h, 60.0f);

        renderer.PopClipRect();
        const Rect outer = *renderer.GetClipRect();
        EXPECT_FLOAT_EQ(outer.w, 100.0f);
        EXPECT_FLOAT_EQ(outer.h, 100.0f);
        renderer.PopClipRect();
    }

    TEST(RendererClipTest, ARectInsideTheOuterOneStaysAsItIs) {
        RecordingRenderer renderer;

        renderer.PushClipRect({0.0f, 0.0f, 100.0f, 100.0f});
        renderer.PushClipRect({10.0f, 10.0f, 20.0f, 30.0f});

        const Rect inner = *renderer.GetClipRect();
        EXPECT_FLOAT_EQ(inner.x, 10.0f);
        EXPECT_FLOAT_EQ(inner.y, 10.0f);
        EXPECT_FLOAT_EQ(inner.w, 20.0f);
        EXPECT_FLOAT_EQ(inner.h, 30.0f);
    }

    TEST(RendererClipTest, DisjointRectsLeaveAnEmptyClipThatHidesEverything) {
        RecordingRenderer renderer;
        renderer.PushClipRect({0.0f, 0.0f, 10.0f, 10.0f});
        renderer.PushClipRect({100.0f, 100.0f, 10.0f, 10.0f});

        EXPECT_TRUE(renderer.IsClipEmpty());
        renderer.DrawFilledRect(0.0f, 0.0f, 500.0f, 500.0f, kRed);
        EXPECT_TRUE(renderer.calls.empty());

        renderer.PopClipRect();
        EXPECT_FALSE(renderer.IsClipEmpty());
        renderer.DrawFilledRect(0.0f, 0.0f, 500.0f, 500.0f, kRed);
        EXPECT_EQ(renderer.calls.size(), 1u);
    }

    TEST(RendererClipTest, ANegativeSizeIsAnEmptyClip) {
        RecordingRenderer renderer;

        renderer.PushClipRect({10.0f, 10.0f, -5.0f, 20.0f});

        EXPECT_TRUE(renderer.IsClipEmpty());
    }

    TEST(RendererClipTest, TheDrawCallsCarryTheClipInEffect) {
        RecordingRenderer renderer;

        renderer.DrawFilledRect(0.0f, 0.0f, 10.0f, 10.0f, kRed);
        renderer.PushClipRect({5.0f, 5.0f, 50.0f, 50.0f});
        renderer.DrawFilledRect(0.0f, 0.0f, 10.0f, 10.0f, kRed);
        renderer.PopClipRect();

        ASSERT_EQ(renderer.calls.size(), 2u);
        EXPECT_FALSE(renderer.calls[0].clip.has_value());
        ASSERT_TRUE(renderer.calls[1].clip.has_value());
        EXPECT_FLOAT_EQ(renderer.calls[1].clip->x, 5.0f);
    }

    TEST(RendererClipTest, TheClipIsInOutputPixelsAndIgnoresTheViewTransform) {
        RecordingRenderer renderer;
        renderer.SetViewTransform({2.0f, 10.0f, 0.0f});

        renderer.PushClipRect({100.0f, 0.0f, 50.0f, 50.0f});
        renderer.DrawFilledRect(0.0f, 0.0f, 10.0f, 10.0f, kRed);

        ASSERT_EQ(renderer.calls.size(), 1u);
        EXPECT_FLOAT_EQ(renderer.calls[0].clip->x, 100.0f);  // not 2 * 100 + 10
        EXPECT_FLOAT_EQ(renderer.calls[0].rect.x, 10.0f);    // the rect is mapped, the clip is not
        EXPECT_FLOAT_EQ(renderer.calls[0].rect.w, 20.0f);
    }

    TEST(RendererTextureTest, TheUntintedOverloadDrawsWithWhite) {
        RecordingRenderer renderer;
        auto texture = MakeTexture(renderer, 4, 4);
        ASSERT_TRUE(texture);

        IRenderer& base = renderer;
        base.DrawTexture(*texture, nullptr, 1.0f, 2.0f, 3.0f, 4.0f);

        ASSERT_EQ(renderer.calls.size(), 1u);
        EXPECT_EQ(renderer.calls[0].kind, DrawKind::Texture);
        EXPECT_EQ(renderer.calls[0].color.r, kOpaque);
        EXPECT_EQ(renderer.calls[0].color.g, kOpaque);
        EXPECT_EQ(renderer.calls[0].color.b, kOpaque);
        EXPECT_EQ(renderer.calls[0].color.a, kOpaque);
        EXPECT_FALSE(renderer.calls[0].source.has_value());
        EXPECT_FLOAT_EQ(renderer.calls[0].rect.w, 3.0f);
    }

    TEST(RendererTextureTest, TheTintReachesTheBackend) {
        RecordingRenderer renderer;
        auto texture = MakeTexture(renderer, 4, 4);
        ASSERT_TRUE(texture);
        const Rect source{0.0f, 0.0f, 2.0f, 2.0f};

        renderer.DrawTexture(*texture, &source, 0.0f, 0.0f, 8.0f, 8.0f, Color{10, 20, 30, 40});

        ASSERT_EQ(renderer.calls.size(), 1u);
        EXPECT_EQ(renderer.calls[0].color.r, 10);
        EXPECT_EQ(renderer.calls[0].color.g, 20);
        EXPECT_EQ(renderer.calls[0].color.b, 30);
        EXPECT_EQ(renderer.calls[0].color.a, 40);
        ASSERT_TRUE(renderer.calls[0].source.has_value());
        EXPECT_FLOAT_EQ(renderer.calls[0].source->w, 2.0f);
    }

    TEST(RendererTextureTest, CreateTextureKeepsTheSizeAndThePixels) {
        RecordingRenderer renderer;
        const eastl::vector<uint8_t> pixels = {1, 2, 3, 4, 5, 6, 7, 8};

        auto texture = renderer.CreateTexture(pixels.data(), 2, 1);

        ASSERT_TRUE(texture);
        EXPECT_FLOAT_EQ(texture->GetWidth(), 2.0f);
        EXPECT_FLOAT_EQ(texture->GetHeight(), 1.0f);
        EXPECT_EQ(texture->GetState(), ResourceState::Loaded);
        const auto* recorded = static_cast<const RecordedTexture*>(texture.Get());
        EXPECT_EQ(recorded->GetPixels(), pixels);
    }

    TEST(RendererTextureTest, CreateTextureRefusesAnEmptyImage) {
        RecordingRenderer renderer;
        const uint8_t pixel[kBytesPerPixel] = {};

        EXPECT_FALSE(renderer.CreateTexture(pixel, 0, 1));
        EXPECT_FALSE(renderer.CreateTexture(pixel, 1, 0));
        EXPECT_FALSE(renderer.CreateTexture(nullptr, 1, 1));
    }

    namespace {

        /// A quad of 10 by 20 at the origin, two triangles
        const eastl::vector<GeometryVertex> kQuad = {
            {0.0f, 0.0f, kRed, 0.0f, 0.0f},
            {10.0f, 0.0f, kRed, 1.0f, 0.0f},
            {10.0f, 20.0f, kRed, 1.0f, 1.0f},
            {0.0f, 20.0f, kRed, 0.0f, 1.0f},
        };
        const eastl::vector<int> kQuadIndices = {0, 1, 2, 0, 2, 3};

    }  // namespace

    TEST(RendererGeometryTest, TheTranslationAndTheViewTransformMoveEveryVertex) {
        RecordingRenderer renderer;
        renderer.SetViewTransform({2.0f, 100.0f, 50.0f});

        renderer.DrawGeometry(nullptr, {kQuad.data(), kQuad.size()}, {kQuadIndices.data(), kQuadIndices.size()}, 5.0f, 1.0f);

        ASSERT_EQ(renderer.calls.size(), 1u);
        const DrawCall& call = renderer.calls[0];
        EXPECT_EQ(call.kind, DrawKind::Geometry);
        ASSERT_EQ(call.vertices.size(), 4u);
        EXPECT_FLOAT_EQ(call.vertices[0].x, (0.0f + 5.0f) * 2.0f + 100.0f);
        EXPECT_FLOAT_EQ(call.vertices[0].y, (0.0f + 1.0f) * 2.0f + 50.0f);
        EXPECT_FLOAT_EQ(call.vertices[2].x, (10.0f + 5.0f) * 2.0f + 100.0f);
        EXPECT_FLOAT_EQ(call.vertices[2].y, (20.0f + 1.0f) * 2.0f + 50.0f);
        EXPECT_FLOAT_EQ(call.vertices[2].u, 1.0f);  // the texture coordinates are not mapped
        EXPECT_FLOAT_EQ(call.rect.x, 110.0f);
        EXPECT_FLOAT_EQ(call.rect.w, 20.0f);
        EXPECT_FLOAT_EQ(call.rect.h, 40.0f);
        EXPECT_EQ(call.indices, kQuadIndices);
    }

    TEST(RendererGeometryTest, TheDrawCarriesTheTextureAndTheClip) {
        RecordingRenderer renderer;
        auto texture = MakeTexture(renderer, 4, 4);
        ASSERT_TRUE(texture);

        renderer.PushClipRect({1.0f, 2.0f, 30.0f, 40.0f});
        renderer.DrawGeometry(texture.Get(), {kQuad.data(), kQuad.size()}, {kQuadIndices.data(), kQuadIndices.size()}, 0.0f, 0.0f);
        renderer.PopClipRect();

        ASSERT_EQ(renderer.calls.size(), 1u);
        EXPECT_EQ(renderer.calls[0].texture, texture.Get());
        ASSERT_TRUE(renderer.calls[0].clip.has_value());
        EXPECT_FLOAT_EQ(renderer.calls[0].clip->w, 30.0f);
    }

    TEST(RendererGeometryTest, AnEmptyClipHidesTheDraw) {
        RecordingRenderer renderer;
        renderer.PushClipRect({0.0f, 0.0f, 10.0f, 10.0f});
        renderer.PushClipRect({50.0f, 50.0f, 10.0f, 10.0f});

        renderer.DrawGeometry(nullptr, {kQuad.data(), kQuad.size()}, {kQuadIndices.data(), kQuadIndices.size()}, 0.0f, 0.0f);

        EXPECT_TRUE(renderer.calls.empty());
    }

    namespace {

        /// A renderer that keeps the default DrawGeometry, like a backend without the primitive
        class NoGeometryRenderer : public RecordingRenderer {
        public:
            void DrawGeometry(ITexture* texture, eastl::span<const GeometryVertex> vertices, eastl::span<const int> indices, float translateX, float translateY) override {
                IRenderer::DrawGeometry(texture, vertices, indices, translateX, translateY);
            }
        };

    }  // namespace

    TEST(RendererGeometryTest, TheDefaultDrawsNothingAndDoesNotCrash) {
        NoGeometryRenderer renderer;

        renderer.DrawGeometry(nullptr, {kQuad.data(), kQuad.size()}, {kQuadIndices.data(), kQuadIndices.size()}, 0.0f, 0.0f);
        renderer.DrawGeometry(nullptr, {kQuad.data(), kQuad.size()}, {kQuadIndices.data(), kQuadIndices.size()}, 0.0f, 0.0f);

        EXPECT_TRUE(renderer.calls.empty());
    }

    namespace {

        constexpr const char* kProbeSceneXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Probe" viewWidth="1000" viewHeight="600"><nodes>
    <node name="Probe"><components>
      <component type="TransformComponent" x="10" y="20" width="1" height="1"/>
      <component type="RenderProbeComponent"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

    }  // namespace

    /// The debug component that exercises every primitive, drawn into a recording renderer
    class RenderProbeTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _renderer = New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kProbeSceneXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(RenderProbeTest, TheClipIsBalancedAfterADraw) {
        _manager->DrawAll();

        EXPECT_EQ(_renderer->GetClipDepth(), 0u);
        EXPECT_FALSE(_renderer->IsClipEmpty());
    }

    TEST_F(RenderProbeTest, ItDrawsTintedAndClippedCalls) {
        _manager->DrawAll();

        size_t textures = 0;
        size_t tinted = 0;
        size_t faded = 0;
        size_t clipped = 0;
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Texture) {
                ++textures;
                tinted += call.color.g != 255 ? 1 : 0;
                faded += call.color.a != 255 ? 1 : 0;
            }
            clipped += call.clip.has_value() ? 1 : 0;
        }
        EXPECT_EQ(_renderer->texturesCreated, 1u);
        EXPECT_EQ(textures, 4u);
        EXPECT_EQ(tinted, 1u);
        EXPECT_EQ(faded, 1u);
        EXPECT_GT(clipped, 0u);
    }

    TEST_F(RenderProbeTest, TheClipsAreTheCellRectsMappedToOutputPixels) {
        _manager->DrawAll();

        // The 1000 x 600 view fits an 800 x 600 output at scale 0.8 with bars above and below (offset y = 60).
        // Cell 4 starts at x = 10 + 4 * 120 and is 100 units wide; the inner clip is inset by 25 units.
        eastl::vector<Rect> clips;
        for (const DrawCall& call : _renderer->calls) {
            if (call.clip) {
                clips.push_back(*call.clip);
            }
        }
        ASSERT_EQ(clips.size(), 3u);  // the outer square, the inner square and the texture
        EXPECT_FLOAT_EQ(clips[0].x, 392.0f);
        EXPECT_FLOAT_EQ(clips[0].y, 76.0f);
        EXPECT_FLOAT_EQ(clips[0].w, 80.0f);
        EXPECT_FLOAT_EQ(clips[0].h, 80.0f);
        EXPECT_FLOAT_EQ(clips[1].x, 412.0f);
        EXPECT_FLOAT_EQ(clips[1].y, 96.0f);
        EXPECT_FLOAT_EQ(clips[1].w, 40.0f);
        EXPECT_FLOAT_EQ(clips[1].h, 40.0f);
        EXPECT_FLOAT_EQ(clips[2].x, clips[1].x);
    }

    TEST_F(RenderProbeTest, ADrawAfterADrawDoesNotCreateAnotherTexture) {
        _manager->DrawAll();
        _manager->DrawAll();

        EXPECT_EQ(_renderer->texturesCreated, 1u);
    }

}  // namespace BECore::Tests
