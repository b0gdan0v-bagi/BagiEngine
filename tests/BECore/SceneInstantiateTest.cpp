#include "SceneEventsTest.h"

#include <BECore/Config/ContentVersion.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Events/SceneEvents.h>
#include <Events/SubscriptionHolder.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // The node file of the instances: a box at layer 2 with a transform, a quad and a counter that subscribes in OnAttached
        constexpr const char* kBoxNode = R"(<node name="Box" layer="2"><components>
  <component type="TransformComponent" x="1" y="2" width="30" height="40"/>
  <component type="QuadRendererComponent"><color r="1" g="2" b="3" a="255"/></component>
  <component type="SceneEventCounter"/>
</components></node>)";

        constexpr const char* kCanvasNode = R"(<node name="Panel"><components>
  <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
    <elements>
      <element type="UILabel" name="Title" x="8" y="8" width="184" height="30" text="Base" fontSize="24"/>
      <element type="UILabel" name="Note" x="8" y="48" width="184" height="30" text="Note" fontSize="24"/>
    </elements>
  </component>
</components></node>)";

        constexpr const char* kBoxRef = "nodes/Box";

        void WriteFile(const std::filesystem::path& path, const std::string& text) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream(path) << text;
        }

        /// A scene "Main" with the given nodes; two plain nodes, A and C, are what the instances are placed between
        std::string SceneText(const std::string& nodesXml) {
            return "<SceneManager><scenes><scene type=\"Scene\" name=\"Main\" viewWidth=\"800\" viewHeight=\"600\"><nodes>" + nodesXml + "</nodes></scene></scenes></SceneManager>";
        }

        constexpr const char* kPlainNodes = R"(<node name="A"/><node name="C"/>)";

        /// What a component writes: its type and its serialization, one text per component of the node
        std::vector<std::string> ComponentTexts(const SceneNode& node) {
            std::vector<std::string> texts;
            for (const auto& component : node.GetComponents()) {
                XmlSerializer serializer;
                component->Serialize(serializer);
                std::ostringstream out;
                out << component->GetTypeMeta().typeName.data() << ":";
                serializer.GetRootNode().print(out, "", pugi::format_raw);
                texts.push_back(out.str());
            }
            return texts;
        }

        std::vector<std::string> ChildNames(const SceneNode& parent) {
            std::vector<std::string> names;
            for (const auto& child : parent.GetChildren()) {
                names.emplace_back(child->GetName().CStr());
            }
            return names;
        }

        /// Counts the errors that LogEvent::Flush() delivers while it lives and that have @p part in the text
        class ErrorCounter : public SubscriptionHolder {
        public:
            explicit ErrorCounter(eastl::string part) : _part(eastl::move(part)) {
                Subscribe<LogEvent, &ErrorCounter::OnLog>(this);
            }

            [[nodiscard]] size_t Count() const {
                LogEvent::Flush();
                return _count;
            }

        private:
            void OnLog(const LogEvent& event) {
                if (event.level == LogLevel::Error && event.message.find(eastl::string_view(_part.data(), _part.size())) != eastl::string_view::npos) {
                    ++_count;
                }
            }

            eastl::string _part;
            size_t _count = 0;
        };

    }  // namespace

    /// Scene::InstantiateNode over node files in a temp folder, with the engine services
    class SceneInstantiateTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("BagiSceneInstantiateTest");
            std::filesystem::remove_all(_dir);
            WriteFile(_dir / "nodes" / "Box.xml", kBoxNode);
            WriteFile(_dir / "nodes" / "Panel.xml", kCanvasNode);
            _loader = MakeDirectoryNodeLoader(_dir);
        }

        void TearDown() override {
            _manager.Reset();
            std::error_code ignored;
            std::filesystem::remove_all(_dir, ignored);
        }

        /// Loads @p sceneXml (a SceneConfig text) and keeps the manager; the scene "Main" is the one tests work on
        Scene& Load(const std::string& sceneXml) {
            const pugi::xml_parse_result parsed = _doc.load_string(sceneXml.c_str());
            EXPECT_TRUE(parsed) << parsed.description();
            _manager = LoadSceneManager(XmlNode{_doc.document_element()}, _loader);
            _manager->Initialize();
            Scene* scene = _manager->GetSceneByName(PoolString::Intern("Main"));
            EXPECT_NE(scene, nullptr);
            return *scene;
        }

        /// A `<node ref>` element of @p text, alive as long as the fixture
        pugi::xml_node Ref(const std::string& text) {
            const pugi::xml_parse_result parsed = _refDoc.load_string(text.c_str());
            EXPECT_TRUE(parsed) << parsed.description();
            return _refDoc.document_element();
        }

        /// The scene Main with A and C, and the instance of @p reference between them, loaded from a file the plain way (the expected one)
        Scene& LoadExpected(const std::string& reference) {
            return Load(SceneText(std::string("<node name=\"A\"/>") + reference + "<node name=\"C\"/>"));
        }

        std::filesystem::path _dir;
        NodeFileLoader _loader;
        pugi::xml_document _doc;
        pugi::xml_document _refDoc;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(SceneInstantiateTest, APlainInstanceHasTheValuesOfItsFile) {
        Scene& scene = Load(SceneText(kPlainNodes));
        const size_t before = scene.GetRootNode().GetChildren().size();

        const IntrusivePtr<SceneNode> node = scene.InstantiateNode(Ref(R"(<node ref="nodes/Box"/>)"), _loader);

        ASSERT_TRUE(node);
        EXPECT_EQ(node->GetName(), PoolString::Intern("Box"));
        EXPECT_EQ(node->GetLayer(), 2);
        EXPECT_EQ(node->GetSourceRef(), PoolString::Intern(kBoxRef));
        EXPECT_EQ(node->GetParent(), &scene.GetRootNode());
        EXPECT_TRUE(scene.GetRegistry().valid(node->GetEntity()));
        EXPECT_EQ(scene.GetRootNode().GetChildren().size(), before + 1);
        EXPECT_EQ(scene.GetRootNode().GetChildren().back().Get(), node.Get());
        ASSERT_EQ(node->GetComponents().size(), 3u);
        const auto transform = node->GetComponent<TransformComponent>();
        ASSERT_TRUE(transform);
        EXPECT_EQ(transform->_x, 1.0f);
        EXPECT_EQ(transform->_width, 30.0f);
        for (const auto& component : node->GetComponents()) {
            EXPECT_EQ(&component->GetNode(), node.Get());
            EXPECT_EQ(&component->GetScene(), &scene);
        }
    }

    TEST_F(SceneInstantiateTest, AReferenceWithoutOverridesFromAString) {
        // The overload without a loader reads through the FileSystem of CoreManager, which has no such node file here: an error, not a node
        Scene& scene = Load(SceneText(kPlainNodes));
        const ErrorCounter errors("nodes/NoSuchNode");

        EXPECT_FALSE(scene.InstantiateNode("nodes/NoSuchNode"));

        EXPECT_EQ(errors.Count(), 1u);
        EXPECT_EQ(ChildNames(scene.GetRootNode()), (std::vector<std::string>{"A", "C"}));
    }

    TEST_F(SceneInstantiateTest, OverridesGiveTheNodeOfTheSameReferenceInAScene) {
        const char* reference = R"(<node ref="nodes/Box" name="Crate" layer="5"><components>
  <component type="TransformComponent" x="100"/>
  <component type="QuadRendererComponent"><color r="9" g="8" b="7" a="255"/></component>
</components></node>)";
        Scene& expected = LoadExpected(reference);
        const SceneNode& expectedNode = *expected.GetRootNode().GetChildren()[1];
        const std::vector<std::string> expectedTexts = ComponentTexts(expectedNode);
        const int32_t expectedRank = expected.GetDrawRank(expectedNode);
        const PoolString expectedSource = expectedNode.GetSourceRef();
        _manager.Reset();

        Scene& scene = Load(SceneText(kPlainNodes));
        const IntrusivePtr<SceneNode> node = scene.InstantiateNode(Ref(reference), _loader, nullptr, 1);

        ASSERT_TRUE(node);
        EXPECT_EQ(ChildNames(scene.GetRootNode()), (std::vector<std::string>{"A", "Crate", "C"}));
        EXPECT_EQ(node->GetName(), PoolString::Intern("Crate"));
        EXPECT_EQ(node->GetLayer(), 5);
        EXPECT_EQ(node->GetSourceRef(), expectedSource);
        EXPECT_EQ(ComponentTexts(*node), expectedTexts);
        EXPECT_EQ(scene.GetDrawRank(*node), expectedRank);
        EXPECT_EQ(node->GetComponent<TransformComponent>()->_x, 100.0f);
        EXPECT_EQ(node->GetComponent<TransformComponent>()->_y, 2.0f);
    }

    TEST_F(SceneInstantiateTest, AnInstanceUnderAParentThatIsNotTheRoot) {
        Scene& scene = Load(SceneText(kPlainNodes));
        SceneNode* parent = scene.GetRootNode().GetChildren()[1].Get();
        const IntrusivePtr<SceneNode> existing = parent->AddChild("Existing"_intern);

        const IntrusivePtr<SceneNode> node = scene.InstantiateNode(Ref(R"(<node ref="nodes/Box"/>)"), _loader, parent);

        ASSERT_TRUE(node);
        EXPECT_EQ(node->GetParent(), parent);
        EXPECT_EQ(ChildNames(*parent), (std::vector<std::string>{"Existing", "Box"}));
        EXPECT_EQ(ChildNames(scene.GetRootNode()), (std::vector<std::string>{"A", "C"}));
        EXPECT_TRUE(scene.GetRegistry().valid(node->GetEntity()));
    }

    TEST_F(SceneInstantiateTest, TheIndexPicksThePlaceAmongTheChildren) {
        Scene& scene = Load(SceneText(kPlainNodes));
        const pugi::xml_node first = Ref(R"(<node ref="nodes/Box" name="M"/>)");

        scene.InstantiateNode(first, _loader, nullptr, 1);
        EXPECT_EQ(ChildNames(scene.GetRootNode()), (std::vector<std::string>{"A", "M", "C"}));

        scene.InstantiateNode(first, _loader, nullptr, 0);
        EXPECT_EQ(ChildNames(scene.GetRootNode()), (std::vector<std::string>{"M", "A", "M", "C"}));

        scene.InstantiateNode(first, _loader, nullptr, 99);
        EXPECT_EQ(ChildNames(scene.GetRootNode()), (std::vector<std::string>{"M", "A", "M", "C", "M"}));

        scene.InstantiateNode(first, _loader, nullptr, Scene::kAppend);
        EXPECT_EQ(scene.GetRootNode().GetChildren().size(), 6u);
        EXPECT_EQ(scene.GetRootNode().GetChildren().back()->GetName(), PoolString::Intern("M"));
    }

    TEST_F(SceneInstantiateTest, TwoInstancesOfOneFileDoNotInfluenceEachOther) {
        Scene& scene = Load(SceneText(kPlainNodes));

        const auto a = scene.InstantiateNode(Ref(R"(<node ref="nodes/Box" name="One"><components><component type="TransformComponent" x="50"/></components></node>)"), _loader);
        const auto b = scene.InstantiateNode(Ref(R"(<node ref="nodes/Box" name="Two" layer="9"/>)"), _loader);

        ASSERT_TRUE(a);
        ASSERT_TRUE(b);
        EXPECT_NE(a->GetEntity(), b->GetEntity());
        EXPECT_NE(a->GetComponent<TransformComponent>().Get(), b->GetComponent<TransformComponent>().Get());
        EXPECT_EQ(a->GetComponent<TransformComponent>()->_x, 50.0f);
        EXPECT_EQ(b->GetComponent<TransformComponent>()->_x, 1.0f);
        EXPECT_EQ(a->GetLayer(), 2);
        EXPECT_EQ(b->GetLayer(), 9);
    }

    TEST_F(SceneInstantiateTest, TheDrawRankFollowsThePositionAndTheLayer) {
        Scene& scene = Load(SceneText(kPlainNodes));
        const pugi::xml_node flat = Ref(R"(<node ref="nodes/Box" layer="0"/>)");

        const auto first = scene.InstantiateNode(flat, _loader, nullptr, 0);
        const auto middle = scene.InstantiateNode(flat, _loader, nullptr, 2);

        // The root is rank 0; on one layer the scene order decides: first, A, middle, C
        EXPECT_LT(scene.GetDrawRank(*first), scene.GetDrawRank(*scene.GetRootNode().GetChildren()[1]));
        EXPECT_LT(scene.GetDrawRank(*scene.GetRootNode().GetChildren()[1]), scene.GetDrawRank(*middle));
        EXPECT_LT(scene.GetDrawRank(*middle), scene.GetDrawRank(*scene.GetRootNode().GetChildren()[3]));

        // A higher layer is drawn after everything of the layer 0, wherever it stands in the order
        const auto front = scene.InstantiateNode(Ref(R"(<node ref="nodes/Box" layer="3"/>)"), _loader, nullptr, 0);
        const int32_t frontRank = scene.GetDrawRank(*front);
        for (const auto& child : scene.GetRootNode().GetChildren()) {
            if (child.Get() != front.Get()) {
                EXPECT_LT(scene.GetDrawRank(*child), frontRank) << child->GetName().CStr();
            }
        }
    }

    TEST_F(SceneInstantiateTest, AComponentThatSubscribesInOnAttachedReceivesEvents) {
        Scene& scene = Load(SceneText(kPlainNodes));
        _manager->SetActiveScene(PoolString::Intern("Main"));

        const auto node = scene.InstantiateNode(Ref(R"(<node ref="nodes/Box"/>)"), _loader);
        ASSERT_TRUE(node);
        const auto counter = node->GetComponent<SceneEventCounter>();
        ASSERT_TRUE(counter);
        EXPECT_EQ(counter->updateCount, 0);

        SceneEvents::SceneUpdateEvent::Emit();

        EXPECT_EQ(counter->updateCount, 1);
    }

    TEST_F(SceneInstantiateTest, ANodeFileWithACanvasIsALiveCanvas) {
        Scene& scene = Load(SceneText(kPlainNodes));
        const auto node = scene.InstantiateNode(Ref(R"(<node ref="nodes/Panel"/>)"), _loader);

        ASSERT_TRUE(node);
        const auto canvas = node->GetComponent<UICanvasComponent>();
        ASSERT_TRUE(canvas);
        EXPECT_EQ(&canvas->GetNode(), node.Get());
        EXPECT_EQ(canvas->_elements.size(), 2u);
    }

    TEST_F(SceneInstantiateTest, ACanvasOverrideReplacesTheWholeList) {
        const char* reference = R"(<node ref="nodes/Panel" name="Short"><components><component type="UICanvasComponent"><elements>
  <element type="UILabel" name="Only" x="1" y="2" width="3" height="4" text="X" fontSize="24"/>
</elements></component></components></node>)";
        Scene& expected = LoadExpected(reference);
        const std::vector<std::string> expectedTexts = ComponentTexts(*expected.GetRootNode().GetChildren()[1]);
        _manager.Reset();

        Scene& scene = Load(SceneText(kPlainNodes));
        const auto node = scene.InstantiateNode(Ref(reference), _loader, nullptr, 1);

        ASSERT_TRUE(node);
        EXPECT_EQ(node->GetComponent<UICanvasComponent>()->_elements.size(), 1u);
        EXPECT_EQ(ComponentTexts(*node), expectedTexts);
    }

    TEST_F(SceneInstantiateTest, ABadReferenceChangesNothingAndLogsOnce) {
        Scene& scene = Load(SceneText(kPlainNodes));
        const std::vector<std::string> before = ChildNames(scene.GetRootNode());
        const ErrorCounter errors("Scene 'Main'");

        EXPECT_FALSE(scene.InstantiateNode(Ref(R"(<node ref="../Box"/>)"), _loader));
        EXPECT_FALSE(scene.InstantiateNode(Ref(R"(<node ref="nodes/Missing"/>)"), _loader));
        EXPECT_FALSE(scene.InstantiateNode(Ref(R"(<node ref="nodes/Box"><components><component type="SpriteRendererComponent"/></components></node>)"), _loader));
        EXPECT_FALSE(scene.InstantiateNode(Ref(R"(<node ref="nodes/Box" color="red"/>)"), _loader));

        EXPECT_EQ(errors.Count(), 4u);
        EXPECT_EQ(ChildNames(scene.GetRootNode()), before);
    }

    TEST_F(SceneInstantiateTest, ALogLineNamesTheSceneAndTheReference) {
        Scene& scene = Load(SceneText(kPlainNodes));
        const ErrorCounter errors("nodes/Missing");

        EXPECT_FALSE(scene.InstantiateNode(Ref(R"(<node ref="nodes/Missing"/>)"), _loader));

        EXPECT_EQ(errors.Count(), 1u);
    }

    TEST_F(SceneInstantiateTest, ASceneThatIsNotInitializedGivesAnError) {
        auto scene = BECore::New<Scene>();
        const ErrorCounter errors("not initialized");

        EXPECT_FALSE(scene->InstantiateNode(Ref(R"(<node ref="nodes/Box"/>)"), _loader));

        EXPECT_EQ(errors.Count(), 1u);
    }

    TEST_F(SceneInstantiateTest, AParentOfAnotherSceneGivesAnError) {
        Scene& scene = Load(SceneText(kPlainNodes));
        auto other = BECore::New<Scene>();
        other->Initialize();
        SceneNode& foreign = other->GetRootNode();
        const ErrorCounter errors("not a node of this scene");

        EXPECT_FALSE(scene.InstantiateNode(Ref(R"(<node ref="nodes/Box"/>)"), _loader, &foreign));

        EXPECT_EQ(errors.Count(), 1u);
        EXPECT_EQ(ChildNames(scene.GetRootNode()), (std::vector<std::string>{"A", "C"}));
        EXPECT_TRUE(foreign.GetChildren().empty());
    }

    TEST_F(SceneInstantiateTest, ARemovedParentGivesAnError) {
        Scene& scene = Load(SceneText(kPlainNodes));
        const IntrusivePtr<SceneNode> removed = scene.GetRootNode().GetChildren()[0];
        scene.GetRootNode().RemoveChild(removed.Get());
        const ErrorCounter errors("not a node of this scene");

        EXPECT_FALSE(scene.InstantiateNode(Ref(R"(<node ref="nodes/Box"/>)"), _loader, removed.Get()));

        EXPECT_EQ(errors.Count(), 1u);
        EXPECT_TRUE(removed->GetChildren().empty());
    }

}  // namespace BECore::Tests
