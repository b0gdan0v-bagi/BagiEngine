#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Resource/XmlResource.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIDocument.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/UITreeCompare.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;

        // The elements and variants of one screen, as the body of an inline <component> and as the body of a .ui.xml file
        constexpr const char* kScreenBody = R"(
  <elements>
    <element type="UIElement" name="Panel" anchor="TopLeft" x="16" y="16" width="220" height="96" color="0,0,0,160" blocksInput="true">
      <children>
        <element type="UIElement" name="Score" x="12" y="10" width="40%" height="28"/>
        <element type="UIElement" name="Row" layout="Stack" direction="Horizontal" spacing="8" padding="4,2,4,2" visible="false"/>
      </children>
    </element>
  </elements>
  <variants>
    <variant when="orientation:portrait">
      <overrides><set element="Panel" anchor="Top" x="0" y="16" width="90%"/></overrides>
    </variant>
  </variants>)";

        String ScenesXml(const String& componentAttributes, const String& componentBody) {
            return "<SceneManager><scenes><scene type=\"Scene\" name=\"Main\"><nodes><node name=\"Hud\"><components>"
                   "<component type=\"UICanvasComponent\" space=\"Screen\" scaleMode=\"ScaleWithScreen\" referenceWidth=\"1280\" referenceHeight=\"720\" match=\"0.5\" " +
                   componentAttributes + ">" + componentBody + "</component></components></node></nodes></scene></scenes></SceneManager>";
        }

        /// A directory of our own under the system's temporary one, removed with the object
        class TempDirectory {
        public:
            // One directory per test: ctest runs them in parallel processes, and the constructor clears the directory
            TempDirectory() : _path(TestTempDirectory("BagiEngineUIDocumentTest")) {
                std::filesystem::remove_all(_path);
                std::filesystem::create_directories(_path);
            }
            ~TempDirectory() {
                std::error_code ignored;
                std::filesystem::remove_all(_path, ignored);
            }

            /// Writes a file and returns its absolute path, which the file system takes as it is
            String Write(const char* name, const String& content) const {
                const auto file = _path / name;
                std::ofstream(file) << content;
                return FromStdString(file.generic_string());
            }

        private:
            std::filesystem::path _path;
        };

        /// What a front-end that is not XML hands over: a type, a name, the declarations of the element and its children
        struct BagElement {
            const char* type;
            const char* name;
            Vector<Pair<const char*, const char*>> declarations;
            Vector<BagElement> children;
        };

        void AddBagElement(pugi::xml_node parent, const BagElement& bag) {
            pugi::xml_node element = parent.append_child("element");
            element.append_attribute("type") = bag.type;
            element.append_attribute("name") = bag.name;
            for (const auto& [key, value] : bag.declarations) {
                element.append_attribute(key) = value;
            }
            if (!bag.children.empty()) {
                pugi::xml_node children = element.append_child("children");
                for (const BagElement& child : bag.children) {
                    AddBagElement(children, child);
                }
            }
        }

    }  // namespace

    class UIDocumentTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetResourceManager().ClearCache();
        }

        /// Loads a scene with one canvas on the node "Hud"
        IntrusivePtr<UICanvasComponent> LoadCanvas(const String& attributes, const String& body) {
            XmlDeserializer deserializer;
            const String xml = ScenesXml(attributes, body);
            EXPECT_TRUE(deserializer.LoadFromString(xml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            EXPECT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            for (const auto& node : _manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern("Hud")) {
                    return node->GetComponent<UICanvasComponent>();
                }
            }
            return {};
        }

        static String SourceAttribute(const String& path) {
            return "source=\"" + path + "\"";
        }

        static String DocumentXml(const String& body) {
            return "<ui>" + body + "</ui>";
        }

        TempDirectory _temp;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    // ---- the same tree from three sources ---------------------------------------------------------------------------------

    TEST_F(UIDocumentTest, AFileBuildsTheSameTreeAsInlineXml) {
        const auto inlineCanvas = LoadCanvas("", kScreenBody);
        ASSERT_TRUE(inlineCanvas);
        ASSERT_FALSE(inlineCanvas->GetRoot().children.empty());
        // Take the inline tree before the scene of the file canvas replaces the manager
        const auto expectedElements = inlineCanvas->GetRoot().children;
        const auto expectedVariants = inlineCanvas->GetLayout().variants;

        const String path = _temp.Write("hud.ui.xml", DocumentXml(kScreenBody));
        const auto fileCanvas = LoadCanvas(SourceAttribute(path), "");
        ASSERT_TRUE(fileCanvas);

        ExpectSameElements(expectedElements, fileCanvas->GetRoot().children);
        EXPECT_TRUE(expectedVariants == fileCanvas->GetLayout().variants);
        EXPECT_TRUE(fileCanvas->_elements.empty()) << "the elements of a file are not the inline ones, so they are not saved back with the scene";
    }

    TEST_F(UIDocumentTest, ABagOfDeclarationsBuildsTheSameTree) {
        const auto inlineCanvas = LoadCanvas("", kScreenBody);
        ASSERT_TRUE(inlineCanvas);
        const auto expectedElements = inlineCanvas->GetRoot().children;
        const auto expectedVariants = inlineCanvas->GetLayout().variants;

        // What a web-style front-end would produce once it has resolved its cascade: a type, a name and the declarations of each element
        const BagElement panel{"UIElement",
                               "Panel",
                               {{"anchor", "TopLeft"}, {"x", "16"}, {"y", "16"}, {"width", "220"}, {"height", "96"}, {"color", "0,0,0,160"}, {"blocksInput", "true"}},
                               {{"UIElement", "Score", {{"x", "12"}, {"y", "10"}, {"width", "40%"}, {"height", "28"}}, {}},
                                {"UIElement", "Row", {{"layout", "Stack"}, {"direction", "Horizontal"}, {"spacing", "8"}, {"padding", "4,2,4,2"}, {"visible", "false"}}, {}}}};
        pugi::xml_document document;
        pugi::xml_node ui = document.append_child("ui");
        AddBagElement(ui.append_child("elements"), panel);
        pugi::xml_node variants = ui.append_child("variants");
        pugi::xml_node variant = variants.append_child("variant");
        variant.append_attribute("when") = "orientation:portrait";
        pugi::xml_node set = variant.append_child("overrides").append_child("set");
        set.append_attribute("element") = "Panel";
        set.append_attribute("anchor") = "Top";
        set.append_attribute("x") = "0";
        set.append_attribute("y") = "16";
        set.append_attribute("width") = "90%";

        // No file and no string: the in-memory tree drives the deserializer
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromXmlNode(XmlNode(document.document_element())));
        UIScreenContent content;
        content.Deserialize(deserializer);

        EXPECT_FALSE(deserializer.HasErrors());
        ExpectSameElements(expectedElements, content.elements);
        EXPECT_TRUE(expectedVariants == content.variants);
    }

    TEST_F(UIDocumentTest, TheDocumentLaysOutLikeTheInlineScreen) {
        const String path = _temp.Write("hud.ui.xml", DocumentXml(kScreenBody));
        const auto canvas = LoadCanvas(SourceAttribute(path), "");
        ASSERT_TRUE(canvas);
        const UIElement& panel = *canvas->GetRoot().children.at(0);

        canvas->UpdateLayout(1280.0f, 720.0f);
        EXPECT_NEAR(panel.rect.x, 16.0f, kTolerance);
        EXPECT_NEAR(panel.rect.w, 220.0f, kTolerance);

        canvas->UpdateLayout(720.0f, 1280.0f);  // portrait: the variant of the file applies
        EXPECT_NEAR(panel.rect.w, 648.0f, kTolerance);
        EXPECT_NEAR(panel.rect.x, 36.0f, kTolerance);
    }

    // ---- the resource -----------------------------------------------------------------------------------------------------

    TEST_F(UIDocumentTest, ResourceManagerPicksTheLoaderByTheLongestSuffix) {
        const String uiPath = _temp.Write("screen.ui.xml", DocumentXml(kScreenBody));
        const String xmlPath = _temp.Write("plain.xml", "<root><value a=\"1\"/></root>");
        auto& resources = CoreManager::GetResourceManager();

        const auto document = resources.Load<UIDocument>(uiPath);
        const auto plain = resources.Load<XmlResource>(xmlPath);

        ASSERT_TRUE(document);
        EXPECT_EQ(document->GetState(), ResourceState::Loaded);
        EXPECT_EQ(document->GetTypeName(), PoolString::Intern("UIDocument"));
        ASSERT_TRUE(plain) << "a plain .xml still goes to XmlResourceLoader";
        EXPECT_EQ(plain->GetState(), ResourceState::Loaded);
    }

    TEST_F(UIDocumentTest, EveryInstantiationHasElementsOfItsOwn) {
        const String path = _temp.Write("hud.ui.xml", DocumentXml(kScreenBody));
        const auto document = CoreManager::GetResourceManager().Load<UIDocument>(path);
        ASSERT_TRUE(document);

        UIScreenContent first;
        UIScreenContent second;
        EXPECT_TRUE(document->Instantiate(first));
        EXPECT_TRUE(document->Instantiate(second));

        ASSERT_EQ(first.elements.size(), 1u);
        ASSERT_EQ(second.elements.size(), 1u);
        EXPECT_NE(first.elements[0].Get(), second.elements[0].Get());
        ExpectSameElements(first.elements, second.elements);
    }

    TEST_F(UIDocumentTest, TwoCanvasesOfOneFileDoNotShareTheirLayout) {
        const String path = _temp.Write("hud.ui.xml", DocumentXml(kScreenBody));
        const auto first = LoadCanvas(SourceAttribute(path), "");
        ASSERT_TRUE(first);
        const UIElement& firstPanel = *first->GetRoot().children.at(0);
        first->UpdateLayout(720.0f, 1280.0f);
        const float firstWidth = firstPanel.rect.w;

        // A second scene on the same file, laid out in the other shape
        const auto second = LoadCanvas(SourceAttribute(path), "");
        ASSERT_TRUE(second);
        second->UpdateLayout(1280.0f, 720.0f);

        EXPECT_NE(second->GetRoot().children.at(0).Get(), &firstPanel);
        EXPECT_NEAR(firstPanel.rect.w, firstWidth, kTolerance);
    }

    // ---- file and inline together -----------------------------------------------------------------------------------------

    TEST_F(UIDocumentTest, InlineElementsAndVariantsComeAfterTheFile) {
        const String path = _temp.Write("base.ui.xml", DocumentXml(R"(<elements><element type="UIElement" name="FromFile"/></elements>
<variants><variant when="orientation:portrait"><overrides><set element="FromFile" x="1"/></overrides></variant></variants>)"));
        const auto canvas = LoadCanvas(SourceAttribute(path), R"(<elements><element type="UIElement" name="Inline"/></elements>
<variants><variant when="orientation:portrait"><overrides><set element="FromFile" x="2"/></overrides></variant></variants>)");
        ASSERT_TRUE(canvas);

        ASSERT_EQ(canvas->GetRoot().children.size(), 2u);
        EXPECT_EQ(canvas->GetRoot().children[0]->name, PoolString::Intern("FromFile"));
        EXPECT_EQ(canvas->GetRoot().children[1]->name, PoolString::Intern("Inline"));
        ASSERT_EQ(canvas->GetLayout().variants.size(), 2u);
        EXPECT_FLOAT_EQ((*canvas->GetLayout().variants[1].overrides[0].x).value, 2.0f);  // the later one wins in the portrait shape
        EXPECT_EQ(canvas->_elements.size(), 1u) << "only the inline element is saved back";
    }

    TEST_F(UIDocumentTest, ARegionOfAnElementAndOfAnOverrideIsReadAndSavedBack) {
        const auto canvas = LoadCanvas("", R"(<elements><element type="UIElement" name="Plate" region="table.plate.1" width="50%"/></elements>
<variants><variant when="orientation:portrait"><overrides><set element="Plate" region="table.plate.2"/></overrides></variant></variants>)");
        ASSERT_TRUE(canvas);
        ASSERT_EQ(canvas->GetRoot().children.size(), 1u);
        EXPECT_EQ(canvas->GetRoot().children[0]->style.rect.region, "table.plate.1"_intern);
        ASSERT_EQ(canvas->GetLayout().variants.size(), 1u);
        ASSERT_TRUE(canvas->GetLayout().variants[0].overrides[0].region.has_value());
        EXPECT_EQ(*canvas->GetLayout().variants[0].overrides[0].region, "table.plate.2"_intern);

        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("component"));
        canvas->Serialize(serializer);
        serializer.EndObject();
        const String xml = serializer.SaveToString();
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(StringView(xml)));
        auto loaded = New<UICanvasComponent>();
        ASSERT_TRUE(deserializer.BeginObject("component"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        EXPECT_FALSE(deserializer.HasErrors());
        loaded->Initialize();

        ExpectSameElements(canvas->GetRoot().children, loaded->GetRoot().children);
        EXPECT_TRUE(canvas->GetLayout().variants == loaded->GetLayout().variants);
    }

    // ---- failures ---------------------------------------------------------------------------------------------------------

    TEST_F(UIDocumentTest, AMissingFileLeavesTheInlineScreen) {
        const String path = FromStdString((std::filesystem::temp_directory_path() / "BagiEngineUIDocumentTest" / "missing.ui.xml").generic_string());
        const auto canvas = LoadCanvas(SourceAttribute(path), R"(<elements><element type="UIElement" name="Inline"/></elements>)");
        ASSERT_TRUE(canvas);

        ASSERT_EQ(canvas->GetRoot().children.size(), 1u);
        EXPECT_EQ(canvas->GetRoot().children[0]->name, PoolString::Intern("Inline"));
    }

    TEST_F(UIDocumentTest, AMalformedFileIsNotLoaded) {
        const String path = _temp.Write("broken.ui.xml", "<ui><elements>");
        const auto document = CoreManager::GetResourceManager().Load<UIDocument>(path);
        UIScreenContent content;

        // The loader may hand back a failed resource or none at all; either way there is nothing to build
        EXPECT_TRUE(!document || document->GetState() != ResourceState::Loaded);
        if (document) {
            EXPECT_FALSE(document->Instantiate(content));
        }
        EXPECT_TRUE(content.elements.empty());
    }

    TEST_F(UIDocumentTest, ABadValueIsReportedAndTheRestIsBuilt) {
        const String path = _temp.Write("bad.ui.xml", DocumentXml(R"(<elements>
  <element type="UIElement" name="Bad" width="wide"/>
  <element type="UIElement" name="Good" width="50"/>
</elements>)"));
        const auto document = CoreManager::GetResourceManager().Load<UIDocument>(path);
        ASSERT_TRUE(document);

        UIScreenContent content;
        EXPECT_FALSE(document->Instantiate(content));
        ASSERT_EQ(content.elements.size(), 2u);
        EXPECT_EQ(content.elements[1]->style.rect.width, UILength::Px(50.0f));
    }

    TEST_F(UIDocumentTest, CompileTimeChecks) {
        static_assert(std::is_base_of_v<IResource, UIDocument>);
    }

}  // namespace BECore::Tests
