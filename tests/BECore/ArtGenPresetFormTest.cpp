#include <BECore/Control/ControlParams.h>
#include <BECore/Control/JsonViewOps.h>
#include <Common/JsonDoc.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Widgets/ArtGen/ArtGenPresetForm.h>
#include <gtest/gtest.h>
#include <initializer_list>

namespace BECore::Tests {

    namespace {

        struct Member {
            StringView key;
            StringView value;  // the JSON text; empty drops the key
        };

        /// The wood preset as the session holds a preset's data (compact, keys in byte order) with @p changes applied; a new key goes last
        String WoodText(std::initializer_list<Member> changes = {}) {
            Vector<Pair<String, String>> members = {
                {"negative", R"("blurry")"}, {"note", R"("kept as it is")"}, {"params", R"({"cfg":6.5,"steps":30})"}, {"preset", "1"}, {"prompt", R"("carved {subject}")"},
                {"size", "[512,768]"},       {"title", R"("Wood")"},         {"workflow", R"("sdxl_basic")"}};
            for (const Member& change : changes) {
                const auto found = std::find_if(members.begin(), members.end(), [&](const auto& member) { return StringView(member.first) == change.key; });
                if (found != members.end() && change.value.empty()) {
                    members.erase(found);
                } else if (found != members.end()) {
                    found->second = String(change.value);
                } else if (!change.value.empty()) {
                    members.emplace_back(String(change.key), String(change.value));
                }
            }
            String text = "{";
            for (const auto& [key, value] : members) {
                text += text.size() > 1 ? "," : "";
                text += Format("\"{}\":{}", key, value);
            }
            return text + "}";
        }

        ArtGenPreset WoodPreset(std::initializer_list<Member> changes = {}) {
            ArtGenPreset preset;
            preset.name = "wood";
            preset.title = "Wood";
            preset.source = "game";
            preset.data = WoodText(changes);
            return preset;
        }

        ArtGenWorkflow Workflow(StringView name, bool withSize) {
            ArtGenWorkflow workflow;
            workflow.name = String(name);
            for (const char* input : {"prompt", "seed", "steps"}) {
                workflow.inputs.push_back(ArtGenWorkflowInput{String(input), ArtGenInputValue::FromInteger(1), {}});
            }
            if (withSize) {
                workflow.inputs.push_back(ArtGenWorkflowInput{"width", ArtGenInputValue::FromInteger(1024), {}});
                workflow.inputs.push_back(ArtGenWorkflowInput{"height", ArtGenInputValue::FromInteger(1024), {}});
            }
            return workflow;
        }

        /// Whether two texts are the same JSON value (the order of keys does not matter)
        bool SameJson(StringView a, StringView b) {
            const JsonDoc first(a);
            const JsonDoc second(b);
            return first.IsParsed() && second.IsParsed() && JsonEquals(first.Root(), second.Root());
        }

        bool Has(const String& json, StringView key) {
            const JsonDoc doc{StringView(json)};
            return FindParam(doc.Root(), key).has_value();
        }

        /// The text of the member @p key of an object text, as written
        String MemberOf(const String& json, StringView key) {
            const JsonDoc doc{StringView(json)};
            const Optional<JsonView> member = FindParam(doc.Root(), key);
            return member ? String(member->Raw()) : String();
        }

        String FileOf(StringView json) {
            return FormatArtGenPresetFile(JsonDoc(json));
        }

        std::expected<void, String> ShapeOf(StringView json) {
            return CheckArtGenPresetShape(JsonDoc(json));
        }

        String ShapeErrorOf(StringView json) {
            const auto shape = ShapeOf(json);
            return shape ? String() : shape.error();
        }

    }  // namespace

    TEST(ArtGenPresetFormTest, ALoadedPresetIsCleanAndWritesBackTheSameContent) {
        ArtGenPresetForm form;
        form.Load(WoodPreset());
        EXPECT_STREQ(form.name, "wood");
        EXPECT_STREQ(form.title, "Wood");
        EXPECT_STREQ(form.prompt, "carved {subject}");
        EXPECT_EQ(form.GetWorkflow(), "sdxl_basic");
        EXPECT_EQ(form.width, 512);
        EXPECT_EQ(form.height, 768);
        EXPECT_EQ(Require(form.FindParam("cfg")), ArtGenInputValue::FromNumber(6.5));
        EXPECT_EQ(form.ToJsonText(), WoodText()) << "a key the form does not show is kept";
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(form.GetBaseSource(), "game");
    }

