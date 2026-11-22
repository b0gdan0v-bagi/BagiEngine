// `VerifyCooked`: what the cook wrote reads as the XML it came from. The negative cases are what keeps the check honest: a comparison that cannot
// fail proves nothing.

#include "ContentCookEquivalenceTest.h"

#include <BECore/Cook/ContentVerify.h>
#include <BECore/Reflection/BinaryFormat.h>
#include <BECore/Reflection/ReaderLimits.h>
#include <BECore/Reflection/SchemaHash.h>
#include <Common/ContentBinaryFiles.h>
#include <Common/CookTestSupport.h>
#include <Common/Printers.h>
#include <Generated/ContentCookEquivalenceTest.gen.hpp>

namespace BECore::Tests {

    using namespace CookEquivalenceData;

    namespace {

        bool Contains(const String& text, StringView part) {
            return text.find(part.data(), 0, part.size()) != String::npos;
        }

        constexpr StringView kDocumentXml = R"(<EquivDocument kind="fire" maybe="3" count="5" title="doc">
  <context caster="EquivStats"/>
  <damage from="caster.attack"/>
  <power value="2.5"/>
  <shapes>
    <shape type="EquivCircle" radius="4"/>
    <shape type="EquivBox" width="2" height="3" label="box"/>
    <shape type="EquivCircle"/>
  </shapes>
</EquivDocument>
)";

        /// A cooker that drops the title: what a type whose `Serialize` is not the inverse of its read would do
        void CookWithoutTitle(const ContentCooker& cooker, IDeserializer& reader, ISerializer& writer, Vector<String>& findings) {
            const auto object = New<EquivDocument>();
            {
                const ValueLinkScope scope(cooker.stem);
                object->Deserialize(reader);
                reader.FinishLoad();
            }
            AppendFindings(reader, findings);
            if (!findings.empty() || !WriteContentStamp(writer, cooker.rootType)) {
                return;
            }
            object->title.clear();
            object->Serialize(writer);
        }

