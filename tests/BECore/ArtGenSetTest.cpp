#include <BECore/Control/Json.h>
#include <Common/Printers.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/Sets/ArtGenSet.h>
#include <chrono>
#include <cstdio>
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
            first.accepted = ArtGenSetAccepted{"white_knight", "2026-11-17T13:00:00Z", false};
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

    TEST(ArtGenSetTest, KeptRoundTripsBothWays) {
        for (const bool kept : {false, true}) {
            ArtGenSet set = MakeSet();
            set.jobs[0].accepted->kept = kept;
            const Json json = ArtGenSetToJson(set);
            EXPECT_EQ(GetJsonBool((*FindJsonMember(json, "jobs"))[0]["accepted"], "kept"), Optional<bool>(kept));
            const auto back = ArtGenSetFromJson(json);
            ASSERT_TRUE(back.has_value()) << back.error();
            EXPECT_EQ(back->jobs[0].accepted->kept, kept);
        }
    }

    TEST(ArtGenSetTest, AManifestWithoutKeptReadsAsKept) {
        Json json = ArtGenSetToJson(MakeSet());
        json["jobs"][0]["accepted"].erase("kept");
        const auto set = ArtGenSetFromJson(json);
        ASSERT_TRUE(set.has_value()) << set.error();
        EXPECT_TRUE(set->jobs[0].accepted->kept);
        EXPECT_FALSE(IsLive(set->jobs[0]));
    }

    TEST(ArtGenSetTest, KeptOfAWrongTypeNamesTheAcceptedObject) {
        for (const Json& value : {Json("yes"), Json(1), Json(nullptr)}) {
            Json json = ArtGenSetToJson(MakeSet());
            json["jobs"][0]["accepted"]["kept"] = value;
            const String error = ErrorOf(ArtGenSetFromJson(json));
            EXPECT_TRUE(Contains(error, "jobs[0].accepted")) << error;
            EXPECT_TRUE(Contains(error, "'kept'")) << error;
        }
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

    TEST(ArtGenSetTest, ProcessingRoundTripsAndAManifestWithoutItReadsAsNone) {
        ArtGenSet set = MakeSet();
        EXPECT_TRUE(FindJsonMember(ArtGenSetToJson(set), "processing")->is_null());
        const auto without = ArtGenSetFromJson(ArtGenSetToJson(set));
        ASSERT_TRUE(without.has_value()) << without.error();
        EXPECT_FALSE(without->processing.has_value());
        ArtGenSet untouched = MakeSet();
        untouched.jobs[0].accepted.reset();
        EXPECT_FALSE(IsLegacyAccepted(untouched)) << "nothing accepted, nothing legacy";
        EXPECT_TRUE(IsLegacyAccepted(set)) << "a row accepted and no processing: accepted before frames existed";

        set.processing = ArtGenSetProcessing{ArtGenImageSettings{true, 5, 128}, ArtGenImageFrame{10, 20, 300, 400, 96, 128}};
        EXPECT_FALSE(IsLegacyAccepted(set));
        const Json json = ArtGenSetToJson(set);
        EXPECT_EQ(GetJsonInt(*FindJsonMember(*FindJsonMember(json, "processing"), "frame"), "outHeight"), Optional<int64_t>(128));
        const auto back = ArtGenSetFromJson(json);
        ASSERT_TRUE(back.has_value()) << back.error();
        EXPECT_EQ(back->processing, set.processing);
        EXPECT_EQ(ArtGenSetToJson(*back), json);
    }

    TEST(ArtGenSetTest, AProcessingOfTheWrongShapeIsAnErrorThatSaysWhere) {
        struct Case {
            const char* processing;
            const char* where;
        };
        const Array<Case, 5> cases{Case{R"("yes")", "manifest.processing: must be an object"}, Case{R"({"settings":{}})", "manifest.processing: needs 'settings' and 'frame'"},
                                   Case{R"({"settings":{"padding":99},"frame":{"x":0,"y":0,"w":10,"h":10,"outWidth":5,"outHeight":5}})", "manifest.processing.settings:"},
                                   Case{R"({"settings":{},"frame":{"x":0,"y":0,"w":10,"h":10,"outWidth":50,"outHeight":5}})", "manifest.processing.frame:"},
                                   Case{R"({"settings":{},"frame":{"x":0,"y":0,"w":"wide","h":10,"outWidth":5,"outHeight":5}})", "manifest.processing.frame:"}};
        for (const Case& item : cases) {
            Json json = ArtGenSetToJson(MakeSet());
            json["processing"] = Manifest(item.processing);
            const String error = ErrorOf(ArtGenSetFromJson(json));
            EXPECT_TRUE(Contains(error, item.where)) << item.processing << " -> " << error;
        }
    }

    TEST(ArtGenSetTest, AReferenceRoundTripsWithAndWithoutAWeight) {
        ArtGenSet set = MakeSet();
        EXPECT_TRUE(FindJsonMember(ArtGenSetToJson(set), "reference")->is_null()) << "no reference: the key is there, null";

        for (const Optional<double> weight : {Optional<double>(), Optional<double>(0.6)}) {
            set.reference = ArtGenReference{"style", "assets/artgen/white_knight.png", weight};
            const Json json = ArtGenSetToJson(set);
            EXPECT_EQ(GetJsonString(*FindJsonMember(json, "reference"), "mode"), Optional<String>("style"));
            EXPECT_EQ(FindJsonMember(*FindJsonMember(json, "reference"), "weight") != nullptr, weight.has_value());
            const auto back = ArtGenSetFromJson(json);
            ASSERT_TRUE(back.has_value()) << back.error();
            EXPECT_EQ(back->reference, set.reference);
            EXPECT_EQ(ArtGenSetToJson(*back), json);
        }
    }

    TEST(ArtGenSetTest, AManifestWithoutAReferenceOrWithANullOneReadsAsNone) {
        Json json = ArtGenSetToJson(MakeSet());
        json.erase("reference");
        const auto without = ArtGenSetFromJson(json);
        ASSERT_TRUE(without.has_value()) << without.error();
        EXPECT_FALSE(without->reference.has_value());
        EXPECT_EQ(kArtGenManifestFormat, 1) << "an additive key: the format number stays";

        const auto minimal = ArtGenSetFromJson(Manifest(kMinimalManifest));
        ASSERT_TRUE(minimal.has_value()) << minimal.error();
        EXPECT_FALSE(minimal->reference.has_value());
        EXPECT_TRUE(FindJsonMember(ArtGenSetToJson(*minimal), "reference")->is_null()) << "rewritten as none, nothing invented";

        json["reference"] = Json(nullptr);
        const auto nulled = ArtGenSetFromJson(json);
        ASSERT_TRUE(nulled.has_value()) << nulled.error();
        EXPECT_FALSE(nulled->reference.has_value());
    }

    TEST(ArtGenSetTest, AReferenceOfTheWrongShapeIsAnErrorThatNamesItsKey) {
        struct Case {
            const char* reference;
            const char* where;
        };
        const Array<Case, 5> cases{Case{R"("yes")", "'reference'"}, Case{R"({"image":"a.png"})", "'reference.mode'"}, Case{R"({"mode":"init"})", "'reference.image'"},
                                   Case{R"({"mode":"style","image":""})", "'reference.image'"}, Case{R"({"mode":"style","image":"a.png","weight":"x"})", "'reference.weight'"}};
        for (const Case& item : cases) {
            Json json = ArtGenSetToJson(MakeSet());
            json["reference"] = Manifest(item.reference);
            const String error = ErrorOf(ArtGenSetFromJson(json));
            EXPECT_TRUE(Contains(error, "manifest: ")) << error;
            EXPECT_TRUE(Contains(error, item.where)) << item.reference << " -> " << error;
        }
    }

    TEST(ArtGenSetTest, AnAtlasGroupRoundTripsAndNoGroupIsWrittenAsNull) {
        ArtGenSet set = MakeSet();
        EXPECT_TRUE(FindJsonMember(ArtGenSetToJson(set), "atlas")->is_null()) << "no group: the key is there, null";

        set.atlas = "pieces";
        const Json json = ArtGenSetToJson(set);
        EXPECT_EQ(*FindJsonMember(json, "atlas"), Json("pieces"));
        const auto back = ArtGenSetFromJson(json);
        ASSERT_TRUE(back.has_value()) << back.error();
        EXPECT_EQ(back->atlas, "pieces");
        EXPECT_EQ(ArtGenSetToJson(*back), json);
    }

    TEST(ArtGenSetTest, AManifestFromBeforeAtlasesOrWithANullOneReadsAsNoGroup) {
        Json json = ArtGenSetToJson(MakeSet());
        json.erase("atlas");
        const auto without = ArtGenSetFromJson(json);
        ASSERT_TRUE(without.has_value()) << without.error();
        EXPECT_TRUE(without->atlas.empty());

        const auto minimal = ArtGenSetFromJson(Manifest(kMinimalManifest));
        ASSERT_TRUE(minimal.has_value()) << minimal.error();
        EXPECT_TRUE(minimal->atlas.empty());
        EXPECT_TRUE(FindJsonMember(ArtGenSetToJson(*minimal), "atlas")->is_null()) << "rewritten as none, nothing invented";

        json["atlas"] = Json(nullptr);
        const auto nulled = ArtGenSetFromJson(json);
        ASSERT_TRUE(nulled.has_value()) << nulled.error();
        EXPECT_TRUE(nulled->atlas.empty());
    }

    TEST(ArtGenSetTest, AnAtlasGroupThatIsNotAGroupNameIsAnErrorThatNamesTheKey) {
        for (const char* bad : {"5", R"("")", R"("a b")", R"("../x")", "[]", "true"}) {
            Json json = ArtGenSetToJson(MakeSet());
            json["atlas"] = Manifest(bad);
            const String error = ErrorOf(ArtGenSetFromJson(json));
            EXPECT_TRUE(Contains(error, "manifest: ")) << error;
            EXPECT_TRUE(Contains(error, "'atlas'")) << bad << " -> " << error;
        }
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

    namespace {

        void TypeSubject(ArtGenSubjectRow& row, const char* text) {
            std::snprintf(row.subject, sizeof(row.subject), "%s", text);
            ArtGenRowSubjectEdited(row);
        }

        void TypeName(ArtGenSubjectRow& row, const char* text) {
            std::snprintf(row.name, sizeof(row.name), "%s", text);
            ArtGenRowNameEdited(row);
        }

    }  // namespace

    TEST(ArtGenSetTest, RowNameFollowsTheSubjectUntilTypedByHand) {
        ArtGenSubjectRow row;
        TypeSubject(row, "white knight");
        EXPECT_STREQ(row.name, "white_knight");
        TypeSubject(row, "White Knight II");
        EXPECT_STREQ(row.name, "white_knight_ii");

        TypeName(row, "wn");
        TypeSubject(row, "a different subject");
        EXPECT_STREQ(row.name, "wn");
    }

    TEST(ArtGenSetTest, AnEmptiedRowNameFollowsAgain) {
        ArtGenSubjectRow row;
        TypeSubject(row, "rook");
        TypeName(row, "r");
        TypeName(row, "");
        EXPECT_STREQ(row.name, "rook");
        TypeSubject(row, "black rook");
        EXPECT_STREQ(row.name, "black_rook");
    }

    TEST(ArtGenSetTest, RowSubjectWithNoLetterSuggestsNothing) {
        ArtGenSubjectRow row;
        TypeSubject(row, "white knight");
        TypeSubject(row, "!!!");
        EXPECT_STREQ(row.name, "");
    }

    TEST(ArtGenSetTest, SubjectsFromRowsSkipEmptyAndKeepTheRowNumberInErrors) {
        Vector<ArtGenSubjectRow> rows(4);
        TypeSubject(rows[0], "White Knight");
        TypeSubject(rows[2], "  black rook  ");
        TypeName(rows[2], "br");
        const auto subjects = ArtGenSubjectsFromRows(rows);
        ASSERT_TRUE(subjects.has_value()) << subjects.error();
        ASSERT_EQ(subjects->size(), 2u);
        EXPECT_EQ((*subjects)[0].name, "white_knight");
        EXPECT_EQ((*subjects)[0].subject, "White Knight");
        EXPECT_EQ((*subjects)[1].name, "br");
        EXPECT_EQ((*subjects)[1].subject, "black rook");

        TypeSubject(rows[3], "another");
        TypeName(rows[3], "br");
        const auto twice = ArtGenSubjectsFromRows(rows);
        ASSERT_FALSE(twice.has_value());
        EXPECT_TRUE(Contains(twice.error(), "row 4")) << twice.error();
        EXPECT_TRUE(Contains(twice.error(), "row 3")) << twice.error();
    }

    TEST(ArtGenSetTest, SubjectsFromRowsRefuseABadNameAndNoSubjects) {
        Vector<ArtGenSubjectRow> rows(2);
        EXPECT_FALSE(ArtGenSubjectsFromRows(rows).has_value());
        TypeSubject(rows[1], "knight");
        TypeName(rows[1], "a b");
        const auto bad = ArtGenSubjectsFromRows(rows);
        ASSERT_FALSE(bad.has_value());
        EXPECT_TRUE(Contains(bad.error(), "row 2")) << bad.error();

        TypeSubject(rows[1], "!!!");
        TypeName(rows[1], "");
        const auto unnamed = ArtGenSubjectsFromRows(rows);
        ASSERT_FALSE(unnamed.has_value());
        EXPECT_TRUE(Contains(unnamed.error(), "no letter or digit")) << unnamed.error();
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

    TEST(ArtGenSetTest, ANumberedDefaultNameKeepsTheWholeAValidName) {
        const std::chrono::system_clock::time_point time = std::chrono::sys_days{std::chrono::year{2026} / std::chrono::January / 2} + std::chrono::hours{3};
        EXPECT_EQ(DefaultArtGenSetName("wood", time, 1), DefaultArtGenSetName("wood", time));
        EXPECT_EQ(DefaultArtGenSetName("wood", time, 2), "wood-20260102-030000-2");
        EXPECT_EQ(DefaultArtGenSetName("wood", time, 13), "wood-20260102-030000-13");
        const String longName(70, 'p');
        for (const int32_t attempt : {1, 2, 9, 10, 100}) {
            const String name = DefaultArtGenSetName(longName, time, attempt);
            EXPECT_LE(name.size(), static_cast<size_t>(kMaxSpriteNameLength)) << name.c_str();
            EXPECT_TRUE(IsValidSpriteName(name)) << name.c_str();
            EXPECT_TRUE(Contains(name, "-20260102-030000")) << name.c_str();
        }
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

    TEST(ArtGenSetTest, LiveKeptAndAlternatives) {
        ArtGenSet set = MakeSet();
        EXPECT_TRUE(IsLive(set.jobs[0]));
        EXPECT_FALSE(IsLive(set.jobs[1]));
        EXPECT_EQ(CountLive(set), 1);
        EXPECT_EQ(CountKept(set), 0);
        EXPECT_EQ(AlternativesOf(set.jobs[0]), 1);
        EXPECT_EQ(AlternativesOf(set.jobs[1]), 0);

        set.jobs[0].accepted->kept = true;
        EXPECT_FALSE(IsLive(set.jobs[0]));
        EXPECT_EQ(CountLive(set), 0);
        EXPECT_EQ(CountKept(set), 1);

        set.jobs[0].pick.reset();
        EXPECT_EQ(AlternativesOf(set.jobs[0]), 2);
    }

    TEST(ArtGenSetTest, StatusOfARowSaysWhetherTheGameHoldsIt) {
        ArtGenSet set = MakeSet();
        ASSERT_TRUE(set.jobs[0].accepted.has_value());
        EXPECT_EQ(StatusOf(set.jobs[0], false), ArtGenRowStatus::Live);
        EXPECT_EQ(StatusOf(set.jobs[0], true), ArtGenRowStatus::Replaced);
        EXPECT_EQ(StatusOf(set.jobs[1], false), ArtGenRowStatus::NotInGame);
        EXPECT_EQ(StatusOf(set.jobs[1], true), ArtGenRowStatus::NotInGame);

        set.jobs[0].accepted->kept = true;
        EXPECT_EQ(StatusOf(set.jobs[0], false), ArtGenRowStatus::Kept);
        EXPECT_EQ(StatusOf(set.jobs[0], true), ArtGenRowStatus::Replaced);
    }

    TEST(ArtGenSetTest, FindJobByName) {
        ArtGenSet set = MakeSet();
        ASSERT_NE(FindJob(set, "black_knight"), nullptr);
        EXPECT_EQ(FindJob(set, "black_knight")->subject, "a black knight");
        EXPECT_EQ(FindJob(set, "queen"), nullptr);
        const ArtGenSet& view = set;
        EXPECT_EQ(FindJob(view, "white_knight"), &set.jobs[0]);
    }

    TEST(ArtGenSetTest, KeepJobTrimsTheVariantsToThePick) {
        ArtGenSet set = MakeSet();
        ArtGenSetJob& job = set.jobs[0];
        ASSERT_TRUE(KeepJob(job, "2026-11-18T10:00:00Z"));
        EXPECT_TRUE(job.accepted->kept);
        EXPECT_EQ(job.accepted->date, "2026-11-18T10:00:00Z");
        ASSERT_EQ(job.variants.size(), 1u);
        EXPECT_EQ(job.variants[0].seed, 4243);
        EXPECT_EQ(job.pick, Optional<int64_t>(4243));

        const auto back = ArtGenSetFromJson(ArtGenSetToJson(set));
        ASSERT_TRUE(back.has_value()) << back.error();
        EXPECT_EQ(back->jobs[0].variants.size(), 1u);
    }

    TEST(ArtGenSetTest, KeepJobRefusesWhatIsNotLive) {
        ArtGenSet set = MakeSet();
        EXPECT_FALSE(KeepJob(set.jobs[1], "d"));
        EXPECT_EQ(set.jobs[1].variants.size(), 0u);
        ASSERT_TRUE(KeepJob(set.jobs[0], "d1"));
        EXPECT_FALSE(KeepJob(set.jobs[0], "d2"));
        EXPECT_EQ(set.jobs[0].accepted->date, "d1");

        ArtGenSetJob picked = MakeSet().jobs[0];
        picked.accepted.reset();
        EXPECT_FALSE(KeepJob(picked, "d"));
        EXPECT_EQ(picked.variants.size(), 2u);
    }

    TEST(ArtGenSetTest, MoreRoundTripsWithAndWithout) {
        ArtGenSet set = MakeSet();
        const Json plain = ArtGenSetToJson(set);
        ASSERT_TRUE(plain["jobs"][0].contains("more"));
        EXPECT_TRUE(plain["jobs"][0]["more"].is_null());
        const auto readPlain = ArtGenSetFromJson(plain);
        ASSERT_TRUE(readPlain.has_value()) << readPlain.error();
        EXPECT_FALSE(readPlain->jobs[0].more.has_value());

        set.jobs[0].state = ArtGenSetJobState::Pending;
        set.jobs[0].more = ArtGenSetMore{9000, 5};
        const Json json = ArtGenSetToJson(set);
        EXPECT_EQ(GetJsonInt(json["jobs"][0]["more"], "seed"), 9000);
        EXPECT_EQ(GetJsonInt(json["jobs"][0]["more"], "count"), 5);
        const auto back = ArtGenSetFromJson(json);
        ASSERT_TRUE(back.has_value()) << back.error();
        ASSERT_TRUE(back->jobs[0].more.has_value());
        EXPECT_EQ(back->jobs[0].more->seed, 9000);
        EXPECT_EQ(back->jobs[0].more->count, 5);
        EXPECT_EQ(ArtGenSetToJson(*back), json);
        EXPECT_FALSE(back->jobs[1].more.has_value());
    }

    TEST(ArtGenSetTest, AManifestWithoutMoreReadsAsNone) {
        const auto set = ArtGenSetFromJson(Manifest(kMinimalManifest));
        ASSERT_TRUE(set.has_value()) << set.error();
        EXPECT_FALSE(set->jobs[0].more.has_value());
    }

    TEST(ArtGenSetTest, ABadMoreIsAnErrorThatSaysWhere) {
        const struct {
            const char* more;
            const char* mentions;
        } cases[] = {
            {R"("two")", "must be an object or null"},
            {R"({"seed":5,"count":0})", "'count' must be 1 to 16"},
            {R"({"seed":5,"count":17})", "'count' must be 1 to 16"},
            {R"({"seed":-1,"count":2})", "'seed' must not be negative"},
            {R"({"seed":"x","count":2})", "'seed' must be an integer"},
            {R"({"seed":5})", "'count' must be an integer"},
        };
        for (const auto& item : cases) {
            Json json = Manifest(kMinimalManifest);
            json["jobs"][0]["more"] = Manifest(item.more);
            const String error = ErrorOf(ArtGenSetFromJson(json));
            EXPECT_TRUE(Contains(error, "jobs[0].more")) << item.more << " -> " << error;
            EXPECT_TRUE(Contains(error, item.mentions)) << item.more << " -> " << error;
        }
    }

    TEST(ArtGenSetTest, MoreBlockerSaysWhyItIsOff) {
        ArtGenSet set = MakeSet();
        // MakeSet: a live Done row and a Failed row
        EXPECT_EQ(ArtGenMoreBlocker(set, 1), nullptr);

        ArtGenSet active = MakeSet();
        active.jobs[1].state = ArtGenSetJobState::Running;
        ASSERT_NE(ArtGenMoreBlocker(active, 1), nullptr);
        EXPECT_TRUE(Contains(String(ArtGenMoreBlocker(active, 1)), "still running"));

        ArtGenSet kept = MakeSet();
        kept.jobs[0].accepted->kept = true;
        ASSERT_NE(ArtGenMoreBlocker(kept, 1), nullptr);
        EXPECT_TRUE(Contains(String(ArtGenMoreBlocker(kept, 1)), "kept"));

        ArtGenSet failedOnly = MakeSet();
        failedOnly.jobs[0].state = ArtGenSetJobState::Cancelled;
        EXPECT_NE(ArtGenMoreBlocker(failedOnly, 1), nullptr);

        // A row that cannot take the count is not runnable, so the button and the refusal agree
        ArtGenSet full = MakeSet();
        full.jobs[0].variants.clear();
        for (int64_t seed = 0; seed < kArtGenJobMaxVariants - 1; ++seed) {
            full.jobs[0].variants.push_back({seed, "a.png", "", "image"});
        }
        EXPECT_EQ(ArtGenMoreBlocker(full, 1), nullptr);
        ASSERT_NE(ArtGenMoreBlocker(full, 2), nullptr);
        EXPECT_TRUE(Contains(String(ArtGenMoreBlocker(full, 2)), "full"));
    }

    TEST(ArtGenSetTest, KeepBlockerIsOnlyForAnActiveSet) {
        ArtGenSet set = MakeSet();
        EXPECT_EQ(ArtGenKeepBlocker(set), nullptr);
        set.jobs[1].state = ArtGenSetJobState::Pending;
        ASSERT_NE(ArtGenKeepBlocker(set), nullptr);
        EXPECT_TRUE(Contains(String(ArtGenKeepBlocker(set)), "still running"));
    }

    TEST(ArtGenSetTest, MoreSkipReasons) {
        const ArtGenSet set = MakeSet();
        EXPECT_EQ(ArtGenMoreSkipReason(set.jobs[0], 1), nullptr);
        EXPECT_STREQ(ArtGenMoreSkipReason(set.jobs[1], 1), "failed");

        ArtGenSetJob job = set.jobs[0];
        job.state = ArtGenSetJobState::Cancelled;
        EXPECT_STREQ(ArtGenMoreSkipReason(job, 1), "cancelled");
        job.state = ArtGenSetJobState::Done;
        job.accepted->kept = true;
        EXPECT_STREQ(ArtGenMoreSkipReason(job, 1), "kept");

        job.accepted.reset();
        job.variants.clear();
        for (int64_t seed = 0; seed < kArtGenJobMaxVariants - 2; ++seed) {
            job.variants.push_back({seed, "a.png", "", "image"});
        }
        EXPECT_EQ(ArtGenMoreSkipReason(job, 2), nullptr);
        EXPECT_STREQ(ArtGenMoreSkipReason(job, 3), "full");
    }

    TEST(ArtGenSetTest, SeedRangeEdges) {
        ArtGenSetJob job;
        job.variants.push_back({100, "a.png", "", "image"});
        EXPECT_TRUE(ArtGenSeedRangeFree(job, 97, 3));    // 97, 98, 99: ends right before 100
        EXPECT_FALSE(ArtGenSeedRangeFree(job, 98, 3));   // 98, 99, 100: touches it
        EXPECT_FALSE(ArtGenSeedRangeFree(job, 100, 1));  // exactly the seed
        EXPECT_TRUE(ArtGenSeedRangeFree(job, 101, 4));   // starts right after it
        EXPECT_TRUE(ArtGenSeedRangeFree(ArtGenSetJob(), 0, kArtGenSetMaxCount));

        ArtGenSetJob extreme;
        extreme.variants.push_back({std::numeric_limits<int64_t>::max(), "a.png", "", "image"});
        EXPECT_TRUE(ArtGenSeedRangeFree(extreme, 0, 16));
        EXPECT_FALSE(ArtGenSeedRangeFree(extreme, std::numeric_limits<int64_t>::max() - 3, 16));
        ArtGenSetJob negative;
        negative.variants.push_back({-5, "a.png", "", "image"});
        EXPECT_TRUE(ArtGenSeedRangeFree(negative, 0, 16));
        EXPECT_FALSE(ArtGenSeedRangeFree(negative, -6, 2));
    }

    TEST(ArtGenSetTest, EndingAPlainJob) {
        ArtGenSetJob job;
        job.state = ArtGenSetJobState::Running;
        EndArtGenJob(job, ArtGenSetJobState::Failed, "boom");
        EXPECT_EQ(job.state, ArtGenSetJobState::Failed);
        EXPECT_EQ(job.error, "boom");

        EndArtGenJob(job, ArtGenSetJobState::Cancelled, "stopped");
        EXPECT_EQ(job.state, ArtGenSetJobState::Cancelled);
        EXPECT_EQ(job.error, "stopped");

        EndArtGenJob(job, ArtGenSetJobState::Done, "ignored");
        EXPECT_EQ(job.state, ArtGenSetJobState::Done);
        EXPECT_TRUE(job.error.empty());
    }

    TEST(ArtGenSetTest, EndingAMoreRunNeverLosesTheJobsWork) {
        for (const ArtGenSetJobState ended : {ArtGenSetJobState::Failed, ArtGenSetJobState::Cancelled}) {
            ArtGenSetJob job = MakeSet().jobs[0];
            job.state = ArtGenSetJobState::Running;
            job.more = ArtGenSetMore{900, 2};
            EndArtGenJob(job, ended, "the service was stopped");
            EXPECT_EQ(job.state, ArtGenSetJobState::Done);
            EXPECT_EQ(job.error, "the service was stopped");
            EXPECT_FALSE(job.more.has_value());
            EXPECT_EQ(job.variants.size(), 2u);
            EXPECT_EQ(job.pick, Optional<int64_t>(4243));
            ASSERT_TRUE(job.accepted.has_value());
            EXPECT_FALSE(job.accepted->kept);
        }

        ArtGenSetJob done = MakeSet().jobs[0];
        done.state = ArtGenSetJobState::Running;
        done.more = ArtGenSetMore{900, 2};
        done.error = "an old note";
        EndArtGenJob(done, ArtGenSetJobState::Done, "");
        EXPECT_EQ(done.state, ArtGenSetJobState::Done);
        EXPECT_TRUE(done.error.empty());
        EXPECT_FALSE(done.more.has_value());
    }

}  // namespace BECore::Tests
