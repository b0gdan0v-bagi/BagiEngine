#include "SceneDrawSystemTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneDrawSystem.h>
#include <BECore/Scene/SceneRenderData.h>
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
        constexpr float kNodeWidth = 100.0f;
        constexpr float kNodeHeight = 50.0f;
        constexpr float kQuarterTurn = 90.0f;
        constexpr float kCornerTolerance = 0.001f;

        void ExpectCorner(const GeometryVertex& vertex, float x, float y) {
            EXPECT_NEAR(vertex.x, x, kCornerTolerance);
            EXPECT_NEAR(vertex.y, y, kCornerTolerance);
        }

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

        /// Adds a node at (@p x, @p y) of 100 x 50 with a quad turned by @p rotation about the pivot (@p pivotX, @p pivotY) of its rect.
        IntrusivePtr<SceneNode> AddTurnedQuad(Scene& scene, float x, float y, float rotation, float pivotX = 0.5f, float pivotY = 0.5f) {
            auto node = scene.CreateNode("Turned"_intern, nullptr);
            auto transform = BECore::New<TransformComponent>();
            transform->_x = x;
            transform->_y = y;
            transform->_width = kNodeWidth;
            transform->_height = kNodeHeight;
            transform->_rotation = rotation;
            transform->_pivotX = pivotX;
            transform->_pivotY = pivotY;
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

        /// Adds a node of 100 x 50 at the origin turned by @p rotation with a sprite (a white square texture); returns the node, the sprite is its component.
        IntrusivePtr<SceneNode> AddSprite(float rotation) {
            auto node = _scene->CreateNode("Sprite"_intern, nullptr);
            auto transform = BECore::New<TransformComponent>();
            transform->_width = kNodeWidth;
            transform->_height = kNodeHeight;
            transform->_rotation = rotation;
            node->AddComponent(transform);
            auto sprite = BECore::New<SpriteRendererComponent>();
            SetWhiteTexture(*sprite);
            node->AddComponent(sprite);
            return node;
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

    TEST_F(SceneDrawSystemTest, ANodeWithNothingSetMakesThePlainCallsWithAlphaBlend) {
        AddQuad(*_scene, nullptr, "Quad", 1.0f);
        AddSprite(0.0f);

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 2u);
        EXPECT_EQ(_renderer->calls[0].kind, DrawKind::Rect);
        EXPECT_EQ(_renderer->calls[0].blend, BlendMode::Alpha);
        EXPECT_EQ(_renderer->calls[1].kind, DrawKind::Texture);
        EXPECT_EQ(_renderer->calls[1].blend, BlendMode::Alpha);
        EXPECT_EQ(_renderer->GetBlendMode(), BlendMode::Alpha);
    }

    TEST_F(SceneDrawSystemTest, ARotatedQuadIsOneGeometryCallTurnedAboutTheCenterOfTheNode) {
        AddTurnedQuad(*_scene, 0.0f, 0.0f, kQuarterTurn);

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 1u);
        const DrawCall& call = _renderer->calls[0];
        EXPECT_EQ(call.kind, DrawKind::Geometry);
        EXPECT_EQ(call.texture, nullptr);
        ASSERT_EQ(call.vertices.size(), 4u);
        // Clockwise on the screen: top-left, top-right, bottom-left, bottom-right of the unturned rect
        ExpectCorner(call.vertices[0], 75.0f, -25.0f);
        ExpectCorner(call.vertices[1], 75.0f, 75.0f);
        ExpectCorner(call.vertices[2], 25.0f, -25.0f);
        ExpectCorner(call.vertices[3], 25.0f, 75.0f);
    }

    TEST_F(SceneDrawSystemTest, ARotatedQuadTurnsAboutTheNormalizedPivotOfItsRect) {
        // The pivot (0, 0) is the top-left corner of the node's rect, here (10, 20)
        AddTurnedQuad(*_scene, 10.0f, 20.0f, kQuarterTurn, 0.0f, 0.0f);

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 1u);
        const DrawCall& call = _renderer->calls[0];
        ASSERT_EQ(call.vertices.size(), 4u);
        ExpectCorner(call.vertices[0], 10.0f, 20.0f);
        ExpectCorner(call.vertices[1], 10.0f, 120.0f);
        ExpectCorner(call.vertices[2], -40.0f, 20.0f);
        ExpectCorner(call.vertices[3], -40.0f, 120.0f);
    }

    TEST_F(SceneDrawSystemTest, ARotatedContainSpriteTurnsAboutThePivotOfTheNodeNotOfTheFittedRect) {
        // A square texture in a 100 x 50 node: the fitted rect is (25, 0, 50, 50)
        auto node = AddSprite(kQuarterTurn);
        node->GetComponent<SpriteRendererComponent>()->_fit = SpriteFit::Contain;
        auto transform = node->GetComponent<TransformComponent>();
        transform->_pivotX = 0.0f;
        transform->_pivotY = 0.0f;

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 1u);
        const DrawCall& call = _renderer->calls[0];
        EXPECT_EQ(call.kind, DrawKind::Geometry);
        EXPECT_NE(call.texture, nullptr);
        ASSERT_EQ(call.vertices.size(), 4u);
        // About the node's top-left (0, 0) the fitted top-left (25, 0) goes to (0, 25); about the fitted rect's own corner it would stay
        ExpectCorner(call.vertices[0], 0.0f, 25.0f);
        ExpectCorner(call.vertices[1], 0.0f, 75.0f);
        ExpectCorner(call.vertices[2], -50.0f, 25.0f);
        ExpectCorner(call.vertices[3], -50.0f, 75.0f);
    }

    TEST_F(SceneDrawSystemTest, ARotatedContainSpriteTurnsAboutTheCenterOfTheNodeByDefault) {
        auto node = AddSprite(kQuarterTurn);
        node->GetComponent<SpriteRendererComponent>()->_fit = SpriteFit::Contain;

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 1u);
        const DrawCall& call = _renderer->calls[0];
        ASSERT_EQ(call.vertices.size(), 4u);
        // The center of the node (50, 25) is the center of the fitted square (25, 0, 50, 50): a quarter turn about it keeps the square's box
        ExpectCorner(call.vertices[0], 75.0f, 0.0f);
        ExpectCorner(call.vertices[1], 75.0f, 50.0f);
        ExpectCorner(call.vertices[2], 25.0f, 0.0f);
        ExpectCorner(call.vertices[3], 25.0f, 50.0f);
    }

    TEST_F(SceneDrawSystemTest, AFlippedSpriteSwapsTheUVsAndDrawsAsGeometry) {
        auto node = AddSprite(0.0f);
        auto sprite = node->GetComponent<SpriteRendererComponent>();
        sprite->_flipX = true;

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 1u);
        const DrawCall& call = _renderer->calls[0];
        EXPECT_EQ(call.kind, DrawKind::Geometry);
        ASSERT_EQ(call.vertices.size(), 4u);
        // Not turned: the corners are the node's rect, the left corners show the right edge of the texture
        ExpectCorner(call.vertices[0], 0.0f, 0.0f);
        ExpectCorner(call.vertices[3], kNodeWidth, kNodeHeight);
        EXPECT_NEAR(call.vertices[0].u, 1.0f, kCornerTolerance);
        EXPECT_NEAR(call.vertices[1].u, 0.0f, kCornerTolerance);
        EXPECT_NEAR(call.vertices[0].v, 0.0f, kCornerTolerance);

        sprite->_flipX = false;
        sprite->_flipY = true;
        Draw();
        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_NEAR(_renderer->calls[0].vertices[0].u, 0.0f, kCornerTolerance);
        EXPECT_NEAR(_renderer->calls[0].vertices[0].v, 1.0f, kCornerTolerance);
    }

    TEST_F(SceneDrawSystemTest, TheBlendModeIsOnTheCallOfTheNodeThatAsksForItAndNotOnTheNextOnes) {
        auto additive = AddQuad(*_scene, nullptr, "Additive", 1.0f);
        additive->GetComponent<QuadRendererComponent>()->_blend = BlendMode::Add;
        // [Quad(Add), Marker]: the OnDraw() of the next component of the same node sees Alpha again
        auto marker = BECore::New<DrawMarkerComponent>();
        marker->marker = kMarkerId;
        additive->AddComponent(marker);
        AddQuad(*_scene, nullptr, "Plain", 2.0f);
        auto spriteNode = AddSprite(0.0f);
        spriteNode->GetComponent<SpriteRendererComponent>()->_blend = BlendMode::Multiply;
        AddQuad(*_scene, nullptr, "Last", 3.0f);

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 5u);
        EXPECT_EQ(_renderer->calls[0].blend, BlendMode::Add);
        EXPECT_EQ(_renderer->calls[1].rect.x, kMarkerId);
        EXPECT_EQ(_renderer->calls[1].blend, BlendMode::Alpha);
        EXPECT_EQ(_renderer->calls[2].blend, BlendMode::Alpha);
        EXPECT_EQ(_renderer->calls[3].kind, DrawKind::Texture);
        EXPECT_EQ(_renderer->calls[3].blend, BlendMode::Multiply);
        EXPECT_EQ(_renderer->calls[4].blend, BlendMode::Alpha);
        EXPECT_EQ(_renderer->GetBlendMode(), BlendMode::Alpha);
    }

    TEST_F(SceneDrawSystemTest, ARotatedAdditiveQuadHasTheBlendOnItsGeometryCall) {
        auto node = AddTurnedQuad(*_scene, 0.0f, 0.0f, 30.0f);
        node->GetComponent<QuadRendererComponent>()->_blend = BlendMode::Add;

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_EQ(_renderer->calls[0].kind, DrawKind::Geometry);
        EXPECT_EQ(_renderer->calls[0].blend, BlendMode::Add);
        EXPECT_EQ(_renderer->GetBlendMode(), BlendMode::Alpha);
    }

    TEST_F(SceneDrawSystemTest, ARotationSetBetweenTwoDrawsShowsInTheSecond) {
        auto node = AddTurnedQuad(*_scene, 0.0f, 0.0f, 0.0f);
        Draw();
        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_EQ(_renderer->calls[0].kind, DrawKind::Rect);

        node->GetComponent<TransformComponent>()->_rotation = kQuarterTurn;
        Draw();
        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_EQ(_renderer->calls[0].kind, DrawKind::Geometry);

        node->GetComponent<TransformComponent>()->_rotation = 0.0f;
        Draw();
        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_EQ(_renderer->calls[0].kind, DrawKind::Rect);
    }

    TEST_F(SceneDrawSystemTest, TheOrderOfDrawIsTheSameForRotatedAndBlendedNodes) {
        auto front = AddTurnedQuad(*_scene, 1.0f, 0.0f, 30.0f);
        front->SetLayer(1);
        auto middle = AddQuad(*_scene, nullptr, "Middle", 2.0f);
        middle->GetComponent<QuadRendererComponent>()->_blend = BlendMode::Add;
        AddQuad(*_scene, nullptr, "Back", 3.0f, -1);

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 3u);
        EXPECT_EQ(_renderer->calls[0].rect.x, 3.0f);
        EXPECT_EQ(_renderer->calls[1].rect.x, 2.0f);
        EXPECT_EQ(_renderer->calls[2].kind, DrawKind::Geometry);
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

    TEST_F(SceneDrawSystemTest, AHiddenNodeDrawsNothingAndTheOthersKeepTheirOrder) {
        AddQuad(*_scene, nullptr, "A", 1.0f);
        auto b = AddQuad(*_scene, nullptr, "B", 2.0f);
        AddQuad(*_scene, nullptr, "C", 3.0f);
        EXPECT_EQ(Draw(), (eastl::vector<float>{1.0f, 2.0f, 3.0f}));

        b->SetEditorFlags(EditorFlags::Hidden);
        EXPECT_EQ(Draw(), (eastl::vector<float>{1.0f, 3.0f}));

        b->SetEditorFlags(EditorFlags::None);
        EXPECT_EQ(Draw(), (eastl::vector<float>{1.0f, 2.0f, 3.0f}));
    }

    TEST_F(SceneDrawSystemTest, AHiddenNodeHidesItsDescendantsAndOnDrawToo) {
        auto a = AddQuad(*_scene, nullptr, "A", 1.0f);
        auto aChild = AddQuad(*_scene, a.Get(), "AChild", 2.0f);
        auto marker = BECore::New<DrawMarkerComponent>();
        marker->marker = kMarkerId;
        aChild->AddComponent(marker);
        AddQuad(*_scene, nullptr, "B", 3.0f);
        EXPECT_EQ(Draw(), (eastl::vector<float>{1.0f, 2.0f, kMarkerId, 3.0f}));

        a->SetEditorFlags(EditorFlags::Hidden);
        EXPECT_EQ(Draw(), (eastl::vector<float>{3.0f}));

        // A hidden child alone leaves its parent and its siblings visible
        a->SetEditorFlags(EditorFlags::None);
        aChild->SetEditorFlags(EditorFlags::Hidden);
        EXPECT_EQ(Draw(), (eastl::vector<float>{1.0f, 3.0f}));
    }

    TEST_F(SceneDrawSystemTest, AHiddenNodeKeepsItsItemAndItsRank) {
        auto a = AddQuad(*_scene, nullptr, "A", 1.0f);
        auto aChild = AddQuad(*_scene, a.Get(), "AChild", 2.0f);
        auto b = AddQuad(*_scene, nullptr, "B", 3.0f, -1);
        _scene->Draw();
        const eastl::vector<PoolString> before = ViewNames(*_scene);
        const int32_t rankA = _scene->GetDrawRank(*a);
        const int32_t rankChild = _scene->GetDrawRank(*aChild);
        const int32_t rankB = _scene->GetDrawRank(*b);

        a->SetEditorFlags(EditorFlags::Hidden);
        _scene->Draw();

        EXPECT_EQ(ViewNames(*_scene), before);
        EXPECT_EQ(_scene->GetDrawRank(*a), rankA);
        EXPECT_EQ(_scene->GetDrawRank(*aChild), rankChild);
        EXPECT_EQ(_scene->GetDrawRank(*b), rankB);
        const auto& registry = _scene->GetRegistry();
        EXPECT_TRUE(registry.get<SceneDrawItem>(a->GetEntity()).hidden);
        EXPECT_TRUE(registry.get<SceneDrawItem>(aChild->GetEntity()).hidden);
        EXPECT_FALSE(registry.get<SceneDrawItem>(b->GetEntity()).hidden);
    }

    TEST_F(SceneDrawSystemTest, TheMirrorOfAHiddenNodeIsStillSynced) {
        auto node = AddQuad(*_scene, nullptr, "A", 1.0f);
        node->SetEditorFlags(EditorFlags::Hidden);

        // The mirror of a hidden node is synced as before, so showing it again needs no extra frame
        node->GetComponent<TransformComponent>()->_x = 7.0f;
        Draw();
        EXPECT_EQ(_scene->GetRegistry().get<TransformData>(node->GetEntity()).x, 7.0f);

        node->SetEditorFlags(EditorFlags::None);
        EXPECT_EQ(Draw(), (eastl::vector<float>{7.0f}));
    }

}  // namespace BECore::Tests
