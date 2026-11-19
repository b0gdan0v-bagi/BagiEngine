#include <BECore/Control/Json.h>
#include <BECore/Format/Format.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/ArtGen/ArtGenVerdict.h>
#include <Widgets/EditorLayout/UserFileIo.h>
#include <filesystem>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        ArtGenVerdict VerdictOf(StringView provenance) {
            auto parsed = ParseJson(provenance);
            EXPECT_TRUE(parsed.has_value());
            return parsed ? ArtGenVerdictFromProvenance(*parsed) : ArtGenVerdict{};
        }

        bool Contains(const String& text, StringView part) {
            return StringView(text).find(part) != StringView::npos;
        }

        ArtGenVerdict UnreadableWithError(const String& error) {
            Json checks = Json::object();
            checks["passed"] = nullptr;
            checks["error"] = MakeJsonString(error);
            Json record = Json::object();
            record["checks"] = checks;
            return ArtGenVerdictFromProvenance(record);
        }

        /// The names of `CHECK_NAMES` in the service (`tools/artgen/artgen_checks.py`), read from the source so the list is not a hand copy
        Vector<String> CheckNamesOfTheService() {
            const std::filesystem::path file = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "tools" / "artgen" / "artgen_checks.py";  //-NoStd: a path
            const String text = ReadFile(file);
            constexpr StringView kDeclaration = "CHECK_NAMES = (";
            Vector<String> names;
            const size_t begin = StringView(text).find(kDeclaration);
            const size_t end = begin == StringView::npos ? begin : StringView(text).find(')', begin);
            if (end == StringView::npos) {
                return names;
            }
            StringView list = StringView(text).substr(begin + kDeclaration.size(), end - begin - kDeclaration.size());
            for (size_t open = list.find('"'); open != StringView::npos; open = list.find('"')) {
                const size_t close = list.find('"', open + 1);
                if (close == StringView::npos) {
                    break;
                }
                names.push_back(String(list.substr(open + 1, close - open - 1)));
                list.remove_prefix(close + 1);
            }
            return names;
        }

        constexpr StringView kUnreadableTip = "Checks could not read the picture";

        // The keys of provenance_of around the checks of a real record
        constexpr StringView kRealRecord =
            R"({"provenance":1,"preset":"pixel_pieces","kind":"image","subject":"a pawn","seed":42,"workflow":"w","date":"2026-11-18T10:00:00+00:00","checks":{"passed":false,"failed":["edge"],"metrics":{"edgeRun":0.5},"thresholds":{"cut":128}}})";

    }  // namespace

    TEST(ArtGenVerdictTest, Passed) {
        const ArtGenVerdict verdict = VerdictOf(R"({"checks":{"passed":true,"failed":[]}})");
        EXPECT_EQ(verdict.kind, ArtGenVerdictKind::Passed);
        EXPECT_EQ(verdict.tip, "Checks passed");
    }

    TEST(ArtGenVerdictTest, FailedNamesBothChecksWithTheirMeaning) {
        const ArtGenVerdict verdict = VerdictOf(R"({"checks":{"passed":false,"failed":["edge","halo"]}})");
        EXPECT_EQ(verdict.kind, ArtGenVerdictKind::Failed);
        EXPECT_TRUE(Contains(verdict.tip, "Checks failed"));
        EXPECT_TRUE(Contains(verdict.tip, "touches the edge of the frame"));
        EXPECT_TRUE(Contains(verdict.tip, "half-transparent"));
    }

    TEST(ArtGenVerdictTest, EveryNameOfTheServiceHasAMeaning) {
        const Vector<String> names = CheckNamesOfTheService();
        EXPECT_GE(names.size(), 8u) << "CHECK_NAMES was not read from artgen_checks.py";
        for (const String& name : names) {
            const ArtGenVerdict verdict = VerdictOf(Format(R"({{"checks":{{"passed":false,"failed":["{}"]}}}})", name));
            EXPECT_TRUE(Contains(verdict.tip, " (")) << name;
        }
    }

    TEST(ArtGenVerdictTest, AnUnknownNameIsShownAsItIs) {
        const ArtGenVerdict verdict = VerdictOf(R"({"checks":{"passed":false,"failed":["blurry"]}})");
        EXPECT_EQ(verdict.kind, ArtGenVerdictKind::Failed);
        EXPECT_EQ(verdict.tip, "Checks failed: blurry");
    }

    TEST(ArtGenVerdictTest, FailedWithoutAUsableNameSaysOnlyFailed) {
        for (const char* text : {R"({"checks":{"passed":false,"failed":[]}})", R"({"checks":{"passed":false,"failed":[1,null,{}]}})", R"({"checks":{"passed":false,"failed":"edge"}})",
                                 R"({"checks":{"passed":false}})"}) {
            const ArtGenVerdict verdict = VerdictOf(text);
            EXPECT_EQ(verdict.kind, ArtGenVerdictKind::Failed) << text;
            EXPECT_EQ(verdict.tip, "Checks failed") << text;
        }
    }

    TEST(ArtGenVerdictTest, AStringAmongNonStringsIsStillNamed) {
        const ArtGenVerdict verdict = VerdictOf(R"({"checks":{"passed":false,"failed":[3,"empty"]}})");
        EXPECT_TRUE(Contains(verdict.tip, "nothing is drawn"));
    }

    TEST(ArtGenVerdictTest, MoreThanTheLimitIsCounted) {
        String names;
        for (size_t i = 0; i < kMaxVerdictNames + 3; ++i) {
            names += Format("{}\"n{}\"", i == 0 ? "" : ",", i);
        }
        const ArtGenVerdict verdict = VerdictOf(Format(R"({{"checks":{{"passed":false,"failed":[{}]}}}})", names));
        EXPECT_TRUE(Contains(verdict.tip, Format("n{}", kMaxVerdictNames - 1)));
        EXPECT_FALSE(Contains(verdict.tip, Format("n{}", kMaxVerdictNames)));
        EXPECT_TRUE(Contains(verdict.tip, "and 3 more"));
    }

    TEST(ArtGenVerdictTest, UnreadableHasTheServicesError) {
        const ArtGenVerdict verdict = VerdictOf(R"({"checks":{"passed":null,"error":"not a PNG"}})");
        EXPECT_EQ(verdict.kind, ArtGenVerdictKind::Unreadable);
        EXPECT_EQ(verdict.tip, "Checks could not read the picture: not a PNG");
    }

    TEST(ArtGenVerdictTest, UnreadableWithoutAnErrorHasTheFixedText) {
        for (const char* text : {R"({"checks":{"passed":null}})", R"({"checks":{"passed":null,"error":7}})", R"({"checks":{"passed":null,"error":""}})"}) {
            const ArtGenVerdict verdict = VerdictOf(text);
            EXPECT_EQ(verdict.kind, ArtGenVerdictKind::Unreadable) << text;
            EXPECT_EQ(verdict.tip, kUnreadableTip) << text;
        }
    }

    TEST(ArtGenVerdictTest, ALongErrorIsCutAtACharacterBoundary) {
        // The limit falls on the second byte of a two-byte character: the character goes whole
        String error(kMaxVerdictError - 1, 'a');
        error += "\xC3\xA9tail";
        const ArtGenVerdict verdict = UnreadableWithError(error);
        const size_t prefix = kUnreadableTip.size() + 2;
        EXPECT_EQ(verdict.tip.size(), prefix + kMaxVerdictError - 1);
        EXPECT_FALSE(Contains(verdict.tip, "tail"));
        EXPECT_FALSE(Contains(verdict.tip, "\xC3"));
    }

    TEST(ArtGenVerdictTest, AnErrorExactlyAtTheLimitIsKept) {
        const String error(kMaxVerdictError, 'b');
        EXPECT_TRUE(Contains(UnreadableWithError(error).tip, error));
    }

    TEST(ArtGenVerdictTest, AProvenanceWithNoUsableChecksIsNone) {
        for (const char* text : {R"({"seed":1})", R"({"checks":"yes"})", R"({"checks":[true]})", R"({"checks":null})", R"({"checks":{}})", R"({"checks":{"passed":1}})",
                                 R"({"checks":{"passed":"true"}})", R"({"checks":{"passed":[]}})", "[]", "7", "\"checks\"", "null"}) {
            const ArtGenVerdict verdict = VerdictOf(text);
            EXPECT_EQ(verdict.kind, ArtGenVerdictKind::None) << text;
            EXPECT_TRUE(verdict.tip.empty()) << text;
        }
    }

    class ArtGenVerdictFileTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _root = TestTempDirectory("artgen_verdict");
            MakeEmptyDirectory(_root);
        }

        void TearDown() override {
            RemoveDirectory(_root);
        }

        std::filesystem::path _root;
    };

    TEST_F(ArtGenVerdictFileTest, ReadsARealRecord) {
        const std::filesystem::path file = _root / "pawn_42.artgen.json";
        ASSERT_TRUE(WriteTextFileAtomically(file, kRealRecord).has_value());
        const ArtGenVerdict verdict = ReadArtGenVerdict(file);
        EXPECT_EQ(verdict.kind, ArtGenVerdictKind::Failed);
        EXPECT_TRUE(Contains(verdict.tip, "touches the edge"));
    }

    TEST_F(ArtGenVerdictFileTest, AMissingFileAnEmptyPathAndBrokenJsonAreNone) {
        EXPECT_EQ(ReadArtGenVerdict(_root / "gone.artgen.json").kind, ArtGenVerdictKind::None);
        EXPECT_EQ(ReadArtGenVerdict({}).kind, ArtGenVerdictKind::None);
        const std::filesystem::path broken = _root / "broken.artgen.json";
        ASSERT_TRUE(WriteTextFileAtomically(broken, R"({"checks": {"passed": tr)").has_value());
        EXPECT_EQ(ReadArtGenVerdict(broken).kind, ArtGenVerdictKind::None);
    }

}  // namespace BECore::Tests
