#include "SceneDrawListTest.h"

#include <BECore/Audio/AudioSourceComponent.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Components/ChangeToRandomColorDebugComponent.h>
#include <BECore/Scene/Components/ClipRectComponent.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneDrawSystem.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/SceneDrawSystemTest.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Generated/SceneDrawListTest.gen.hpp>
#include <entt/entt.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    void OnDrawTestComponent::OnDraw() {
        ++drawn;
        if (marker >= 0.0f) {
            CoreManager::GetRenderer()->DrawFilledRect(marker, 0.0f, 1.0f, 1.0f, Color{1, 2, 3, 4});
        }
    }

    class SceneDrawListTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _scene = BECore::New<Scene>();
            _scene->Initialize();
            _node = _scene->CreateNode("Node"_intern, nullptr);
        }

        IntrusivePtr<Scene> _scene;
        IntrusivePtr<SceneNode> _node;
    };

    TEST_F(SceneDrawListTest, TheBuiltInsThatNeverDrawStayOutOfTheList) {
        EXPECT_FALSE(TransformComponent().HasOnDraw());
        EXPECT_FALSE(QuadRendererComponent().HasOnDraw());
        EXPECT_FALSE(SpriteRendererComponent().HasOnDraw());
        EXPECT_FALSE(ClipRectComponent().HasOnDraw());
        EXPECT_FALSE(AudioSourceComponent().HasOnDraw());
        EXPECT_FALSE(ChangeToRandomColorDebugComponent().HasOnDraw());
    }

    TEST_F(SceneDrawListTest, AComponentThatDrawsOrSaysNothingStaysIn) {
        EXPECT_TRUE(OnDrawTestComponent().HasOnDraw());
        EXPECT_TRUE(PlainTestComponent().HasOnDraw()) << "a component that overrides nothing is in the list: false is an opt-out";
    }

    TEST_F(SceneDrawListTest, TheListIsStaleUntilTheFirstBuild) {
        EXPECT_FALSE(_scene->IsDrawListValid());
    }

    TEST_F(SceneDrawListTest, AComponentAddedOrRemovedMakesTheListStale) {
        _node->AddComponent(BECore::New<TransformComponent>());
        EXPECT_FALSE(_scene->IsDrawListValid());
        _node->RemoveComponent("TransformComponent"_intern);
        EXPECT_FALSE(_scene->IsDrawListValid());
    }

    TEST_F(SceneDrawListTest, ChangedEditorFlagsMakeTheListStale) {
        _node->SetEditorFlags(EditorFlags::Hidden);
        EXPECT_FALSE(_scene->IsDrawListValid());
    }

    namespace {

        constexpr const char* kBoxFile = R"(<node name="Box"><components>
  <component type="TransformComponent" x="1" y="1" width="2" height="2"/>
  <component type="QuadRendererComponent"/>
</components></node>)";

        constexpr float kMarkerBase = 100.0f;
        constexpr uint32_t kTextureSize = 2;
        constexpr size_t kBytesPerPixel = 4;
        constexpr size_t kSteadyNodes = 100;
        constexpr int kSteadyFrames = 10;
        constexpr float kClipSize = 10.0f;
        constexpr Color kGreen{0, 255, 0, 255};

        /// A node with a unit quad at x = @p id; the quad's rect in a recorded call tells which node it was.
        IntrusivePtr<SceneNode> AddQuad(Scene& scene, SceneNode* parent, const char* name, float id, int32_t layer = 0) {
            auto node = scene.CreateNode(PoolString::Intern(name), parent);
            node->SetLayer(layer);
            auto transform = BECore::New<TransformComponent>();
            transform->_x = id;
            node->AddComponent(transform);
            node->AddComponent(BECore::New<QuadRendererComponent>());
            return node;
        }

        IntrusivePtr<DrawMarkerComponent> MakeMarker(float marker) {
            auto component = BECore::New<DrawMarkerComponent>();
            component->marker = marker;
            return component;
        }

    }  // namespace

    /// When the draw list rebuilds and what it holds, seen through the draw calls and Scene::GetDrawListBuilds()
    class SceneDrawListBuildTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

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

        /// Draws once and returns the x of every rect call in order: the id of a quad or of a marker.
        Vector<float> Draw() {
            _renderer->calls.clear();
            _scene->Draw();
            Vector<float> ids;
            for (const DrawCall& call : _renderer->calls) {
                ids.push_back(call.rect.x);
            }
            return ids;
        }

        /// After a draw has settled the list, @p change must cost exactly one rebuild, at the next draw and no more.
        template <typename TChange>
        void ExpectOneBuildAfter(TChange&& change) {
            Draw();
            ASSERT_TRUE(_scene->IsDrawListValid());
            const uint32_t before = _scene->GetDrawListBuilds();

            change();
            EXPECT_FALSE(_scene->IsDrawListValid());
            EXPECT_EQ(_scene->GetDrawListBuilds(), before) << "nothing rebuilds before the next draw";
            Draw();
            EXPECT_EQ(_scene->GetDrawListBuilds(), before + 1);
            EXPECT_TRUE(_scene->IsDrawListValid());
            Draw();
            EXPECT_EQ(_scene->GetDrawListBuilds(), before + 1);
        }

        /// After a draw has settled the list, @p change must rebuild nothing.
        template <typename TChange>
        void ExpectNoBuildAfter(TChange&& change) {
            Draw();
            const uint32_t before = _scene->GetDrawListBuilds();
            change();
            Draw();
            EXPECT_EQ(_scene->GetDrawListBuilds(), before);
            EXPECT_TRUE(_scene->IsDrawListValid());
        }

        IntrusivePtr<ITexture> MakeTexture() {
            const Vector<uint8_t> pixels(static_cast<size_t>(kTextureSize) * kTextureSize * kBytesPerPixel, 255);
            return _renderer->CreateTexture(pixels.data(), kTextureSize, kTextureSize);
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtr<Scene> _scene;
    };

    TEST_F(SceneDrawListBuildTest, TwoDrawsInARowBuildOnce) {
        AddQuad(*_scene, nullptr, "A", 1.0f);
        EXPECT_EQ(_scene->GetDrawListBuilds(), 0u);

        Draw();
        EXPECT_EQ(_scene->GetDrawListBuilds(), 1u);
        EXPECT_TRUE(_scene->IsDrawListValid());

        Draw();
        EXPECT_EQ(_scene->GetDrawListBuilds(), 1u);
    }

    TEST_F(SceneDrawListBuildTest, AFieldChangeBuildsNothingAndShowsInTheNextDraw) {
        auto quad = AddQuad(*_scene, nullptr, "Quad", 1.0f);
        auto spriteNode = AddQuad(*_scene, nullptr, "Sprite", 2.0f);
        spriteNode->RemoveComponent("QuadRendererComponent"_intern);
        auto sprite = BECore::New<SpriteRendererComponent>();
        const IntrusivePtr<ITexture> first = MakeTexture();
        const IntrusivePtr<ITexture> second = MakeTexture();
        sprite->SetTexture(first);
        spriteNode->AddComponent(sprite);
        ASSERT_EQ(Draw(), (Vector<float>{1.0f, 2.0f}));
        ASSERT_EQ(At(_renderer->calls, 1).texture, first.Get());
        const uint32_t builds = _scene->GetDrawListBuilds();

        Require(quad->GetComponent<TransformComponent>())._x = 30.0f;
        Require(quad->GetComponent<QuadRendererComponent>())._color = kGreen;
        Require(quad->GetComponent<QuadRendererComponent>())._blend = BlendMode::Add;
        sprite->SetTexture(second);

        EXPECT_EQ(Draw(), (Vector<float>{30.0f, 2.0f}));
        EXPECT_EQ(At(_renderer->calls, 0).color.g, kGreen.g);
        EXPECT_EQ(At(_renderer->calls, 0).blend, BlendMode::Add);
        EXPECT_EQ(At(_renderer->calls, 1).texture, second.Get());
        EXPECT_EQ(_scene->GetDrawListBuilds(), builds);
    }

    TEST_F(SceneDrawListBuildTest, ANodeCreatedBuildsOnce) {
        ExpectOneBuildAfter([&] { AddQuad(*_scene, nullptr, "New", 1.0f); });
    }

    TEST_F(SceneDrawListBuildTest, ANodeRemovedBuildsOnce) {
        auto node = AddQuad(*_scene, nullptr, "A", 1.0f);
        ExpectOneBuildAfter([&] { _scene->GetRootNode().RemoveChild(node.Get()); });
    }

    TEST_F(SceneDrawListBuildTest, ANodeMovedBuildsOnce) {
        auto a = AddQuad(*_scene, nullptr, "A", 1.0f);
        auto b = AddQuad(*_scene, nullptr, "B", 2.0f);
        ExpectOneBuildAfter([&] { EXPECT_TRUE(_scene->MoveNode(*a, b.Get())); });
    }

    TEST_F(SceneDrawListBuildTest, ANodeDuplicatedBuildsOnce) {
        auto a = AddQuad(*_scene, nullptr, "A", 1.0f);
        ExpectOneBuildAfter([&] { EXPECT_TRUE(_scene->DuplicateNode(*a)); });
    }

    TEST_F(SceneDrawListBuildTest, ANodeInstantiatedBuildsOnce) {
        pugi::xml_document reference;
        ASSERT_TRUE(reference.load_string(R"(<node ref="nodes/Box"/>)"));
        const auto loader = [](StringView, pugi::xml_document& out, String&) { return static_cast<bool>(out.load_string(kBoxFile)); };

        ExpectOneBuildAfter([&] { EXPECT_TRUE(_scene->InstantiateNode(reference.document_element(), loader)); });
        EXPECT_EQ(Draw().size(), 1u);
    }

    TEST_F(SceneDrawListBuildTest, ALayerSetToAnotherValueBuildsOnceAndTheSameValueBuildsNothing) {
        auto a = AddQuad(*_scene, nullptr, "A", 1.0f);
        ExpectNoBuildAfter([&] { a->SetLayer(0); });
        ExpectOneBuildAfter([&] { a->SetLayer(3); });
        ExpectNoBuildAfter([&] { a->SetLayer(3); });
    }

    TEST_F(SceneDrawListBuildTest, AComponentAddedOrRemovedBuildsOnce) {
        auto node = AddQuad(*_scene, nullptr, "A", 1.0f);
        ExpectOneBuildAfter([&] { node->AddComponent(MakeMarker(kMarkerBase)); });
        ExpectOneBuildAfter([&] { node->RemoveComponent("DrawMarkerComponent"_intern); });
    }

    TEST_F(SceneDrawListBuildTest, TheEditorFlagsBuildOnlyWhenTheyChange) {
        auto node = AddQuad(*_scene, nullptr, "A", 1.0f);
        ExpectNoBuildAfter([&] { node->SetEditorFlags(EditorFlags::None); });
        ExpectOneBuildAfter([&] { node->SetEditorFlags(EditorFlags::Hidden); });
        EXPECT_TRUE(Draw().empty());
        ExpectNoBuildAfter([&] { node->SetEditorFlags(EditorFlags::Hidden); });
        ExpectOneBuildAfter([&] { node->SetEditorFlags(EditorFlags::None); });
        EXPECT_EQ(Draw(), (Vector<float>{1.0f}));
    }

    TEST_F(SceneDrawListBuildTest, ANodeWithNothingToDrawAndAHiddenSubtreeKeepTheirItemAndRank) {
        auto bare = _scene->CreateNode("Bare"_intern, nullptr);
        auto hidden = AddQuad(*_scene, nullptr, "Hidden", 1.0f);
        auto hiddenChild = AddQuad(*_scene, hidden.Get(), "HiddenChild", 2.0f);
        auto shown = AddQuad(*_scene, nullptr, "Shown", 3.0f);
        Draw();
        const int32_t rankOfShown = _scene->GetDrawRank(*shown);
        const int32_t rankOfChild = _scene->GetDrawRank(*hiddenChild);

        hidden->SetEditorFlags(EditorFlags::Hidden);

        EXPECT_EQ(Draw(), (Vector<float>{3.0f}));
        const auto& registry = _scene->GetRegistry();
        for (const SceneNode* node : {bare.Get(), hidden.Get(), hiddenChild.Get()}) {
            ASSERT_TRUE(registry.all_of<SceneDrawItem>(node->GetEntity())) << node->GetName().CStr();
            EXPECT_EQ(registry.get<SceneDrawItem>(node->GetEntity()).rank, _scene->GetDrawRank(*node));
        }
        EXPECT_EQ(_scene->GetDrawRank(*shown), rankOfShown);
        EXPECT_EQ(_scene->GetDrawRank(*hiddenChild), rankOfChild);
        EXPECT_LT(_scene->GetDrawRank(*bare), _scene->GetDrawRank(*hidden));
    }

    TEST_F(SceneDrawListBuildTest, AnOnDrawComponentStaysInItsPlaceAmongTheQuadOfItsNodeAndAcrossLayers) {
        auto node = _scene->CreateNode("Mixed"_intern, nullptr);
        auto transform = BECore::New<TransformComponent>();
        transform->_x = 1.0f;
        node->AddComponent(transform);
        node->AddComponent(MakeMarker(kMarkerBase));
        auto plain = BECore::New<PlainTestComponent>();
        node->AddComponent(plain);
        node->AddComponent(BECore::New<QuadRendererComponent>());
        auto counted = BECore::New<OnDrawTestComponent>();
        counted->marker = kMarkerBase + 1.0f;
        node->AddComponent(counted);
        auto silent = BECore::New<SilentTestComponent>();
        node->AddComponent(silent);
        auto front = AddQuad(*_scene, nullptr, "Front", 2.0f, 1);
        front->AddComponent(MakeMarker(kMarkerBase + 2.0f));
        auto back = _scene->CreateNode("Back"_intern, nullptr);
        back->SetLayer(-1);
        back->AddComponent(MakeMarker(kMarkerBase + 3.0f));

        // The component order of the node is the draw order inside it: the marker, the quad, the counted component
        const Vector<float> expected = {kMarkerBase + 3.0f, kMarkerBase, 1.0f, kMarkerBase + 1.0f, 2.0f, kMarkerBase + 2.0f};
        EXPECT_EQ(Draw(), expected);
        EXPECT_EQ(Draw(), expected);
        EXPECT_EQ(counted->drawn, 2);
        EXPECT_EQ(silent->drawn, 0) << "a component that says it never draws is never called";
    }

    TEST_F(SceneDrawListBuildTest, AClipRectComponentAddedOrRemovedBetweenTwoDrawsChangesTheClip) {
        auto owner = _scene->CreateNode("Owner"_intern, nullptr);
        auto ownerTransform = BECore::New<TransformComponent>();
        ownerTransform->_width = kClipSize;
        ownerTransform->_height = kClipSize;
        owner->AddComponent(ownerTransform);
        auto child = AddQuad(*_scene, owner.Get(), "Child", 2.0f);
        Require(child->GetComponent<TransformComponent>())._width = kClipSize * 5.0f;
        Require(child->GetComponent<TransformComponent>())._height = kClipSize * 5.0f;

        ASSERT_EQ(Draw().size(), 1u);
        EXPECT_FALSE(At(_renderer->calls, 0).clip.has_value());

        owner->AddComponent(BECore::New<ClipRectComponent>());
        ASSERT_EQ(Draw().size(), 1u);
        ASSERT_TRUE(At(_renderer->calls, 0).clip.has_value());
        EXPECT_EQ(Require(At(_renderer->calls, 0).clip).w, kClipSize);

        owner->RemoveComponent("ClipRectComponent"_intern);
        ASSERT_EQ(Draw().size(), 1u);
        EXPECT_FALSE(At(_renderer->calls, 0).clip.has_value());
    }

    TEST_F(SceneDrawListBuildTest, ASteadyFrameAllocatesNothing) {
        for (size_t index = 0; index < kSteadyNodes; ++index) {
            auto node = AddQuad(*_scene, nullptr, "Quad", static_cast<float>(index));
            auto sprite = AddQuad(*_scene, nullptr, "Sprite", static_cast<float>(index));
            sprite->RemoveComponent("QuadRendererComponent"_intern);
            auto spriteComponent = BECore::New<SpriteRendererComponent>();
            spriteComponent->SetTexture(MakeTexture());
            sprite->AddComponent(spriteComponent);
            auto custom = _scene->CreateNode("Custom"_intern, nullptr);
            custom->AddComponent(BECore::New<OnDrawTestComponent>());
        }
        Draw();
        ASSERT_EQ(_renderer->calls.size(), 2 * kSteadyNodes);
        auto first = AddQuad(*_scene, nullptr, "Changed", 0.0f);
        Draw();

        ExpectNoAllocations([&] {
            for (int frame = 0; frame < kSteadyFrames; ++frame) {
                Require(first->GetComponent<TransformComponent>())._x = static_cast<float>(frame);
                Require(first->GetComponent<QuadRendererComponent>())._color = kGreen;
                _renderer->ClearCalls();
                _scene->Draw();
            }
        });
    }

    TEST_F(SceneDrawListBuildTest, ASceneDrawnWithNoRendererStillCallsOnDraw) {
        AddQuad(*_scene, nullptr, "Quad", 1.0f);
        auto custom = _scene->CreateNode("Custom"_intern, nullptr);
        auto counted = BECore::New<OnDrawTestComponent>();
        custom->AddComponent(counted);
        CoreManager::GetRendererManager().SetRenderer({});

        _scene->Draw();
        _scene->Draw();

        EXPECT_EQ(counted->drawn, 2);
        EXPECT_EQ(_scene->GetDrawListBuilds(), 1u);
    }

}  // namespace BECore::Tests