        String FirstMessage(const CookRow* row) {
            return row != nullptr && !row->messages.empty() ? row->messages.front() : String{};
        }

    }  // namespace

    class ContentCookEquivalenceTest : public ::testing::Test {
    protected:
        void Cookable(StringView stem, bool strictValues = false) {
            _registry.Register({stem, EquivDocument::GetStaticTypeName(), &CookDetail::CookReflected<EquivDocument>, &CookDetail::DescribeReflected<EquivDocument>, strictValues});
        }

        CookReport Cook() {
            return CookContent(_tree.Sources(), _tree.Out(), _registry);
        }

        CookReport Verify() {
            return VerifyCooked(_tree.Sources(), _tree.Out(), _registry);
        }

        CookTestTree _tree;
        ContentCookRegistry _registry;
    };

    TEST_F(ContentCookEquivalenceTest, AValueFieldAContextAnOptionalAPoolStringAndAFactoryListVerifyEqual) {
        Cookable("Doc");
        _tree.Write(0, "Doc.xml", kDocumentXml);

        ASSERT_TRUE(Cook().Ok());
        const CookReport report = Verify();
        EXPECT_TRUE(report.Ok()) << report.ToText().c_str();
        ASSERT_NE(FindRow(report, 0, "Doc.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 0, "Doc.xml")->outcome, CookOutcome::Cooked);
    }

    TEST_F(ContentCookEquivalenceTest, AnOptionalThatIsNotEngagedAndALenientNumberVerifyEqual) {
        Cookable("Lenient");
        _tree.Write(1, "Lenient.xml", R"(<EquivDocument count="12x" kind="a"/>)");

        ASSERT_TRUE(Cook().Ok());
        const CookReport report = Verify();
        EXPECT_TRUE(report.Ok()) << report.ToText().c_str();
        EXPECT_EQ(FindRow(report, 1, "Lenient.xml")->outcome, CookOutcome::Cooked);
    }

    TEST_F(ContentCookEquivalenceTest, APoolStringOverTheReaderLimitFailsTheCookAndOneAtTheLimitVerifies) {
        Cookable("AtLimit");
        Cookable("OverLimit");
        _tree.Write(0, "AtLimit.xml", Format(R"(<EquivDocument kind="{}"/>)", String(ReaderLimits::kMaxPoolStringBytes, 'k')));
        _tree.Write(0, "OverLimit.xml", Format(R"(<EquivDocument kind="{}"/>)", String(ReaderLimits::kMaxPoolStringBytes + 1, 'k')));

        const CookReport cooked = Cook();
        ASSERT_NE(FindRow(cooked, 0, "AtLimit.xml"), nullptr);
        EXPECT_EQ(FindRow(cooked, 0, "AtLimit.xml")->outcome, CookOutcome::Cooked);
        ASSERT_NE(FindRow(cooked, 0, "OverLimit.xml"), nullptr);
        EXPECT_EQ(FindRow(cooked, 0, "OverLimit.xml")->outcome, CookOutcome::Failed) << "the XML reader reports it, so no .bin can differ from it";

        const CookReport report = Verify();
        ASSERT_NE(FindRow(report, 0, "AtLimit.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 0, "AtLimit.xml")->outcome, CookOutcome::Cooked) << report.ToText().c_str();
    }

    TEST_F(ContentCookEquivalenceTest, AScenePlacedFromANodeFileWithOverridesAndTheNodeFileVerifyEqual) {
        _tree.Write(0, "nodes/Hud.xml", HudNodeText("Hud"));
        _tree.Write(1, "SceneConfig.xml", SceneText(R"(<node name="A" layer="3"><components><component type="TransformComponent" x="5"/></components></node>
<node ref="nodes/Hud" name="Crate" layer="5"><components>
  <component type="TransformComponent" x="100"/>
  <component type="QuadRendererComponent"><color r="1" g="2" b="3" a="255"/></component>
</components></node>)"));

        ASSERT_TRUE(Cook().Ok());
        const CookReport report = Verify();
        EXPECT_TRUE(report.Ok()) << report.ToText().c_str();
        ASSERT_NE(FindRow(report, 1, "SceneConfig.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 1, "SceneConfig.xml")->outcome, CookOutcome::Cooked);
        ASSERT_NE(FindRow(report, 0, "nodes/Hud.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 0, "nodes/Hud.xml")->outcome, CookOutcome::Cooked);
    }

    TEST_F(ContentCookEquivalenceTest, ANodeFileThatAnUpperLayerShadowsIsVerifiedInItsOwnLayer) {
        _tree.Write(0, "nodes/Hud.xml", HudNodeText("Low"));
        _tree.Write(1, "nodes/Hud.xml", HudNodeText("Up"));

        ASSERT_TRUE(Cook().Ok());
        const CookReport report = Verify();
        EXPECT_TRUE(report.Ok()) << report.ToText().c_str();
        EXPECT_EQ(report.Count(CookOutcome::Cooked), 2u);

        // The lower layer's cooked node is the one that is wrong: the check must not hide behind the winner
        _tree.Write(0, "nodes/Hud.xml", HudNodeText("Changed"));
        const CookReport changed = Verify();
        ASSERT_NE(FindRow(changed, 0, "nodes/Hud.xml"), nullptr);
        EXPECT_EQ(FindRow(changed, 0, "nodes/Hud.xml")->outcome, CookOutcome::Failed);
        EXPECT_EQ(FindRow(changed, 1, "nodes/Hud.xml")->outcome, CookOutcome::Cooked);
    }

    TEST_F(ContentCookEquivalenceTest, ACopiedFileIsCopiedAndUnchangedUntilItsSourceMoves) {
        _tree.Write(0, "Other.xml", "<Other keep=\"as is\"/>");
        _tree.Write(0, "data/notes.txt", "text");

        ASSERT_TRUE(Cook().Ok());
        const CookReport report = Verify();
        EXPECT_TRUE(report.Ok()) << report.ToText().c_str();
        EXPECT_EQ(report.Count(CookOutcome::Copied), 2u);

        _tree.Write(0, "data/notes.txt", "changed");
        const CookReport stale = Verify();
        ASSERT_NE(FindRow(stale, 0, "data/notes.txt"), nullptr);
        EXPECT_EQ(FindRow(stale, 0, "data/notes.txt")->outcome, CookOutcome::Failed);
        EXPECT_EQ(FindRow(stale, 0, "Other.xml")->outcome, CookOutcome::Copied);
    }

    TEST_F(ContentCookEquivalenceTest, ACookerThatDropsAFieldFailsTheCheckAndTheRowNamesTheLine) {
        _registry.Register({"Doc", EquivDocument::GetStaticTypeName(), &CookWithoutTitle, &CookDetail::DescribeReflected<EquivDocument>, false});
        _tree.Write(0, "Doc.xml", kDocumentXml);

        ASSERT_TRUE(Cook().Ok()) << "the cook itself sees nothing wrong";
        const CookReport report = Verify();
        EXPECT_FALSE(report.Ok());
        const CookRow* row = FindRow(report, 0, "Doc.xml");
        ASSERT_NE(row, nullptr);
        EXPECT_EQ(row->outcome, CookOutcome::Failed);
        const String message = FirstMessage(row);
        EXPECT_TRUE(Contains(message, "'Doc'")) << message.c_str();
        EXPECT_TRUE(Contains(message, "title")) << message.c_str();
        EXPECT_TRUE(Contains(message, "line ")) << message.c_str();
    }

    TEST_F(ContentCookEquivalenceTest, ABinEditedAfterTheCookFails) {
        Cookable("Doc");
        _tree.Write(0, "Doc.xml", kDocumentXml);
        ASSERT_TRUE(Cook().Ok());
        const std::filesystem::path bin = _tree.OutConfig(0) / "Doc.bin";
        Vector<uint8_t> bytes = ReadFileBytes(bin);
        const StringView needle = "doc";
        const auto at = eastl::search(bytes.begin(), bytes.end(), needle.begin(), needle.end());
        ASSERT_NE(at, bytes.end()) << "the title is stored inline";
        *at = 'x';
        WriteContentBytes(bin, bytes);

        const CookReport report = Verify();
        const CookRow* row = FindRow(report, 0, "Doc.xml");
        ASSERT_NE(row, nullptr);
        EXPECT_EQ(row->outcome, CookOutcome::Failed);
        EXPECT_TRUE(Contains(FirstMessage(row), "xoc")) << FirstMessage(row).c_str();
    }

    TEST_F(ContentCookEquivalenceTest, ASourceEditedAfterTheCookFails) {
        Cookable("Doc");
        _tree.Write(0, "Doc.xml", kDocumentXml);
        ASSERT_TRUE(Cook().Ok());
        _tree.Write(0, "Doc.xml", R"(<EquivDocument kind="water" maybe="3" count="5" title="doc"/>)");

        const CookReport report = Verify();
        ASSERT_NE(FindRow(report, 0, "Doc.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 0, "Doc.xml")->outcome, CookOutcome::Failed);
    }

    TEST_F(ContentCookEquivalenceTest, ABinOfAnotherLayoutHashIsRefusedAndTheReasonIsReported) {
        Cookable("Doc");
        _tree.Write(0, "Doc.xml", kDocumentXml);
        ASSERT_TRUE(Cook().Ok());
        const uint64_t layout = ComputeLayoutHash(EquivDocument::GetStaticTypeName()).value_or(0);
        WriteContentBytes(_tree.OutConfig(0) / "Doc.bin", StampedContentBytesWithLayout(EquivDocument::GetStaticTypeName(), layout ^ 1, *New<EquivDocument>()));

        const CookReport report = Verify();
        const CookRow* row = FindRow(report, 0, "Doc.xml");
        ASSERT_NE(row, nullptr);
        EXPECT_EQ(row->outcome, CookOutcome::Failed);
        EXPECT_TRUE(Contains(FirstMessage(row), "refuses")) << FirstMessage(row).c_str();
        EXPECT_TRUE(Contains(FirstMessage(row), "layout")) << FirstMessage(row).c_str();
    }

    TEST_F(ContentCookEquivalenceTest, ABinThatIsNoDocumentIsRefused) {
        Cookable("Doc");
        _tree.Write(0, "Doc.xml", kDocumentXml);
        ASSERT_TRUE(Cook().Ok());
        WriteFile(_tree.OutConfig(0) / "Doc.bin", "x");

        const CookReport report = Verify();
        ASSERT_NE(FindRow(report, 0, "Doc.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 0, "Doc.xml")->outcome, CookOutcome::Failed);
        EXPECT_TRUE(Contains(FirstMessage(FindRow(report, 0, "Doc.xml")), "refuses"));
    }

    TEST_F(ContentCookEquivalenceTest, AFileOfTheCookedTreeThatTheCookDidNotWriteAndAMissingOneFail) {
        Cookable("Doc");
        _tree.Write(0, "Doc.xml", kDocumentXml);
        _tree.Write(1, "Gone.xml", "<Other/>");
        ASSERT_TRUE(Cook().Ok());
        WriteFile(_tree.OutConfig(0) / "Extra.bin", "x");
        std::filesystem::remove(_tree.OutConfig(1) / "Gone.xml");

        const CookReport report = Verify();
        EXPECT_FALSE(report.Ok());
        ASSERT_NE(FindRow(report, 0, "Extra.bin"), nullptr);
        EXPECT_EQ(FindRow(report, 0, "Extra.bin")->outcome, CookOutcome::Failed);
        ASSERT_NE(FindRow(report, 1, "Gone.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 1, "Gone.xml")->outcome, CookOutcome::Failed);
        EXPECT_EQ(FindRow(report, 0, "Doc.xml")->outcome, CookOutcome::Cooked);
    }

    TEST_F(ContentCookEquivalenceTest, ABinOfAStemNoCookerClaimsFails) {
        _tree.Write(0, "Doc.xml", kDocumentXml);
        Cookable("Doc");
        ASSERT_TRUE(Cook().Ok());

        ContentCookRegistry other;
        const CookReport report = VerifyCooked(_tree.Sources(), _tree.Out(), other);
        ASSERT_NE(FindRow(report, 0, "Doc.xml"), nullptr);
        EXPECT_EQ(FindRow(report, 0, "Doc.xml")->outcome, CookOutcome::Failed);
        EXPECT_TRUE(Contains(FirstMessage(FindRow(report, 0, "Doc.xml")), "no cooker"));
    }

    TEST_F(ContentCookEquivalenceTest, ACookedLayerThatIsMissingOrHasNoSourceLayerFails) {
        _tree.Write(0, "Other.xml", "<Other/>");
        const CookReport nothingCooked = Verify();
        EXPECT_FALSE(nothingCooked.Ok());
        EXPECT_EQ(nothingCooked.Count(CookOutcome::Failed), CookTestTree::kLayers);

        ASSERT_TRUE(Cook().Ok());
        MakeDirectory(_tree.OutConfig(CookTestTree::kLayers));
        const CookReport extra = Verify();
        EXPECT_FALSE(extra.Ok());
        EXPECT_TRUE(Contains(extra.ToText(), "no source layer")) << extra.ToText().c_str();
    }

    TEST(ContentVerifyDifference, EqualTextsHaveNoDifference) {
        EXPECT_TRUE(DescribeDifference("a\nb\n", "a\nb\n").empty());
        EXPECT_TRUE(DescribeDifference("", "").empty());
    }

    TEST(ContentVerifyDifference, TheFirstDifferingLineAndTheCountAreReported) {
        const String text = DescribeDifference("one\ntwo\nthree\nfour\n", "one\nTWO\nthree\nFOUR\n");
        EXPECT_TRUE(Contains(text, "line 2")) << text.c_str();
        EXPECT_TRUE(Contains(text, "'two'")) << text.c_str();
        EXPECT_TRUE(Contains(text, "'TWO'")) << text.c_str();
        EXPECT_TRUE(Contains(text, "2 lines")) << text.c_str();
    }

    TEST(ContentVerifyDifference, ATextThatEndsEarlierNamesTheEnd) {
        const String text = DescribeDifference("one\ntwo\n", "one\n");
        EXPECT_TRUE(Contains(text, "line 2")) << text.c_str();
        EXPECT_TRUE(Contains(text, "<end of text>")) << text.c_str();
    }

    TEST(ContentVerifyDifference, ALongLineIsCut) {
        const String text = DescribeDifference(String(kMaxQuotedLineLength * 3, 'a'), "b");
        EXPECT_LT(text.size(), kMaxQuotedLineLength * 2);
        EXPECT_TRUE(Contains(text, "..."));
    }

}  // namespace BECore::Tests
