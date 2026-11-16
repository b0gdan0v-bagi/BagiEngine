#include <BECore/Control/Json.h>
#include <Common/Printers.h>
#include <Widgets/ArtGen/ArtGenPresetForm.h>
#include <cstring>
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
        EXPECT_EQ(*form.FindParam("cfg"), Json(6.5));
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

    TEST(ArtGenPresetFormTest, TheServiceFillsItsOwnInputs) {
        for (const char* input : {"prompt", "negative", "seed", "width", "height", "output"}) {
            EXPECT_TRUE(IsArtGenServiceInput(input)) << input;
        }
        EXPECT_FALSE(IsArtGenServiceInput("steps"));
    }

}  // namespace BECore::Tests
