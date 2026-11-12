#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Components/ClickableComponent.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNodeInstance.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kBoxRef = "nodes/Box";
        constexpr const char* kPanelRef = "nodes/Panel";

        constexpr const char* kBoxNode = R"(<node name="Box" layer="2"><components>
  <component type="TransformComponent" x="1" y="2" width="30" height="40"/>
  <component type="QuadRendererComponent"><color r="1" g="2" b="3" a="255"/></component>
</components></node>)";

        // The same box with the transform written untidily: extra whitespace, a reversed order of attributes, defaults written out
        constexpr const char* kUntidyBoxNode = R"(<node   layer="2"   name="Box"  >

  <components>
      <component   height="40" width="30" y="2.0" x="1" rotation="0" pivotX="0" type="TransformComponent"   />
      <component type="QuadRendererComponent">
         <color a="255" b="3" g="2" r="1"/>
      </component>
  </components>
</node>)";

        // The colour of the quad is an attribute here (`r,g,b,a`) and an element in kBoxNode: the serializer writes one spelling for both
        constexpr const char* kAttributeBoxNode = R"(<node name="Box" layer="2"><components>
  <component type="TransformComponent" x="1" y="2" width="30" height="40"/>
  <component type="QuadRendererComponent" color="1,2,3,255"/>
</components></node>)";

        constexpr const char* kPanelNode = R"(<node name="Panel"><components>
  <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
    <elements>
      <element type="UILabel" name="Title" x="8" y="8" width="184" height="30" text="Base" fontSize="24"/>
      <element type="UILabel" name="Note" x="8" y="48" width="184" height="30" text="Note" fontSize="24"/>
    </elements>
  </component>