    TEST(ArtGenPresetFormTest, ADataTextInAnotherOrderAndLayoutIsTheSameContent) {
        ArtGenPreset preset = WoodPreset();
        preset.data = R"({ "workflow": "sdxl_basic", "preset": 1, "prompt": "carved {subject}", "params": {"steps": 30, "cfg": 6.50},
                          "title": "Wood", "negative": "blurry", "size": [512, 768], "note": "kept as it is" })";
        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_FALSE(form.IsDirty()) << "the order and the layout of the file are not a change";
        EXPECT_EQ(form.ToJsonText(), WoodText());
    }

    TEST(ArtGenPresetFormTest, AnEditMakesItDirtyAndRevertGoesBack) {
        ArtGenPresetForm form;
        form.Load(WoodPreset());
        std::strcpy(form.prompt, "painted {subject}");
        form.Touch();
        EXPECT_TRUE(form.IsDirty());
        EXPECT_EQ(MemberOf(form.ToJsonText(), "prompt"), R"("painted {subject}")");
        form.Revert();
        EXPECT_FALSE(form.IsDirty());
        EXPECT_STREQ(form.prompt, "carved {subject}");

        form.SetParam("steps", ArtGenInputValue::FromInteger(12));
        EXPECT_TRUE(form.IsDirty());
        form.SetParam("steps", ArtGenInputValue::FromInteger(30));
        EXPECT_FALSE(form.IsDirty()) << "the same value as the file is not a change";
        form.SetParam("cfg", ArtGenInputValue::FromInteger(6));
        EXPECT_TRUE(form.IsDirty());
        form.SetParam("cfg", ArtGenInputValue::FromNumber(6.5));
        form.ClearParam("cfg");
        EXPECT_EQ(form.FindParam("cfg"), nullptr);
        EXPECT_TRUE(form.IsDirty());
    }

    TEST(ArtGenPresetFormTest, ARenameIsAChangeAndSavingMakesItTheGamesOwn) {
        ArtGenPreset starter = WoodPreset();
        starter.source = "engine";
        ArtGenPresetForm form;
        form.Load(starter);
        std::strcpy(form.name, "oak");
        form.Touch();
        EXPECT_TRUE(form.IsDirty());
        form.MarkSaved();
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(form.GetBaseName(), "oak");
        EXPECT_EQ(form.GetBaseSource(), "game");
    }

    TEST(ArtGenPresetFormTest, NewAndDuplicateAreUnsavedUntilSaved) {
        ArtGenPresetForm form;
        EXPECT_TRUE(form.IsEmpty());
        form.New("sdxl_cutout");
        EXPECT_FALSE(form.IsEmpty());
        EXPECT_FALSE(form.HasBase());
        EXPECT_TRUE(form.IsDirty());
        const String fresh = form.ToJsonText();
        EXPECT_EQ(fresh, R"({"preset":1,"prompt":"{subject}","workflow":"sdxl_cutout"})");
        EXPECT_TRUE(ShapeOf(fresh).has_value());

        form.Load(WoodPreset());
        form.Duplicate();
        EXPECT_STREQ(form.name, "wood_copy");
        EXPECT_FALSE(form.HasBase());
        EXPECT_TRUE(form.IsDirty());
        form.Revert();
        EXPECT_STREQ(form.name, "wood_copy") << "a copy has nothing to revert to";
    }

    TEST(ArtGenPresetFormTest, APresetWithNoDataIsAnEmptyFormThatIsNeverClean) {
        ArtGenPreset broken;
        broken.name = "broken";
        ArtGenPresetForm form;
        form.Load(broken);
        EXPECT_TRUE(form.IsEmpty());
        EXPECT_TRUE(form.IsDirty());
    }

    TEST(ArtGenPresetFormTest, AnotherWorkflowDropsTheParamsAndTheSizeItCannotTake) {
        ArtGenPresetForm form;
        form.Load(WoodPreset());
        const ArtGenWorkflow noSize = Workflow("plain", false);
        form.SetWorkflow("plain", &noSize);
        EXPECT_NE(form.FindParam("steps"), nullptr);
        EXPECT_EQ(form.FindParam("cfg"), nullptr);
        EXPECT_EQ(form.width, 0);
        EXPECT_FALSE(Has(form.ToJsonText(), "size"));

        const ArtGenWorkflow sized = Workflow("sized", true);
        EXPECT_TRUE(sized.HasSize());
        EXPECT_FALSE(noSize.HasSize());
    }

    TEST(ArtGenPresetFormTest, TheFileHasTheKeysInTheOrderOfTheDocs) {
        const String text = FileOf(WoodText());
        EXPECT_EQ(text, "{\n  \"preset\": 1,\n  \"title\": \"Wood\",\n  \"workflow\": \"sdxl_basic\",\n  \"prompt\": \"carved {subject}\",\n  \"negative\": \"blurry\",\n"
                        "  \"size\": [512,768],\n  \"params\": {\"cfg\":6.5,\"steps\":30},\n  \"note\": \"kept as it is\"\n}\n");
        EXPECT_TRUE(SameJson(text, WoodText()));
    }

    TEST(ArtGenPresetFormTest, TheKeysTheFormDoesNotKnowComeAfterTheDocsOnesInByteOrder) {
        const String text = FileOf(R"({"zeta":1,"alpha":{"b":2,"a":1},"workflow":"w","prompt":"p","preset":1,"é":0,"Beta":[]})");
        EXPECT_EQ(text, "{\n  \"preset\": 1,\n  \"workflow\": \"w\",\n  \"prompt\": \"p\",\n  \"Beta\": [],\n  \"alpha\": {\"a\":1,\"b\":2},\n  \"zeta\": 1,\n  \"é\": 0\n}\n");
    }

    TEST(ArtGenPresetFormTest, AnUntouchedFileOfTheFormIsWrittenBackByteForByte) {
        const ArtGenPreset preset = WoodPreset({{"accept", R"({"trim":false})"}, {"atlas", R"("pieces")"}, {"extra", R"({"z":[1,2,{"y":null}],"a":true})"}});
        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(FileOf(form.ToJsonText()), FileOf(preset.data));
        EXPECT_TRUE(SameJson(form.ToJsonText(), preset.data));
    }

    TEST(ArtGenPresetFormTest, TheShapeCheckNamesWhatIsWrong) {
        EXPECT_TRUE(ShapeOf(WoodText()).has_value());
        EXPECT_NE(ShapeErrorOf(WoodText({{"preset", "2"}})).find("format version"), String::npos);
        EXPECT_NE(ShapeErrorOf(WoodText({{"preset", R"("1")"}})).find("format version"), String::npos);
        EXPECT_NE(ShapeErrorOf(WoodText({{"workflow", R"("")"}})).find("\"workflow\" is required"), String::npos);
        EXPECT_NE(ShapeErrorOf(WoodText({{"prompt", "5"}})).find("\"prompt\" is required"), String::npos);
        for (const StringView size : {StringView("[1]"), StringView("[1,2,3]"), StringView("[0,5]"), StringView("[1.5,2]"), StringView(R"(["1",2])"), StringView("{}")}) {
            EXPECT_NE(ShapeErrorOf(WoodText({{"size", size}})).find("\"size\" must be [width, height] of positive integers"), String::npos) << String(size).c_str();
        }
        EXPECT_NE(ShapeErrorOf(WoodText({{"params", "[]"}})).find("\"params\" must be an object"), String::npos);
        EXPECT_NE(ShapeErrorOf(WoodText({{"accept", R"({"maxSize":5})"}})).find("\"accept\": "), String::npos);
        EXPECT_NE(ShapeErrorOf(WoodText({{"reference", R"({"mode":"init","image":""})"}})).find("reference.image"), String::npos);
        EXPECT_EQ(ShapeErrorOf("[]"), "a preset is a JSON object");
        EXPECT_EQ(ShapeErrorOf("not json"), "a preset is a JSON object") << "a view that is not valid is no object";
    }

    TEST(ArtGenPresetFormTest, APresetWithoutAcceptHasTheDefaultsAndWritesNoKey) {
        ArtGenPresetForm form;
        form.Load(WoodPreset());
        EXPECT_FALSE(form.HasImageAccept());
        EXPECT_EQ(form.GetImageAccept(), ArtGenImageSettings{});
        EXPECT_FALSE(Has(form.ToJsonText(), "accept"));

        ArtGenImageSettings changed;
        changed.maxSize = 128;
        form.SetImageAccept(changed);
        EXPECT_TRUE(form.HasImageAccept());
        EXPECT_TRUE(form.IsDirty());
        EXPECT_EQ(MemberOf(form.ToJsonText(), "accept"), R"({"maxSize":128,"padding":2,"trim":true})");
        form.SetImageAccept(ArtGenImageSettings{});
        EXPECT_FALSE(form.IsDirty()) << "back to the defaults is the file as it was";
        EXPECT_FALSE(Has(form.ToJsonText(), "accept"));

        form.SetImageAccept(changed);
        form.Revert();
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(form.GetImageAccept(), ArtGenImageSettings{});
    }

    TEST(ArtGenPresetFormTest, AnAcceptKeyOfTheFileIsKeptAsWrittenUntilEdited) {
        const ArtGenPreset preset = WoodPreset({{"accept", R"({"trim":false})"}});
        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_TRUE(form.HasImageAccept());
        EXPECT_FALSE(form.GetImageAccept().trim);
        EXPECT_EQ(form.GetImageAccept().maxSize, ArtGenImageSettings::kDefaultMaxSize) << "a missing key keeps the default";
        EXPECT_EQ(MemberOf(form.ToJsonText(), "accept"), R"({"trim":false})") << "the file's own text comes back, not the three keys";
        EXPECT_FALSE(form.IsDirty());

        ArtGenImageSettings edited = form.GetImageAccept();
        edited.padding = 10;
        form.SetImageAccept(edited);
        EXPECT_TRUE(form.IsDirty());
        EXPECT_EQ(MemberOf(form.ToJsonText(), "accept"), R"({"maxSize":256,"padding":10,"trim":false})");
        form.Revert();
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(form.GetImageAccept().padding, ArtGenImageSettings::kDefaultPadding);

        form.MarkSaved();
        EXPECT_FALSE(form.IsDirty());
    }

    TEST(ArtGenPresetFormTest, ClearingAcceptDropsTheKey) {
        const ArtGenPreset preset = WoodPreset({{"accept", R"({"trim":false,"maxSize":0})"}});
        ArtGenPresetForm form;
        form.Load(preset);
        form.ClearImageAccept();
        EXPECT_FALSE(form.HasImageAccept());
        EXPECT_EQ(form.GetImageAccept(), ArtGenImageSettings{});
        EXPECT_FALSE(Has(form.ToJsonText(), "accept"));
        EXPECT_TRUE(form.IsDirty());
        form.Revert();
        EXPECT_FALSE(form.IsDirty());
        EXPECT_FALSE(form.GetImageAccept().trim);

        form.MarkSaved();
        form.ClearImageAccept();
        form.MarkSaved();
        EXPECT_FALSE(form.IsDirty());
        EXPECT_FALSE(Has(form.ToJsonText(), "accept"));
    }

    TEST(ArtGenPresetFormTest, ABadAcceptIsKeptAndRefusedByTheShapeCheck) {
        ArtGenPreset preset = WoodPreset({{"accept", R"({"maxSize":5})"}});
        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_FALSE(form.HasImageAccept());
        EXPECT_EQ(form.GetImageAccept(), ArtGenImageSettings{});
        EXPECT_TRUE(SameJson(form.ToJsonText(), preset.data)) << "nothing of the file is lost";
        EXPECT_FALSE(form.IsDirty());

        const String shape = ShapeErrorOf(preset.data);
        EXPECT_NE(shape.find("accept"), String::npos);
        EXPECT_NE(shape.find("maxSize"), String::npos);

        preset = WoodPreset({{"accept", R"("no")"}});
        form.Load(preset);
        EXPECT_TRUE(SameJson(form.ToJsonText(), preset.data));
        EXPECT_FALSE(ShapeOf(preset.data).has_value());

        EXPECT_TRUE(ShapeOf(WoodText({{"accept", R"({"trim":false,"padding":5,"maxSize":0})"}})).has_value());
    }

    TEST(ArtGenPresetFormTest, AcceptComesAfterParamsInTheFileAndSurvivesNewAndAWorkflowChange) {
        const ArtGenPreset preset = WoodPreset({{"accept", R"({"maxSize":64})"}});
        const String text = FileOf(preset.data);
        const size_t params = text.find("\"params\"");
        const size_t accept = text.find("\"accept\"");
        const size_t note = text.find("\"note\"");
        ASSERT_NE(accept, String::npos);
        EXPECT_LT(params, accept);
        EXPECT_LT(accept, note);

        ArtGenPresetForm form;
        form.Load(preset);
        const ArtGenWorkflow audio = Workflow("sound", false);
        form.SetWorkflow("sound", &audio);
        EXPECT_EQ(form.GetImageAccept().maxSize, 64) << "a sound workflow keeps the key in the file";
        EXPECT_TRUE(Has(form.ToJsonText(), "accept"));
        form.Duplicate();
        EXPECT_EQ(form.GetImageAccept().maxSize, 64);
        form.New("sdxl_cutout");
        EXPECT_FALSE(form.HasImageAccept());
        EXPECT_FALSE(Has(form.ToJsonText(), "accept"));
    }

    TEST(ArtGenPresetFormTest, TheServiceFillsItsOwnInputs) {
        for (const char* input : {"prompt", "negative", "seed", "width", "height", "output", "reference_image"}) {
            EXPECT_TRUE(IsArtGenServiceInput(input)) << input;
        }
        EXPECT_FALSE(IsArtGenServiceInput("steps"));
    }

    TEST(ArtGenPresetFormTest, TheParamsOfAFileKeepEveryKindOfValue) {
        const ArtGenPreset preset = WoodPreset({{"params", R"({"on":true,"off":false,"none":null,"list":[3,{"b":1,"a":2}],"n":2,"x":1.0,"s":"text"})"}});
        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_EQ(Require(form.FindParam("on")), ArtGenInputValue::FromBool(true));
        EXPECT_EQ(Require(form.FindParam("none")).kind, ArtGenInputKind::None);
        EXPECT_EQ(Require(form.FindParam("s")), ArtGenInputValue::FromText("text"));
        EXPECT_EQ(Require(form.FindParam("list")).text, R"([3,{"a":2,"b":1}])");
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(MemberOf(form.ToJsonText(), "params"), R"({"list":[3,{"a":2,"b":1}],"n":2,"none":null,"off":false,"on":true,"s":"text","x":1.0})");
    }

    TEST(ArtGenPresetFormTest, OfTwoEqualParamKeysTheFirstCounts) {
        ArtGenPreset preset = WoodPreset({{"params", R"({"steps":1,"steps":2})"}});
        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_EQ(Require(form.FindParam("steps")), ArtGenInputValue::FromInteger(1));
    }

    TEST(ArtGenPresetFormTest, AMalformedSizeOrParamsIsKeptAmongTheOtherKeys) {
        const ArtGenPreset preset = WoodPreset({{"size", "[0,5]"}, {"params", "[1,2]"}});
        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_EQ(form.width, 0);
        EXPECT_TRUE(form.GetParams().empty());
        EXPECT_TRUE(SameJson(form.ToJsonText(), preset.data)) << "nothing of the file is lost";
        EXPECT_FALSE(form.IsDirty());
    }

    TEST(ArtGenPresetFormTest, AReferenceKeyOfTheFileRoundTripsByteForByte) {
        const ArtGenPreset preset = WoodPreset({{"reference", R"({"mode":"init","image":"assets/artgen/rook.png","weight":0.35})"}});
        const String fileText = FileOf(preset.data);

        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_FALSE(form.IsDirty());
        EXPECT_TRUE(SameJson(form.ToJsonText(), preset.data));
        EXPECT_EQ(FileOf(form.ToJsonText()), fileText);

        form.Duplicate();
        EXPECT_EQ(MemberOf(form.ToJsonText(), "reference"), R"({"image":"assets/artgen/rook.png","mode":"init","weight":0.35})") << "a copy keeps it";
        form.Load(preset);
        std::strcpy(form.prompt, "changed");
        form.Touch();
        form.Revert();
        EXPECT_TRUE(SameJson(form.ToJsonText(), preset.data));
        form.New("sdxl_basic");
        EXPECT_FALSE(Has(form.ToJsonText(), "reference")) << "a new preset has none";
    }

    TEST(ArtGenPresetFormTest, TheReferenceComesAfterAcceptAndBeforeTheOtherKeys) {
        const ArtGenPreset preset = WoodPreset({{"accept", R"({"maxSize":64})"}, {"reference", R"({"mode":"init","image":"a.png"})"}});
        const String text = FileOf(preset.data);
        const size_t accept = text.find("\"accept\"");
        const size_t reference = text.find("\"reference\"");
        const size_t note = text.find("\"note\"");
        ASSERT_NE(reference, String::npos);
        EXPECT_LT(accept, reference);
        EXPECT_LT(reference, note);
    }

    TEST(ArtGenPresetFormTest, TheAtlasGroupIsKeptAsTheFileHasItAndComesAfterAccept) {
        const ArtGenPreset preset = WoodPreset({{"accept", R"({"maxSize":64})"}, {"atlas", R"("pieces")"}, {"reference", R"({"mode":"init","image":"a.png"})"}});
        EXPECT_TRUE(ShapeOf(preset.data).has_value());

        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_TRUE(SameJson(form.ToJsonText(), preset.data));
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(FileOf(form.ToJsonText()), FileOf(preset.data));

        const String text = FileOf(preset.data);
        const size_t accept = text.find("\"accept\"");
        const size_t atlas = text.find("\"atlas\"");
        const size_t reference = text.find("\"reference\"");
        const size_t note = text.find("\"note\"");
        ASSERT_NE(atlas, String::npos);
        EXPECT_LT(accept, atlas);
        EXPECT_LT(atlas, reference);
        EXPECT_LT(reference, note);

        form.Duplicate();
        EXPECT_EQ(MemberOf(form.ToJsonText(), "atlas"), R"("pieces")") << "a copy keeps it";
        form.MarkSaved();
        EXPECT_EQ(MemberOf(form.ToJsonText(), "atlas"), R"("pieces")");
    }

    TEST(ArtGenPresetFormTest, ABadAtlasGroupIsRefusedBeforeASaveAndNamesTheKey) {
        const String tooLong = Format("\"{}\"", String(65, 'a'));
        for (const StringView bad : {StringView("5"), StringView(R"("")"), StringView(R"("a b")"), StringView(R"("../x")"), StringView(tooLong), StringView("[]"), StringView("null")}) {
            ArtGenPreset preset = WoodPreset({{"atlas", bad}});
            EXPECT_NE(ShapeErrorOf(preset.data).find("atlas"), String::npos) << String(bad).c_str();
            ArtGenPresetForm form;
            form.Load(preset);
            EXPECT_TRUE(SameJson(form.ToJsonText(), preset.data)) << "nothing of the file is lost: " << String(bad).c_str();
        }
        EXPECT_TRUE(ShapeOf(WoodText()).has_value());
    }

    TEST(ArtGenPresetFormTest, AMalformedReferenceIsKeptAndRefusedBeforeASave) {
        const ArtGenPreset preset = WoodPreset({{"reference", R"({"mode":"init","image":"a.png","weight":"heavy"})"}});
        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_TRUE(SameJson(form.ToJsonText(), preset.data)) << "nothing of the file is lost";

        EXPECT_NE(ShapeErrorOf(preset.data).find("reference.weight"), String::npos);
        for (const StringView bad : {StringView(R"("a.png")"), StringView("[]"), StringView("{}"), StringView(R"({"mode":"init","image":""})")}) {
            EXPECT_FALSE(ShapeOf(WoodText({{"reference", bad}})).has_value()) << String(bad).c_str();
        }
        EXPECT_TRUE(ShapeOf(WoodText({{"reference", R"({"mode":"init","image":"a.png"})"}})).has_value());
    }

    TEST(ArtGenPresetFormTest, AReferenceWithoutAWeightOrWithANullOneRoundTripsAsTheFileHas) {
        for (const StringView written : {StringView(R"({"mode":"style","image":"a.png"})"), StringView(R"({"mode":"style","image":"a.png","weight":null})")}) {
            const ArtGenPreset preset = WoodPreset({{"reference", written}});
            ArtGenPresetForm form;
            form.Load(preset);
            ASSERT_TRUE(form.GetReference().has_value());
            EXPECT_EQ(form.GetReference()->mode, "style");
            EXPECT_FALSE(form.GetReference()->weight.has_value());
            EXPECT_FALSE(form.IsDirty());
            EXPECT_EQ(FileOf(form.ToJsonText()), FileOf(preset.data)) << String(written).c_str();
        }
    }

    TEST(ArtGenPresetFormTest, EditingTheReferenceMakesTheFormDirtyAndTheFileHasTheNewValue) {
        const ArtGenPreset preset = WoodPreset({{"reference", R"({"mode":"init","image":"assets/artgen/rook.png","weight":0.35})"}});
        ArtGenPresetForm form;
        form.Load(preset);

        ArtGenReference edited = *form.GetReference();
        edited.mode = "style";
        edited.image = "assets/artgen/king.png";
        edited.weight = 0.8;
        form.SetReference(edited);
        EXPECT_TRUE(form.IsDirty());
        const String written = MemberOf(form.ToJsonText(), "reference");
        EXPECT_EQ(written, R"({"image":"assets/artgen/king.png","mode":"style","weight":0.8})");

        form.MarkSaved();
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(MemberOf(form.ToJsonText(), "reference"), written);

        form.SetReference(ArtGenReference{"init", "assets/artgen/rook.png", 0.35});
        EXPECT_TRUE(form.IsDirty()) << "back to the first value is a change from the saved one";
        form.Revert();
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(form.GetReference()->image, "assets/artgen/king.png");
    }

    TEST(ArtGenPresetFormTest, TheEditedReferenceSurvivesADuplicateAndRevertGoesBackToTheFile) {
        const ArtGenPreset preset = WoodPreset({{"reference", R"({"mode":"init","image":"a.png"})"}});
        ArtGenPresetForm form;
        form.Load(preset);
        form.SetReference(ArtGenReference{"style", "b.png", 0.5});
        form.Duplicate();
        EXPECT_TRUE(form.IsDirty());
        EXPECT_EQ(form.GetReference()->image, "b.png");

        form.Load(preset);
        form.SetReference(ArtGenReference{"style", "b.png", 0.5});
        form.Revert();
        EXPECT_TRUE(SameJson(form.ToJsonText(), preset.data));
        EXPECT_FALSE(form.IsDirty());
    }

    TEST(ArtGenPresetFormTest, SettingAReferenceOnAPresetWithNoneAddsTheKeyAndRemovingItDropsTheKey) {
        ArtGenPresetForm form;
        form.Load(WoodPreset());
        form.SetReference(ArtGenReference{"init", "a.png", std::nullopt});
        EXPECT_TRUE(form.IsDirty());
        EXPECT_EQ(MemberOf(form.ToJsonText(), "reference"), R"({"image":"a.png","mode":"init"})");

        form.ClearReference();
        EXPECT_FALSE(Has(form.ToJsonText(), "reference"));
        EXPECT_FALSE(form.IsDirty()) << "the file had none";

        const ArtGenPreset preset = WoodPreset({{"reference", R"({"mode":"init","image":"a.png"})"}});
        form.Load(preset);
        form.ClearReference();
        EXPECT_TRUE(form.IsDirty());
        EXPECT_FALSE(Has(form.ToJsonText(), "reference"));
    }

    TEST(ArtGenPresetFormTest, AnUnreadableReferenceIsKeptUntilItIsReplacedOrRemoved) {
        const ArtGenPreset preset = WoodPreset({{"reference", R"({"mode":"init","image":"a.png","weight":"heavy"})"}});
        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_FALSE(form.GetReference().has_value());
        EXPECT_TRUE(form.HasUnreadableReference());
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(FileOf(form.ToJsonText()), FileOf(preset.data)) << "an untouched file is written as it is";

        form.SetReference(ArtGenReference{"init", "b.png", 0.4});
        EXPECT_FALSE(form.HasUnreadableReference());
        EXPECT_TRUE(ShapeOf(form.ToJsonText()).has_value());
        EXPECT_TRUE(form.IsDirty());

        form.Revert();
        EXPECT_TRUE(form.HasUnreadableReference());
        form.ClearReference();
        EXPECT_FALSE(form.HasUnreadableReference());
        EXPECT_FALSE(Has(form.ToJsonText(), "reference"));
        EXPECT_TRUE(form.IsDirty());
    }

    TEST(ArtGenPresetFormTest, ANewReferenceWithAnEmptyImageIsRefusedBeforeASave) {
        ArtGenPresetForm form;
        form.Load(WoodPreset());
        form.SetReference(ArtGenReference{"init", "", std::nullopt});
        EXPECT_NE(ShapeErrorOf(form.ToJsonText()).find("reference.image"), String::npos);
    }

    TEST(ArtGenPresetFormTest, AWorkflowWithTheReferenceImageInputIsAReferenceVariant) {
        ArtGenWorkflow plain = Workflow("sdxl_basic", true);
        EXPECT_FALSE(plain.IsReferenceVariant());
        ArtGenWorkflow variant = Workflow("sdxl_img2img", true);
        variant.inputs.push_back(ArtGenWorkflowInput{"reference_image", ArtGenInputValue::FromText("example.png"), {}});
        EXPECT_TRUE(variant.IsReferenceVariant());
    }

}  // namespace BECore::Tests
