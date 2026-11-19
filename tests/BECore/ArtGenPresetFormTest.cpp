#include <BECore/Control/Json.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Widgets/ArtGen/ArtGenPresetForm.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        ArtGenPreset WoodPreset() {
            ArtGenPreset preset;
            preset.name = "wood";
            preset.title = "Wood";
            preset.source = "game";
            preset.data = Json{{"preset", 1},
                               {"title", "Wood"},
                               {"workflow", "sdxl_basic"},
                               {"prompt", "carved {subject}"},
                               {"negative", "blurry"},
                               {"size", Json::array({512, 768})},
                               {"params", Json{{"steps", 30}, {"cfg", 6.5}}},
                               {"note", "kept as it is"}};
            return preset;
        }

        ArtGenWorkflow Workflow(StringView name, bool withSize) {
            ArtGenWorkflow workflow;
            workflow.name = String(name);
            for (const char* input : {"prompt", "seed", "steps"}) {
                workflow.inputs.push_back(ArtGenWorkflowInput{String(input), Json(1), Json()});
            }
            if (withSize) {
                workflow.inputs.push_back(ArtGenWorkflowInput{"width", Json(1024), Json()});
                workflow.inputs.push_back(ArtGenWorkflowInput{"height", Json(1024), Json()});
            }
            return workflow;
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
        EXPECT_EQ(Require(form.FindParam("cfg")), Json(6.5));
        EXPECT_EQ(form.ToJson(), WoodPreset().data) << "a key the form does not show is kept";
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(form.GetBaseSource(), "game");
    }

    TEST(ArtGenPresetFormTest, AnEditMakesItDirtyAndRevertGoesBack) {
        ArtGenPresetForm form;
        form.Load(WoodPreset());
        std::strcpy(form.prompt, "painted {subject}");
        form.Touch();
        EXPECT_TRUE(form.IsDirty());
        EXPECT_EQ(GetJsonString(form.ToJson(), "prompt"), Optional<String>("painted {subject}"));
        form.Revert();
        EXPECT_FALSE(form.IsDirty());
        EXPECT_STREQ(form.prompt, "carved {subject}");

        form.SetParam("steps", Json(12));
        EXPECT_TRUE(form.IsDirty());
        form.SetParam("steps", Json(30));
        EXPECT_FALSE(form.IsDirty()) << "the same value as the file is not a change";
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
        const Json fresh = form.ToJson();
        EXPECT_EQ(fresh, (Json{{"preset", 1}, {"workflow", "sdxl_cutout"}, {"prompt", "{subject}"}}));
        EXPECT_TRUE(CheckArtGenPresetShape(fresh).has_value());

        form.Load(WoodPreset());
        form.Duplicate();
        EXPECT_STREQ(form.name, "wood_copy");
        EXPECT_FALSE(form.HasBase());
        EXPECT_TRUE(form.IsDirty());
        form.Revert();
        EXPECT_STREQ(form.name, "wood_copy") << "a copy has nothing to revert to";
    }

    TEST(ArtGenPresetFormTest, AnotherWorkflowDropsTheParamsAndTheSizeItCannotTake) {
        ArtGenPresetForm form;
        form.Load(WoodPreset());
        const ArtGenWorkflow noSize = Workflow("plain", false);
        form.SetWorkflow("plain", &noSize);
        EXPECT_NE(form.FindParam("steps"), nullptr);
        EXPECT_EQ(form.FindParam("cfg"), nullptr);
        EXPECT_EQ(form.width, 0);
        EXPECT_FALSE(form.ToJson().contains("size"));

        const ArtGenWorkflow sized = Workflow("sized", true);
        EXPECT_TRUE(sized.HasSize());
        EXPECT_FALSE(noSize.HasSize());
    }

    TEST(ArtGenPresetFormTest, TheFileHasTheKeysInTheOrderOfTheDocs) {
        const String text = FormatArtGenPresetFile(WoodPreset().data);
        EXPECT_EQ(text, "{\n  \"preset\": 1,\n  \"title\": \"Wood\",\n  \"workflow\": \"sdxl_basic\",\n  \"prompt\": \"carved {subject}\",\n  \"negative\": \"blurry\",\n"
                        "  \"size\": [512,768],\n  \"params\": {\"cfg\":6.5,\"steps\":30},\n  \"note\": \"kept as it is\"\n}\n");
        const auto parsed = ParseJson(text);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, WoodPreset().data);
    }

    TEST(ArtGenPresetFormTest, TheShapeCheckNamesWhatIsWrong) {
        const Json good = WoodPreset().data;
        EXPECT_TRUE(CheckArtGenPresetShape(good).has_value());
        const auto errorOf = [](Json preset) { return CheckArtGenPresetShape(preset).error(); };
        Json changed = good;
        changed["preset"] = 2;
        EXPECT_NE(errorOf(changed).find("format version"), String::npos);
        changed = good;
        changed["workflow"] = "";
        EXPECT_NE(errorOf(changed).find("\"workflow\" is required"), String::npos);
        changed = good;
        changed["size"] = Json::array({1});
        EXPECT_NE(errorOf(changed).find("\"size\""), String::npos);
        changed = good;
        changed["params"] = Json::array();
        EXPECT_NE(errorOf(changed).find("\"params\""), String::npos);
        EXPECT_FALSE(CheckArtGenPresetShape(Json::array()).has_value());
    }

    TEST(ArtGenPresetFormTest, APresetWithoutAcceptHasTheDefaultsAndWritesNoKey) {
        ArtGenPresetForm form;
        form.Load(WoodPreset());
        EXPECT_FALSE(form.HasImageAccept());
        EXPECT_EQ(form.GetImageAccept(), ArtGenImageSettings{});
        EXPECT_FALSE(form.ToJson().contains("accept"));

        ArtGenImageSettings changed;
        changed.maxSize = 128;
        form.SetImageAccept(changed);
        EXPECT_TRUE(form.HasImageAccept());
        EXPECT_TRUE(form.IsDirty());
        EXPECT_EQ(form.ToJson()["accept"], (Json{{"trim", true}, {"padding", 2}, {"maxSize", 128}}));
        form.SetImageAccept(ArtGenImageSettings{});
        EXPECT_FALSE(form.IsDirty()) << "back to the defaults is the file as it was";
        EXPECT_FALSE(form.ToJson().contains("accept"));

        form.SetImageAccept(changed);
        form.Revert();
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(form.GetImageAccept(), ArtGenImageSettings{});
    }

    TEST(ArtGenPresetFormTest, AnAcceptKeyOfTheFileIsKeptAsWrittenUntilEdited) {
        ArtGenPreset preset = WoodPreset();
        preset.data["accept"] = Json{{"trim", false}};
        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_TRUE(form.HasImageAccept());
        EXPECT_FALSE(form.GetImageAccept().trim);
        EXPECT_EQ(form.GetImageAccept().maxSize, ArtGenImageSettings::kDefaultMaxSize) << "a missing key keeps the default";
        EXPECT_EQ(form.ToJson(), preset.data) << "the file's own text comes back, not the three keys";
        EXPECT_FALSE(form.IsDirty());

        ArtGenImageSettings edited = form.GetImageAccept();
        edited.padding = 10;
        form.SetImageAccept(edited);
        EXPECT_TRUE(form.IsDirty());
        EXPECT_EQ(form.ToJson()["accept"], (Json{{"trim", false}, {"padding", 10}, {"maxSize", 256}}));
        form.Revert();
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(form.GetImageAccept().padding, ArtGenImageSettings::kDefaultPadding);

        form.MarkSaved();
        EXPECT_FALSE(form.IsDirty());
    }

    TEST(ArtGenPresetFormTest, ClearingAcceptDropsTheKey) {
        ArtGenPreset preset = WoodPreset();
        preset.data["accept"] = Json{{"trim", false}, {"maxSize", 0}};
        ArtGenPresetForm form;
        form.Load(preset);
        form.ClearImageAccept();
        EXPECT_FALSE(form.HasImageAccept());
        EXPECT_EQ(form.GetImageAccept(), ArtGenImageSettings{});
        EXPECT_FALSE(form.ToJson().contains("accept"));
        EXPECT_TRUE(form.IsDirty());
        form.Revert();
        EXPECT_FALSE(form.IsDirty());
        EXPECT_FALSE(form.GetImageAccept().trim);

        form.MarkSaved();
        form.ClearImageAccept();
        form.MarkSaved();
        EXPECT_FALSE(form.IsDirty());
        EXPECT_FALSE(form.ToJson().contains("accept"));
    }

    TEST(ArtGenPresetFormTest, ABadAcceptIsKeptAndRefusedByTheShapeCheck) {
        ArtGenPreset preset = WoodPreset();
        preset.data["accept"] = Json{{"maxSize", 5}};
        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_FALSE(form.HasImageAccept());
        EXPECT_EQ(form.GetImageAccept(), ArtGenImageSettings{});
        EXPECT_EQ(form.ToJson(), preset.data) << "nothing of the file is lost";
        EXPECT_FALSE(form.IsDirty());

        const auto shape = CheckArtGenPresetShape(preset.data);
        ASSERT_FALSE(shape.has_value());
        EXPECT_NE(shape.error().find("accept"), String::npos);
        EXPECT_NE(shape.error().find("maxSize"), String::npos);

        preset.data["accept"] = "no";
        form.Load(preset);
        EXPECT_EQ(form.ToJson(), preset.data);
        EXPECT_FALSE(CheckArtGenPresetShape(preset.data).has_value());

        preset.data["accept"] = Json{{"trim", false}, {"padding", 5}, {"maxSize", 0}};
        EXPECT_TRUE(CheckArtGenPresetShape(preset.data).has_value());
    }

    TEST(ArtGenPresetFormTest, AcceptComesAfterParamsInTheFileAndSurvivesNewAndAWorkflowChange) {
        ArtGenPreset preset = WoodPreset();
        preset.data["accept"] = Json{{"maxSize", 64}};
        const String text = FormatArtGenPresetFile(preset.data);
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
        EXPECT_TRUE(form.ToJson().contains("accept"));
        form.Duplicate();
        EXPECT_EQ(form.GetImageAccept().maxSize, 64);
        form.New("sdxl_cutout");
        EXPECT_FALSE(form.HasImageAccept());
        EXPECT_FALSE(form.ToJson().contains("accept"));
    }

    TEST(ArtGenPresetFormTest, TheServiceFillsItsOwnInputs) {
        for (const char* input : {"prompt", "negative", "seed", "width", "height", "output", "reference_image"}) {
            EXPECT_TRUE(IsArtGenServiceInput(input)) << input;
        }
        EXPECT_FALSE(IsArtGenServiceInput("steps"));
    }

    TEST(ArtGenPresetFormTest, AReferenceKeyOfTheFileRoundTripsByteForByte) {
        ArtGenPreset preset = WoodPreset();
        preset.data["reference"] = Json{{"mode", "init"}, {"image", "assets/artgen/rook.png"}, {"weight", 0.35}};
        const String fileText = FormatArtGenPresetFile(preset.data);

        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(form.ToJson(), preset.data);
        EXPECT_EQ(FormatArtGenPresetFile(form.ToJson()), fileText);

        form.Duplicate();
        EXPECT_EQ(*FindJsonMember(form.ToJson(), "reference"), preset.data["reference"]) << "a copy keeps it";
        form.Load(preset);
        std::strcpy(form.prompt, "changed");
        form.Touch();
        form.Revert();
        EXPECT_EQ(form.ToJson(), preset.data);
        form.New("sdxl_basic");
        EXPECT_FALSE(form.ToJson().contains("reference")) << "a new preset has none";
    }

    TEST(ArtGenPresetFormTest, TheReferenceComesAfterAcceptAndBeforeTheOtherKeys) {
        ArtGenPreset preset = WoodPreset();
        preset.data["accept"] = Json{{"maxSize", 64}};
        preset.data["reference"] = Json{{"mode", "init"}, {"image", "a.png"}};
        const String text = FormatArtGenPresetFile(preset.data);
        const size_t accept = text.find("\"accept\"");
        const size_t reference = text.find("\"reference\"");
        const size_t note = text.find("\"note\"");
        ASSERT_NE(reference, String::npos);
        EXPECT_LT(accept, reference);
        EXPECT_LT(reference, note);
    }

    TEST(ArtGenPresetFormTest, TheAtlasGroupIsKeptAsTheFileHasItAndComesAfterAccept) {
        ArtGenPreset preset = WoodPreset();
        preset.data["accept"] = Json{{"maxSize", 64}};
        preset.data["atlas"] = "pieces";
        preset.data["reference"] = Json{{"mode", "init"}, {"image", "a.png"}};
        EXPECT_TRUE(CheckArtGenPresetShape(preset.data).has_value());

        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_EQ(form.ToJson(), preset.data);
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(FormatArtGenPresetFile(form.ToJson()), FormatArtGenPresetFile(preset.data));

        const String text = FormatArtGenPresetFile(preset.data);
        const size_t accept = text.find("\"accept\"");
        const size_t atlas = text.find("\"atlas\"");
        const size_t reference = text.find("\"reference\"");
        const size_t note = text.find("\"note\"");
        ASSERT_NE(atlas, String::npos);
        EXPECT_LT(accept, atlas);
        EXPECT_LT(atlas, reference);
        EXPECT_LT(reference, note);

        form.Duplicate();
        EXPECT_EQ(*FindJsonMember(form.ToJson(), "atlas"), Json("pieces")) << "a copy keeps it";
        form.MarkSaved();
        EXPECT_EQ(*FindJsonMember(form.ToJson(), "atlas"), Json("pieces"));
    }

    TEST(ArtGenPresetFormTest, ABadAtlasGroupIsRefusedBeforeASaveAndNamesTheKey) {
        ArtGenPreset preset = WoodPreset();
        for (const Json& bad : {Json(5), Json(""), Json("a b"), Json("../x"), Json(String(65, 'a').c_str()), Json::array(), Json(nullptr)}) {
            preset.data["atlas"] = bad;
            const auto shape = CheckArtGenPresetShape(preset.data);
            ASSERT_FALSE(shape.has_value()) << DumpJson(bad).c_str();
            EXPECT_NE(shape.error().find("atlas"), String::npos) << DumpJson(bad).c_str();
            ArtGenPresetForm form;
            form.Load(preset);
            EXPECT_EQ(form.ToJson(), preset.data) << "nothing of the file is lost";
        }
        preset.data.erase("atlas");
        EXPECT_TRUE(CheckArtGenPresetShape(preset.data).has_value());
    }

    TEST(ArtGenPresetFormTest, AMalformedReferenceIsKeptAndRefusedBeforeASave) {
        ArtGenPreset preset = WoodPreset();
        preset.data["reference"] = Json{{"mode", "init"}, {"image", "a.png"}, {"weight", "heavy"}};
        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_EQ(form.ToJson(), preset.data) << "nothing of the file is lost";

        const auto shape = CheckArtGenPresetShape(preset.data);
        ASSERT_FALSE(shape.has_value());
        EXPECT_NE(shape.error().find("reference.weight"), String::npos);
        for (const Json& bad : {Json("a.png"), Json::array(), Json::object(), Json{{"mode", "init"}, {"image", ""}}}) {
            preset.data["reference"] = bad;
            EXPECT_FALSE(CheckArtGenPresetShape(preset.data).has_value()) << DumpJson(bad).c_str();
        }
        preset.data["reference"] = Json{{"mode", "init"}, {"image", "a.png"}};
        EXPECT_TRUE(CheckArtGenPresetShape(preset.data).has_value());
    }

    TEST(ArtGenPresetFormTest, AReferenceWithoutAWeightOrWithANullOneRoundTripsAsTheFileHasIt) {
        for (const Json& written : {Json{{"mode", "style"}, {"image", "a.png"}}, Json{{"mode", "style"}, {"image", "a.png"}, {"weight", nullptr}}}) {
            ArtGenPreset preset = WoodPreset();
            preset.data["reference"] = written;
            ArtGenPresetForm form;
            form.Load(preset);
            ASSERT_TRUE(form.GetReference().has_value());
            EXPECT_EQ(form.GetReference()->mode, "style");
            EXPECT_FALSE(form.GetReference()->weight.has_value());
            EXPECT_FALSE(form.IsDirty());
            EXPECT_EQ(FormatArtGenPresetFile(form.ToJson()), FormatArtGenPresetFile(preset.data));
        }
    }

    TEST(ArtGenPresetFormTest, EditingTheReferenceMakesTheFormDirtyAndTheFileHasTheNewValue) {
        ArtGenPreset preset = WoodPreset();
        preset.data["reference"] = Json{{"mode", "init"}, {"image", "assets/artgen/rook.png"}, {"weight", 0.35}};
        ArtGenPresetForm form;
        form.Load(preset);

        ArtGenReference edited = *form.GetReference();
        edited.mode = "style";
        edited.image = "assets/artgen/king.png";
        edited.weight = 0.8;
        form.SetReference(edited);
        EXPECT_TRUE(form.IsDirty());
        const Json written = *FindJsonMember(form.ToJson(), "reference");
        EXPECT_EQ(written, (Json{{"mode", "style"}, {"image", "assets/artgen/king.png"}, {"weight", 0.8}}));

        form.MarkSaved();
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(*FindJsonMember(form.ToJson(), "reference"), written);

        form.SetReference(*ReadArtGenReference(*FindJsonMember(preset.data, "reference")));
        EXPECT_TRUE(form.IsDirty()) << "back to the first value is a change from the saved one";
        form.Revert();
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(form.GetReference()->image, "assets/artgen/king.png");
    }

    TEST(ArtGenPresetFormTest, TheEditedReferenceSurvivesADuplicateAndRevertGoesBackToTheFile) {
        ArtGenPreset preset = WoodPreset();
        preset.data["reference"] = Json{{"mode", "init"}, {"image", "a.png"}};
        ArtGenPresetForm form;
        form.Load(preset);
        form.SetReference(ArtGenReference{"style", "b.png", 0.5});
        form.Duplicate();
        EXPECT_TRUE(form.IsDirty());
        EXPECT_EQ(form.GetReference()->image, "b.png");

        form.Load(preset);
        form.SetReference(ArtGenReference{"style", "b.png", 0.5});
        form.Revert();
        EXPECT_EQ(form.ToJson(), preset.data);
        EXPECT_FALSE(form.IsDirty());
    }

    TEST(ArtGenPresetFormTest, SettingAReferenceOnAPresetWithNoneAddsTheKeyAndRemovingItDropsTheKey) {
        ArtGenPresetForm form;
        form.Load(WoodPreset());
        form.SetReference(ArtGenReference{"init", "a.png", std::nullopt});
        EXPECT_TRUE(form.IsDirty());
        EXPECT_EQ(*FindJsonMember(form.ToJson(), "reference"), (Json{{"mode", "init"}, {"image", "a.png"}}));

        form.ClearReference();
        EXPECT_FALSE(form.ToJson().contains("reference"));
        EXPECT_FALSE(form.IsDirty()) << "the file had none";

        ArtGenPreset preset = WoodPreset();
        preset.data["reference"] = Json{{"mode", "init"}, {"image", "a.png"}};
        form.Load(preset);
        form.ClearReference();
        EXPECT_TRUE(form.IsDirty());
        EXPECT_FALSE(form.ToJson().contains("reference"));
    }

    TEST(ArtGenPresetFormTest, AnUnreadableReferenceIsKeptUntilItIsReplacedOrRemoved) {
        ArtGenPreset preset = WoodPreset();
        preset.data["reference"] = Json{{"mode", "init"}, {"image", "a.png"}, {"weight", "heavy"}};
        ArtGenPresetForm form;
        form.Load(preset);
        EXPECT_FALSE(form.GetReference().has_value());
        EXPECT_TRUE(form.HasUnreadableReference());
        EXPECT_FALSE(form.IsDirty());
        EXPECT_EQ(FormatArtGenPresetFile(form.ToJson()), FormatArtGenPresetFile(preset.data)) << "an untouched file is written as it is";

        form.SetReference(ArtGenReference{"init", "b.png", 0.4});
        EXPECT_FALSE(form.HasUnreadableReference());
        EXPECT_TRUE(CheckArtGenPresetShape(form.ToJson()).has_value());
        EXPECT_TRUE(form.IsDirty());

        form.Revert();
        EXPECT_TRUE(form.HasUnreadableReference());
        form.ClearReference();
        EXPECT_FALSE(form.HasUnreadableReference());
        EXPECT_FALSE(form.ToJson().contains("reference"));
        EXPECT_TRUE(form.IsDirty());
    }

    TEST(ArtGenPresetFormTest, ANewReferenceWithAnEmptyImageIsRefusedBeforeASave) {
        ArtGenPresetForm form;
        form.Load(WoodPreset());
        form.SetReference(ArtGenReference{"init", "", std::nullopt});
        const auto shape = CheckArtGenPresetShape(form.ToJson());
        ASSERT_FALSE(shape.has_value());
        EXPECT_NE(shape.error().find("reference.image"), String::npos);
    }

    TEST(ArtGenPresetFormTest, AWorkflowWithTheReferenceImageInputIsAReferenceVariant) {
        ArtGenWorkflow plain = Workflow("sdxl_basic", true);
        EXPECT_FALSE(plain.IsReferenceVariant());
        ArtGenWorkflow variant = Workflow("sdxl_img2img", true);
        variant.inputs.push_back(ArtGenWorkflowInput{"reference_image", Json("example.png"), Json()});
        EXPECT_TRUE(variant.IsReferenceVariant());
    }

}  // namespace BECore::Tests
