#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/DrawTransform.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Components/ClipRectComponent.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/SceneDrawSystemTest.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // The output of the RecordingRenderer; a scene without a view size draws one unit to one pixel
        constexpr float kOutputWidth = 800.0f;
        constexpr float kOutputHeight = 600.0f;
        constexpr float kSide = 10.0f;
        constexpr float kPastTheMargin = 5.0f;
        constexpr float kFarAway = 5000.0f;

        bool MeetsOutput(const Array<Vec2, 4>& corners) {
            float left = corners[0].x;
            float top = corners[0].y;
            float right = left;
            float bottom = top;
            for (const Vec2& corner : corners) {
                left = std::min(left, corner.x);
                top = std::min(top, corner.y);
                right = std::max(right, corner.x);
                bottom = std::max(bottom, corner.y);
            }
            return right > 0.0f && left < kOutputWidth && bottom > 0.0f && top < kOutputHeight;
        }

    }  // namespace

    class SceneDrawCullingTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _renderer = BECore::New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);
            _scene = BECore::New<Scene>();
            _scene->Initialize();
            _texture = MakeTexture(*_renderer);
        }

        void TearDown() override {
            _scene.Reset();
            _texture.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        IntrusivePtr<TransformComponent> AddNode(Scene& scene, float x, float y, float w, float h, SceneNode* parent = nullptr) {
            auto node = scene.CreateNode("Node"_intern, parent);
            auto transform = BECore::New<TransformComponent>();
            transform->_x = x;
            transform->_y = y;
            transform->_width = w;
            transform->_height = h;
            node->AddComponent(transform);
            return transform;
        }

        IntrusivePtr<TransformComponent> AddQuad(float x, float y, float w = kSide, float h = kSide) {
            auto transform = AddNode(*_scene, x, y, w, h);
            transform->GetNode().AddComponent(BECore::New<QuadRendererComponent>());
            return transform;
        }

        IntrusivePtr<TransformComponent> AddSprite(Scene& scene, float x, float y, float w = kSide, float h = kSide, SceneNode* parent = nullptr) {
            auto transform = AddNode(scene, x, y, w, h, parent);
            auto sprite = BECore::New<SpriteRendererComponent>();
            sprite->SetTexture(_texture);
            transform->GetNode().AddComponent(sprite);
            return transform;
        }

        size_t DrawCalls(Scene& scene) {
            _renderer->ClearCalls();
            scene.Draw();
            return _renderer->calls.size();
        }

        size_t DrawCalls() {
            return DrawCalls(*_scene);
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtr<Scene> _scene;
        IntrusivePtr<ITexture> _texture;
    };

    TEST_F(SceneDrawCullingTest, AQuadOrASpriteBeyondAnySideOfTheOutputIsNotDrawn) {
        const Vec2 beyond[] = {{-kSide - kPastTheMargin, 100.0f}, {kOutputWidth + kPastTheMargin, 100.0f}, {100.0f, -kSide - kPastTheMargin}, {100.0f, kOutputHeight + kPastTheMargin}};
        for (const Vec2& place : beyond) {
            AddQuad(place.x, place.y);
            AddSprite(*_scene, place.x, place.y);
        }

        EXPECT_EQ(DrawCalls(), 0u);
        EXPECT_EQ(_renderer->TakeRenderCounters().primitives, 0u);
    }

    TEST_F(SceneDrawCullingTest, WhatReachesTheOutputInPartIsDrawn) {
        const Vec2 straddling[] = {{1.0f - kSide, 100.0f}, {kOutputWidth - 1.0f, 100.0f}, {100.0f, 1.0f - kSide}, {100.0f, kOutputHeight - 1.0f}};
        for (const Vec2& place : straddling) {
            AddQuad(place.x, place.y);
            AddSprite(*_scene, place.x, place.y);
        }
        AddQuad(-kSide, -kSide, 2.0f * kOutputWidth, 2.0f * kOutputHeight);  // the output lies inside it

        EXPECT_EQ(DrawCalls(), 2 * std::size(straddling) + 1);
    }

    TEST_F(SceneDrawCullingTest, ANegativeSizeSpansFromTheOtherEnd) {
        AddQuad(kOutputWidth + kPastTheMargin, kOutputHeight + kPastTheMargin, -2.0f * kPastTheMargin - kSide, -2.0f * kPastTheMargin - kSide);
        EXPECT_EQ(DrawCalls(), 1u) << "it starts outside and ends inside";

        AddQuad(-kPastTheMargin, 100.0f, -kSide, kSide);
        EXPECT_EQ(DrawCalls(), 1u) << "all of it is left of the output";
    }

    TEST_F(SceneDrawCullingTest, ATurnThatBringsARectOntoTheOutputDrawsIt) {
        constexpr float kHalfTurn = 180.0f;
        auto aboutItsEdge = AddQuad(-110.0f, 300.0f, 100.0f, kSide);
        aboutItsEdge->_pivotX = 1.0f;
        auto aboutAFarPoint = AddSprite(*_scene, -300.0f, 100.0f);
        aboutAFarPoint->_pivotX = 31.0f;  // at x = 10
        ASSERT_EQ(DrawCalls(), 0u);

        aboutItsEdge->_rotation = kHalfTurn;
        aboutAFarPoint->_rotation = kHalfTurn;

        EXPECT_EQ(DrawCalls(), 2u);
    }

    TEST_F(SceneDrawCullingTest, NoTurnedQuadThatMeetsTheOutputIsCulled) {
        auto transform = AddQuad(0.0f, 0.0f, 120.0f, 40.0f);
        const Vec2 pivots[] = {{0.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 1.0f}, {-2.0f, 3.0f}};
        size_t met = 0;
        for (const Vec2& pivot : pivots) {
            for (float rotation = 15.0f; rotation < 360.0f; rotation += 30.0f) {
                for (float x = -400.0f; x <= kOutputWidth + 400.0f; x += 85.0f) {
                    for (float y = -400.0f; y <= kOutputHeight + 400.0f; y += 85.0f) {
                        transform->_x = x;
                        transform->_y = y;
                        transform->_pivotX = pivot.x;
                        transform->_pivotY = pivot.y;
                        transform->_rotation = rotation;
                        const DrawTransform turn{rotation, x + pivot.x * transform->_width, y + pivot.y * transform->_height, false, false};
                        if (MeetsOutput(MakeTransformedQuad(Rect{x, y, transform->_width, transform->_height}, turn))) {
                            ++met;
                            ASSERT_EQ(DrawCalls(), 1u) << "at " << x << ", " << y << " turned " << rotation << " about " << pivot.x << ", " << pivot.y;
                        }
                    }
                }
            }
        }
        EXPECT_GT(met, 0u);
    }

    TEST_F(SceneDrawCullingTest, ASpriteInALetterboxBarIsDrawn) {
        // A view of 400 x 200 in 800 x 600: twice the size, with bars of 100 pixels (50 units) above and below it
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(R"(<SceneManager><scenes><scene type="Scene" name="Bars" viewWidth="400" viewHeight="200"/></scenes></SceneManager>)"));
        auto manager = BECore::New<SceneManager>();
        manager->Deserialize(deserializer);
        ASSERT_FALSE(deserializer.HasErrors());
        manager->Initialize();
        Scene& scene = Require(manager->GetSceneByName("Bars"_intern));
        AddSprite(scene, 100.0f, -40.0f);
        AddSprite(scene, 100.0f, 230.0f);
        AddSprite(scene, 100.0f, -80.0f);  // above the output

        EXPECT_EQ(DrawCalls(scene), 2u);
    }

    TEST_F(SceneDrawCullingTest, AnOutputWithoutASizeCullsNothing) {
        _renderer->width = 0.0f;
        _renderer->height = 0.0f;
        AddQuad(kFarAway, kFarAway);
        AddSprite(*_scene, -kFarAway, -kFarAway);

        EXPECT_EQ(DrawCalls(), 2u);
    }

    TEST_F(SceneDrawCullingTest, ARectWithANanIsNotCulled) {
        constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
        AddQuad(kNan, 100.0f);
        AddQuad(100.0f, 100.0f, kNan, kSide);
        AddQuad(100.0f, kNan)->_rotation = 30.0f;

        EXPECT_EQ(DrawCalls(), 3u);
    }

    TEST_F(SceneDrawCullingTest, ACustomDrawIsNotCulled) {
        auto transform = AddNode(*_scene, kFarAway, kFarAway, kSide, kSide);
        auto marker = BECore::New<DrawMarkerComponent>();
        marker->marker = kFarAway;
        transform->GetNode().AddComponent(marker);

        EXPECT_EQ(DrawCalls(), 1u);
    }

    TEST_F(SceneDrawCullingTest, AClipSubtreeOffTheOutputLeavesItsNeighboursInOneBatch) {
        _renderer->SetBatchingEnabled(true);
        AddSprite(*_scene, 0.0f, 0.0f);
        auto clip = AddNode(*_scene, kFarAway, 0.0f, kSide, kSide);
        clip->GetNode().AddComponent(BECore::New<ClipRectComponent>());
        AddSprite(*_scene, kFarAway, 0.0f, kSide, kSide, &clip->GetNode());
        AddSprite(*_scene, 20.0f, 0.0f);

        ASSERT_EQ(DrawCalls(), 1u);
        EXPECT_EQ(At(_renderer->calls, 0).quads, 2u);
        EXPECT_FALSE(At(_renderer->calls, 0).clip.has_value());
    }

    TEST_F(SceneDrawCullingTest, ANodeMovedBackIsDrawnInTheNextFrameWithTheSameList) {
        auto transform = AddQuad(kFarAway, 100.0f);
        ASSERT_EQ(DrawCalls(), 0u);
        const uint32_t builds = _scene->GetDrawListBuilds();

        transform->_x = 100.0f;

        EXPECT_EQ(DrawCalls(), 1u);
        EXPECT_EQ(_scene->GetDrawListBuilds(), builds);
    }

}  // namespace BECore::Tests
