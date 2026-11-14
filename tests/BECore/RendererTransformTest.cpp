#include <BECore/Renderer/DrawTransform.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    static_assert(DrawTransform{}.IsIdentity());
    static_assert(!DrawTransform{.flipX = true}.IsIdentity());
    static_assert(sizeof(BlendMode) == 1);

    namespace {

        constexpr float kTolerance = 1e-4f;
        constexpr uint8_t kOpaque = 255;
        constexpr uint32_t kBytesPerPixel = 4;
        const Color kRed{kOpaque, 0, 0, kOpaque};
        const Color kTint{10, 20, 30, 40};
        const Vector<int> kExpectedIndices = {0, 1, 2, 2, 1, 3};

        void ExpectPoint(const Vec2& point, float x, float y) {
            EXPECT_NEAR(point.x, x, kTolerance);
            EXPECT_NEAR(point.y, y, kTolerance);
        }

        void ExpectVertex(const GeometryVertex& vertex, float x, float y, float u, float v) {
            EXPECT_NEAR(vertex.x, x, kTolerance);
            EXPECT_NEAR(vertex.y, y, kTolerance);
            EXPECT_NEAR(vertex.u, u, kTolerance);
            EXPECT_NEAR(vertex.v, v, kTolerance);
        }

        void ExpectColor(const Color& actual, const Color& expected) {
            EXPECT_EQ(actual.r, expected.r);
            EXPECT_EQ(actual.g, expected.g);
            EXPECT_EQ(actual.b, expected.b);
            EXPECT_EQ(actual.a, expected.a);
        }

        IntrusivePtr<ITexture> MakeTexture(RecordingRenderer& renderer, uint32_t width, uint32_t height) {
            const Vector<uint8_t> pixels(static_cast<size_t>(width) * height * kBytesPerPixel, kOpaque);
            return renderer.CreateTexture(pixels.data(), width, height);
        }

    }  // namespace

    TEST(RendererTransformTest, TheIdentityKeepsTheRect) {
        // std: MakeTransformedQuad returns std::array until core-type-aliases-cleanup
        const std::array<Vec2, 4> corners = MakeTransformedQuad(Rect{10.0f, 20.0f, 100.0f, 50.0f}, DrawTransform{});

        ExpectPoint(corners[0], 10.0f, 20.0f);
        ExpectPoint(corners[1], 110.0f, 20.0f);
        ExpectPoint(corners[2], 10.0f, 70.0f);
        ExpectPoint(corners[3], 110.0f, 70.0f);
    }

    TEST(RendererTransformTest, ANinetyDegreeTurnAboutTheCenterIsClockwiseOnTheScreen) {
        const DrawTransform transform{90.0f, 50.0f, 25.0f};
        // std: MakeTransformedQuad returns std::array until core-type-aliases-cleanup
        const std::array<Vec2, 4> corners = MakeTransformedQuad(Rect{0.0f, 0.0f, 100.0f, 50.0f}, transform);

        ExpectPoint(corners[0], 75.0f, -25.0f);  // the top-left corner goes to the top-right
        ExpectPoint(corners[1], 75.0f, 75.0f);
        ExpectPoint(corners[2], 25.0f, -25.0f);
        ExpectPoint(corners[3], 25.0f, 75.0f);
    }

    TEST(RendererTransformTest, AHalfTurnSwapsOppositeCorners) {
        const DrawTransform transform{180.0f, 50.0f, 25.0f};
        // std: MakeTransformedQuad returns std::array until core-type-aliases-cleanup
        const std::array<Vec2, 4> corners = MakeTransformedQuad(Rect{0.0f, 0.0f, 100.0f, 50.0f}, transform);

        ExpectPoint(corners[0], 100.0f, 50.0f);
        ExpectPoint(corners[1], 0.0f, 50.0f);
        ExpectPoint(corners[2], 100.0f, 0.0f);
        ExpectPoint(corners[3], 0.0f, 0.0f);
    }

    TEST(RendererTransformTest, ThePivotMayBeACorner) {
        const DrawTransform transform{90.0f, 0.0f, 0.0f};
        // std: MakeTransformedQuad returns std::array until core-type-aliases-cleanup
        const std::array<Vec2, 4> corners = MakeTransformedQuad(Rect{0.0f, 0.0f, 100.0f, 50.0f}, transform);

        ExpectPoint(corners[0], 0.0f, 0.0f);  // the pivot stays where it is
        ExpectPoint(corners[1], 0.0f, 100.0f);
        ExpectPoint(corners[2], -50.0f, 0.0f);
        ExpectPoint(corners[3], -50.0f, 100.0f);
    }

    TEST(RendererTransformTest, ThePivotIsAnAbsolutePointNotRelativeToTheRect) {
        const DrawTransform transform{90.0f, 0.0f, 0.0f};
        // std: MakeTransformedQuad returns std::array until core-type-aliases-cleanup
        const std::array<Vec2, 4> corners = MakeTransformedQuad(Rect{100.0f, 100.0f, 10.0f, 10.0f}, transform);

        ExpectPoint(corners[0], -100.0f, 100.0f);
        ExpectPoint(corners[3], -110.0f, 110.0f);
    }

    TEST(RendererTransformTest, IsIdentityLooksAtTheRotationAndTheFlipsOnly) {
        EXPECT_TRUE((DrawTransform{0.0f, 5.0f, 7.0f}).IsIdentity());
        EXPECT_FALSE((DrawTransform{1.0f}).IsIdentity());
        EXPECT_FALSE((DrawTransform{0.0f, 0.0f, 0.0f, true, false}).IsIdentity());
        EXPECT_FALSE((DrawTransform{0.0f, 0.0f, 0.0f, false, true}).IsIdentity());
    }

    TEST(RendererTransformTest, AnIdentityRectIsThePlainRectCall) {
        RecordingRenderer renderer;

        renderer.DrawTransformedRect(10.0f, 20.0f, 30.0f, 40.0f, kRed, DrawTransform{0.0f, 15.0f, 25.0f});

        ASSERT_EQ(renderer.calls.size(), 1u);
        EXPECT_EQ(renderer.calls[0].kind, DrawKind::Rect);
        EXPECT_FLOAT_EQ(renderer.calls[0].rect.x, 10.0f);
        EXPECT_FLOAT_EQ(renderer.calls[0].rect.h, 40.0f);
        ExpectColor(renderer.calls[0].color, kRed);
    }

    TEST(RendererTransformTest, AnIdentityTextureIsThePlainTextureCall) {
        RecordingRenderer renderer;
        const IntrusivePtr<ITexture> texture = MakeTexture(renderer, 4, 2);
        const Rect source{1.0f, 0.0f, 2.0f, 2.0f};

        renderer.DrawTransformedTexture(*texture, &source, 10.0f, 20.0f, 30.0f, 40.0f, kTint, DrawTransform{});

        ASSERT_EQ(renderer.calls.size(), 1u);
        EXPECT_EQ(renderer.calls[0].kind, DrawKind::Texture);
        EXPECT_EQ(renderer.calls[0].texture, texture.Get());
        ExpectColor(renderer.calls[0].color, kTint);
        ASSERT_TRUE(renderer.calls[0].source.has_value());
        EXPECT_FLOAT_EQ(renderer.calls[0].source->w, 2.0f);
    }

    TEST(RendererTransformTest, ARotatedRectIsOneGeometryCallInOutputPixels) {
        RecordingRenderer renderer;
        renderer.SetViewTransform(ViewTransform{2.0f, 10.0f, 20.0f});

        renderer.DrawTransformedRect(0.0f, 0.0f, 100.0f, 50.0f, kRed, DrawTransform{90.0f, 50.0f, 25.0f});

        ASSERT_EQ(renderer.calls.size(), 1u);
        const DrawCall& call = renderer.calls[0];
        EXPECT_EQ(call.kind, DrawKind::Geometry);
        EXPECT_EQ(call.texture, nullptr);
        ASSERT_EQ(call.vertices.size(), 4u);
        EXPECT_EQ(call.indices, kExpectedIndices);
        // the corners of the 90 degree test above, scaled by 2 and moved by (10, 20)
        ExpectVertex(call.vertices[0], 160.0f, -30.0f, 0.0f, 0.0f);
        ExpectVertex(call.vertices[1], 160.0f, 170.0f, 1.0f, 0.0f);
        ExpectVertex(call.vertices[2], 60.0f, -30.0f, 0.0f, 1.0f);
        ExpectVertex(call.vertices[3], 60.0f, 170.0f, 1.0f, 1.0f);
        for (const GeometryVertex& vertex : call.vertices) {
            ExpectColor(vertex.color, kRed);
        }
    }

    TEST(RendererTransformTest, ARotatedTextureCarriesTheTintAndTheWholeTextureAsUVs) {
        RecordingRenderer renderer;
        const IntrusivePtr<ITexture> texture = MakeTexture(renderer, 4, 2);

        renderer.DrawTransformedTexture(*texture, nullptr, 0.0f, 0.0f, 100.0f, 50.0f, kTint, DrawTransform{90.0f, 50.0f, 25.0f});

        ASSERT_EQ(renderer.calls.size(), 1u);
        const DrawCall& call = renderer.calls[0];
        EXPECT_EQ(call.kind, DrawKind::Geometry);
        EXPECT_EQ(call.texture, texture.Get());
        ASSERT_EQ(call.vertices.size(), 4u);
        EXPECT_EQ(call.indices, kExpectedIndices);
        ExpectVertex(call.vertices[0], 75.0f, -25.0f, 0.0f, 0.0f);
        ExpectVertex(call.vertices[3], 25.0f, 75.0f, 1.0f, 1.0f);
        for (const GeometryVertex& vertex : call.vertices) {
            ExpectColor(vertex.color, kTint);
        }
    }

    TEST(RendererTransformTest, TheSourceRectGivesTheUVs) {
        RecordingRenderer renderer;
        const IntrusivePtr<ITexture> texture = MakeTexture(renderer, 4, 2);
        const Rect source{1.0f, 0.0f, 2.0f, 1.0f};

        renderer.DrawTransformedTexture(*texture, &source, 0.0f, 0.0f, 10.0f, 10.0f, kTint, DrawTransform{45.0f, 5.0f, 5.0f});

        ASSERT_EQ(renderer.calls.size(), 1u);
        const DrawCall& call = renderer.calls[0];
        ASSERT_EQ(call.vertices.size(), 4u);
        EXPECT_NEAR(call.vertices[0].u, 0.25f, kTolerance);
        EXPECT_NEAR(call.vertices[0].v, 0.0f, kTolerance);
        EXPECT_NEAR(call.vertices[3].u, 0.75f, kTolerance);
        EXPECT_NEAR(call.vertices[3].v, 0.5f, kTolerance);
    }

    TEST(RendererTransformTest, AnEmptySourceRectMeansTheWholeTexture) {
        RecordingRenderer renderer;
        const IntrusivePtr<ITexture> texture = MakeTexture(renderer, 4, 2);
        const Rect source{};

        renderer.DrawTransformedTexture(*texture, &source, 0.0f, 0.0f, 10.0f, 10.0f, kTint, DrawTransform{45.0f, 5.0f, 5.0f});

        ASSERT_EQ(renderer.calls.size(), 1u);
        EXPECT_NEAR(renderer.calls[0].vertices[0].u, 0.0f, kTolerance);
        EXPECT_NEAR(renderer.calls[0].vertices[3].u, 1.0f, kTolerance);
        EXPECT_NEAR(renderer.calls[0].vertices[3].v, 1.0f, kTolerance);
    }

    TEST(RendererTransformTest, AFlipSwapsTheUVsAndLeavesTheCorners) {
        RecordingRenderer renderer;
        const IntrusivePtr<ITexture> texture = MakeTexture(renderer, 4, 2);

        renderer.DrawTransformedTexture(*texture, nullptr, 10.0f, 20.0f, 30.0f, 40.0f, kTint, DrawTransform{0.0f, 0.0f, 0.0f, true, false});
        renderer.DrawTransformedTexture(*texture, nullptr, 10.0f, 20.0f, 30.0f, 40.0f, kTint, DrawTransform{0.0f, 0.0f, 0.0f, false, true});

        ASSERT_EQ(renderer.calls.size(), 2u);
        // flip X: the left corners take the right edge of the picture
        ExpectVertex(renderer.calls[0].vertices[0], 10.0f, 20.0f, 1.0f, 0.0f);
        ExpectVertex(renderer.calls[0].vertices[1], 40.0f, 20.0f, 0.0f, 0.0f);
        ExpectVertex(renderer.calls[0].vertices[2], 10.0f, 60.0f, 1.0f, 1.0f);
        ExpectVertex(renderer.calls[0].vertices[3], 40.0f, 60.0f, 0.0f, 1.0f);
        // flip Y: the top corners take the bottom edge
        ExpectVertex(renderer.calls[1].vertices[0], 10.0f, 20.0f, 0.0f, 1.0f);
        ExpectVertex(renderer.calls[1].vertices[3], 40.0f, 60.0f, 1.0f, 0.0f);
    }

    TEST(RendererTransformTest, AFlipSwapsTheUVsOfTheSourceRect) {
        RecordingRenderer renderer;
        const IntrusivePtr<ITexture> texture = MakeTexture(renderer, 4, 2);
        const Rect source{1.0f, 0.0f, 2.0f, 2.0f};

        renderer.DrawTransformedTexture(*texture, &source, 0.0f, 0.0f, 10.0f, 10.0f, kTint, DrawTransform{0.0f, 0.0f, 0.0f, true, true});

        ASSERT_EQ(renderer.calls.size(), 1u);
        const DrawCall& call = renderer.calls[0];
        EXPECT_NEAR(call.vertices[0].u, 0.75f, kTolerance);
        EXPECT_NEAR(call.vertices[0].v, 1.0f, kTolerance);
        EXPECT_NEAR(call.vertices[3].u, 0.25f, kTolerance);
        EXPECT_NEAR(call.vertices[3].v, 0.0f, kTolerance);
    }

    TEST(RendererTransformTest, ATextureWithoutSizeDrawsNothing) {
        RecordingRenderer renderer;
        const IntrusivePtr<ITexture> texture = New<RecordedTexture>(Vector<uint8_t>{}, 0u, 0u);

        renderer.DrawTransformedTexture(*texture, nullptr, 0.0f, 0.0f, 10.0f, 10.0f, kTint, DrawTransform{45.0f, 5.0f, 5.0f});

        EXPECT_TRUE(renderer.calls.empty());
    }

    TEST(RendererTransformTest, TheBlendModeIsAlphaByDefaultAndIsOnEveryRecordedCall) {
        RecordingRenderer renderer;
        const IntrusivePtr<ITexture> texture = MakeTexture(renderer, 2, 2);
        EXPECT_EQ(renderer.GetBlendMode(), BlendMode::Alpha);

        renderer.DrawFilledRect(0.0f, 0.0f, 1.0f, 1.0f, kRed);
        renderer.SetBlendMode(BlendMode::Add);
        renderer.DrawFilledRect(0.0f, 0.0f, 1.0f, 1.0f, kRed);
        renderer.DrawTexture(*texture, nullptr, 0.0f, 0.0f, 1.0f, 1.0f);
        renderer.DrawTransformedRect(0.0f, 0.0f, 10.0f, 10.0f, kRed, DrawTransform{30.0f, 5.0f, 5.0f});
        renderer.SetBlendMode(BlendMode::Multiply);
        renderer.DrawTransformedTexture(*texture, nullptr, 0.0f, 0.0f, 10.0f, 10.0f, kTint, DrawTransform{30.0f, 5.0f, 5.0f});

        ASSERT_EQ(renderer.calls.size(), 5u);
        EXPECT_EQ(renderer.calls[0].blend, BlendMode::Alpha);
        EXPECT_EQ(renderer.calls[1].blend, BlendMode::Add);
        EXPECT_EQ(renderer.calls[2].blend, BlendMode::Add);
        EXPECT_EQ(renderer.calls[3].blend, BlendMode::Add);
        EXPECT_EQ(renderer.calls[4].blend, BlendMode::Multiply);
    }

    TEST(RendererTransformTest, ScopedBlendModeRestoresThePreviousMode) {
        RecordingRenderer renderer;
        renderer.SetBlendMode(BlendMode::Multiply);

        {
            ScopedBlendMode outer(renderer, BlendMode::Add);
            EXPECT_EQ(renderer.GetBlendMode(), BlendMode::Add);
            {
                ScopedBlendMode inner(renderer, BlendMode::Alpha);
                EXPECT_EQ(renderer.GetBlendMode(), BlendMode::Alpha);
            }
            EXPECT_EQ(renderer.GetBlendMode(), BlendMode::Add);
        }

        EXPECT_EQ(renderer.GetBlendMode(), BlendMode::Multiply);
    }

    TEST(RendererTransformTest, TheClipHidesATransformedDraw) {
        RecordingRenderer renderer;
        const IntrusivePtr<ITexture> texture = MakeTexture(renderer, 2, 2);
        renderer.PushClipRect({0.0f, 0.0f, 10.0f, 10.0f});
        renderer.PushClipRect({100.0f, 100.0f, 10.0f, 10.0f});

        renderer.DrawTransformedRect(0.0f, 0.0f, 10.0f, 10.0f, kRed, DrawTransform{30.0f, 5.0f, 5.0f});
        renderer.DrawTransformedTexture(*texture, nullptr, 0.0f, 0.0f, 10.0f, 10.0f, kTint, DrawTransform{30.0f, 5.0f, 5.0f});

        EXPECT_TRUE(renderer.calls.empty());
        renderer.PopClipRect();
        renderer.PopClipRect();
    }

    TEST(RendererTransformTest, ATransformedDrawCarriesTheClip) {
        RecordingRenderer renderer;
        renderer.PushClipRect({0.0f, 0.0f, 50.0f, 50.0f});

        renderer.DrawTransformedRect(0.0f, 0.0f, 10.0f, 10.0f, kRed, DrawTransform{30.0f, 5.0f, 5.0f});

        ASSERT_EQ(renderer.calls.size(), 1u);
        ASSERT_TRUE(renderer.calls[0].clip.has_value());
        EXPECT_FLOAT_EQ(renderer.calls[0].clip->w, 50.0f);
        renderer.PopClipRect();
    }

    TEST(RendererTransformTest, CompileTimeChecks) {}

}  // namespace BECore::Tests
