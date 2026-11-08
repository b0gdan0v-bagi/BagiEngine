#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneRenderData.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr uint32_t kTextureWidth = 4;
        constexpr uint32_t kTextureHeight = 8;
        constexpr Color kGreen{0, 255, 0, 255};

        bool IsGreen(const Color& color) {
            return color.r == kGreen.r && color.g == kGreen.g && color.b == kGreen.b && color.a == kGreen.a;
        }

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

    class SceneRenderDataTest : public ::testing::Test {
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

        entt::registry& Registry() {
            return _scene->GetRegistry();
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtr<Scene> _scene;
    };

    TEST_F(SceneRenderDataTest, ADrawMirrorsTheTransformAndTheQuadOfANode) {
        auto node = AddNode(*_scene, "Quad", 5.0f, true);
        node->GetComponent<QuadRendererComponent>()->_color = kGreen;
        auto bare = AddNode(*_scene, "Bare", 9.0f, false);

        _scene->Draw();

        auto& registry = Registry();
        ASSERT_TRUE(registry.all_of<TransformData>(node->GetEntity()));
        const TransformData& transform = registry.get<TransformData>(node->GetEntity());
        EXPECT_EQ(transform.x, 5.0f);
        EXPECT_EQ(transform.y, 6.0f);
        EXPECT_EQ(transform.width, 7.0f);
        EXPECT_EQ(transform.height, 8.0f);
        ASSERT_TRUE(registry.all_of<QuadDrawData>(node->GetEntity()));
        EXPECT_TRUE(IsGreen(registry.get<QuadDrawData>(node->GetEntity()).color));

        // A node with a transform and no renderer has the transform only; the root has neither
        EXPECT_TRUE(registry.all_of<TransformData>(bare->GetEntity()));
        EXPECT_FALSE(registry.all_of<QuadDrawData>(bare->GetEntity()));
        EXPECT_FALSE(registry.all_of<SpriteDrawData>(bare->GetEntity()));
        EXPECT_FALSE(registry.all_of<TransformData>(_scene->GetRootNode().GetEntity()));
        EXPECT_FALSE(registry.all_of<QuadDrawData>(_scene->GetRootNode().GetEntity()));
        EXPECT_EQ(registry.view<TransformData>().size(), 2u);
        EXPECT_EQ(registry.view<QuadDrawData>().size(), 1u);
        EXPECT_EQ(registry.view<SpriteDrawData>().size(), 0u);
    }

    TEST_F(SceneRenderDataTest, AFieldChangedBetweenTwoDrawsShowsInTheSecond) {
        auto node = AddNode(*_scene, "Quad", 1.0f, true);
        _scene->Draw();
        ASSERT_EQ(Registry().get<TransformData>(node->GetEntity()).x, 1.0f);

        node->GetComponent<TransformComponent>()->_x = 40.0f;
        node->GetComponent<QuadRendererComponent>()->_color = kGreen;

        // The mirror is refreshed by the draw, not by the write
        EXPECT_EQ(Registry().get<TransformData>(node->GetEntity()).x, 1.0f);
        _scene->Draw();

        EXPECT_EQ(Registry().get<TransformData>(node->GetEntity()).x, 40.0f);
        EXPECT_TRUE(IsGreen(Registry().get<QuadDrawData>(node->GetEntity()).color));
    }

    TEST_F(SceneRenderDataTest, ARemovedComponentLosesItsRegistryComponentInTheNextDraw) {
        auto node = AddNode(*_scene, "Quad", 1.0f, true);
        _scene->Draw();
        ASSERT_TRUE(Registry().all_of<QuadDrawData>(node->GetEntity()));

        node->RemoveComponent("QuadRendererComponent"_intern);
        _scene->Draw();

        EXPECT_FALSE(Registry().all_of<QuadDrawData>(node->GetEntity()));
        EXPECT_TRUE(Registry().all_of<TransformData>(node->GetEntity()));

        node->RemoveComponent("TransformComponent"_intern);
        _scene->Draw();

        EXPECT_FALSE(Registry().all_of<TransformData>(node->GetEntity()));
        EXPECT_EQ(Registry().view<TransformData>().size(), 0u);
        EXPECT_EQ(Registry().view<QuadDrawData>().size(), 0u);
    }

    TEST_F(SceneRenderDataTest, AComponentAddedBetweenTwoDrawsAppearsInTheSecond) {
        auto node = AddNode(*_scene, "Node", 1.0f, false);
        _scene->Draw();
        ASSERT_FALSE(Registry().all_of<QuadDrawData>(node->GetEntity()));

        node->AddComponent(BECore::New<QuadRendererComponent>());
        _scene->Draw();

        EXPECT_TRUE(Registry().all_of<QuadDrawData>(node->GetEntity()));
    }

    TEST_F(SceneRenderDataTest, ASpriteMirrorsItsTextureSourceAndFit) {
        auto node = AddNode(*_scene, "Sprite", 1.0f, false);
        auto sprite = BECore::New<SpriteRendererComponent>();
        sprite->_srcRect = Rect{1.0f, 2.0f, 3.0f, 4.0f};
        sprite->_fit = SpriteFit::Contain;
        node->AddComponent(sprite);
        const eastl::vector<uint8_t> pixels(static_cast<size_t>(kTextureWidth) * kTextureHeight * 4, 255);
        IntrusivePtr<ITexture> texture = _renderer->CreateTexture(pixels.data(), kTextureWidth, kTextureHeight);
        ASSERT_TRUE(texture);
        sprite->SetTexture(texture);

        _scene->Draw();

        ASSERT_TRUE(Registry().all_of<SpriteDrawData>(node->GetEntity()));
        const SpriteDrawData& data = Registry().get<SpriteDrawData>(node->GetEntity());
        EXPECT_EQ(data.texture, texture.Get());
        EXPECT_EQ(data.srcRect.x, 1.0f);
        EXPECT_EQ(data.srcRect.y, 2.0f);
        EXPECT_EQ(data.srcRect.w, 3.0f);
        EXPECT_EQ(data.srcRect.h, 4.0f);
        EXPECT_EQ(data.fit, SpriteFit::Contain);
        EXPECT_EQ(data.owner, sprite.Get());
    }

    TEST_F(SceneRenderDataTest, ASpriteWithoutATextureHasANullTexture) {
        auto node = AddNode(*_scene, "Sprite", 1.0f, false);
        auto sprite = BECore::New<SpriteRendererComponent>();
        node->AddComponent(sprite);

        _scene->Draw();

        ASSERT_TRUE(Registry().all_of<SpriteDrawData>(node->GetEntity()));
        EXPECT_EQ(Registry().get<SpriteDrawData>(node->GetEntity()).texture, nullptr);

        const eastl::vector<uint8_t> pixels(static_cast<size_t>(kTextureWidth) * kTextureHeight * 4, 255);
        sprite->SetTexture(_renderer->CreateTexture(pixels.data(), kTextureWidth, kTextureHeight));
        _scene->Draw();
        EXPECT_NE(Registry().get<SpriteDrawData>(node->GetEntity()).texture, nullptr);

        sprite->ReloadTexture();
        _scene->Draw();
        EXPECT_EQ(Registry().get<SpriteDrawData>(node->GetEntity()).texture, nullptr);
    }

    TEST_F(SceneRenderDataTest, TheOwnerOfTheQuadDataIsTheNodesQuadComponent) {
        auto node = AddNode(*_scene, "Quad", 1.0f, true);
        auto other = AddNode(*_scene, "Other", 2.0f, true);

        _scene->Draw();

        EXPECT_EQ(Registry().get<QuadDrawData>(node->GetEntity()).owner, node->GetComponent<QuadRendererComponent>().Get());
        EXPECT_EQ(Registry().get<QuadDrawData>(other->GetEntity()).owner, other->GetComponent<QuadRendererComponent>().Get());
        EXPECT_NE(Registry().get<QuadDrawData>(node->GetEntity()).owner, Registry().get<QuadDrawData>(other->GetEntity()).owner);
    }

    TEST_F(SceneRenderDataTest, RemovingANodeLeavesNoRenderDataBehind) {
        auto removed = AddNode(*_scene, "Removed", 1.0f, true);
        auto child = _scene->CreateNode("Child"_intern, removed.Get());
        child->AddComponent(BECore::New<TransformComponent>());
        child->AddComponent(BECore::New<QuadRendererComponent>());
        auto kept = AddNode(*_scene, "Kept", 4.0f, true);
        _scene->Draw();
        auto& registry = Registry();
        ASSERT_EQ(registry.view<TransformData>().size(), 3u);
        ASSERT_EQ(registry.view<QuadDrawData>().size(), 3u);

        _scene->GetRootNode().RemoveChild(removed.Get());

        EXPECT_EQ(registry.view<TransformData>().size(), 1u);
        EXPECT_EQ(registry.view<QuadDrawData>().size(), 1u);
        _scene->Draw();
        EXPECT_EQ(registry.view<TransformData>().size(), 1u);
        EXPECT_EQ(registry.view<QuadDrawData>().size(), 1u);
        EXPECT_TRUE(registry.all_of<QuadDrawData>(kept->GetEntity()));
    }

}  // namespace BECore::Tests
