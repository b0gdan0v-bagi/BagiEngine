#include "ContentCookTest.h"

#include <BECore/Config/ContentVersion.h>
#include <BECore/Cook/ContentCook.h>
#include <BECore/Cook/CookCommand.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNodeInstance.h>
#include <Common/CookTestSupport.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <Generated/ContentCookTest.gen.hpp>

namespace BECore::Tests {

    using namespace CookTestData;

    namespace {

        constexpr StringView kProbeXml = R"(<CookProbe id="7" title="seven" kind="seven-kind" extra="9">
  <items><item>1</item><item>2</item></items>
  <parts><part label="first" weight="1.5"/><part label="second"/></parts>
</CookProbe>
)";

        /// The object a config reads as: the unit of comparison between the XML and the cooked content
        String ReadAsText(ConfigManager& configs, StringView stem) {
            const auto reader = configs.CreateDeserializer(stem);
            if (!reader) {
                ADD_FAILURE() << "no config " << String(stem).c_str();
                return {};
            }
            const auto object = New<CookProbe>();
            object->Deserialize(*reader);
            reader->FinishLoad();
            EXPECT_FALSE(reader->HasErrors());
            XmlSerializer text;
            object->Serialize(text);
            return text.SaveToString();
        }

        bool Contains(const String& text, StringView part) {
            return text.find(part.data(), 0, part.size()) != String::npos;
        }

        constexpr StringView kHudRef = "nodes/Hud";

        /// The SceneManager a game reads from the layers of @p fs and @p configs, as text
        String SceneManagerText(ConfigManager& configs, const FileSystem& fs) {
            const IntrusivePtrAtomic<SceneManager> manager = LoadSceneManagerFromConfig(configs, fs);
            XmlSerializer text;
            manager->Serialize(text);
            return text.SaveToString();
        }

        /// The node file @p ref as the loader of @p fs gives it, read as a node and printed
        String NodeFileText(const FileSystem& fs, StringView ref) {
            const NodeFileLoader loader = MakeFileSystemNodeLoader(fs);
            pugi::xml_document document;
            String error;
            if (!loader(ref, document, error)) {
                return error;
            }
            pugi::xml_document out;
            AppendNodeElement(out, *DeserializeNode(document.document_element(), "test"));
            return XmlTextIndented(out);
        }

