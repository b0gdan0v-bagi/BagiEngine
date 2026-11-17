#include <BECore/Control/Json.h>
#include <BECore/Format/Format.h>
#include <Common/Printers.h>
#include <Widgets/ArtGen/Sets/ArtGenSet.h>
#include <chrono>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        ArtGenSet MakeSet() {
            ArtGenSet set;
            set.name = "knights-20261117-123456";
            set.created = "2026-11-17T12:34:56Z";
            set.preset = "pixel_pieces";
            set.count = 3;
            set.seed = 4242;
            ArtGenSetJob first;
            first.name = "white_knight";
            first.subject = "a white knight";
            first.state = ArtGenSetJobState::Done;
            first.variants.push_back({4242, "white_knight/white_knight_4242.png", "white_knight/white_knight_4242.artgen.json", "image"});
            first.variants.push_back({4243, "white_knight/white_knight_4243.png", "white_knight/white_knight_4243.artgen.json", "image"});
            first.pick = 4243;
            first.accepted = ArtGenSetAccepted{"white_knight", "2026-11-17T13:00:00Z"};
            set.jobs.push_back(std::move(first));
            ArtGenSetJob second;
            second.name = "black_knight";
            second.subject = "a black knight";
            second.state = ArtGenSetJobState::Failed;
            second.error = "the generator is not ready";
            set.jobs.push_back(std::move(second));
            return set;
        }

        Json Manifest(StringView text) {
            auto parsed = ParseJson(text);
            EXPECT_TRUE(parsed.has_value());
            return parsed ? *parsed : Json();
        }

        constexpr StringView kMinimalManifest = R"({"manifest":1,"set":"s","kind":"image","preset":"p","count":2,"seed":7,"jobs":[{"name":"a","subject":"x","state":"Pending","variants":[]}]})";

        String ErrorOf(const std::expected<ArtGenSet, String>& result) {
            EXPECT_FALSE(result.has_value());
            return result ? String() : result.error();
        }

        bool Contains(const String& text, StringView part) {
            return StringView(text).find(part) != StringView::npos;
        }

    }  // namespace

    TEST(ArtGenSetTest, JsonRoundTrip) {
        const ArtGenSet set = MakeSet();
        const Json json = ArtGenSetToJson(set);
        EXPECT_EQ(GetJsonInt(json, "manifest"), kArtGenManifestFormat);

        const auto back = ArtGenSetFromJson(json);
        ASSERT_TRUE(back.has_value()) << back.error();
        EXPECT_EQ(ArtGenSetToJson(*back), json);
        EXPECT_EQ(back->name, set.name);
        EXPECT_EQ(back->seed, 4242);
        ASSERT_EQ(back->jobs.size(), 2u);
        EXPECT_EQ(back->jobs[0].pick, Optional<int64_t>(4243));
        EXPECT_EQ(back->jobs[0].accepted->as, "white_knight");
        EXPECT_EQ(back->jobs[1].state, ArtGenSetJobState::Failed);
        EXPECT_EQ(back->jobs[1].error, "the generator is not ready");
        EXPECT_FALSE(back->jobs[1].pick.has_value());
        EXPECT_FALSE(back->jobs[1].accepted.has_value());
    }

    TEST(ArtGenSetTest, JsonKeepsADraftsPresetData) {
        ArtGenSet set = MakeSet();
        set.presetData = Manifest(R"({"workflow":"w","prompt":"p"})");
        const auto back = ArtGenSetFromJson(ArtGenSetToJson(set));
        ASSERT_TRUE(back.has_value()) << back.error();
        EXPECT_EQ(back->presetData, set.presetData);

        const auto byName = ArtGenSetFromJson(ArtGenSetToJson(MakeSet()));
        ASSERT_TRUE(byName.has_value());
        EXPECT_TRUE(byName->presetData.is_null());
        EXPECT_TRUE(FindJsonMember(ArtGenSetToJson(MakeSet()), "presetData")->is_null());
    }

    TEST(ArtGenSetTest, PathsInTheManifestUseForwardSlashes) {
        const Json json = ArtGenSetToJson(MakeSet());
        const Json& variant = (*FindJsonMember(json, "jobs"))[0]["variants"][0];
        EXPECT_EQ(GetJsonString(variant, "file"), "white_knight/white_knight_4242.png");
        EXPECT_EQ(GetJsonString(variant, "file")->find('\\'), String::npos);
    }

    TEST(ArtGenSetTest, MinimalManifestReads) {
        const auto set = ArtGenSetFromJson(Manifest(kMinimalManifest));
        ASSERT_TRUE(set.has_value()) << set.error();
        EXPECT_EQ(set->jobs.size(), 1u);
        EXPECT_EQ(set->jobs[0].state, ArtGenSetJobState::Pending);
        EXPECT_TRUE(set->created.empty());
    }

    TEST(ArtGenSetTest, UnknownKeysAreIgnored) {
        Json json = Manifest(kMinimalManifest);
        json["future"] = 1;
        json["jobs"][0]["note"] = "x";
        EXPECT_TRUE(ArtGenSetFromJson(json).has_value());
    }

    TEST(ArtGenSetTest, UnknownFormatNumberIsRefused) {
        Json json = Manifest(kMinimalManifest);
        json["manifest"] = 2;
        EXPECT_TRUE(Contains(ErrorOf(ArtGenSetFromJson(json)), "format 2"));
        json.erase("manifest");
        EXPECT_FALSE(ArtGenSetFromJson(json).has_value());
        EXPECT_FALSE(ArtGenSetFromJson(Json::array()).has_value());
    }

    TEST(ArtGenSetTest, WrongTypesAreErrorsNotAborts) {
        struct Case {
            const char* path;
            Json value;
            const char* mentions;
        };
        const Case cases[] = {
            {"set", Json(5), "'set'"},
            {"set", Json("bad name!"), "not a valid set name"},
            {"kind", Json("video"), "kind"},
            {"count", Json("4"), "'count'"},
            {"count", Json(0), "'count'"},
            {"count", Json(17), "'count'"},
            {"seed", Json(1.5), "'seed'"},
            {"preset", Json(3), "'preset'"},
            {"presetData", Json(3), "presetData"},
            {"jobs", Json("x"), "'jobs'"},
            {"jobs", Json::array(), "'jobs'"},
        };
        for (const auto& item : cases) {
            Json json = Manifest(kMinimalManifest);
            json[item.path] = item.value;
            const String error = ErrorOf(ArtGenSetFromJson(json));
            EXPECT_TRUE(Contains(error, item.mentions)) << item.path << ": " << error;
        }
    }

    TEST(ArtGenSetTest, WrongJobMembersNameTheJob) {
        struct Case {
            const char* member;
            Json value;
            const char* mentions;
        };
        const Case cases[] = {
            {"name", Json(3), "jobs[0]: 'name'"},
            {"name", Json("a b"), "not a valid name"},
            {"subject", Json(nullptr), "'subject'"},
            {"state", Json("Sleeping"), "unknown state 'Sleeping'"},
            {"state", Json(1), "'state'"},
            {"error", Json(1), "'error'"},
            {"variants", Json("x"), "'variants'"},
            {"variants", Json::array({Json(1)}), "jobs[0].variants[0]: must be an object"},
            {"variants", Json::parse(R"([{"seed":"1","file":"a.png"}])"), "'seed'"},
            {"variants", Json::parse(R"([{"seed":1}])"), "'file'"},
            {"variants", Json::parse(R"([{"seed":1,"file":"a.png","kind":"video"}])"), "kind"},
            {"pick", Json("x"), "'pick'"},
            {"pick", Json(99), "not a seed"},
            {"accepted", Json(1), "'accepted'"},
            {"accepted", Json::parse(R"({"date":"x"})"), "'as'"},
        };
        for (const auto& item : cases) {
            Json json = Manifest(kMinimalManifest);
            json["jobs"][0][item.member] = item.value;
            const String error = ErrorOf(ArtGenSetFromJson(json));
            EXPECT_TRUE(Contains(error, item.mentions)) << item.member << ": " << error;
        }
    }

    TEST(ArtGenSetTest, DuplicateJobNamesAreRefused) {
        Json json = Manifest(kMinimalManifest);
        json["jobs"].push_back(json["jobs"][0]);
        EXPECT_TRUE(Contains(ErrorOf(ArtGenSetFromJson(json)), "used twice"));
    }

    TEST(ArtGenSetTest, APickOfAVariantReads) {
        Json json = Manifest(kMinimalManifest);
        json["jobs"][0]["variants"] = Json::parse(R"([{"seed":5,"file":"a/a_5.png","provenance":"a/a_5.artgen.json"}])");
        json["jobs"][0]["pick"] = 5;
        const auto set = ArtGenSetFromJson(json);
        ASSERT_TRUE(set.has_value()) << set.error();
        EXPECT_EQ(set->jobs[0].pick, Optional<int64_t>(5));
    }

    TEST(ArtGenSetTest, NameFromSubject) {
        EXPECT_EQ(ArtGenNameFromSubject("White Knight!"), "white_knight");
        EXPECT_EQ(ArtGenNameFromSubject("  a -- b  "), "a_b");
        EXPECT_EQ(ArtGenNameFromSubject("Rook 2"), "rook_2");
        EXPECT_EQ(ArtGenNameFromSubject("!!!"), "");
        EXPECT_EQ(ArtGenNameFromSubject(""), "");
        const String longName = ArtGenNameFromSubject(String(100, 'a') + " b");
        EXPECT_EQ(longName.size(), 64u);
        EXPECT_NE(longName.back(), '_');
    }

    TEST(ArtGenSetTest, SubjectLines) {
        const auto subjects = ParseArtGenSubjects("White Knight\n  black rook  \r\nqueen: a crowned queen\n");
        ASSERT_TRUE(subjects.has_value()) << subjects.error();
        ASSERT_EQ(subjects->size(), 3u);
        EXPECT_EQ((*subjects)[0].name, "white_knight");
        EXPECT_EQ((*subjects)[0].subject, "White Knight");
        EXPECT_EQ((*subjects)[1].name, "black_rook");
        EXPECT_EQ((*subjects)[1].subject, "black rook");
        EXPECT_EQ((*subjects)[2].name, "queen");
        EXPECT_EQ((*subjects)[2].subject, "a crowned queen");
    }

    TEST(ArtGenSetTest, SubjectLinesSkipEmptyAndComments) {
        const auto subjects = ParseArtGenSubjects("# pieces\n\n   \nknight\n  # not this\nrook");
        ASSERT_TRUE(subjects.has_value()) << subjects.error();
        ASSERT_EQ(subjects->size(), 2u);
        EXPECT_EQ((*subjects)[0].name, "knight");
        EXPECT_EQ((*subjects)[1].name, "rook");
    }

    TEST(ArtGenSetTest, ATextBeforeTheColonThatIsNotANameIsPartOfTheSubject) {
        const auto subjects = ParseArtGenSubjects("a cat: sitting\nA B: c");
        ASSERT_TRUE(subjects.has_value()) << subjects.error();
        EXPECT_EQ((*subjects)[0].name, "a_cat_sitting");
        EXPECT_EQ((*subjects)[0].subject, "a cat: sitting");
        EXPECT_EQ((*subjects)[1].name, "a_b_c");
    }

    TEST(ArtGenSetTest, NameWithNoSubjectIsAnError) {
        const auto subjects = ParseArtGenSubjects("knight\nrook:   ");
        ASSERT_FALSE(subjects.has_value());
        EXPECT_TRUE(Contains(subjects.error(), "line 2"));
    }

    TEST(ArtGenSetTest, DuplicateNamesNameBothLines) {
        const auto subjects = ParseArtGenSubjects("white knight\n# x\nWhite  Knight!");
        ASSERT_FALSE(subjects.has_value());
        EXPECT_TRUE(Contains(subjects.error(), "line 3"));
        EXPECT_TRUE(Contains(subjects.error(), "line 1"));
        EXPECT_TRUE(Contains(subjects.error(), "white_knight"));
    }

    TEST(ArtGenSetTest, ASubjectWithNoLettersIsAnError) {
        const auto subjects = ParseArtGenSubjects("ok\n???");
        ASSERT_FALSE(subjects.has_value());
        EXPECT_TRUE(Contains(subjects.error(), "line 2"));
    }

    TEST(ArtGenSetTest, NoSubjectsIsAnError) {
        EXPECT_FALSE(ParseArtGenSubjects("").has_value());
        EXPECT_FALSE(ParseArtGenSubjects("# only a comment\n\n").has_value());
    }

    TEST(ArtGenSetTest, AtMostSixtyFourJobs) {
        String text;
        for (int32_t index = 0; index < kMaxSetJobs; ++index) {
            text += Format("job{}: subject {}\n", index, index);
        }
        const auto full = ParseArtGenSubjects(text);
        ASSERT_TRUE(full.has_value()) << full.error();
        EXPECT_EQ(full->size(), static_cast<size_t>(kMaxSetJobs));

        text += "one_more: subject\n";
        const auto over = ParseArtGenSubjects(text);
        ASSERT_FALSE(over.has_value());
        EXPECT_TRUE(Contains(over.error(), Format("line {}", kMaxSetJobs + 1)));
    }

    TEST(ArtGenSetTest, CheckSubjectsOfAControlCall) {
        EXPECT_TRUE(CheckArtGenSubjects({{"a", "x"}, {"b", "y"}}).has_value());
        EXPECT_FALSE(CheckArtGenSubjects({}).has_value());
        EXPECT_FALSE(CheckArtGenSubjects({{"a b", "x"}}).has_value());
        EXPECT_FALSE(CheckArtGenSubjects({{"a", "x"}, {"a", "y"}}).has_value());
        EXPECT_FALSE(CheckArtGenSubjects({{"a", "   "}}).has_value());
        Vector<ArtGenSubject> many;
        for (int32_t index = 0; index <= kMaxSetJobs; ++index) {
            many.push_back({Format("j{}", index), "x"});
        }
        EXPECT_FALSE(CheckArtGenSubjects(many).has_value());
    }

    TEST(ArtGenSetTest, DefaultNameAndTimestampAreUtc) {
        using namespace std::chrono;
        const system_clock::time_point time = sys_days{year{2026} / November / 17} + hours{12} + minutes{34} + seconds{56};
        EXPECT_EQ(DefaultArtGenSetName("pixel_pieces", time), "pixel_pieces-20261117-123456");
        EXPECT_EQ(ArtGenTimestamp(time), "2026-11-17T12:34:56Z");

        const sys_days epoch = sys_days{year{1970} / January / 1};
        EXPECT_EQ(ArtGenTimestamp(epoch), "1970-01-01T00:00:00Z");
    }

    TEST(ArtGenSetTest, DefaultNameStaysAValidName) {
        using namespace std::chrono;
        const system_clock::time_point time = sys_days{year{2026} / January / 2} + hours{3};
        const String longName = DefaultArtGenSetName(String(64, 'x'), time);
        EXPECT_LE(longName.size(), 64u);
        EXPECT_TRUE(Contains(longName, "-20260102-030000"));
        EXPECT_EQ(DefaultArtGenSetName("my preset!", time), "my_preset_-20260102-030000");
        EXPECT_EQ(DefaultArtGenSetName("", time), "set-20260102-030000");
    }

    TEST(ArtGenSetTest, ActiveCountsAndPicks) {
        ArtGenSet set = MakeSet();
        EXPECT_FALSE(IsActive(set));
        EXPECT_EQ(CountJobs(set, ArtGenSetJobState::Done), 1);
        EXPECT_EQ(CountJobs(set, ArtGenSetJobState::Failed), 1);
        EXPECT_EQ(CountJobs(set, ArtGenSetJobState::Pending), 0);
        EXPECT_EQ(PickedCount(set), 1);

        set.jobs[1].state = ArtGenSetJobState::Pending;
        EXPECT_TRUE(IsActive(set));
        set.jobs[1].state = ArtGenSetJobState::Running;
        EXPECT_TRUE(IsActive(set));
        set.jobs[1].state = ArtGenSetJobState::Cancelled;
        EXPECT_FALSE(IsActive(set));
    }

    TEST(ArtGenSetTest, PickVariant) {
        ArtGenSet set = MakeSet();
        ArtGenSetJob& job = set.jobs[0];
        EXPECT_TRUE(PickVariant(job, 4242));
        EXPECT_EQ(job.pick, Optional<int64_t>(4242));
        EXPECT_FALSE(PickVariant(job, 1));
        EXPECT_EQ(job.pick, Optional<int64_t>(4242));
        EXPECT_EQ(FindVariant(job, 4243)->seed, 4243);
        EXPECT_EQ(FindVariant(job, 5), nullptr);
    }

}  // namespace BECore::Tests