</components></node>)";

        void WriteFile(const std::filesystem::path& path, const std::string& text) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream(path) << text;
        }

        eastl::string Printed(pugi::xml_node node) {
            std::ostringstream out;
            node.print(out, "", pugi::format_raw);
            return eastl::string(out.str().c_str());
        }

        /// The components of @p node as text: type and serialized fields, in order
        std::vector<std::string> ComponentTexts(const SceneNode& node) {
            std::vector<std::string> texts;
            for (const auto& component : node.GetComponents()) {
                XmlSerializer serializer;
                component->Serialize(serializer);
                texts.push_back(std::string(component->GetTypeMeta().typeName.data()) + ":" + Printed(serializer.GetRootNode()).c_str());
            }
            return texts;
        }

    }  // namespace

    /// BuildNodeReference on instances of node files in a temp folder, with the engine services
    class SceneNodeInstanceTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("BagiSceneNodeInstanceTest");
            std::filesystem::remove_all(_dir);
            WriteFile(_dir / "nodes" / "Box.xml", kBoxNode);
            WriteFile(_dir / "nodes" / "Panel.xml", kPanelNode);
            _loader = MakeDirectoryNodeLoader(_dir);
        }

        void TearDown() override {
            _manager.Reset();
            std::error_code ignored;
            std::filesystem::remove_all(_dir, ignored);
        }

        /// Loads a scene Main whose only node is @p nodeXml (a `<node ref>` or an inline node) and returns that node
        SceneNode& LoadNode(const std::string& nodeXml) {
            const std::string text = "<SceneManager><scenes><scene type=\"Scene\" name=\"Main\"><nodes>" + nodeXml + "</nodes></scene></scenes></SceneManager>";
            const pugi::xml_parse_result parsed = _doc.load_string(text.c_str());
            EXPECT_TRUE(parsed) << parsed.description();
            _manager = LoadSceneManager(XmlNode{_doc.document_element()}, _loader);
            _manager->Initialize();
            Scene* scene = _manager->GetSceneByName(PoolString::Intern("Main"));
            EXPECT_NE(scene, nullptr);
            return *scene->GetRootNode().GetChildren().front();
        }

        /// BuildNodeReference into a fresh `<nodes>`; the result is the printed element that was appended, empty when nothing was
        bool Build(const SceneNode& node, std::string& result, eastl::string& reason) {
            _out.reset();
            pugi::xml_node nodesEl = _out.append_child("nodes");
            const bool built = BuildNodeReference(node, _loader, nodesEl, reason);
            if (!built) {
                EXPECT_FALSE(nodesEl.first_child()) << "a refusal appended something";
            }
            result = Printed(nodesEl).c_str();
            return built;
        }

        /// The round trip property: the reference that was built expands and loads to components that serialize as the live node's
        void ExpectRoundTrip(const SceneNode& live) {
            const pugi::xml_node refEl = _out.child("nodes").first_child();
            ASSERT_TRUE(refEl);
            pugi::xml_document expanded;
            eastl::string error;
            ASSERT_TRUE(ExpandNodeReference(refEl, _loader, false, expanded, error)) << error.c_str();
            const IntrusivePtr<SceneNode> loaded = DeserializeNode(expanded.document_element(), "round trip");
            EXPECT_EQ(ComponentTexts(*loaded), ComponentTexts(live));
            EXPECT_EQ(loaded->GetName(), live.GetName());
            EXPECT_EQ(loaded->GetLayer(), live.GetLayer());
        }

        /// Build must succeed and give exactly @p expected; the round trip holds
        void ExpectBuilt(const SceneNode& node, const std::string& expected) {
            std::string result;
            eastl::string reason;
            ASSERT_TRUE(Build(node, result, reason)) << reason.c_str();
            EXPECT_EQ(result, "<nodes>" + expected + "</nodes>");
            ExpectRoundTrip(node);
        }

        std::filesystem::path _dir;
        NodeFileLoader _loader;
        pugi::xml_document _doc;
        pugi::xml_document _out;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(SceneNodeInstanceTest, AnUntouchedInstanceIsABareReference) {
        SceneNode& node = LoadNode(R"(<node ref="nodes/Box"/>)");

        ExpectBuilt(node, R"(<node ref="nodes/Box"/>)");
    }

    TEST_F(SceneNodeInstanceTest, AnInstanceLoadedWithOverridesKeepsThemAsTheyAre) {
        SceneNode& node = LoadNode(R"(<node ref="nodes/Box" name="Crate"><components><component type="TransformComponent" x="9"/></components></node>)");

        ExpectBuilt(node, R"(<node ref="nodes/Box" name="Crate"><components><component type="TransformComponent" x="9"/></components></node>)");
    }

    TEST_F(SceneNodeInstanceTest, OneChangedFieldIsOneAttributeInOneComponent) {
        SceneNode& node = LoadNode(R"(<node ref="nodes/Box"/>)");
        node.GetComponent<TransformComponent>()->_x = 7.0f;

        ExpectBuilt(node, R"(<node ref="nodes/Box"><components><component type="TransformComponent" x="7"/></components></node>)");
    }

    TEST_F(SceneNodeInstanceTest, AValueChangedBackIsNoOverride) {
        SceneNode& node = LoadNode(R"(<node ref="nodes/Box" name="Crate"/>)");
        node.GetComponent<TransformComponent>()->_x = 7.0f;
        node.GetComponent<TransformComponent>()->_x = 1.0f;
        node.SetName(PoolString::Intern("Box"));

        ExpectBuilt(node, R"(<node ref="nodes/Box"/>)");
    }

    TEST_F(SceneNodeInstanceTest, NameAndLayerAreWrittenWhenTheyDiffer) {
        SceneNode& node = LoadNode(R"(<node ref="nodes/Box"/>)");
        node.SetName(PoolString::Intern("Crate"));
        node.SetLayer(5);

        ExpectBuilt(node, R"(<node ref="nodes/Box" name="Crate" layer="5"/>)");
    }

    TEST_F(SceneNodeInstanceTest, ALayerAloneAndANameAlone) {
        SceneNode& node = LoadNode(R"(<node ref="nodes/Box"/>)");
        node.SetLayer(0);
        ExpectBuilt(node, R"(<node ref="nodes/Box" layer="0"/>)");

        node.SetLayer(2);
        node.SetName(PoolString{});
        ExpectBuilt(node, R"(<node ref="nodes/Box" name=""/>)");
    }

    TEST_F(SceneNodeInstanceTest, ChangesInTwoComponentsAreTwoComponentElements) {
        SceneNode& node = LoadNode(R"(<node ref="nodes/Box"/>)");
        node.GetComponent<TransformComponent>()->_y = 9.0f;
        node.GetComponent<TransformComponent>()->_width = 31.0f;
        node.GetComponent<QuadRendererComponent>()->_color = Color{9, 8, 7, 255};

        std::string result;
        eastl::string reason;
        ASSERT_TRUE(Build(node, result, reason)) << reason.c_str();
        ExpectRoundTrip(node);
        // A field is written once, in one spelling, and a component that did not change is not named
        const pugi::xml_node components = _out.child("nodes").child("node").child("components");
        EXPECT_EQ(std::distance(components.children("component").begin(), components.children("component").end()), 2);
        const pugi::xml_node transform = components.find_child_by_attribute("component", "type", "TransformComponent");
        EXPECT_STREQ(transform.attribute("y").value(), "9");
        EXPECT_STREQ(transform.attribute("width").value(), "31");
        EXPECT_FALSE(transform.attribute("x"));
        EXPECT_TRUE(components.find_child_by_attribute("component", "type", "QuadRendererComponent"));
    }

    TEST_F(SceneNodeInstanceTest, AFieldTheFileWritesAsAnElementIsOneOverrideInTheSpellingOfTheSerializer) {
        // kBoxNode has `<color r g b a/>`; the serializer writes a colour as an element, so that is the override
        SceneNode& node = LoadNode(R"(<node ref="nodes/Box"/>)");
        node.GetComponent<QuadRendererComponent>()->_color = Color{200, 100, 50, 255};

        ExpectBuilt(node, R"(<node ref="nodes/Box"><components><component type="QuadRendererComponent"><color r="200" g="100" b="50" a="255"/></component></components></node>)");
    }

    TEST_F(SceneNodeInstanceTest, AFileThatSpellsAFieldAsAnAttributeGivesTheSameOverride) {
        // The file has `color="1,2,3,255"`: untouched it equals the live node, edited it gives the same override as above
        WriteFile(_dir / "nodes" / "Box.xml", kAttributeBoxNode);
        SceneNode& node = LoadNode(R"(<node ref="nodes/Box"/>)");
        ExpectBuilt(node, R"(<node ref="nodes/Box"/>)");

        node.GetComponent<QuadRendererComponent>()->_color = Color{200, 100, 50, 255};

        ExpectBuilt(node, R"(<node ref="nodes/Box"><components><component type="QuadRendererComponent"><color r="200" g="100" b="50" a="255"/></component></components></node>)");
    }

    TEST_F(SceneNodeInstanceTest, ACanvasWhoseElementsChangedGivesTheWholeList) {
        SceneNode& node = LoadNode(R"(<node ref="nodes/Panel"/>)");
        auto canvas = node.GetComponent<UICanvasComponent>();
        ASSERT_TRUE(canvas);
        ASSERT_EQ(canvas->_elements.size(), 2u);
        canvas->_elements.pop_back();

        std::string result;
        eastl::string reason;
        ASSERT_TRUE(Build(node, result, reason)) << reason.c_str();
        ExpectRoundTrip(node);
        const pugi::xml_node component = _out.child("nodes").child("node").child("components").child("component");
        EXPECT_STREQ(component.attribute("type").value(), "UICanvasComponent");
        const pugi::xml_node elements = component.child("elements");
        ASSERT_TRUE(elements) << result;
        EXPECT_EQ(std::distance(elements.children("element").begin(), elements.children("element").end()), 1);
        EXPECT_FALSE(component.attribute("space")) << "an unchanged field is not written: " << result;
    }

    TEST_F(SceneNodeInstanceTest, AnEmptiedListIsAnEmptyElementOrARefusal) {
        SceneNode& node = LoadNode(R"(<node ref="nodes/Panel"/>)");
        node.GetComponent<UICanvasComponent>()->_elements.clear();

        std::string result;
        eastl::string reason;
        ASSERT_TRUE(Build(node, result, reason)) << reason.c_str();

        // The serializer leaves an empty list out; the baseline has it, so the override is an empty element that replaces the file's list
        const pugi::xml_node component = _out.child("nodes").child("node").child("components").child("component");
        ASSERT_TRUE(component) << result;
        const pugi::xml_node elements = component.child("elements");
        ASSERT_TRUE(elements) << result;
        EXPECT_FALSE(elements.first_child()) << result;
        ExpectRoundTrip(node);
        // And it really loads empty
        pugi::xml_document expanded;
        eastl::string error;
        ASSERT_TRUE(ExpandNodeReference(_out.child("nodes").first_child(), _loader, false, expanded, error)) << error.c_str();
        EXPECT_TRUE(DeserializeNode(expanded.document_element(), "check")->GetComponent<UICanvasComponent>()->_elements.empty());
    }

    TEST_F(SceneNodeInstanceTest, AnAddedComponentIsRefused) {
        SceneNode& node = LoadNode(R"(<node ref="nodes/Box"/>)");
        node.AddComponent(BECore::New<ClickableComponent>());

        std::string result;
        eastl::string reason;
        EXPECT_FALSE(Build(node, result, reason));
        EXPECT_FALSE(reason.empty());
        EXPECT_EQ(result, "<nodes/>");
    }

    TEST_F(SceneNodeInstanceTest, ARemovedComponentIsRefused) {
        SceneNode& node = LoadNode(R"(<node ref="nodes/Box"/>)");
        node.RemoveComponent(PoolString::Intern("QuadRendererComponent"));

        std::string result;
        eastl::string reason;
        EXPECT_FALSE(Build(node, result, reason));
        EXPECT_FALSE(reason.empty());
        EXPECT_EQ(result, "<nodes/>");
    }

    TEST_F(SceneNodeInstanceTest, AReorderedComponentIsRefused) {
        SceneNode& node = LoadNode(R"(<node ref="nodes/Box"/>)");
        // Taking the transform out and putting it back puts it behind the quad
        const IntrusivePtr<IComponent> transform = node.GetComponent("TransformComponent");
        node.RemoveComponent(PoolString::Intern("TransformComponent"));
        node.AddComponent(transform);
        ASSERT_EQ(node.GetComponents().size(), 2u);

        std::string result;
        eastl::string reason;
        EXPECT_FALSE(Build(node, result, reason));
        EXPECT_FALSE(reason.empty());
        EXPECT_EQ(result, "<nodes/>");
    }

    TEST_F(SceneNodeInstanceTest, ANodeFileThatIsGoneIsRefused) {
        SceneNode& node = LoadNode(R"(<node ref="nodes/Box"/>)");
        std::filesystem::remove(_dir / "nodes" / "Box.xml");

        std::string result;
        eastl::string reason;
        EXPECT_FALSE(Build(node, result, reason));
        EXPECT_FALSE(reason.empty());
        EXPECT_EQ(result, "<nodes/>");
    }

    TEST_F(SceneNodeInstanceTest, ANodeWithoutASourceRefIsRefused) {
        SceneNode& node = LoadNode(R"(<node name="Own"/>)");

        std::string result;
        eastl::string reason;
        EXPECT_FALSE(Build(node, result, reason));
        EXPECT_FALSE(reason.empty());
        EXPECT_EQ(result, "<nodes/>");
    }

    TEST_F(SceneNodeInstanceTest, ANodeFileWithUnrelatedFormattingGivesNoOverrideForAnUntouchedInstance) {
        WriteFile(_dir / "nodes" / "Box.xml", kUntidyBoxNode);
        SceneNode& node = LoadNode(R"(<node ref="nodes/Box"/>)");

        ExpectBuilt(node, R"(<node ref="nodes/Box"/>)");
    }

    TEST_F(SceneNodeInstanceTest, AnEditOfAnUntidyFileIsStillOneOverride) {
        WriteFile(_dir / "nodes" / "Box.xml", kUntidyBoxNode);
        SceneNode& node = LoadNode(R"(<node ref="nodes/Box"/>)");
        node.GetComponent<TransformComponent>()->_rotation = 45.0f;

        ExpectBuilt(node, R"(<node ref="nodes/Box"><components><component type="TransformComponent" rotation="45"/></components></node>)");
    }

    TEST_F(SceneNodeInstanceTest, TheResultIsAppendedAfterWhatIsThere) {
        SceneNode& node = LoadNode(R"(<node ref="nodes/Box"/>)");
        pugi::xml_document out;
        pugi::xml_node nodesEl = out.append_child("nodes");
        nodesEl.append_child("node").append_attribute("name").set_value("First");
        eastl::string reason;

        ASSERT_TRUE(BuildNodeReference(node, _loader, nodesEl, reason)) << reason.c_str();

        EXPECT_EQ(Printed(nodesEl), eastl::string(R"(<nodes><node name="First"/><node ref="nodes/Box"/></nodes>)"));
    }

}  // namespace BECore::Tests