        /// What `Scene::InstantiateNode` makes of @p reference with the node files of @p fs: the node, its source reference and its layer, printed
        String InstanceText(const FileSystem& fs, const pugi::xml_node& reference) {
            const NodeFileLoader loader = MakeFileSystemNodeLoader(fs);
            pugi::xml_document scenes;
            EXPECT_TRUE(scenes.load_string(SceneText("<node name=\"A\"/>").c_str()));
            IntrusivePtrAtomic<SceneManager> manager = LoadSceneManager(XmlNode{scenes.document_element()}, loader);
            manager->Initialize();
            Scene& scene = Require(manager->GetSceneByName("Main"_intern));
            const IntrusivePtr<SceneNode> node = scene.InstantiateNode(reference, loader);
            String text;
            if (node) {
                pugi::xml_document out;
                AppendNodeElement(out, *node);
                text = Format("{}ref={}\n{}", XmlTextIndented(out), node->GetSourceRef().CStr(), TreeOf(scene.GetRootNode()));
            }
            manager.Reset();
            return text;
        }

    }  // namespace

    class ContentCookTest : public ::testing::Test {
    protected:
        CookReport Cook() {
            return CookContent(_tree.Sources(), _tree.Out(), _registry);
        }

        CookTestTree _tree;
        ContentCookRegistry _registry;
        CookableConfig<CookProbe> _probe{"Probe", false, _registry};
        CookableConfig<CookProbe> _strict{"StrictProbe", true, _registry};
    };

    TEST_F(ContentCookTest, ACookedConfigReadsEqualToItsXml) {
        _tree.Write(0, "Probe.xml", kProbeXml);

        const CookReport report = Cook();
        ASSERT_TRUE(report.Ok()) << report.ToText().c_str();
        ASSERT_NE(FindRow(report, 0, "Probe.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 0, "Probe.xml")->outcome, CookOutcome::Cooked);
        EXPECT_FALSE(std::filesystem::exists(_tree.OutConfig(0) / "Probe.xml")) << "a .xml beside a .bin of one stem is an error of ConfigManager";

        SourceContent source(_tree);
        CookedContent cooked(_tree.Out(), CookTestTree::kLayers);
        EXPECT_TRUE(cooked.Configs().IsBinaryConfig("Probe"));
        const String fromXml = ReadAsText(source.Configs(), "Probe");
        EXPECT_FALSE(fromXml.empty());
        EXPECT_EQ(ReadAsText(cooked.Configs(), "Probe"), fromXml);
    }

    TEST_F(ContentCookTest, TheUpperLayerStillWinsAfterTheCook) {
        _tree.Write(0, "Probe.xml", "<CookProbe id=\"1\" title=\"lower\"/>");
        _tree.Write(1, "Probe.xml", "<CookProbe id=\"2\" title=\"upper\"/>");
        _tree.Write(0, "OnlyLower.xml", "<CookProbe id=\"3\" title=\"only\"/>");
        _registry.Register({"OnlyLower", "CookProbe", &CookDetail::CookReflected<CookProbe>, &CookDetail::DescribeReflected<CookProbe>, false});

        const CookReport report = Cook();
        ASSERT_TRUE(report.Ok()) << report.ToText().c_str();

        CookedContent cooked(_tree.Out(), CookTestTree::kLayers);
        SourceContent source(_tree);
        EXPECT_TRUE(Contains(ReadAsText(cooked.Configs(), "Probe"), "upper"));
        EXPECT_EQ(ReadAsText(cooked.Configs(), "Probe"), ReadAsText(source.Configs(), "Probe"));
        EXPECT_EQ(ReadAsText(cooked.Configs(), "OnlyLower"), ReadAsText(source.Configs(), "OnlyLower"));
    }

    TEST_F(ContentCookTest, AFileWithNoCookerIsCopiedByteForByteAndListed) {
        constexpr StringView kOther = "<Other  keep=\"as is\"/>\r\n";
        _tree.Write(0, "Other.xml", kOther);
        _tree.Write(0, "data/notes.txt", "text");
        _tree.Write(0, ".DS_Store", "junk");

        const CookReport report = Cook();
        ASSERT_TRUE(report.Ok()) << report.ToText().c_str();
        ASSERT_NE(FindRow(report, 0, "Other.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 0, "Other.xml")->outcome, CookOutcome::Copied);
        ASSERT_NE(FindRow(report, 0, "data/notes.txt"), nullptr);
        EXPECT_EQ(FindRow(report, 0, "data/notes.txt")->outcome, CookOutcome::Copied);
        EXPECT_EQ(ReadFile(_tree.OutConfig(0) / "Other.xml"), kOther);
        EXPECT_EQ(ReadFile(_tree.OutConfig(0) / "data" / "notes.txt"), "text");
        EXPECT_EQ(FindRow(report, 0, ".DS_Store"), nullptr);
        EXPECT_TRUE(Contains(report.ToText(), "Copied: layer0/config/Other.xml")) << report.ToText().c_str();
    }

    TEST_F(ContentCookTest, ARelativeFolderIsKept) {
        _tree.Write(1, "sub/deeper/Probe.xml", kProbeXml);

        const CookReport report = Cook();
        ASSERT_TRUE(report.Ok()) << report.ToText().c_str();
        EXPECT_TRUE(std::filesystem::is_regular_file(_tree.OutConfig(1) / "sub" / "deeper" / "Probe.bin"));
        ASSERT_NE(FindRow(report, 1, "sub/deeper/Probe.xml"), nullptr);
    }

    TEST_F(ContentCookTest, AnUnknownKeyFailsTheCookAndNamesTheFileAndThePath) {
        _tree.Write(1, "Probe.xml", "<CookProbe id=\"1\" bogus=\"3\"><parts><part label=\"a\" wieght=\"2\"/></parts></CookProbe>");

        const CookReport report = Cook();
        EXPECT_FALSE(report.Ok());
        const CookRow* row = FindRow(report, 1, "Probe.xml");
        ASSERT_NE(row, nullptr);
        EXPECT_EQ(row->outcome, CookOutcome::Failed);
        EXPECT_GE(row->messages.size(), 2u);
        const String text = report.ToText();
        EXPECT_TRUE(Contains(text, "layer1/config/Probe.xml")) << text.c_str();
        EXPECT_TRUE(Contains(text, "bogus")) << text.c_str();
        EXPECT_TRUE(Contains(text, "wieght")) << text.c_str();
        EXPECT_FALSE(std::filesystem::exists(_tree.OutConfig(1) / "Probe.bin"));
    }

    TEST_F(ContentCookTest, AMalformedNumberFailsOnlyUnderStrictValues) {
        _tree.Write(0, "StrictProbe.xml", "<CookProbe id=\"12x\"/>");
        _tree.Write(0, "Probe.xml", "<CookProbe id=\"12x\"/>");

        const CookReport report = Cook();
        ASSERT_NE(FindRow(report, 0, "StrictProbe.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 0, "StrictProbe.xml")->outcome, CookOutcome::Failed);
        ASSERT_NE(FindRow(report, 0, "Probe.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 0, "Probe.xml")->outcome, CookOutcome::Cooked) << "the lenient read of a cooker that did not ask for strict values stays lenient";
    }

    TEST_F(ContentCookTest, AFileThatDoesNotParseFailsTheCook) {
        _tree.Write(0, "Probe.xml", "<CookProbe id=\"1\"");

        const CookReport report = Cook();
        EXPECT_FALSE(report.Ok());
        ASSERT_NE(FindRow(report, 0, "Probe.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 0, "Probe.xml")->outcome, CookOutcome::Failed);
        EXPECT_FALSE(std::filesystem::exists(_tree.OutConfig(0) / "Probe.bin"));
    }

    TEST_F(ContentCookTest, AnOutputFolderInsideAContentFolderIsRefusedBeforeAnythingIsWritten) {
        _tree.Write(1, "Probe.xml", kProbeXml);
        const std::filesystem::path inside = _tree.SourceConfig(1) / "cooked";

        const CookReport report = CookContent(_tree.Sources(), inside, _registry);
        EXPECT_FALSE(report.Ok());
        EXPECT_FALSE(std::filesystem::exists(inside));
        EXPECT_FALSE(std::filesystem::exists(_tree.SourceConfig(1) / "Probe.bin"));
    }

    TEST_F(ContentCookTest, AContentFolderInsideTheOutputFolderIsRefusedAndKept) {
        _tree.Write(0, "Probe.xml", kProbeXml);

        const CookReport report = CookContent(_tree.Sources(), _tree.Root(), _registry);
        EXPECT_FALSE(report.Ok());
        EXPECT_TRUE(std::filesystem::is_regular_file(_tree.SourceConfig(0) / "Probe.xml"));
    }

    TEST_F(ContentCookTest, AnXmlAndABinOfOneStemInOneLayerFailTheCook) {
        _tree.Write(0, "Probe.xml", kProbeXml);
        WriteFile(_tree.SourceConfig(0) / "Probe.bin", "x");
        _tree.Write(1, "Fine.xml", "<Fine/>");

        const CookReport report = Cook();
        EXPECT_FALSE(report.Ok());
        EXPECT_EQ(report.Count(CookOutcome::Failed), 2u) << "both files of the stem are named";
        ASSERT_NE(FindRow(report, 1, "Fine.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 1, "Fine.xml")->outcome, CookOutcome::Copied);
    }

    TEST_F(ContentCookTest, TwoCooksOfTheSameInputsGiveTheSameBytes) {
        _tree.Write(0, "Probe.xml", kProbeXml);
        _tree.Write(1, "sub/Probe2.xml", "<CookProbe id=\"2\" title=\"two\" kind=\"k\"/>");
        _registry.Register({"Probe2", "CookProbe", &CookDetail::CookReflected<CookProbe>, &CookDetail::DescribeReflected<CookProbe>, false});

        ASSERT_TRUE(Cook().Ok());
        const Vector<uint8_t> first0 = ReadFileBytes(_tree.OutConfig(0) / "Probe.bin");
        const Vector<uint8_t> first1 = ReadFileBytes(_tree.OutConfig(1) / "sub" / "Probe2.bin");
        ASSERT_TRUE(Cook().Ok());
        EXPECT_FALSE(first0.empty());
        EXPECT_EQ(ReadFileBytes(_tree.OutConfig(0) / "Probe.bin"), first0);
        EXPECT_EQ(ReadFileBytes(_tree.OutConfig(1) / "sub" / "Probe2.bin"), first1);
    }

    TEST_F(ContentCookTest, AFileOfAnEarlierCookIsGoneAndOtherFilesOfTheOutputFolderStay) {
        _tree.Write(0, "Probe.xml", kProbeXml);
        WriteFile(_tree.OutConfig(0) / "Gone.bin", "stale");
        WriteFile(_tree.OutConfig(1) / "deep" / "Gone.txt", "stale");
        WriteFile(_tree.Out() / "keep.txt", "mine");

        ASSERT_TRUE(Cook().Ok());
        EXPECT_FALSE(std::filesystem::exists(_tree.OutConfig(0) / "Gone.bin"));
        EXPECT_FALSE(std::filesystem::exists(_tree.OutConfig(1) / "deep"));
        EXPECT_EQ(ReadFile(_tree.Out() / "keep.txt"), "mine");
    }

    TEST_F(ContentCookTest, ASceneWithInlineNodesAndAReferenceCooksAndLoadsLikeItsXml) {
        _tree.Write(0, "nodes/Hud.xml", HudNodeText("Hud"));
        _tree.Write(1, "SceneConfig.xml", SceneText(R"(<node name="A" layer="3"><components><component type="TransformComponent" x="5"/></components></node>
<node ref="nodes/Hud" name="Crate" layer="5"><components>
  <component type="TransformComponent" x="100"/>
  <component type="QuadRendererComponent"><color r="1" g="2" b="3" a="255"/></component>
</components></node>)"));

        const CookReport report = Cook();
        ASSERT_TRUE(report.Ok()) << report.ToText().c_str();
        ASSERT_NE(FindRow(report, 1, "SceneConfig.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 1, "SceneConfig.xml")->outcome, CookOutcome::Cooked);
        EXPECT_TRUE(std::filesystem::is_regular_file(_tree.OutConfig(1) / "SceneConfig.bin"));
        EXPECT_FALSE(std::filesystem::exists(_tree.OutConfig(1) / "SceneConfig.xml"));

        SourceContent source(_tree);
        CookedContent cooked(_tree.Out(), CookTestTree::kLayers);
        ASSERT_TRUE(cooked.Configs().IsBinaryConfig(kSceneConfigName));
        const String fromXml = SceneManagerText(source.Configs(), _tree.Sources());
        EXPECT_TRUE(Contains(fromXml, "Crate"));
        EXPECT_TRUE(Contains(fromXml, "sourceRef=\"nodes/Hud\"")) << "the expansion marks the node with its file, as the loader does";
        EXPECT_EQ(SceneManagerText(cooked.Configs(), cooked.Files()), fromXml);
    }

    TEST_F(ContentCookTest, ASceneWithADanglingReferenceFailsTheCookWithTheExpansionMessage) {
        _tree.Write(1, "SceneConfig.xml", SceneText(R"(<node name="A"/><node ref="nodes/Missing"/>)"));

        const CookReport report = Cook();
        EXPECT_FALSE(report.Ok());
        const CookRow* row = FindRow(report, 1, "SceneConfig.xml");
        ASSERT_NE(row, nullptr);
        EXPECT_EQ(row->outcome, CookOutcome::Failed);
        EXPECT_TRUE(Contains(report.ToText(), "nodes/Missing")) << report.ToText().c_str();
        EXPECT_FALSE(std::filesystem::exists(_tree.OutConfig(1) / "SceneConfig.bin"));
    }

    TEST_F(ContentCookTest, AnUnknownKeyInASceneFailsTheCook) {
        _tree.Write(1, "SceneConfig.xml", SceneText(R"(<node name="A" colour="red"/>)"));

        const CookReport report = Cook();
        EXPECT_FALSE(report.Ok());
        EXPECT_TRUE(Contains(report.ToText(), "colour")) << report.ToText().c_str();
    }

    TEST_F(ContentCookTest, ASceneOrANodeFileOfAnotherContentVersionFailsLikeTheLoader) {
        _tree.Write(1, "SceneConfig.xml", SceneText(R"(<node name="A"/>)", 4));
        _tree.Write(0, "nodes/Old.xml", HudNodeText("Old", 4));

        const CookReport report = Cook();
        EXPECT_FALSE(report.Ok());
        ASSERT_NE(FindRow(report, 1, "SceneConfig.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 1, "SceneConfig.xml")->outcome, CookOutcome::Failed);
        ASSERT_NE(FindRow(report, 0, "nodes/Old.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 0, "nodes/Old.xml")->outcome, CookOutcome::Failed);
        EXPECT_TRUE(Contains(report.ToText(), "content version 4")) << report.ToText().c_str();
        EXPECT_FALSE(std::filesystem::exists(_tree.OutConfig(1) / "SceneConfig.bin"));
    }

    TEST_F(ContentCookTest, ANodeFileCooksToTheNodeOfItsXmlAndInstantiatesTheSame) {
        _tree.Write(0, "nodes/Hud.xml", HudNodeText("Hud"));

        const CookReport report = Cook();
        ASSERT_TRUE(report.Ok()) << report.ToText().c_str();
        EXPECT_EQ(FindRow(report, 0, "nodes/Hud.xml")->outcome, CookOutcome::Cooked);
        EXPECT_TRUE(std::filesystem::is_regular_file(_tree.OutConfig(0) / "nodes" / "Hud.bin")) << "the reference nodes/Hud finds the file by this path";
        EXPECT_FALSE(std::filesystem::exists(_tree.OutConfig(0) / "nodes" / "Hud.xml"));

        CookedContent cooked(_tree.Out(), CookTestTree::kLayers);
        const String fromXml = NodeFileText(_tree.Sources(), kHudRef);
        EXPECT_TRUE(Contains(fromXml, "Child"));
        EXPECT_EQ(NodeFileText(cooked.Files(), kHudRef), fromXml);

        pugi::xml_document plain;
        ASSERT_TRUE(plain.load_string(R"(<node ref="nodes/Hud"/>)"));
        pugi::xml_document overridden;
        ASSERT_TRUE(overridden.load_string(R"(<node ref="nodes/Hud" name="Crate" layer="5"><components>
  <component type="TransformComponent" x="100"/>
  <component type="QuadRendererComponent"><color r="1" g="2" b="3" a="255"/></component>
</components></node>)"));
        for (const pugi::xml_node reference : {plain.document_element(), overridden.document_element()}) {
            const String expected = InstanceText(_tree.Sources(), reference);
            EXPECT_TRUE(Contains(expected, "ref=nodes/Hud")) << expected.c_str();
            EXPECT_EQ(InstanceText(cooked.Files(), reference), expected);
        }
    }

    TEST_F(ContentCookTest, ANodeFileWithAnUnknownKeyOrAReferenceOfItsOwnFailsTheCook) {
        _tree.Write(0, "nodes/Typo.xml", R"(<node contentVersion="5" name="Typo" layr="1"/>)");
        _tree.Write(0, "nodes/Loop.xml", R"(<node contentVersion="5" ref="nodes/Other"/>)");

        const CookReport report = Cook();
        EXPECT_EQ(report.Count(CookOutcome::Failed), 2u) << report.ToText().c_str();
        EXPECT_TRUE(Contains(report.ToText(), "layr")) << report.ToText().c_str();
        EXPECT_TRUE(Contains(report.ToText(), "reference")) << report.ToText().c_str();
    }

    TEST_F(ContentCookTest, ANodeFileOfTheLowerLayerThatTheUpperOneShadowsIsCookedInItsOwnLayerOnly) {
        _tree.Write(0, "nodes/Hud.xml", HudNodeText("Low"));
        _tree.Write(1, "nodes/Hud.xml", HudNodeText("Up"));

        const CookReport report = Cook();
        ASSERT_TRUE(report.Ok()) << report.ToText().c_str();
        EXPECT_TRUE(std::filesystem::is_regular_file(_tree.OutConfig(0) / "nodes" / "Hud.bin"));
        EXPECT_TRUE(std::filesystem::is_regular_file(_tree.OutConfig(1) / "nodes" / "Hud.bin"));

        CookedContent both(_tree.Out(), CookTestTree::kLayers);
        EXPECT_TRUE(Contains(NodeFileText(both.Files(), kHudRef), "Up"));
        CookedContent lowerOnly(_tree.Out(), 1);
        EXPECT_TRUE(Contains(NodeFileText(lowerOnly.Files(), kHudRef), "Low"));
    }

    namespace {

        std::expected<Optional<CookRequest>, String> Parse(std::initializer_list<const char*> arguments) {
            Vector<String> list;
            for (const char* argument : arguments) {
                list.emplace_back(argument);
            }
            return ParseCookArguments(list);
        }

    }  // namespace

    TEST(CookCommandTest, NoCookFlagIsNoRequestAndForeignFlagsAreIgnored) {
        const auto none = Parse({"--control", "--cooked=1", "--cook-verifier"});
        ASSERT_TRUE(none.has_value());
        EXPECT_FALSE(none->has_value());
    }

    TEST(CookCommandTest, TheFolderAndTheVerifyFlagMakeTheRequestInAnyOrder) {
        for (const auto& parsed : {Parse({"--cook=out/cooked", "--cook-verify"}), Parse({"--cook-verify", "--headless", "--cook=out/cooked"})}) {
            ASSERT_TRUE(parsed.has_value()) << parsed.error().c_str();
            ASSERT_TRUE(parsed->has_value());
            EXPECT_EQ((*parsed)->outDir, "out/cooked");
            EXPECT_TRUE((*parsed)->verify);
        }
        const auto plain = Parse({"--cook=out"});
        ASSERT_TRUE(plain.has_value() && plain->has_value());
        EXPECT_FALSE((*plain)->verify);
    }

    TEST(CookCommandTest, AnEmptyMissingOrRepeatedFolderAndAVerifyWithoutACookAreMessages) {
        for (const auto& parsed : {Parse({"--cook="}), Parse({"--cook"}), Parse({"--cook=a", "--cook=b"}), Parse({"--cook-verify"})}) {
            ASSERT_FALSE(parsed.has_value());
            EXPECT_FALSE(parsed.error().empty());
        }
    }

    TEST(CookCommandTest, TheRunCooksAndVerifiesAFolderAndReturnsTheExitCode) {
        CookTestTree tree;
        tree.Write(0, "Plain.xml", "<Plain/>");
        tree.Write(1, "SceneConfig.xml", SceneText(R"(<node name="A"/>)"));
        const CookRequest request{FromStdString(tree.Out().string()), true};

        EXPECT_EQ(RunCookCommand(request, tree.Sources()), 0);
        EXPECT_TRUE(std::filesystem::is_regular_file(tree.OutConfig(1) / "SceneConfig.bin"));
        EXPECT_TRUE(std::filesystem::is_regular_file(tree.OutConfig(0) / "Plain.xml"));

        tree.Write(1, "SceneConfig.xml", SceneText(R"(<node name="A"/>)", 4));
        EXPECT_EQ(RunCookCommand(request, tree.Sources()), kCookExitFailed);
    }

}  // namespace BECore::Tests
