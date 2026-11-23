#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Components/ClickableComponent.h>
#include <BECore/Scene/Components/ClipRectComponent.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kSpilledComponents = SceneNode::kInlineComponents + 2;
        constexpr size_t kSpilledChildren = SceneNode::kInlineChildren + 2;

        // Four distinct types, in the order a node lists them: a node holds one component of each type
        constexpr StringView kComponentXml[] = {
            R"(<component type="TransformComponent" x="1" y="2" width="3" height="4"/>)",
            R"(<component type="ClipRectComponent"/>)",
            R"(<component type="ClickableComponent" actionName="Tap"/>)",
            R"(<component type="QuadRendererComponent"/>)",
        };
        constexpr size_t kComponentTypes = sizeof(kComponentXml) / sizeof(kComponentXml[0]);
        static_assert(kSpilledComponents <= kComponentTypes, "the test needs one more component type for a larger inline count");

        /// A scene `Main` with one node `Wide` that holds @p components components and @p children children, `Wide` itself on the root
        String WideSceneText(size_t components, size_t children) {
            String componentsXml;
            for (size_t i = 0; i < components; ++i) {
                componentsXml.append(kComponentXml[i].data(), kComponentXml[i].size());
            }
            String childrenXml;
            for (size_t i = 0; i < children; ++i) {
                childrenXml.append(Format(R"(<node name="C{}"/>)", i));
            }
            return SceneConfigText(Format(R"(<node name="Wide" layer="3"><components>{}</components><children>{}</children></node>)", componentsXml, childrenXml));
        }

        /// The type names of @p node's components in list order, one string
        String ComponentNames(const SceneNode& node) {
            String names;
            for (const auto& component : node.GetComponents()) {
                if (!names.empty()) {
                    names.append(" ");
                }
                names.append(component->GetTypeMeta().typeName.data(), component->GetTypeMeta().typeName.size());
            }
            return names;
        }

    }  // namespace

    /// The inline lists of SceneNode: what fits costs no heap block, what does not fit keeps the order and the saved text
    class SceneNodeInlineListsTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void Load(const String& text) {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(text.c_str()));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _scene = _manager->GetSceneByName("Main"_intern);
            ASSERT_NE(_scene, nullptr);
        }

        void TearDown() override {
            _manager.Reset();
        }

        SceneNode& Node(const char* name) {
            return Require(FindNodeBelow(_scene->GetRootNode(), name));
        }

        /// The saved element of the scene as one line
        String SavedScene() {
            _scene->PrepareForSave();
            XmlSerializer serializer;
            _scene->Serialize(serializer);
            return XmlText(serializer.GetRootNode());
        }

        IntrusivePtrAtomic<SceneManager> _manager;
        Scene* _scene = nullptr;
    };

    TEST_F(SceneNodeInlineListsTest, ANodeWithTheInlineCountsOwnsNoHeapBlockForItsLists) {
        Load(SceneConfigText(""));
        const IntrusivePtr<SceneNode> parent = _scene->CreateNode("Parent"_intern);

        // One round is the warm-up: the pool of nodes holds its page and the registry its free entity afterwards
        const auto buildAndDrop = [&]() {
            const auto transform = New<TransformComponent>();
            const auto clip = New<ClipRectComponent>();
            static_assert(SceneNode::kInlineComponents == 2, "the two components below are the inline count");
            static_assert(SceneNode::kInlineChildren == 1, "the one child below is the inline count");

            const AllocCount made = CountAllocations([&] {
                const IntrusivePtr<SceneNode> node = parent->AddChild("Node"_intern);
                node->AddComponent(transform);
                node->AddComponent(clip);
                node->AddChild("Child"_intern);
                EXPECT_EQ(node->GetComponents().size(), SceneNode::kInlineComponents);
                EXPECT_EQ(node->GetChildren().size(), SceneNode::kInlineChildren);
                parent->RemoveChild(node.Get());
            });
            return made;
        };

        buildAndDrop();
        const AllocCount second = buildAndDrop();
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(second.count, 0u) << second.bytes << " bytes";
        } else {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

    TEST_F(SceneNodeInlineListsTest, OneMoreThanTheInlineCountSpillsAndKeepsTheOrder) {
        ASSERT_NO_FATAL_FAILURE(Load(WideSceneText(kSpilledComponents, kSpilledChildren)));
        SceneNode& wide = Node("Wide");

        ASSERT_EQ(wide.GetComponents().size(), kSpilledComponents);
        ASSERT_EQ(wide.GetChildren().size(), kSpilledChildren);
        EXPECT_EQ(ComponentNames(wide), "TransformComponent ClipRectComponent ClickableComponent QuadRendererComponent");
        EXPECT_EQ(TreeOf(wide), "C0 C1 C2");
        EXPECT_EQ(wide.GetChildren()[2]->GetParent(), &wide);
    }

    TEST_F(SceneNodeInlineListsTest, RemovingFromASpilledNodeKeepsTheOtherEntriesInOrder) {
        ASSERT_NO_FATAL_FAILURE(Load(WideSceneText(kSpilledComponents, kSpilledChildren)));
        SceneNode& wide = Node("Wide");

        wide.RemoveComponent("ClipRectComponent"_intern);
        wide.RemoveChild(&Node("C1"));

        EXPECT_EQ(ComponentNames(wide), "TransformComponent ClickableComponent QuadRendererComponent");
        EXPECT_EQ(TreeOf(wide), "C0 C2");

        // The spilled list went back under the inline count of children and still takes new entries at the end
        wide.AddChild("C3"_intern);
        EXPECT_EQ(TreeOf(wide), "C0 C2 C3");
    }

    TEST_F(SceneNodeInlineListsTest, MoveAndDuplicateGiveTheSameTreeAsBefore) {
        ASSERT_NO_FATAL_FAILURE(Load(WideSceneText(kSpilledComponents, kSpilledChildren)));
        SceneNode& wide = Node("Wide");

        ASSERT_TRUE(_scene->MoveNode(Node("C0"), nullptr, 0));
        EXPECT_EQ(TreeOf(_scene->GetRootNode()), "C0 Wide C1. C2.");

        ASSERT_TRUE(_scene->MoveNode(Node("C0"), &wide, 1));
        EXPECT_EQ(TreeOf(_scene->GetRootNode()), "Wide C1. C0. C2.");

        const IntrusivePtr<SceneNode> copy = _scene->DuplicateNode(wide);
        ASSERT_TRUE(copy);
        EXPECT_EQ(TreeOf(_scene->GetRootNode()), "Wide C1. C0. C2. Wide (copy) C1. C0. C2.");
        EXPECT_EQ(ComponentNames(*copy), ComponentNames(wide));
        EXPECT_EQ(copy->GetLayer(), 3);
    }

    // The literals are the text the Vector form wrote for the same nodes (the lists are sequences of items in either form)
    TEST_F(SceneNodeInlineListsTest, ASpilledNodeIsSavedAsTheTextTheVectorFormWrote) {
        ASSERT_NO_FATAL_FAILURE(Load(WideSceneText(kSpilledComponents, kSpilledChildren)));

        constexpr StringView kExpected =
            R"(<root name="Main" viewWidth="0" viewHeight="0"><nodes><item type="SceneNode" name="Wide" layer="3" sourceRef=""><components><item type="TransformComponent" x="1" y="2" width="3" height="4" rotation="0" pivotX="0.5" pivotY="0.5"/><item type="ClipRectComponent"/><item type="ClickableComponent" actionName="Tap" shape="Rect" blocksInput="false"/><item type="QuadRendererComponent" blend="Alpha"><color r="255" g="0" b="0" a="255"/></item></components><children><item type="SceneNode" name="C0" layer="0" sourceRef=""><components/><children/></item><item type="SceneNode" name="C1" layer="0" sourceRef=""><components/><children/></item><item type="SceneNode" name="C2" layer="0" sourceRef=""><components/><children/></item></children></item></nodes></root>)";
        EXPECT_EQ(SavedScene(), kExpected);
    }

    TEST_F(SceneNodeInlineListsTest, ANodeWithinTheInlineCountsIsSavedAsTheTextTheVectorFormWrote) {
        constexpr StringView kNode =
            R"(<node name="Small" layer="1"><components><component type="TransformComponent" x="1" y="2" width="3" height="4"/><component type="ClipRectComponent"/></components><children><node name="Leaf"/></children></node>)";
        ASSERT_NO_FATAL_FAILURE(Load(SceneConfigText(kNode)));

        constexpr StringView kExpected =
            R"(<root name="Main" viewWidth="0" viewHeight="0"><nodes><item type="SceneNode" name="Small" layer="1" sourceRef=""><components><item type="TransformComponent" x="1" y="2" width="3" height="4" rotation="0" pivotX="0.5" pivotY="0.5"/><item type="ClipRectComponent"/></components><children><item type="SceneNode" name="Leaf" layer="0" sourceRef=""><components/><children/></item></children></item></nodes></root>)";
        EXPECT_EQ(SavedScene(), kExpected);
    }

}  // namespace BECore::Tests
