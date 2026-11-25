#include "SceneGeneratedNodesTypes.h"

#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <Generated/SceneGeneratedNodesTypes.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    void GeneratingTestComponent::OnAttached() {
        for (int32_t i = 0; i < count; ++i) {
            const IntrusivePtr<SceneNode> part = GetNode().AddChild(PoolString::Intern(Format("Part{}", i).c_str()));
            part->AddComponent(New<TransformComponent>());
        }
    }

    namespace {

        constexpr const char* kScene = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Maker"><components><component type="GeneratingTestComponent" count="2"/></components></node>
    <node name="Plain"><children><node name="Inner"/></children></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr size_t kMadeParts = 2;

    }  // namespace

    /// SceneNode::IsGenerated: what is made by code is not saved and not copied
    class SceneGeneratedNodesTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kScene));
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
            SceneNode* node = FindNodeBelow(_scene->GetRootNode(), name);
            EXPECT_NE(node, nullptr) << name;
            return Require(node);
        }

        IntrusivePtr<SceneManager> _manager;
        Scene* _scene = nullptr;
    };

    TEST_F(SceneGeneratedNodesTest, ANodeFromAFileIsAuthoredAndWhatItsComponentMakesIsGenerated) {
        EXPECT_FALSE(Node("Maker").IsGenerated());
        EXPECT_FALSE(Node("Inner").IsGenerated());
        ASSERT_EQ(Node("Maker").GetChildren().size(), kMadeParts);
        EXPECT_TRUE(Node("Part0").IsGenerated());
        EXPECT_TRUE(Node("Part1").IsGenerated());
    }

    TEST_F(SceneGeneratedNodesTest, AddChildAndCreateNodeMakeGeneratedNodes) {
        EXPECT_TRUE(Node("Plain").AddChild("Added"_intern)->IsGenerated());
        EXPECT_TRUE(_scene->CreateNode("Created"_intern)->IsGenerated());
    }

    TEST_F(SceneGeneratedNodesTest, AnInstantiatedNodeIsGenerated) {
        pugi::xml_document reference;
        reference.append_child("node").append_attribute("ref").set_value("nodes/MakerFile");
        // A node file that makes its own children
        const auto loadMakerFile = [](StringView, pugi::xml_document& out, String&) {
            return static_cast<bool>(out.load_string(R"(<node name="MakerFile"><components><component type="GeneratingTestComponent" count="3"/></components></node>)"));
        };

        const IntrusivePtr<SceneNode> node = _scene->InstantiateNode(reference.document_element(), loadMakerFile);

        ASSERT_TRUE(node);
        EXPECT_TRUE(node->IsGenerated());
        EXPECT_EQ(node->GetChildren().size(), 3u);
    }

    TEST_F(SceneGeneratedNodesTest, PrepareForSaveLeavesAGeneratedTopLevelNodeOut) {
        const IntrusivePtr<SceneNode> spawned = _scene->CreateNode("Spawned"_intern);
        const IntrusivePtr<SceneNode> added = _scene->CreateNode("Added"_intern);
        added->SetGenerated(false);

        _scene->PrepareForSave();

        // Only the nodes the file holds and the one marked authored are in the saved list, in tree order
        XmlSerializer serializer;
        _scene->Serialize(serializer);
        Vector<String> saved;
        for (pugi::xml_node node : serializer.GetRootNode().child("nodes").children()) {
            saved.emplace_back(node.attribute("name").value());
        }
        EXPECT_EQ(saved, (Vector<String>{"Maker", "Plain", "Added"}));
    }

    TEST_F(SceneGeneratedNodesTest, ADuplicateIsAuthoredAndMakesItsOwnGeneratedChildren) {
        const IntrusivePtr<SceneNode> copy = _scene->DuplicateNode(Node("Maker"));

        ASSERT_TRUE(copy);
        EXPECT_FALSE(copy->IsGenerated());
        // The component of the copy made its two parts again; the two of the original were not copied, or there would be four
        ASSERT_EQ(copy->GetChildren().size(), kMadeParts);
        EXPECT_TRUE(copy->GetChildren()[0]->IsGenerated());
        EXPECT_EQ(Node("Maker").GetChildren().size(), kMadeParts);
    }

    TEST_F(SceneGeneratedNodesTest, ADuplicateOfAGeneratedNodeIsAuthoredAndLeavesItsGeneratedChildrenOut) {
        const IntrusivePtr<SceneNode> spawned = _scene->CreateNode("Spawned"_intern);
        spawned->AddChild("Inner"_intern);

        const IntrusivePtr<SceneNode> copy = _scene->DuplicateNode(*spawned);

        // The top node is copied whatever its flag, and the copy is authored; the generated child below it is left out
        ASSERT_TRUE(copy);
        EXPECT_FALSE(copy->IsGenerated());
        EXPECT_TRUE(copy->GetChildren().empty());
    }

    TEST_F(SceneGeneratedNodesTest, SetGeneratedFalseMakesANodeAuthored) {
        const IntrusivePtr<SceneNode> added = Node("Plain").AddChild("Added"_intern);
        added->SetGenerated(false);

        EXPECT_FALSE(added->IsGenerated());
        const IntrusivePtr<SceneNode> copy = _scene->DuplicateNode(Node("Plain"));
        ASSERT_TRUE(copy);
        EXPECT_EQ(copy->GetChildren().size(), 2u);  // Inner and Added
    }

}  // namespace BECore::Tests
