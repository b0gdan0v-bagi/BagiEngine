#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        struct WalkEntry {
            const SceneNode* node = nullptr;
            int32_t layer = 0;
            size_t order = 0;
        };

        void CollectWalk(const SceneNode& node, Vector<WalkEntry>& entries) {
            entries.push_back({&node, node.GetLayer(), entries.size()});
            for (const auto& child : node.GetChildren()) {
                CollectWalk(*child, entries);
            }
        }

        /// The rank of every node of the tree as the draw order defines it (layer, then depth-first order), worked out here and not by the scene
        Vector<int32_t> ExpectedRanks(Scene& scene, Vector<const SceneNode*>& nodes) {
            Vector<WalkEntry> entries;
            CollectWalk(scene.GetRootNode(), entries);
            Vector<WalkEntry> sorted = entries;
            eastl::sort(sorted.begin(), sorted.end(), [](const WalkEntry& a, const WalkEntry& b) { return a.layer != b.layer ? a.layer < b.layer : a.order < b.order; });
            Vector<int32_t> ranks;
            for (const WalkEntry& entry : entries) {
                nodes.push_back(entry.node);
                const auto at = eastl::find_if(sorted.begin(), sorted.end(), [&entry](const WalkEntry& other) { return other.node == entry.node; });
                ranks.push_back(static_cast<int32_t>(at - sorted.begin()));
            }
            return ranks;
        }

        IntrusivePtr<SceneNode> AddNode(Scene& scene, SceneNode* parent, const char* name, int32_t layer = 0) {
            auto node = scene.CreateNode(PoolString::Intern(name), parent);
            node->SetLayer(layer);
            return node;
        }

    }  // namespace

    class SceneInputOrderTest : public ::testing::Test {
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

        /// Every node's GetDrawRank is the rank the draw order gives it
        void ExpectRanksMatchTheOrder() {
            Vector<const SceneNode*> nodes;
            const Vector<int32_t> expected = ExpectedRanks(*_scene, nodes);
            for (size_t i = 0; i < nodes.size(); ++i) {
                EXPECT_EQ(_scene->GetDrawRank(*nodes[i]), expected[i]) << nodes[i]->GetName().CStr();
            }
        }

        /// A tree with nested nodes and layers: Root, A (layer 2, with AChild on layer -1 and AGrand under it), B, C (layer 2, with CChild)
        void BuildTree() {
            _a = AddNode(*_scene, nullptr, "A", 2);
            _aChild = AddNode(*_scene, _a.Get(), "AChild", -1);
            AddNode(*_scene, _aChild.Get(), "AGrand");
            _b = AddNode(*_scene, nullptr, "B");
            _c = AddNode(*_scene, nullptr, "C", 2);
            AddNode(*_scene, _c.Get(), "CChild");
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtr<Scene> _scene;
        IntrusivePtr<SceneNode> _a;
        IntrusivePtr<SceneNode> _aChild;
        IntrusivePtr<SceneNode> _b;
        IntrusivePtr<SceneNode> _c;
    };

    TEST_F(SceneInputOrderTest, TwoComponentsOfOneNodeFollowTheComponentOrder) {
        auto node = AddNode(*_scene, nullptr, "N");
        auto transform = BECore::New<TransformComponent>();
        auto quad = BECore::New<QuadRendererComponent>();
        node->AddComponent(transform);
        node->AddComponent(quad);

        const int32_t first = _scene->GetInputOrder(*transform);
        const int32_t second = _scene->GetInputOrder(*quad);

        EXPECT_EQ(second, first + 1);
        EXPECT_EQ(first, _scene->GetDrawRank(*node) * Scene::kMaxComponentsPerNode);
    }

    TEST_F(SceneInputOrderTest, ALaterSiblingAChildAndAHigherLayerGiveAHigherKey) {
        auto first = AddNode(*_scene, nullptr, "First");
        auto child = AddNode(*_scene, first.Get(), "Child");
        auto second = AddNode(*_scene, nullptr, "Second");
        auto front = AddNode(*_scene, nullptr, "Front", 1);
        const auto key = [this](SceneNode& node) {
            auto component = BECore::New<TransformComponent>();
            node.AddComponent(component);
            return _scene->GetInputOrder(*component);
        };

        const int32_t firstKey = key(*first);
        const int32_t childKey = key(*child);
        const int32_t secondKey = key(*second);
        const int32_t frontKey = key(*front);

        EXPECT_LT(firstKey, childKey);   // a child is drawn over its parent
        EXPECT_LT(childKey, secondKey);  // a later sibling is over the earlier one and its subtree
        EXPECT_LT(secondKey, frontKey);  // a higher layer is over everything on a lower one
    }

    TEST_F(SceneInputOrderTest, ComponentsPastTheLastPlaceShareTheLastKeyAndStayBelowTheNextNode) {
        auto node = AddNode(*_scene, nullptr, "N");
        auto later = AddNode(*_scene, nullptr, "Later");
        auto laterTransform = BECore::New<TransformComponent>();
        later->AddComponent(laterTransform);
        auto transform = BECore::New<TransformComponent>();
        node->AddComponent(transform);

        // Only the arithmetic of the clamp is checked here: the first place of a node is its rank times the factor, the last one stays inside its factor
        const int32_t base = _scene->GetDrawRank(*node) * Scene::kMaxComponentsPerNode;
        EXPECT_EQ(_scene->GetInputOrder(*transform), base);
        EXPECT_LT(base + Scene::kMaxComponentsPerNode - 1, _scene->GetInputOrder(*laterTransform));
    }

    TEST_F(SceneInputOrderTest, TheRankBeforeAnyDrawIsTheWalkOfTheTree) {
        BuildTree();

        ExpectRanksMatchTheOrder();
    }

    TEST_F(SceneInputOrderTest, TheRankAfterAPassEqualsTheWalk) {
        BuildTree();

        _scene->Draw();

        ExpectRanksMatchTheOrder();
    }

    TEST_F(SceneInputOrderTest, TheRankFollowsAMoveAfterAPass) {
        BuildTree();
        _scene->Draw();

        ASSERT_TRUE(_scene->MoveNode(*_c, _a.Get(), 0));
        ExpectRanksMatchTheOrder();

        _scene->Draw();
        ExpectRanksMatchTheOrder();
    }

    TEST_F(SceneInputOrderTest, TheRankFollowsAnAddedNodeAfterAPass) {
        BuildTree();
        _scene->Draw();

        AddNode(*_scene, _b.Get(), "BChild", 5);
        _a->AddChild("AChild2"_intern);

        ExpectRanksMatchTheOrder();
    }

    TEST_F(SceneInputOrderTest, TheRankFollowsALayerChangeAfterAPass) {
        BuildTree();
        _scene->Draw();

        _b->SetLayer(3);  // B was in the middle, now in front of every node
        ExpectRanksMatchTheOrder();

        _aChild->SetLayer(2);  // another node's layer moves the rank of nodes it did not touch
        ExpectRanksMatchTheOrder();
    }

    TEST_F(SceneInputOrderTest, TheRankFollowsARemovedAndADuplicatedNodeAfterAPass) {
        BuildTree();
        _scene->Draw();

        _a->RemoveChild(_aChild.Get());
        ExpectRanksMatchTheOrder();

        _scene->Draw();
        ASSERT_TRUE(_scene->DuplicateNode(*_c));
        ExpectRanksMatchTheOrder();
    }

    TEST_F(SceneInputOrderTest, TheRankIsStillRightAfterTheEditAndTheNextPass) {
        BuildTree();
        _scene->Draw();
        _b->SetLayer(3);

        _scene->Draw();

        ExpectRanksMatchTheOrder();
    }

    TEST_F(SceneInputOrderTest, ANodeOfAnotherSceneHasRankZero) {
        BuildTree();
        _scene->Draw();
        auto other = BECore::New<Scene>();
        other->Initialize();
        auto foreign = other->CreateNode("Foreign"_intern, nullptr);

        EXPECT_EQ(_scene->GetDrawRank(*foreign), 0);
    }

    TEST_F(SceneInputOrderTest, TheDrawPassMovesOncePerDraw) {
        EXPECT_EQ(_scene->GetDrawPass(), 0u);

        _scene->Draw();
        EXPECT_EQ(_scene->GetDrawPass(), 1u);

        _scene->Draw();
        _scene->Draw();
        EXPECT_EQ(_scene->GetDrawPass(), 3u);
    }

    TEST_F(SceneInputOrderTest, ASceneWithoutARootDoesNotStartAPass) {
        auto bare = BECore::New<Scene>();

        bare->Draw();

        EXPECT_EQ(bare->GetDrawPass(), 0u);
    }

}  // namespace BECore::Tests
