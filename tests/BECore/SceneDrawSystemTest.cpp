#include "SceneDrawSystemTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Components/ClipRectComponent.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneDrawSystem.h>
#include <Common/AllocCheck.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Generated/SceneDrawSystemTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    void DrawMarkerComponent::OnDraw() {
        CoreManager::GetRenderer()->DrawFilledRect(marker, 0.0f, 1.0f, 1.0f, Color{1, 2, 3, 4});
    }

    void DrawGeometryMarkerComponent::OnDraw() {
        const GeometryVertex triangle[3] = {{marker, 0.0f, Color{1, 2, 3, 4}, 0.0f, 0.0f}, {marker + 1.0f, 0.0f, Color{1, 2, 3, 4}, 1.0f, 0.0f}, {marker, 1.0f, Color{1, 2, 3, 4}, 0.0f, 1.0f}};
        const int indices[3] = {0, 1, 2};
        CoreManager::GetRenderer()->DrawGeometry(nullptr, triangle, indices, 0.0f, 0.0f);
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
        Vector<float> DrawnIds(const RecordingRenderer& renderer) {
            Vector<float> ids;
            for (const DrawCall& call : renderer.calls) {
                ids.push_back(call.rect.x);
            }
            return ids;
        }

        /// The names of the nodes in the order of SceneDrawList::items, which is the order Draw() calls OnDraw().
        Vector<PoolString> ViewNames(Scene& scene) {
            Vector<PoolString> names;
            for (const SceneDrawItem& item : scene.GetDrawList().items) {
                names.push_back(item.node->GetName());
            }
            return names;
        }

        const SceneDrawItem* FindItem(const Scene& scene, const SceneNode& node) {
            for (const SceneDrawItem& item : scene.GetDrawList().items) {
                if (item.node == &node) {
                    return &item;
                }
            }
            return nullptr;
        }

        void CollectNodes(SceneNode& node, Vector<SceneNode*>& nodes) {
            nodes.push_back(&node);
            for (const auto& child : node.GetChildren()) {
                CollectNodes(*child, nodes);
            }
        }

        constexpr float kQuadCorners = 4;
        constexpr float kFloatTolerance = 1e-4f;
        constexpr int kWarmFrames = 3;

        /// What the renderer was asked to draw, one entry per primitive in the order of the draw calls: a batch is split into its quads
        struct DrawnPrimitive {
            const ITexture* texture = nullptr;
            BlendMode blend = BlendMode::Alpha;
            Optional<Rect> clip;
            Rect box;
            Color color;
        };

        Vector<DrawnPrimitive> Flatten(const Vector<DrawCall>& calls) {
            Vector<DrawnPrimitive> primitives;
            for (const DrawCall& call : calls) {
                if (call.kind != DrawKind::Batch) {
                    primitives.push_back({call.texture, call.blend, call.clip, call.rect, call.color});
                    continue;
                }
                for (size_t first = 0; first + kQuadCorners <= call.vertices.size(); first += kQuadCorners) {
                    float left = call.vertices[first].x;
                    float top = call.vertices[first].y;
                    float right = left;
                    float bottom = top;
                    for (size_t i = first; i < first + kQuadCorners; ++i) {
                        left = std::min(left, call.vertices[i].x);
                        top = std::min(top, call.vertices[i].y);
                        right = std::max(right, call.vertices[i].x);
                        bottom = std::max(bottom, call.vertices[i].y);
                    }
                    primitives.push_back({call.texture, call.blend, call.clip, Rect{left, top, right - left, bottom - top}, call.vertices[first].color});
                }
            }
            return primitives;
        }

        void ExpectSamePrimitive(const DrawnPrimitive& batched, const DrawnPrimitive& plain, size_t index) {
            EXPECT_EQ(batched.texture, plain.texture) << index;
            EXPECT_EQ(batched.blend, plain.blend) << index;
            ASSERT_EQ(batched.clip.has_value(), plain.clip.has_value()) << index;
            if (batched.clip) {
                EXPECT_NEAR(batched.clip->x, plain.clip->x, kFloatTolerance) << index;
                EXPECT_NEAR(batched.clip->y, plain.clip->y, kFloatTolerance) << index;
                EXPECT_NEAR(batched.clip->w, plain.clip->w, kFloatTolerance) << index;
                EXPECT_NEAR(batched.clip->h, plain.clip->h, kFloatTolerance) << index;
            }
            EXPECT_NEAR(batched.box.x, plain.box.x, kFloatTolerance) << index;
            EXPECT_NEAR(batched.box.y, plain.box.y, kFloatTolerance) << index;
            EXPECT_NEAR(batched.box.w, plain.box.w, kFloatTolerance) << index;
            EXPECT_NEAR(batched.box.h, plain.box.h, kFloatTolerance) << index;
            EXPECT_EQ(batched.color.r, plain.color.r) << index;
            EXPECT_EQ(batched.color.a, plain.color.a) << index;
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
        Vector<float> Draw() {
            _renderer->calls.clear();
            _scene->Draw();
            return DrawnIds(*_renderer);
        }

        /// Gives @p sprite a white texture of kTextureSize x kTextureSize from the recording renderer.
        void SetWhiteTexture(SpriteRendererComponent& sprite) {
            const Vector<uint8_t> pixels(static_cast<size_t>(kTextureSize) * kTextureSize * kBytesPerPixel, 255);
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

        /// A texture the sprites of a scene can share, so that they are one batch
        IntrusivePtr<ITexture> MakeSharedTexture() {
            const Vector<uint8_t> pixels(static_cast<size_t>(kTextureSize) * kTextureSize * kBytesPerPixel, 255);
            return _renderer->CreateTexture(pixels.data(), kTextureSize, kTextureSize);
        }

        /// Adds a node of 10 x 10 at (@p x, 0) under @p parent with a sprite of @p texture, drawn with @p blend
        IntrusivePtr<SceneNode> AddSharedSprite(const IntrusivePtr<ITexture>& texture, float x, SceneNode* parent = nullptr, BlendMode blend = BlendMode::Alpha, float rotation = 0.0f) {
            auto node = _scene->CreateNode("Shared"_intern, parent);
            auto transform = BECore::New<TransformComponent>();
            transform->_x = x;
            transform->_width = kSpriteSize;
            transform->_height = kSpriteSize;
            transform->_rotation = rotation;
            node->AddComponent(transform);
            auto sprite = BECore::New<SpriteRendererComponent>();
            sprite->SetTexture(texture);
            sprite->_blend = blend;
            node->AddComponent(sprite);
            return node;
        }

        /// A node of @p size at the origin that clips its subtree
        IntrusivePtr<SceneNode> AddClip(float size) {
            auto node = _scene->CreateNode("Clip"_intern, nullptr);
            auto transform = BECore::New<TransformComponent>();
            transform->_width = size;
            transform->_height = size;
            node->AddComponent(transform);
            node->AddComponent(BECore::New<ClipRectComponent>());
            return node;
        }

        void AddGeometryMarker(float x) {
            auto node = _scene->CreateNode("Geometry"_intern, nullptr);
            auto marker = BECore::New<DrawGeometryMarkerComponent>();
            marker->marker = x;
            node->AddComponent(marker);
        }

        /// A scene of every kind of break: textures, blend modes, a clip subtree, a custom geometry draw, a turned sprite and quads
        void BuildMixedScene(const IntrusivePtr<ITexture>& first, const IntrusivePtr<ITexture>& second) {
            AddSharedSprite(first, 0.0f);
            AddSharedSprite(first, 11.0f);
            AddSharedSprite(first, 22.0f);
            AddSharedSprite(second, 33.0f);
            AddSharedSprite(second, 44.0f, nullptr, BlendMode::Add);
            AddSharedSprite(second, 55.0f);
            auto clip = AddClip(kClipSize);
            AddSharedSprite(second, 5.0f, clip.Get());
            AddQuad(*_scene, clip.Get(), "ClippedQuad", 6.0f);
            AddGeometryMarker(70.0f);
            AddSharedSprite(first, 80.0f);
            AddQuad(*_scene, nullptr, "Quad", 90.0f);
            AddSharedSprite(first, 100.0f, nullptr, BlendMode::Alpha, kTurn);
            AddSharedSprite(first, 111.0f);
        }

        static constexpr float kSpriteSize = 10.0f;
        static constexpr float kClipSize = 8.0f;
        static constexpr float kTurn = 30.0f;

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtr<Scene> _scene;
    };

    TEST_F(SceneDrawSystemTest, EveryLiveNodeHasADrawItemInTreeOrder) {
        auto a = AddQuad(*_scene, nullptr, "A", 1.0f, 1);
        auto aChild = AddQuad(*_scene, a.Get(), "AChild", 2.0f, -1);
        auto b = AddQuad(*_scene, nullptr, "B", 3.0f);

        _scene->Draw();

        Vector<SceneNode*> nodes;
        CollectNodes(_scene->GetRootNode(), nodes);
        ASSERT_EQ(nodes.size(), 4u);
        EXPECT_EQ(_scene->GetDrawList().items.size(), nodes.size());
        for (size_t index = 0; index < nodes.size(); ++index) {
            const SceneNode& node = *nodes[index];
            const SceneDrawItem* item = FindItem(*_scene, node);
            ASSERT_NE(item, nullptr) << node.GetName().CStr();
            EXPECT_EQ(item->layer, node.GetLayer()) << node.GetName().CStr();
            EXPECT_EQ(item->order, static_cast<uint32_t>(index)) << node.GetName().CStr();
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
        const Vector<PoolString> expected = {"C"_intern, "Root"_intern, "A"_intern, "AChild"_intern, "D"_intern, "AGrandChild"_intern, "B"_intern};
        EXPECT_EQ(ViewNames(*_scene), expected);
    }

    TEST_F(SceneDrawSystemTest, DrawCallsFollowTheLayersWhateverThePlaceInTheScene) {
        AddQuad(*_scene, nullptr, "Front", 1.0f, 1);
        AddQuad(*_scene, nullptr, "Middle", 2.0f);
        AddQuad(*_scene, nullptr, "Back", 3.0f, -1);
        AddQuad(*_scene, nullptr, "Middle2", 4.0f);

        const Vector<float> expected = {3.0f, 2.0f, 4.0f, 1.0f};
        EXPECT_EQ(Draw(), expected);
    }

    TEST_F(SceneDrawSystemTest, ALayerChangedBetweenTwoDrawsChangesTheSecondDraw) {
        auto a = AddQuad(*_scene, nullptr, "A", 1.0f);
        AddQuad(*_scene, nullptr, "B", 2.0f);
        AddQuad(*_scene, nullptr, "C", 3.0f);

        EXPECT_EQ(Draw(), (Vector<float>{1.0f, 2.0f, 3.0f}));

        a->SetLayer(1);
        EXPECT_EQ(Draw(), (Vector<float>{2.0f, 3.0f, 1.0f}));

        a->SetLayer(-1);
        EXPECT_EQ(Draw(), (Vector<float>{1.0f, 2.0f, 3.0f}));
        const SceneDrawItem* item = FindItem(*_scene, *a);
        ASSERT_NE(item, nullptr);
        EXPECT_EQ(item->layer, -1);
    }

    TEST_F(SceneDrawSystemTest, RemovingANodeDropsItsWholeSubtreeFromTheDrawOrder) {
        auto removed = AddQuad(*_scene, nullptr, "Removed", 1.0f);
        auto child = AddQuad(*_scene, removed.Get(), "Child", 2.0f);
        auto grandChild = AddQuad(*_scene, child.Get(), "GrandChild", 3.0f);
        AddQuad(*_scene, nullptr, "Kept", 4.0f);
        EXPECT_EQ(Draw(), (Vector<float>{1.0f, 2.0f, 3.0f, 4.0f}));
        ASSERT_GT(_scene->GetDrawRank(*grandChild), 0);

        _scene->GetRootNode().RemoveChild(removed.Get());

        // Nothing may read a rank or an item that points into the removed subtree before the next draw
        EXPECT_FALSE(_scene->IsDrawListValid());
        EXPECT_EQ(removed->GetParent(), nullptr);
        EXPECT_FALSE(_scene->MoveNode(*removed, nullptr));
        EXPECT_FALSE(_scene->DuplicateNode(*child));
        EXPECT_EQ(_scene->GetDrawRank(*removed), 0);
        EXPECT_EQ(_scene->GetDrawRank(*grandChild), 0);

        EXPECT_EQ(Draw(), (Vector<float>{4.0f}));
        EXPECT_EQ(_scene->GetDrawList().items.size(), 2u);
    }

    TEST_F(SceneDrawSystemTest, ANodeAddedBetweenTwoDrawsIsDrawnAtItsScenePosition) {
        auto a = AddQuad(*_scene, nullptr, "A", 1.0f);
        AddQuad(*_scene, nullptr, "B", 2.0f);
        EXPECT_EQ(Draw(), (Vector<float>{1.0f, 2.0f}));

        auto child = AddQuad(*_scene, a.Get(), "AChild", 3.0f);

        EXPECT_EQ(Draw(), (Vector<float>{1.0f, 3.0f, 2.0f}));
        EXPECT_NE(FindItem(*_scene, *child), nullptr);
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
        const Vector<PoolString> expected = {"B"_intern, "Root"_intern, "A"_intern, "AChild"_intern};
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
        EXPECT_EQ(Draw(), (Vector<float>{kMarkerId, 1.0f, 2.0f, kMarkerId + 1.0f}));
    }

    TEST_F(SceneDrawSystemTest, AQuadWithoutATransformDrawsNothing) {
        auto node = _scene->CreateNode("NoTransform"_intern, nullptr);
        node->AddComponent(BECore::New<QuadRendererComponent>());
        AddQuad(*_scene, nullptr, "Plain", 1.0f);

        EXPECT_EQ(Draw(), (Vector<float>{1.0f}));
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

        const Vector<float> ids = Draw();

        // The sprite is first in the scene, the quad on layer -1 is drawn before it
        EXPECT_EQ(ids, (Vector<float>{2.0f, 1.0f}));
        ASSERT_EQ(_renderer->calls.size(), 2u);
        EXPECT_EQ(At(_renderer->calls, 0).kind, DrawKind::Rect);
        EXPECT_EQ(At(_renderer->calls, 1).kind, DrawKind::Texture);
    }

    TEST_F(SceneDrawSystemTest, ANodeWithNothingSetMakesThePlainCallsWithAlphaBlend) {
        AddQuad(*_scene, nullptr, "Quad", 1.0f);
        AddSprite(0.0f);

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 2u);
        EXPECT_EQ(At(_renderer->calls, 0).kind, DrawKind::Rect);
        EXPECT_EQ(At(_renderer->calls, 0).blend, BlendMode::Alpha);
        EXPECT_EQ(At(_renderer->calls, 1).kind, DrawKind::Texture);
        EXPECT_EQ(At(_renderer->calls, 1).blend, BlendMode::Alpha);
        EXPECT_EQ(_renderer->GetBlendMode(), BlendMode::Alpha);
    }

    TEST_F(SceneDrawSystemTest, ARotatedQuadIsOneGeometryCallTurnedAboutTheCenterOfTheNode) {
        AddTurnedQuad(*_scene, 0.0f, 0.0f, kQuarterTurn);

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 1u);
        const DrawCall& call = At(_renderer->calls, 0);
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
        const DrawCall& call = At(_renderer->calls, 0);
        ASSERT_EQ(call.vertices.size(), 4u);
        ExpectCorner(call.vertices[0], 10.0f, 20.0f);
        ExpectCorner(call.vertices[1], 10.0f, 120.0f);
        ExpectCorner(call.vertices[2], -40.0f, 20.0f);
        ExpectCorner(call.vertices[3], -40.0f, 120.0f);
    }

    TEST_F(SceneDrawSystemTest, ARotatedContainSpriteTurnsAboutThePivotOfTheNodeNotOfTheFittedRect) {
        // A square texture in a 100 x 50 node: the fitted rect is (25, 0, 50, 50)
        auto node = AddSprite(kQuarterTurn);
        Require(node->GetComponent<SpriteRendererComponent>())._fit = SpriteFit::Contain;
        auto transform = node->GetComponent<TransformComponent>();
        ASSERT_TRUE(transform);
        transform->_pivotX = 0.0f;
        transform->_pivotY = 0.0f;

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 1u);
        const DrawCall& call = At(_renderer->calls, 0);
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
        Require(node->GetComponent<SpriteRendererComponent>())._fit = SpriteFit::Contain;

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 1u);
        const DrawCall& call = At(_renderer->calls, 0);
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
        ASSERT_TRUE(sprite);
        sprite->_flipX = true;

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 1u);
        const DrawCall& call = At(_renderer->calls, 0);
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
        EXPECT_NEAR(At(_renderer->calls, 0).vertices[0].u, 0.0f, kCornerTolerance);
        EXPECT_NEAR(At(_renderer->calls, 0).vertices[0].v, 1.0f, kCornerTolerance);
    }

    TEST_F(SceneDrawSystemTest, TheBlendModeIsOnTheCallOfTheNodeThatAsksForItAndNotOnTheNextOnes) {
        auto additive = AddQuad(*_scene, nullptr, "Additive", 1.0f);
        Require(additive->GetComponent<QuadRendererComponent>())._blend = BlendMode::Add;
        // [Quad(Add), Marker]: the OnDraw() of the next component of the same node sees Alpha again
        auto marker = BECore::New<DrawMarkerComponent>();
        marker->marker = kMarkerId;
        additive->AddComponent(marker);
        AddQuad(*_scene, nullptr, "Plain", 2.0f);
        auto spriteNode = AddSprite(0.0f);
        Require(spriteNode->GetComponent<SpriteRendererComponent>())._blend = BlendMode::Multiply;
        AddQuad(*_scene, nullptr, "Last", 3.0f);

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 5u);
        EXPECT_EQ(At(_renderer->calls, 0).blend, BlendMode::Add);
        EXPECT_EQ(At(_renderer->calls, 1).rect.x, kMarkerId);
        EXPECT_EQ(At(_renderer->calls, 1).blend, BlendMode::Alpha);
        EXPECT_EQ(At(_renderer->calls, 2).blend, BlendMode::Alpha);
        EXPECT_EQ(At(_renderer->calls, 3).kind, DrawKind::Texture);
        EXPECT_EQ(At(_renderer->calls, 3).blend, BlendMode::Multiply);
        EXPECT_EQ(At(_renderer->calls, 4).blend, BlendMode::Alpha);
        EXPECT_EQ(_renderer->GetBlendMode(), BlendMode::Alpha);
    }

    TEST_F(SceneDrawSystemTest, ARotatedAdditiveQuadHasTheBlendOnItsGeometryCall) {
        auto node = AddTurnedQuad(*_scene, 0.0f, 0.0f, 30.0f);
        Require(node->GetComponent<QuadRendererComponent>())._blend = BlendMode::Add;

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_EQ(At(_renderer->calls, 0).kind, DrawKind::Geometry);
        EXPECT_EQ(At(_renderer->calls, 0).blend, BlendMode::Add);
        EXPECT_EQ(_renderer->GetBlendMode(), BlendMode::Alpha);
    }

    TEST_F(SceneDrawSystemTest, ARotationSetBetweenTwoDrawsShowsInTheSecond) {
        auto node = AddTurnedQuad(*_scene, 0.0f, 0.0f, 0.0f);
        Draw();
        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_EQ(At(_renderer->calls, 0).kind, DrawKind::Rect);

        Require(node->GetComponent<TransformComponent>())._rotation = kQuarterTurn;
        Draw();
        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_EQ(At(_renderer->calls, 0).kind, DrawKind::Geometry);

        Require(node->GetComponent<TransformComponent>())._rotation = 0.0f;
        Draw();
        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_EQ(At(_renderer->calls, 0).kind, DrawKind::Rect);
    }

    TEST_F(SceneDrawSystemTest, TheOrderOfDrawIsTheSameForRotatedAndBlendedNodes) {
        auto front = AddTurnedQuad(*_scene, 1.0f, 0.0f, 30.0f);
        front->SetLayer(1);
        auto middle = AddQuad(*_scene, nullptr, "Middle", 2.0f);
        Require(middle->GetComponent<QuadRendererComponent>())._blend = BlendMode::Add;
        AddQuad(*_scene, nullptr, "Back", 3.0f, -1);

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 3u);
        EXPECT_EQ(At(_renderer->calls, 0).rect.x, 3.0f);
        EXPECT_EQ(At(_renderer->calls, 1).rect.x, 2.0f);
        EXPECT_EQ(At(_renderer->calls, 2).kind, DrawKind::Geometry);
    }

    TEST_F(SceneDrawSystemTest, AColorChangedBetweenTwoDrawsChangesTheSecondDraw) {
        auto node = AddQuad(*_scene, nullptr, "A", 1.0f);
        auto quad = node->GetComponent<QuadRendererComponent>();
        ASSERT_TRUE(quad);
        quad->_color = Color{10, 20, 30, 255};

        Draw();
        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_EQ(At(_renderer->calls, 0).color.r, 10);

        quad->_color = Color{200, 100, 50, 255};
        Draw();
        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_EQ(At(_renderer->calls, 0).color.r, 200);
        EXPECT_EQ(At(_renderer->calls, 0).color.g, 100);
        EXPECT_EQ(At(_renderer->calls, 0).color.b, 50);
    }

    TEST_F(SceneDrawSystemTest, AHiddenNodeDrawsNothingAndTheOthersKeepTheirOrder) {
        AddQuad(*_scene, nullptr, "A", 1.0f);
        auto b = AddQuad(*_scene, nullptr, "B", 2.0f);
        AddQuad(*_scene, nullptr, "C", 3.0f);
        EXPECT_EQ(Draw(), (Vector<float>{1.0f, 2.0f, 3.0f}));

        b->SetEditorFlags(EditorFlags::Hidden);
        EXPECT_EQ(Draw(), (Vector<float>{1.0f, 3.0f}));

        b->SetEditorFlags(EditorFlags::None);
        EXPECT_EQ(Draw(), (Vector<float>{1.0f, 2.0f, 3.0f}));
    }

    TEST_F(SceneDrawSystemTest, AHiddenNodeHidesItsDescendantsAndOnDrawToo) {
        auto a = AddQuad(*_scene, nullptr, "A", 1.0f);
        auto aChild = AddQuad(*_scene, a.Get(), "AChild", 2.0f);
        auto marker = BECore::New<DrawMarkerComponent>();
        marker->marker = kMarkerId;
        aChild->AddComponent(marker);
        AddQuad(*_scene, nullptr, "B", 3.0f);
        EXPECT_EQ(Draw(), (Vector<float>{1.0f, 2.0f, kMarkerId, 3.0f}));

        a->SetEditorFlags(EditorFlags::Hidden);
        EXPECT_EQ(Draw(), (Vector<float>{3.0f}));

        // A hidden child alone leaves its parent and its siblings visible
        a->SetEditorFlags(EditorFlags::None);
        aChild->SetEditorFlags(EditorFlags::Hidden);
        EXPECT_EQ(Draw(), (Vector<float>{1.0f, 3.0f}));
    }

    TEST_F(SceneDrawSystemTest, AHiddenNodeKeepsItsItemAndItsRank) {
        auto a = AddQuad(*_scene, nullptr, "A", 1.0f);
        auto aChild = AddQuad(*_scene, a.Get(), "AChild", 2.0f);
        auto b = AddQuad(*_scene, nullptr, "B", 3.0f, -1);
        _scene->Draw();
        const Vector<PoolString> before = ViewNames(*_scene);
        const int32_t rankA = _scene->GetDrawRank(*a);
        const int32_t rankChild = _scene->GetDrawRank(*aChild);
        const int32_t rankB = _scene->GetDrawRank(*b);

        a->SetEditorFlags(EditorFlags::Hidden);
        _scene->Draw();

        EXPECT_EQ(ViewNames(*_scene), before);
        EXPECT_EQ(_scene->GetDrawRank(*a), rankA);
        EXPECT_EQ(_scene->GetDrawRank(*aChild), rankChild);
        EXPECT_EQ(_scene->GetDrawRank(*b), rankB);
        ASSERT_NE(FindItem(*_scene, *a), nullptr);
        ASSERT_NE(FindItem(*_scene, *aChild), nullptr);
        ASSERT_NE(FindItem(*_scene, *b), nullptr);
        EXPECT_TRUE(FindItem(*_scene, *a)->hidden);
        EXPECT_TRUE(FindItem(*_scene, *aChild)->hidden);
        EXPECT_FALSE(FindItem(*_scene, *b)->hidden);
    }

    TEST_F(SceneDrawSystemTest, AFieldChangedWhileANodeIsHiddenShowsWhenItIsShownAgain) {
        auto node = AddQuad(*_scene, nullptr, "A", 1.0f);
        node->SetEditorFlags(EditorFlags::Hidden);

        Require(node->GetComponent<TransformComponent>())._x = 7.0f;
        EXPECT_TRUE(Draw().empty());

        node->SetEditorFlags(EditorFlags::None);
        EXPECT_EQ(Draw(), (Vector<float>{7.0f}));
    }

    TEST_F(SceneDrawSystemTest, SpritesOfOneTextureInARowAreOneBatchAndOneSubmission) {
        _renderer->SetBatchingEnabled(true);
        const auto texture = MakeSharedTexture();
        for (int i = 0; i < 4; ++i) {
            AddSharedSprite(texture, static_cast<float>(i) * 11.0f);
        }

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 1u) << "the pass ends with a flush: nothing is left in the batch";
        EXPECT_EQ(_renderer->calls[0].kind, DrawKind::Batch);
        EXPECT_EQ(_renderer->calls[0].quads, 4u);
        const RenderCounters counters = _renderer->TakeRenderCounters();
        EXPECT_EQ(counters.primitives, 4u);
        EXPECT_EQ(counters.drawCalls, 1u);
    }

    TEST_F(SceneDrawSystemTest, ATextureOrABlendChangeSplitsTheBatchInTheOrderOfTheScene) {
        _renderer->SetBatchingEnabled(true);
        const auto first = MakeSharedTexture();
        const auto second = MakeSharedTexture();
        AddSharedSprite(first, 0.0f);
        AddSharedSprite(first, 11.0f);
        AddSharedSprite(second, 22.0f);
        AddSharedSprite(second, 33.0f, nullptr, BlendMode::Add);
        AddSharedSprite(second, 44.0f);

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 4u);
        const uint32_t quads[] = {2, 1, 1, 1};
        const ITexture* textures[] = {first.Get(), second.Get(), second.Get(), second.Get()};
        const BlendMode blends[] = {BlendMode::Alpha, BlendMode::Alpha, BlendMode::Add, BlendMode::Alpha};
        for (size_t i = 0; i < 4; ++i) {
            EXPECT_EQ(At(_renderer->calls, i).quads, quads[i]) << i;
            EXPECT_EQ(At(_renderer->calls, i).texture, textures[i]) << i;
            EXPECT_EQ(At(_renderer->calls, i).blend, blends[i]) << i;
        }
        EXPECT_EQ(_renderer->GetBlendMode(), BlendMode::Alpha);
    }

    TEST_F(SceneDrawSystemTest, AClipRectSubtreeSplitsTheBatchAndEachPartKeepsItsClip) {
        _renderer->SetBatchingEnabled(true);
        const auto texture = MakeSharedTexture();
        AddSharedSprite(texture, 0.0f);
        auto clip = AddClip(kClipSize);
        AddSharedSprite(texture, 1.0f, clip.Get());
        AddSharedSprite(texture, 2.0f, clip.Get());
        AddSharedSprite(texture, 50.0f);

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 3u);
        EXPECT_EQ(At(_renderer->calls, 0).quads, 1u);
        EXPECT_FALSE(At(_renderer->calls, 0).clip.has_value());
        EXPECT_EQ(At(_renderer->calls, 1).quads, 2u);
        ASSERT_TRUE(At(_renderer->calls, 1).clip.has_value());
        EXPECT_FLOAT_EQ(At(_renderer->calls, 1).clip->w, kClipSize);
        EXPECT_EQ(At(_renderer->calls, 2).quads, 1u);
        EXPECT_FALSE(At(_renderer->calls, 2).clip.has_value());
    }

    TEST_F(SceneDrawSystemTest, ACustomGeometryDrawBetweenSpritesKeepsItsPlace) {
        _renderer->SetBatchingEnabled(true);
        const auto texture = MakeSharedTexture();
        AddSharedSprite(texture, 0.0f);
        AddSharedSprite(texture, 11.0f);
        AddGeometryMarker(kMarkerId);
        AddSharedSprite(texture, 22.0f);

        Draw();

        ASSERT_EQ(_renderer->calls.size(), 3u);
        EXPECT_EQ(At(_renderer->calls, 0).kind, DrawKind::Batch);
        EXPECT_EQ(At(_renderer->calls, 0).quads, 2u);
        EXPECT_EQ(At(_renderer->calls, 1).kind, DrawKind::Geometry);
        EXPECT_EQ(At(_renderer->calls, 2).kind, DrawKind::Batch);
        EXPECT_EQ(At(_renderer->calls, 2).quads, 1u);
    }

    TEST_F(SceneDrawSystemTest, ABatchedSceneDrawsTheSamePrimitivesInTheSameOrderAndUnderTheSameClipAsTheUnbatchedOne) {
        const auto first = MakeSharedTexture();
        const auto second = MakeSharedTexture();
        BuildMixedScene(first, second);

        _renderer->SetBatchingEnabled(false);
        _scene->Draw();
        const Vector<DrawnPrimitive> plain = Flatten(_renderer->calls);
        const RenderCounters plainCounters = _renderer->TakeRenderCounters();
        _renderer->ClearCalls();

        _renderer->SetBatchingEnabled(true);
        _scene->Draw();
        const Vector<DrawnPrimitive> batched = Flatten(_renderer->calls);
        const RenderCounters batchedCounters = _renderer->TakeRenderCounters();

        ASSERT_EQ(batched.size(), plain.size());
        for (size_t i = 0; i < plain.size(); ++i) {
            ExpectSamePrimitive(At(batched, i), At(plain, i), i);
        }
        EXPECT_EQ(batchedCounters.primitives, plainCounters.primitives);
        EXPECT_EQ(plainCounters.drawCalls, plainCounters.primitives);
        EXPECT_LT(batchedCounters.drawCalls, plainCounters.drawCalls);
        EXPECT_EQ(batchedCounters.drawCalls, _renderer->calls.size()) << "drawCalls is the number of batches and geometry draws";
    }

    TEST_F(SceneDrawSystemTest, ASteadyBatchedFrameAllocatesNothing) {
        _renderer->SetBatchingEnabled(true);
        const auto first = MakeSharedTexture();
        const auto second = MakeSharedTexture();
        BuildMixedScene(first, second);
        const auto frame = [&] {
            _renderer->ClearCalls();
            _scene->Draw();
            _renderer->TakeRenderCounters();
        };
        for (int i = 0; i < kWarmFrames; ++i) {
            frame();
        }
        ExpectNoAllocations(frame);
    }

}  // namespace BECore::Tests
