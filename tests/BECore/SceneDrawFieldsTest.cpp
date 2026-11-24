#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr uint32_t kTextureWidth = 4;
        constexpr uint32_t kTextureHeight = 8;
        constexpr Color kGreen{0, 255, 0, 255};

        bool IsGreen(const Color& color) {
            return color.r == kGreen.r && color.g == kGreen.g && color.b == kGreen.b && color.a == kGreen.a;
        }

        constexpr float kTolerance = 0.001f;

        /// Adds a node with a transform at x = @p x and, when @p withQuad is set, a quad, under the root.
        IntrusivePtr<SceneNode> AddNode(Scene& scene, const char* name, float x, bool withQuad) {
            auto node = scene.CreateNode(PoolString::Intern(name), nullptr);
            auto transform = BECore::New<TransformComponent>();
            transform->_x = x;
            transform->_y = x + 1.0f;
            transform->_width = x + 2.0f;
            transform->_height = x + 3.0f;
            node->AddComponent(transform);
            if (withQuad) {
                node->AddComponent(BECore::New<QuadRendererComponent>());
            }
            return node;
        }

    }  // namespace

    /// What a draw reads from the components at draw time: a change of a field shows in the next draw, with no event or setter hook.
    class SceneDrawFieldsTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _renderer = BECore::New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);
            _scene = BECore::New<Scene>();
            _scene->Initialize();
        }

        void TearDown() override {
            _scene.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        /// Draws once and returns the calls of that draw.
        const Vector<DrawCall>& Draw() {
            _renderer->calls.clear();
            _scene->Draw();
            return _renderer->calls;
        }

        IntrusivePtr<ITexture> MakeTexture() {
            const Vector<uint8_t> pixels(static_cast<size_t>(kTextureWidth) * kTextureHeight * 4, 255);
            return _renderer->CreateTexture(pixels.data(), kTextureWidth, kTextureHeight);
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtr<Scene> _scene;
    };

    TEST_F(SceneDrawFieldsTest, AFieldChangedBetweenTwoDrawsShowsInTheSecond) {
        auto node = AddNode(*_scene, "Quad", 1.0f, true);
        {
            const Vector<DrawCall>& calls = Draw();
            ASSERT_EQ(calls.size(), 1u);
            EXPECT_EQ(At(calls, 0).rect.x, 1.0f);
            EXPECT_FALSE(IsGreen(At(calls, 0).color));
        }

        Require(node->GetComponent<TransformComponent>())._x = 40.0f;
        Require(node->GetComponent<QuadRendererComponent>())._color = kGreen;

        const Vector<DrawCall>& calls = Draw();
        ASSERT_EQ(calls.size(), 1u);
        EXPECT_EQ(At(calls, 0).rect.x, 40.0f);
        EXPECT_TRUE(IsGreen(At(calls, 0).color));
    }

    TEST_F(SceneDrawFieldsTest, ARotationSetBetweenTwoDrawsTurnsTheQuad) {
        auto node = AddNode(*_scene, "Quad", 5.0f, true);
        EXPECT_EQ(At(Draw(), 0).kind, DrawKind::Rect);

        Require(node->GetComponent<TransformComponent>())._rotation = 30.0f;

        EXPECT_EQ(At(Draw(), 0).kind, DrawKind::Geometry);
    }

    TEST_F(SceneDrawFieldsTest, TheBlendModeOfAQuadShowsInTheNextDrawOnlyForThatDraw) {
        auto node = AddNode(*_scene, "Quad", 1.0f, true);
        EXPECT_EQ(At(Draw(), 0).blend, BlendMode::Alpha);

        Require(node->GetComponent<QuadRendererComponent>())._blend = BlendMode::Multiply;

        EXPECT_EQ(At(Draw(), 0).blend, BlendMode::Multiply);
        EXPECT_EQ(_renderer->GetBlendMode(), BlendMode::Alpha);
    }

    TEST_F(SceneDrawFieldsTest, ASpriteShowsItsBlendModeAndFlipsInTheNextDraw) {
        auto node = AddNode(*_scene, "Sprite", 1.0f, false);
        auto sprite = BECore::New<SpriteRendererComponent>();
        sprite->SetTexture(MakeTexture());
        node->AddComponent(sprite);
        {
            const Vector<DrawCall>& calls = Draw();
            ASSERT_EQ(calls.size(), 1u);
            EXPECT_EQ(At(calls, 0).kind, DrawKind::Texture);
            EXPECT_EQ(At(calls, 0).blend, BlendMode::Alpha);
        }

        sprite->_blend = BlendMode::Add;
        sprite->_flipX = true;
        {
            const Vector<DrawCall>& calls = Draw();
            ASSERT_EQ(calls.size(), 1u);
            EXPECT_EQ(At(calls, 0).kind, DrawKind::Geometry) << "a flipped sprite is one transformed draw";
            EXPECT_EQ(At(calls, 0).blend, BlendMode::Add);
        }

        sprite->_flipX = false;
        sprite->_blend = BlendMode::Alpha;
        EXPECT_EQ(At(Draw(), 0).kind, DrawKind::Texture);
    }

    TEST_F(SceneDrawFieldsTest, ARemovedComponentIsNotDrawnAndAnAddedOneIsDrawn) {
        auto node = AddNode(*_scene, "Quad", 1.0f, true);
        EXPECT_EQ(Draw().size(), 1u);

        node->RemoveComponent("QuadRendererComponent"_intern);
        EXPECT_TRUE(Draw().empty());

        node->AddComponent(BECore::New<QuadRendererComponent>());
        EXPECT_EQ(Draw().size(), 1u);

        node->RemoveComponent("TransformComponent"_intern);
        EXPECT_TRUE(Draw().empty()) << "a quad without a transform draws nothing";
    }

    TEST_F(SceneDrawFieldsTest, ASpriteDrawsItsSourceRectAndFit) {
        auto node = AddNode(*_scene, "Sprite", 1.0f, false);
        auto sprite = BECore::New<SpriteRendererComponent>();
        sprite->SetTexture(MakeTexture());
        node->AddComponent(sprite);

        // The node is 3 x 4 at (1, 2), the texture 4 x 8: Stretch covers the node, Contain keeps the aspect and centers the picture
        {
            const Vector<DrawCall>& calls = Draw();
            ASSERT_EQ(calls.size(), 1u);
            EXPECT_NEAR(At(calls, 0).rect.w, 3.0f, kTolerance);
            EXPECT_NEAR(At(calls, 0).rect.h, 4.0f, kTolerance);
            EXPECT_FALSE(At(calls, 0).source.has_value());
        }

        sprite->_fit = SpriteFit::Contain;
        {
            const Vector<DrawCall>& calls = Draw();
            ASSERT_EQ(calls.size(), 1u);
            EXPECT_NEAR(At(calls, 0).rect.w, 2.0f, kTolerance);
            EXPECT_NEAR(At(calls, 0).rect.h, 4.0f, kTolerance);
            EXPECT_NEAR(At(calls, 0).rect.x, 1.5f, kTolerance);
        }

        sprite->_srcRect = Rect{1.0f, 2.0f, 3.0f, 4.0f};
        const Vector<DrawCall>& calls = Draw();
        ASSERT_EQ(calls.size(), 1u);
        ASSERT_TRUE(At(calls, 0).source.has_value());
        EXPECT_EQ(Require(At(calls, 0).source).x, 1.0f);
        EXPECT_EQ(Require(At(calls, 0).source).h, 4.0f);
    }

    TEST_F(SceneDrawFieldsTest, ASpriteWithoutATextureDrawsNothing) {
        auto node = AddNode(*_scene, "Sprite", 1.0f, false);
        auto sprite = BECore::New<SpriteRendererComponent>();
        node->AddComponent(sprite);
        EXPECT_TRUE(Draw().empty());

        sprite->SetTexture(MakeTexture());
        EXPECT_EQ(Draw().size(), 1u);

        sprite->ReloadTexture();
        EXPECT_TRUE(Draw().empty());
    }

    TEST_F(SceneDrawFieldsTest, ARemovedNodeAndItsSubtreeAreNotDrawnAnyMore) {
        auto removed = AddNode(*_scene, "Removed", 1.0f, true);
        auto child = _scene->CreateNode("Child"_intern, removed.Get());
        child->AddComponent(BECore::New<TransformComponent>());
        child->AddComponent(BECore::New<QuadRendererComponent>());
        AddNode(*_scene, "Kept", 4.0f, true);
        EXPECT_EQ(Draw().size(), 3u);

        _scene->GetRootNode().RemoveChild(removed.Get());

        const Vector<DrawCall>& calls = Draw();
        ASSERT_EQ(calls.size(), 1u);
        EXPECT_EQ(At(calls, 0).rect.x, 4.0f);
    }

}  // namespace BECore::Tests
