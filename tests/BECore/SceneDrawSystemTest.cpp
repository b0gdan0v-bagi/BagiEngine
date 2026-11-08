#include "SceneDrawSystemTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneDrawSystem.h>
#include <Common/RecordingRenderer.h>
#include <Generated/SceneDrawSystemTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    void DrawMarkerComponent::OnDraw() {
        CoreManager::GetRenderer()->DrawFilledRect(marker, 0.0f, 1.0f, 1.0f, Color{1, 2, 3, 4});
    }

    namespace {

        constexpr float kMarkerId = 100.0f;
        constexpr uint32_t kTextureSize = 4;
        constexpr size_t kBytesPerPixel = 4;

        /// Adds a node with a unit quad at x = @p id under @p parent (the root when null). The quad's rect in a recorded draw call tells which node it was.
        IntrusivePtr<SceneNode> AddQuad(Scene& scene, SceneNode* parent, const char* name, float id, int32_t layer = 0) {
            auto node = scene.CreateNode(PoolString::Intern(name), parent);
            node->SetLayer(layer);
            auto transform = BECore::New<TransformComponent>();
            transform->_x = id;
            node->AddComponent(transform);
            node->AddComponent(BECore::New<QuadRendererComponent>());
            return node;
        }

        /// The quad ids in the order of the draw calls of the last draw.
        eastl::vector<float> DrawnIds(const RecordingRenderer& renderer) {
            eastl::vector<float> ids;
            for (const DrawCall& call : renderer.calls) {
                ids.push_back(call.rect.x);
            }
            return ids;
        }

        /// The names of the nodes in the order of the view over SceneDrawItem, which is the order Draw() calls OnDraw().
        eastl::vector<PoolString> ViewNames(Scene& scene) {
            eastl::vector<PoolString> names;
            for (const auto [entity, item] : scene.GetRegistry().view<SceneDrawItem>().each()) {
                names.push_back(item.node->GetName());
            }
            return names;
        }

        void CollectNodes(SceneNode& node, eastl::vector<SceneNode*>& nodes) {
            nodes.push_back(&node);
            for (const auto& child : node.GetChildren()) {
                CollectNodes(*child, nodes);
            }
        }

    }  // namespace

    class SceneDrawSystemTest : public ::testing::Test {
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

        /// Draws once and returns the quad ids in draw order.
        eastl::vector<float> Draw() {
            _renderer->calls.clear();
            _scene->Draw();
            return DrawnIds(*_renderer);
        }

        /// Gives @p sprite a white texture of kTextureSize x kTextureSize from the recording renderer.
        void SetWhiteTexture(SpriteRendererComponent& sprite) {
            const eastl::vector<uint8_t> pixels(static_cast<size_t>(kTextureSize) * kTextureSize * kBytesPerPixel, 255);
            sprite.SetTexture(_renderer->CreateTexture(pixels.data(), kTextureSize, kTextureSize));
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtr<Scene> _scene;
    };

    TEST_F(SceneDrawSystemTest, EveryLiveNodeHasADrawItemInTreeOrder) {
        auto a = AddQuad(*_scene, nullptr, "A", 1.0f, 1);
        auto aChild = AddQuad(*_scene, a.Get(), "AChild", 2.0f, -1);
        auto b = AddQuad(*_scene, nullptr, "B", 3.0f);

        _scene->Draw();

        eastl::vector<SceneNode*> nodes;
        CollectNodes(_scene->GetRootNode(), nodes);
        ASSERT_EQ(nodes.size(), 4u);
        auto& registry = _scene->GetRegistry();
        EXPECT_EQ(registry.view<SceneDrawItem>().size(), nodes.size());
        for (size_t index = 0; index < nodes.size(); ++index) {
            const SceneNode& node = *nodes[index];
            ASSERT_TRUE(registry.valid(node.GetEntity())) << node.GetName().CStr();
            ASSERT_TRUE(registry.all_of<SceneDrawItem>(node.GetEntity())) << node.GetName().CStr();
            const SceneDrawItem& item = registry.get<SceneDrawItem>(node.GetEntity());
            EXPECT_EQ(item.node, &node) << node.GetName().CStr();
            EXPECT_EQ(item.layer, node.GetLayer()) << node.GetName().CStr();
            EXPECT_EQ(item.order, static_cast<uint32_t>(index)) << node.GetName().CStr();
        }
    }

    TEST_F(SceneDrawSystemTest, TheViewYieldsTheNodesByLayerThenSceneOrder) {
        auto a = AddQuad(*_scene, nullptr, "A", 1.0f);
        auto aChild = AddQuad(*_scene, a.Get(), "AChild", 2.0f);
        auto aGrandChild = AddQuad(*_scene, aChild.Get(), "AGrandChild", 3.0f, 1);
        auto b = AddQuad(*_scene, nullptr, "B", 4.0f, 1);
        auto c = AddQuad(*_scene, nullptr, "C", 5.0f, -1);
        auto d = AddQuad(*_scene, nullptr, "D", 6.0f);

        _scene->Draw();

        // Scene order: Root, A, AChild, AGrandChild, B, C, D. Layer -1 first, then 0, then 1.
        const eastl::vector<PoolString> expected = {"C"_intern, "Root"_intern, "A"_intern, "AChild"_intern, "D"_intern, "AGrandChild"_intern, "B"_intern};
        EXPECT_EQ(ViewNames(*_scene), expected);
    }

    TEST_F(SceneDrawSystemTest, DrawCallsFollowTheLayersWhateverThePlaceInTheScene) {
        AddQuad(*_scene, nullptr, "Front", 1.0f, 1);
        AddQuad(*_scene, nullptr, "Middle", 2.0f);
        AddQuad(*_scene, nullptr, "Back", 3.0f, -1);
        AddQuad(*_scene, nullptr, "Middle2", 4.0f);

        const eastl::vector<float> expected = {3.0f, 2.0f, 4.0f, 1.0f};
        EXPECT_EQ(Draw(), expected);
    }

    TEST_F(SceneDrawSystemTest, ALayerChangedBetweenTwoDrawsChangesTheSecondDraw) {
        auto a = AddQuad(*_scene, nullptr, "A", 1.0f);
        AddQuad(*_scene, nullptr, "B", 2.0f);
        AddQuad(*_scene, nullptr, "C", 3.0f);

        EXPECT_EQ(Draw(), (eastl::vector<float>{1.0f, 2.0f, 3.0f}));

        a->SetLayer(1);
        EXPECT_EQ(Draw(), (eastl::vector<float>{2.0f, 3.0f, 1.0f}));

        a->SetLayer(-1);
        EXPECT_EQ(Draw(), (eastl::vector<float>{1.0f, 2.0f, 3.0f}));
        EXPECT_EQ(_scene->GetRegistry().get<SceneDrawItem>(a->GetEntity()).layer, -1);
    }

    TEST_F(SceneDrawSystemTest, RemovingANodeDestroysTheEntitiesOfItsWholeSubtree) {
        auto removed = AddQuad(*_scene, nullptr, "Removed", 1.0f);
        auto child = AddQuad(*_scene, removed.Get(), "Child", 2.0f);
        auto grandChild = AddQuad(*_scene, child.Get(), "GrandChild", 3.0f);
        auto kept = AddQuad(*_scene, nullptr, "Kept", 4.0f);
        EXPECT_EQ(Draw(), (eastl::vector<float>{1.0f, 2.0f, 3.0f, 4.0f}));

        auto& registry = _scene->GetRegistry();
        const entt::entity removedEntities[] = {removed->GetEntity(), child->GetEntity(), grandChild->GetEntity()};
        for (const entt::entity entity : removedEntities) {
            ASSERT_TRUE(registry.valid(entity));
        }

        _scene->GetRootNode().RemoveChild(removed.Get());

        for (const entt::entity entity : removedEntities) {
            EXPECT_FALSE(registry.valid(entity));
        }
        EXPECT_TRUE(removed->GetEntity() == entt::null);
        EXPECT_TRUE(child->GetEntity() == entt::null);
        EXPECT_TRUE(grandChild->GetEntity() == entt::null);
        EXPECT_TRUE(registry.valid(kept->GetEntity()));
        // Only the root and Kept are left: the items went with the entities, before any further draw
        EXPECT_EQ(registry.view<SceneDrawItem>().size(), 2u);

        EXPECT_EQ(Draw(), (eastl::vector<float>{4.0f}));
        EXPECT_EQ(registry.view<SceneDrawItem>().size(), 2u);
    }

    TEST_F(SceneDrawSystemTest, ANodeAddedBetweenTwoDrawsIsDrawnAtItsScenePosition) {
        auto a = AddQuad(*_scene, nullptr, "A", 1.0f);
        AddQuad(*_scene, nullptr, "B", 2.0f);
        EXPECT_EQ(Draw(), (eastl::vector<float>{1.0f, 2.0f}));

        auto child = AddQuad(*_scene, a.Get(), "AChild", 3.0f);

        EXPECT_EQ(Draw(), (eastl::vector<float>{1.0f, 3.0f, 2.0f}));
        EXPECT_TRUE(_scene->GetRegistry().all_of<SceneDrawItem>(child->GetEntity()));
    }

    TEST_F(SceneDrawSystemTest, DrawRankAgreesWithTheViewOrderWithoutADraw) {
        auto a = AddQuad(*_scene, nullptr, "A", 1.0f);
        auto aChild = AddQuad(*_scene, a.Get(), "AChild", 2.0f, 1);
        auto b = AddQuad(*_scene, nullptr, "B", 3.0f, -1);

        // No draw yet: the rank is computed from the tree. Root, A, AChild, B on layers 0, 0, 1, -1.
        EXPECT_EQ(_scene->GetDrawRank(*b), 0);
        EXPECT_EQ(_scene->GetDrawRank(_scene->GetRootNode()), 1);
        EXPECT_EQ(_scene->GetDrawRank(*a), 2);
        EXPECT_EQ(_scene->GetDrawRank(*aChild), 3);

        _scene->Draw();
        const eastl::vector<PoolString> expected = {"B"_intern, "Root"_intern, "A"_intern, "AChild"_intern};
        EXPECT_EQ(ViewNames(*_scene), expected);
    }

    TEST_F(SceneDrawSystemTest, AQuadIsDrawnAtItsPlaceAmongTheComponentsOfItsNode) {
        auto marker = BECore::New<DrawMarkerComponent>();
        marker->marker = kMarkerId;
        auto before = _scene->CreateNode("MarkerFirst"_intern, nullptr);
        auto beforeTransform = BECore::New<TransformComponent>();
        beforeTransform->_x = 1.0f;
        before->AddComponent(beforeTransform);
        before->AddComponent(marker);
        before->AddComponent(BECore::New<QuadRendererComponent>());

        auto afterMarker = BECore::New<DrawMarkerComponent>();
        afterMarker->marker = kMarkerId + 1.0f;
        auto after = AddQuad(*_scene, nullptr, "QuadFirst", 2.0f);
        after->AddComponent(afterMarker);

        // [Marker, Quad] draws the marker before the quad, [Quad, Marker] the quad before the marker
        EXPECT_EQ(Draw(), (eastl::vector<float>{kMarkerId, 1.0f, 2.0f, kMarkerId + 1.0f}));
    }

    TEST_F(SceneDrawSystemTest, AQuadWithoutATransformDrawsNothing) {
        auto node = _scene->CreateNode("NoTransform"_intern, nullptr);
        node->AddComponent(BECore::New<QuadRendererComponent>());
        AddQuad(*_scene, nullptr, "Plain", 1.0f);

        EXPECT_EQ(Draw(), (eastl::vector<float>{1.0f}));
    }

    TEST_F(SceneDrawSystemTest, ASpriteWithoutATransformDrawsNothing) {
        auto node = _scene->CreateNode("NoTransform"_intern, nullptr);
        auto sprite = BECore::New<SpriteRendererComponent>();
        SetWhiteTexture(*sprite);
        node->AddComponent(sprite);

        EXPECT_TRUE(Draw().empty());
    }

    TEST_F(SceneDrawSystemTest, AQuadAndASpriteAreOrderedTogetherByLayer) {
        auto spriteNode = _scene->CreateNode("Sprite"_intern, nullptr);
        auto transform = BECore::New<TransformComponent>();
        transform->_x = 1.0f;
        spriteNode->AddComponent(transform);
        auto sprite = BECore::New<SpriteRendererComponent>();
        SetWhiteTexture(*sprite);
        spriteNode->AddComponent(sprite);
        AddQuad(*_scene, nullptr, "Back", 2.0f, -1);

        const eastl::vector<float> ids = Draw();

        // The sprite is first in the scene, the quad on layer -1 is drawn before it
        EXPECT_EQ(ids, (eastl::vector<float>{2.0f, 1.0f}));
        ASSERT_EQ(_renderer->calls.size(), 2u);
        EXPECT_EQ(_renderer->calls[0].kind, DrawKind::Rect);
        EXPECT_EQ(_renderer->calls[1].kind, DrawKind::Texture);
    }

    TEST_F(SceneDrawSystemTest, AColorChangedBetweenTwoDrawsChangesTheSecondDraw) {
        auto node = AddQuad(*_scene, nullptr, "A", 1.0f);
        auto quad = node->GetComponent<QuadRendererComponent>();
        quad->_color = Color{10, 20, 30, 255};

        Draw();
        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_EQ(_renderer->calls[0].color.r, 10);

        quad->_color = Color{200, 100, 50, 255};
        Draw();
        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_EQ(_renderer->calls[0].color.r, 200);
        EXPECT_EQ(_renderer->calls[0].color.g, 100);
        EXPECT_EQ(_renderer->calls[0].color.b, 50);
    }

}  // namespace BECore::Tests
