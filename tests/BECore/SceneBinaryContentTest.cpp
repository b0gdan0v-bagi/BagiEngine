#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ContentVersion.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/Scene/SceneNodeInstance.h>
#include <Common/ContentBinaryFiles.h>
#include <Common/EngineEnvironment.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kRootType = "SceneNode";
        constexpr StringView kHudRef = "nodes/Hud";

        constexpr StringView kHudBody = R"(name="Hud" layer="10"><components>
  <component type="TransformComponent" x="1" y="2" width="3" height="4"/>
  <component type="QuadRendererComponent"><color r="9" g="8" b="7" a="255"/></component>
</components><children><node name="Child" layer="1"/></children></node>)";

        String HudXml() {
            return Format("<node contentVersion=\"{}\" {}", kContentVersion, kHudBody);
        }

        pugi::xml_document Parse(StringView text) {
            pugi::xml_document doc;
            const pugi::xml_parse_result result = doc.load_buffer(text.data(), text.size());
            EXPECT_TRUE(result) << result.description();
            return doc;
        }

        /// The node of @p xml as a SceneNode, which is what a cook of the node file writes
        IntrusivePtr<SceneNode> NodeOf(StringView xml) {
            const pugi::xml_document doc = Parse(xml);
            return DeserializeNode(doc.document_element(), "test");
        }

        /// A document read back through the typed node and printed: the same text means the same node whatever the file's spelling
        String Normalized(const pugi::xml_document& doc) {
            pugi::xml_document out;
            AppendNodeElement(out, *DeserializeNode(doc.document_element(), "test"));
            return XmlTextIndented(out);
        }

        pugi::xml_document RefElement(StringView text) {
            return Parse(text);
        }

    }  // namespace

    /// Node loaders over two content layers (engine below, game above) in a temp folder
    class SceneBinaryContentTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = Path(TestTempDirectory("BagiSceneBinaryContentTest"));
            RemoveDirectory(_dir);
        }

        void TearDown() override {
            RemoveDirectory(_dir);
        }

        Path Engine(StringView ref, StringView extension) const {
            return _dir / "engineRoot" / "config" / Format("{}{}", ref, extension);
        }

        Path Game(StringView ref, StringView extension) const {
            return _dir / "gameRoot" / "config" / Format("{}{}", ref, extension);
        }

        void WriteBin(const Path& file, StringView xml) const {
            WriteContentBin(file, kRootType, *NodeOf(xml));
        }

        /// Both layers mounted, whatever each holds (a layer without files is still a `config` folder)
        const FileSystem& Mount() {
            MakeDirectory(_dir / "engineRoot" / "config");
            MakeDirectory(_dir / "gameRoot" / "config");
            _fs.AddContentRoot(_dir / "engineRoot");
            _fs.AddContentRoot(_dir / "gameRoot");
            _fs.Initialize();
            return _fs;
        }

        /// The loader's answer for @p ref: the normalized node, or the error
        String Load(StringView ref, bool* ok = nullptr) {
            const NodeFileLoader loader = MakeFileSystemNodeLoader(Mount());
            pugi::xml_document doc;
            String error;
            const bool loaded = loader(ref, doc, error);
            if (ok != nullptr) {
                *ok = loaded;
            }
            return loaded ? Normalized(doc) : error;
        }

        Path _dir;
        FileSystem _fs;
    };

    TEST_F(SceneBinaryContentTest, ACookedNodeFileGivesTheNodeOfItsXml) {
        WriteFile(Engine(kHudRef, ".xml"), HudXml());
        const String fromXml = Load(kHudRef);
        RemoveFileIfPresent(Engine(kHudRef, ".xml"));
        WriteBin(Engine(kHudRef, ".bin"), HudXml());
        _fs = FileSystem{};

        bool ok = false;
        const String fromBinary = Load(kHudRef, &ok);

        ASSERT_TRUE(ok) << fromBinary.c_str();
        EXPECT_EQ(fromBinary, fromXml);
        EXPECT_NE(fromBinary.find("Child"), String::npos);
    }

    TEST_F(SceneBinaryContentTest, OverridesMeanTheSameOnACookedNode) {
        constexpr StringView reference = R"(<node ref="nodes/Hud" name="Crate" layer="5"><components>
  <component type="TransformComponent" x="100"/>
  <component type="QuadRendererComponent"><color r="1" g="2" b="3" a="255"/></component>
</components></node>)";
        WriteFile(Engine(kHudRef, ".xml"), HudXml());
        const pugi::xml_document ref = RefElement(reference);
        pugi::xml_document expected;
        String error;
        ASSERT_TRUE(ExpandNodeReference(ref.document_element(), MakeFileSystemNodeLoader(Mount()), true, expected, error)) << error.c_str();
        const String expectedText = Normalized(expected);
        RemoveFileIfPresent(Engine(kHudRef, ".xml"));
        WriteBin(Engine(kHudRef, ".bin"), HudXml());
        _fs = FileSystem{};

        pugi::xml_document actual;
        ASSERT_TRUE(ExpandNodeReference(ref.document_element(), MakeFileSystemNodeLoader(Mount()), true, actual, error)) << error.c_str();

        EXPECT_EQ(Normalized(actual), expectedText);
        EXPECT_NE(expectedText.find("Crate"), String::npos);
        EXPECT_STREQ(actual.document_element().attribute(kNodeSourceAttribute.data()).as_string(), kHudRef.data());
    }

    TEST_F(SceneBinaryContentTest, TheUpperLayerWinsWhateverTheExtension) {
        WriteFile(Engine(kHudRef, ".xml"), Format("<node contentVersion=\"{}\" name=\"FromEngineXml\"/>", kContentVersion));
        WriteBin(Game(kHudRef, ".bin"), "<node name=\"FromGameBin\"/>");
        EXPECT_NE(Load(kHudRef).find("FromGameBin"), String::npos);

        _fs = FileSystem{};
        RemoveFileIfPresent(Game(kHudRef, ".bin"));
        RemoveFileIfPresent(Engine(kHudRef, ".xml"));
        WriteBin(Engine(kHudRef, ".bin"), "<node name=\"FromEngineBin\"/>");
        WriteFile(Game(kHudRef, ".xml"), Format("<node contentVersion=\"{}\" name=\"FromGameXml\"/>", kContentVersion));
        EXPECT_NE(Load(kHudRef).find("FromGameXml"), String::npos);
    }

    TEST_F(SceneBinaryContentTest, AnXmlAndABinOfOneReferenceInOneLayerAreAnErrorAndNoLowerLayerIsTried) {
        // The engine's file is fine: it must not stand in for the game's two
        WriteFile(Engine(kHudRef, ".xml"), Format("<node contentVersion=\"{}\" name=\"Engine\"/>", kContentVersion));
        WriteFile(Game(kHudRef, ".xml"), Format("<node contentVersion=\"{}\" name=\"Upper\"/>", kContentVersion));
        WriteBin(Game(kHudRef, ".bin"), "<node name=\"UpperBin\"/>");

        bool ok = true;
        const String error = Load(kHudRef, &ok);

        EXPECT_FALSE(ok);
        EXPECT_NE(error.find("Hud.xml"), String::npos);
        EXPECT_NE(error.find("Hud.bin"), String::npos);
    }

    TEST_F(SceneBinaryContentTest, ABinThatIsNotAStampedNodeIsADroppedReferenceThatNamesTheFile) {
        // Another root type, the right type with no stamp, and a file that is not a document
        WriteContentBin(Engine("nodes/WrongType", ".bin"), "SceneManager", SceneManager{});
        WriteContentBytes(Engine("nodes/NoStamp", ".bin"), UnstampedContentBytes(*NodeOf("<node name=\"A\"/>")));
        WriteContentBytes(Engine("nodes/Damaged", ".bin"), Vector<uint8_t>{1, 2, 3});

        for (const char* name : {"WrongType", "NoStamp", "Damaged"}) {
            bool ok = true;
            const String error = Load(Format("nodes/{}", name), &ok);
            EXPECT_FALSE(ok) << name;
            EXPECT_NE(error.find(Format("{}.bin", name)), String::npos) << error.c_str();
            _fs = FileSystem{};
        }
    }

    TEST_F(SceneBinaryContentTest, ACookedNodeIsInstantiatedAsItsXmlIs) {
        constexpr StringView reference = R"(<node ref="nodes/Hud" name="Crate"/>)";
        constexpr StringView sceneNodes = R"(<node name="A"/>)";
        WriteFile(Engine(kHudRef, ".xml"), HudXml());
        const pugi::xml_document ref = RefElement(reference);

        const auto instantiate = [&](Vector<String>& texts, PoolString& sourceRef) {
            const NodeFileLoader loader = MakeFileSystemNodeLoader(Mount());
            const pugi::xml_document config = Parse(SceneConfigText(sceneNodes, kMainSceneView800x600));
            auto manager = LoadSceneManager(XmlNode{config.document_element()}, loader);
            manager->Initialize();
            Scene& scene = Require(manager->GetSceneByName("Main"_intern));
            const IntrusivePtr<SceneNode> node = scene.InstantiateNode(ref.document_element(), loader);
            ASSERT_TRUE(node);
            texts = ComponentTexts(*node);
            sourceRef = node->GetSourceRef();
            texts.push_back(TreeOf(*node));
            manager.Reset();
        };
        Vector<String> expected;
        PoolString expectedSource;
        ASSERT_NO_FATAL_FAILURE(instantiate(expected, expectedSource));
        RemoveFileIfPresent(Engine(kHudRef, ".xml"));
        WriteBin(Engine(kHudRef, ".bin"), HudXml());
        _fs = FileSystem{};

        Vector<String> actual;
        PoolString actualSource;
        ASSERT_NO_FATAL_FAILURE(instantiate(actual, actualSource));

        EXPECT_EQ(actual, expected);
        EXPECT_EQ(actualSource, expectedSource);
        EXPECT_EQ(actualSource, PoolString::Intern(kHudRef));
    }

    /// Scene files: a scene config with inline nodes and one expanded from a node file, XML against its cooked form
    class SceneBinaryScenesTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = Path(TestTempDirectory("BagiSceneBinaryScenesTest"));
            RemoveDirectory(_dir);
            WriteFile(_dir / "nodes" / "Hud.xml", HudXml());
        }

        void TearDown() override {
            RemoveDirectory(_dir);
        }

        /// The scenes of the XML config: inline nodes with children and components, and a node file with an override
        IntrusivePtr<SceneManager> LoadFromXml() {
            const String text = SceneConfigText(R"(<node name="A" layer="3"><components><component type="TransformComponent" x="5" y="6"/></components><children><node name="A1"/></children></node>
<node ref="nodes/Hud" name="Panel"/>)",
                                                kMainSceneView800x600);
            const pugi::xml_parse_result parsed = _doc.load_string(text.c_str());
            EXPECT_TRUE(parsed) << parsed.description();
            return LoadSceneManager(XmlNode{_doc.document_element()}, MakeDirectoryNodeLoader(_dir));
        }

        static String Printed(SceneManager& manager) {
            XmlSerializer serializer;
            manager.Serialize(serializer);
            return serializer.SaveToString();
        }

        Path _dir;
        pugi::xml_document _doc;
    };

    TEST_F(SceneBinaryScenesTest, ACookedSceneConfigSerializesToTheTextOfTheXmlOne) {
        const auto fromXml = LoadFromXml();
        BinarySerializer writer;
        ASSERT_TRUE(WriteContentStamp(writer, kSceneManagerRootType));
        fromXml->Serialize(writer);
        const Vector<uint8_t> bytes = writer.GetData();

        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromView(bytes.data(), bytes.size()));
        const auto fromBinary = LoadSceneManagerFromBinary(reader);

        const String expected = Printed(*fromXml);
        EXPECT_EQ(Printed(*fromBinary), expected);
        EXPECT_NE(expected.find("Panel"), String::npos);
        EXPECT_NE(expected.find("TransformComponent"), String::npos);
        EXPECT_NE(expected.find(kHudRef.data()), String::npos) << "sourceRef of the expanded node";
    }

    TEST_F(SceneBinaryScenesTest, AStampOfAnotherTypeIsOneErrorAndAnEmptyManager) {
        const Vector<uint8_t> bytes = StampedContentBytes(kRootType, *NodeOf("<node name=\"A\"/>"));
        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromView(bytes.data(), bytes.size()));
        const LogRecorder log;

        const auto manager = LoadSceneManagerFromBinary(reader, "WrongRoot");

        LogEvent::Flush();
        EXPECT_EQ(log.Count(LogLevel::Error, {"WrongRoot"}), 1u);
        EXPECT_EQ(Printed(*manager), Printed(*New<SceneManager>()));
    }

    TEST_F(SceneBinaryScenesTest, LoadSceneManagerFromConfigReadsABinarySceneConfig) {
        const auto fromXml = LoadFromXml();
        WriteContentBin(_dir / "config" / "SceneConfig.bin", kSceneManagerRootType, *fromXml);
        FileSystem fs;
        ASSERT_TRUE(fs.Mount(MountPoints::config, _dir / "config"));
        ConfigManager configs{fs};
        configs.Initialize();
        ASSERT_TRUE(configs.IsBinaryConfig(kSceneConfigName));

        const auto loaded = LoadSceneManagerFromConfig(configs, fs);

        EXPECT_EQ(Printed(*loaded), Printed(*fromXml));
    }

    TEST_F(SceneBinaryScenesTest, ACookedSceneConfigOfAnotherLayoutIsAbsentAndTheManagerEmpty) {
        const auto fromXml = LoadFromXml();
        WriteContentBytes(_dir / "config" / "SceneConfig.bin", StampedContentBytesWithLayout(kSceneManagerRootType, 0x1234, *fromXml));
        FileSystem fs;
        ASSERT_TRUE(fs.Mount(MountPoints::config, _dir / "config"));
        ConfigManager configs{fs};
        const LogRecorder log;
        configs.Initialize();

        const auto loaded = LoadSceneManagerFromConfig(configs, fs);

        LogEvent::Flush();
        EXPECT_FALSE(configs.HasConfig(kSceneConfigName));
        EXPECT_EQ(Printed(*loaded), Printed(*New<SceneManager>()));
        EXPECT_GE(log.Count(LogLevel::Error, {"SceneConfig"}), 1u);
    }

}  // namespace BECore::Tests
