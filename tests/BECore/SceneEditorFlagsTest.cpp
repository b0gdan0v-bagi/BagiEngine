#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNodeInstance.h>
#include <Common/EngineEnvironment.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        String Print(const pugi::xml_node& node) {
            std::ostringstream out;
            node.print(out);
            return String(out.str().c_str());
        }

    }  // namespace

    /// The editor flags of a node (hidden, locked): editor state that no file, serializer or copy sees
    class SceneEditorFlagsTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _scene = BECore::New<Scene>();
            _scene->Initialize();
            _parent = _scene->CreateNode("Parent"_intern, nullptr);
            _child = _scene->CreateNode("Child"_intern, _parent.Get());
            _child->AddComponent(BECore::New<TransformComponent>());
        }

        IntrusivePtr<Scene> _scene;
        IntrusivePtr<SceneNode> _parent;
        IntrusivePtr<SceneNode> _child;
    };

    TEST_F(SceneEditorFlagsTest, ANodeStartsVisibleAndUnlocked) {
        EXPECT_EQ(_parent->GetEditorFlags(), EditorFlags::None);
        EXPECT_FALSE(_parent->HasEditorFlag(EditorFlags::Hidden));
        EXPECT_FALSE(_parent->HasEditorFlag(EditorFlags::Locked));
    }

    TEST_F(SceneEditorFlagsTest, TheTwoFlagsAreIndependent) {
        _parent->SetEditorFlags(EditorFlags::Hidden);
        EXPECT_TRUE(_parent->HasEditorFlag(EditorFlags::Hidden));
        EXPECT_FALSE(_parent->HasEditorFlag(EditorFlags::Locked));

        _parent->SetEditorFlags(_parent->GetEditorFlags() | EditorFlags::Locked);
        EXPECT_TRUE(_parent->HasEditorFlag(EditorFlags::Hidden));
        EXPECT_TRUE(_parent->HasEditorFlag(EditorFlags::Locked));

        _parent->SetEditorFlags(_parent->GetEditorFlags() & ~EditorFlags::Hidden);
        EXPECT_FALSE(_parent->HasEditorFlag(EditorFlags::Hidden));
        EXPECT_TRUE(_parent->HasEditorFlag(EditorFlags::Locked));
    }

    TEST_F(SceneEditorFlagsTest, TheFlagsAreInNeitherSerializeNorTheNodeElement) {
        const String plainElement = [&] {
            pugi::xml_document document;
            AppendNodeElement(document, *_parent);
            return Print(document);
        }();
        const String plainSerialized = [&] {
            XmlSerializer serializer;
            _parent->Serialize(serializer);
            return Print(serializer.GetRootNode());
        }();

        _parent->SetEditorFlags(EditorFlags::Hidden | EditorFlags::Locked);
        _child->SetEditorFlags(EditorFlags::Hidden);

        pugi::xml_document document;
        AppendNodeElement(document, *_parent);
        XmlSerializer serializer;
        _parent->Serialize(serializer);
        EXPECT_EQ(Print(document), plainElement);
        EXPECT_EQ(Print(serializer.GetRootNode()), plainSerialized);
    }

    TEST_F(SceneEditorFlagsTest, ADuplicateStartsVisibleAndUnlocked) {
        _parent->SetEditorFlags(EditorFlags::Hidden | EditorFlags::Locked);
        _child->SetEditorFlags(EditorFlags::Locked);

        auto copy = _scene->DuplicateNode(*_parent);

        ASSERT_TRUE(copy);
        EXPECT_EQ(copy->GetEditorFlags(), EditorFlags::None);
        ASSERT_EQ(copy->GetChildren().size(), 1u);
        EXPECT_EQ(copy->GetChildren()[0]->GetEditorFlags(), EditorFlags::None);
        EXPECT_EQ(_parent->GetEditorFlags(), EditorFlags::Hidden | EditorFlags::Locked);
    }

    TEST_F(SceneEditorFlagsTest, ClearEditorFlagsResetsTheWholeTree) {
        _scene->GetRootNode().SetEditorFlags(EditorFlags::Locked);
        _parent->SetEditorFlags(EditorFlags::Hidden);
        _child->SetEditorFlags(EditorFlags::Hidden | EditorFlags::Locked);

        _scene->ClearEditorFlags();

        EXPECT_EQ(_scene->GetRootNode().GetEditorFlags(), EditorFlags::None);
        EXPECT_EQ(_parent->GetEditorFlags(), EditorFlags::None);
        EXPECT_EQ(_child->GetEditorFlags(), EditorFlags::None);
    }

}  // namespace BECore::Tests
