#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <Common/ControlTestSupport.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/EditorLayout/UserFileIo.h>
#include <gtest/gtest.h>
#include <initializer_list>

namespace BECore::Tests {

    namespace {

        constexpr int kFrameGuard = 100;

        Json Object() {
            return Json::object();
        }

        Json Preset(StringView name, StringView error = {}) {
            Json item = Json::object();
            item["name"] = ToStdString(name);
            item["title"] = ToStdString(name);
            if (!error.empty()) {
                item["error"] = ToStdString(error);
            }
            return item;
        }

        Json ReadyPoll() {
            Json params = Json::object();
            params["state"] = "ready";
            params["generator"] = "fake";
            params["presets"] = Json::array({Preset("wood"), Preset("broken", "no workflow 'x'")});
            return params;
        }

        Json Report(uint32_t id) {
            Json params = Json::object();
            params["id"] = id;
            return params;
        }

        /// What the fake acceptor was called with (a function pointer cannot capture, so the test reads it here)
        struct AcceptCall {
            int64_t seed = -1;
            String sprite;
            bool withNode = false;
            ArtGenImageSettings settings;
            bool hasFrame = false;
            String atlas;
            int calls = 0;
        };
        AcceptCall gAccepted;

        /// What the fake sound acceptor was called with
        struct SoundAcceptCall {
            int64_t seed = -1;
            ArtGenSoundTarget target;
            ArtGenSoundSettings settings;
            int calls = 0;
        };
        SoundAcceptCall gSoundAccepted;

        std::expected<Json, ControlError> FakeSoundAcceptor(const ArtGenVariant& variant, const ArtGenSoundTarget& target, const ArtGenSoundSettings& settings, SceneNode* /*node*/) {
            gSoundAccepted.seed = variant.seed;
            gSoundAccepted.target = target;
            gSoundAccepted.settings = settings;
            ++gSoundAccepted.calls;
            Json reply = Json::object();
            reply["clip"] = ToStdString(target.clip.empty() ? target.name : target.clip);
            return reply;
        }

        std::expected<Json, ControlError> FakeAcceptor(const ArtGenVariant& variant, StringView sprite, const ArtGenImagePlan& plan, SceneNode* node) {
            gAccepted.seed = variant.seed;
            gAccepted.settings = plan.settings;
            gAccepted.hasFrame = plan.frame.has_value();
            gAccepted.atlas = plan.atlas;
            EXPECT_FALSE(plan.deferPack) << "a single Accept packs at once";
            gAccepted.sprite = String(sprite);
            gAccepted.withNode = node != nullptr;
            ++gAccepted.calls;
            Json reply = Json::object();
            reply["sprite"] = ToStdString(sprite);
            return reply;
        }

    }  // namespace

    class ArtGenSessionTest : public ::testing::Test {
    protected:
        ArtGenSessionTest() : session([this] { return now; }) {}

        /// One control call through the service, as the bridge makes it; the parsed response
        Json Call(StringView method, const Json& params = Json::object()) {
            Json request = Json::object();
            request["id"] = ++_nextId;
            request["method"] = ToStdString(method);
            request["params"] = params;
            Optional<Json> response;
            service.Post(DumpJson(request), [&response](String reply) {
                auto parsed = ParseJson(reply);
                response = parsed ? std::move(*parsed) : Json();
            });
            for (int frame = 0; !response && frame < kFrameGuard; ++frame) {
                service.Drain();
                service.EndFrame();
            }
            return response ? std::move(*response) : Json();
        }

        /// Runs a request to its end with images of @p seeds, as the service would
        uint32_t RunWithImages(std::initializer_list<int64_t> seeds, bool keep = false) {
            const uint32_t id = Require(session.Request("wood", "queen", static_cast<int32_t>(seeds.size()), *seeds.begin(), keep));
            Call("artgen_poll", ReadyPoll());
            for (int64_t seed : seeds) {
                Json image = Report(id);
                image["seed"] = seed;
                image["path"] = Format("D:/out/queen_{}.png", seed).c_str();
                Call("artgen_image", image);
            }
            Json done = Report(id);
            done["status"] = "ok";
            Call("artgen_done", done);
            return id;
        }

        static const Json& Result(const Json& response) {
            static const Json kNull;
            const Json* result = FindJsonMember(response, "result");
            return result != nullptr ? *result : kNull;
        }

        void SetUp() override {
            gAccepted = AcceptCall{};
            gSoundAccepted = SoundAcceptCall{};
            session.SetGameRoot("D:/Games/Chess");
            session.RegisterControl(service);
        }

        double now = 100.0;
        ControlService service;
        ArtGenSession session;

    private:
        int64_t _nextId = 0;
    };

    TEST_F(ArtGenSessionTest, APollReportsTheServiceAndHandsTheRequestOverOnce) {
        EXPECT_FALSE(session.IsServiceConnected());
        Json poll = Call("artgen_poll", ReadyPoll());
        EXPECT_EQ(GetJsonString(Result(poll), "gameRoot"), Optional<String>("D:/Games/Chess"));
        EXPECT_EQ(FindJsonMember(Result(poll), "request"), nullptr);
        EXPECT_TRUE(session.IsServiceConnected());
        ASSERT_EQ(session.GetServiceStatus().presets.size(), 2u);

        Json params = Object();
        params["preset"] = "wood";
        params["subject"] = "white knight";
        params["count"] = 2;
        params["seed"] = 7;
        Json generated = Call("artgen_generate", params);
        const auto id = GetJsonInt(Result(generated), "id");
        ASSERT_TRUE(id.has_value());
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Queued);

        poll = Call("artgen_poll", ReadyPoll());
        const Json* request = FindJsonMember(Result(poll), "request");
        ASSERT_NE(request, nullptr);
        EXPECT_EQ(GetJsonInt(*request, "id"), id);
        EXPECT_EQ(GetJsonString(*request, "preset"), Optional<String>("wood"));
        EXPECT_EQ(GetJsonString(*request, "subject"), Optional<String>("white knight"));
        EXPECT_EQ(GetJsonInt(*request, "count"), Optional<int64_t>(2));
        EXPECT_EQ(GetJsonInt(*request, "seed"), Optional<int64_t>(7));
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Running);

        poll = Call("artgen_poll", ReadyPoll());
        EXPECT_EQ(FindJsonMember(Result(poll), "request"), nullptr);
    }

    TEST_F(ArtGenSessionTest, ProgressImagesAndDoneFillTheRun) {
        const uint32_t id = Require(session.Request("wood", "rook", 2, Optional<int64_t>()));
        Call("artgen_poll", ReadyPoll());
        const Json request = Result(Call("artgen_poll", ReadyPoll()));
        EXPECT_EQ(FindJsonMember(request, "request"), nullptr);

        Json progress = Report(id);
        progress["image"] = 0;
        progress["step"] = 5;
        progress["steps"] = 25;
        EXPECT_EQ(GetJsonBool(Result(Call("artgen_progress", progress)), "stale"), Optional<bool>(false));
        EXPECT_EQ(session.GetRun().step, 5);
        EXPECT_EQ(session.GetRun().steps, 25);

        for (int64_t seed : {11, 12}) {
            Json image = Report(id);
            image["seed"] = seed;
            image["path"] = Format("D:/Games/Chess/artgen/out/rook_{}.png", seed).c_str();
            image["provenance"] = Format("D:/Games/Chess/artgen/out/rook_{}.artgen.json", seed).c_str();
            Call("artgen_image", image);
        }
        ASSERT_EQ(session.GetRun().variants.size(), 2u);
        EXPECT_EQ(session.GetRun().variants[1].seed, 12);
        EXPECT_EQ(session.GetRun().variants[1].path, "D:/Games/Chess/artgen/out/rook_12.png");

        Json done = Report(id);
        done["status"] = "ok";
        Call("artgen_done", done);
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Done);
        EXPECT_FALSE(session.IsBusy());

        const Json state = Result(Call("artgen_state"));
        const Json* run = FindJsonMember(state, "run");
        ASSERT_NE(run, nullptr);
        EXPECT_EQ(GetJsonString(*run, "state"), Optional<String>("Done"));
        EXPECT_EQ(Require(FindJsonMember(*run, "variants")).size(), 2u);
    }

    TEST_F(ArtGenSessionTest, AFailureKeepsItsReason) {
        const uint32_t id = Require(session.Request("wood", "rook", 1, 3));
        Call("artgen_poll", ReadyPoll());
        Json done = Report(id);
        done["status"] = "failed";
        done["error"] = "ComfyUI failed: out of memory";
        Call("artgen_done", done);
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Failed);
        EXPECT_EQ(session.GetRun().error, "ComfyUI failed: out of memory");
    }

    TEST_F(ArtGenSessionTest, ReportsAboutAnOlderRunAreStale) {
        const uint32_t first = Require(session.Request("wood", "a", 1, 1));
        Call("artgen_poll", ReadyPoll());
        Json done = Report(first);
        done["status"] = "cancelled";
        Call("artgen_done", done);
        const uint32_t second = Require(session.Request("wood", "b", 1, 1));
        EXPECT_NE(first, second);
        Call("artgen_poll", ReadyPoll());

        Json late = Report(first);
        late["seed"] = 1;
        late["path"] = "x.png";
        EXPECT_EQ(GetJsonBool(Result(Call("artgen_image", late)), "stale"), Optional<bool>(true));
        EXPECT_TRUE(session.GetRun().variants.empty());
        EXPECT_EQ(session.GetRun().subject, "b");
    }

    TEST_F(ArtGenSessionTest, CancelBeforeTheServiceTookItNeedsNoService) {
        Require(session.Request("wood", "a", 1, 1));
        Call("artgen_cancel");
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Cancelled);
        EXPECT_EQ(FindJsonMember(Result(Call("artgen_poll", ReadyPoll())), "request"), nullptr);
    }

    TEST_F(ArtGenSessionTest, CancelOfARunningRequestGoesToTheServiceOnTheNextPoll) {
        const uint32_t id = Require(session.Request("wood", "a", 3, 1));
        Call("artgen_poll", ReadyPoll());
        session.Cancel();
        EXPECT_TRUE(session.GetRun().cancelRequested);
        EXPECT_EQ(GetJsonInt(Result(Call("artgen_poll", ReadyPoll())), "cancel"), Optional<int64_t>(id));
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Running);  // until the service says it stopped
        Json done = Report(id);
        done["status"] = "cancelled";
        Call("artgen_done", done);
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Cancelled);
        EXPECT_EQ(ErrorCode(Call("artgen_cancel")), "artgen_no_run");
    }

    TEST_F(ArtGenSessionTest, RequestsThatCannotRunAreRefused) {
        Call("artgen_poll", ReadyPoll());
        EXPECT_FALSE(session.Request("wood", "a", 0, 1).has_value());
        EXPECT_FALSE(session.Request("wood", "a", ArtGenSession::kMaxCount + 1, 1).has_value());
        EXPECT_FALSE(session.Request("wood", "a", 1, -1).has_value());
        EXPECT_FALSE(session.Request("", "a", 1, 1).has_value());
        EXPECT_FALSE(session.Request("broken", "a", 1, 1).has_value());

        Json params = Object();
        params["preset"] = "wood";
        EXPECT_EQ(ErrorCode(Call("artgen_generate", params)), "invalid_params");
        params["subject"] = "a";
        params["count"] = "four";
        EXPECT_EQ(ErrorCode(Call("artgen_generate", params)), "invalid_params");
        params.erase("count");
        EXPECT_TRUE(GetJsonInt(Result(Call("artgen_generate", params)), "id").has_value());
        EXPECT_EQ(session.GetRun().count, 4);
        EXPECT_EQ(ErrorCode(Call("artgen_generate", params)), "artgen_busy");
    }

    TEST_F(ArtGenSessionTest, TheServiceIsGoneWhenItStopsPolling) {
        Call("artgen_poll", ReadyPoll());
        now += ArtGenSession::kServiceTimeoutSeconds - 0.5;
        EXPECT_TRUE(session.IsServiceConnected());
        now += 1.0;
        EXPECT_FALSE(session.IsServiceConnected());
    }

    TEST_F(ArtGenSessionTest, ANotReadyGeneratorIsShownWithItsReason) {
        Json params = Object();
        params["state"] = "not_running";
        params["reason"] = "ComfyUI is installed in D:/ComfyUI but not started";
        const uint64_t before = session.Version();
        Call("artgen_poll", params);
        EXPECT_GT(session.Version(), before);
        EXPECT_EQ(session.GetServiceStatus().state, "not_running");
        EXPECT_EQ(session.GetServiceStatus().reason, "ComfyUI is installed in D:/ComfyUI but not started");
        EXPECT_TRUE(session.GetServiceStatus().presets.empty());
    }

    TEST_F(ArtGenSessionTest, ASecondRegistrationOnTheSameServiceChangesNothing) {
        session.RegisterControl(service);
        Call("artgen_poll", ReadyPoll());
        EXPECT_EQ(session.PollCount(), 1u);
        EXPECT_NE(FindJsonMember(Result(Call("artgen_state")), "service"), nullptr);
    }

    TEST_F(ArtGenSessionTest, AcceptGoesToTheAcceptorWithTheVariantOfThatSeed) {
        RunWithImages({21, 22});
        Json params = Object();
        params["seed"] = 22;
        params["sprite"] = "white_queen";
        EXPECT_EQ(ErrorCode(Call("artgen_accept", params)), "artgen_accept_failed");  // no acceptor yet
        session.SetAcceptor(&FakeAcceptor);
        const Json reply = Call("artgen_accept", params);
        EXPECT_EQ(GetJsonString(Result(reply), "sprite"), Optional<String>("white_queen"));
        EXPECT_EQ(gAccepted.seed, 22);
        EXPECT_EQ(gAccepted.sprite, "white_queen");
        EXPECT_FALSE(gAccepted.withNode);
    }

    TEST_F(ArtGenSessionTest, AcceptRefusesAnUnknownSeedOrAMissingSelection) {
        session.SetAcceptor(&FakeAcceptor);
        RunWithImages({5});
        Json params = Object();
        params["seed"] = 6;
        params["sprite"] = "q";
        EXPECT_EQ(ErrorCode(Call("artgen_accept", params)), "artgen_no_variant");
        params["seed"] = 5;
        params["useOnSelected"] = true;
        EXPECT_EQ(ErrorCode(Call("artgen_accept", params)), "artgen_accept_failed");
        params["useOnSelected"] = "yes";
        EXPECT_EQ(ErrorCode(Call("artgen_accept", params)), "invalid_params");
        params.erase("sprite");
        EXPECT_EQ(ErrorCode(Call("artgen_accept", params)), "invalid_params");
        EXPECT_EQ(gAccepted.calls, 0);
    }

    TEST_F(ArtGenSessionTest, ARunCanKeepTheVariantsOfTheLastOne) {
        RunWithImages({1, 2});
        RunWithImages({10}, true);
        ASSERT_EQ(session.GetRun().variants.size(), 3u);
        EXPECT_EQ(session.GetRun().keptVariants, 2);
        EXPECT_EQ(session.GetRun().variants[2].seed, 10);
        EXPECT_NE(session.FindVariant(1), nullptr);
        RunWithImages({20});
        ASSERT_EQ(session.GetRun().variants.size(), 1u);
        EXPECT_EQ(session.FindVariant(1), nullptr);
    }

    TEST_F(ArtGenSessionTest, ASetupActionIsHandedOverOnceAndItsReportsFillIt) {
        Json asked = Object();
        asked["action"] = "Plan";
        const auto id = GetJsonInt(Result(Call("artgen_setup", asked)), "id");
        ASSERT_TRUE(id.has_value());
        EXPECT_EQ(session.GetSetup().state, ArtGenSetupState::Queued);

        Json poll = Call("artgen_poll", ReadyPoll());
        const Json* setup = FindJsonMember(Result(poll), "setup");
        ASSERT_NE(setup, nullptr);
        EXPECT_EQ(GetJsonInt(*setup, "id"), id);
        EXPECT_EQ(GetJsonString(*setup, "action"), Optional<String>("Plan"));
        EXPECT_EQ(session.GetSetup().state, ArtGenSetupState::Running);
        EXPECT_EQ(FindJsonMember(Result(Call("artgen_poll", ReadyPoll())), "setup"), nullptr);

        Json plan = Report(static_cast<uint32_t>(*id));
        plan["event"] = "plan";
        plan["platform"] = "windows-nvidia";
        plan["root"] = "D:/ComfyUI";
        plan["downloadBytes"] = 9000;
        Json step = Object();
        step["what"] = "checkpoints/sd_xl_base_1.0.safetensors";
        step["state"] = "download";
        step["size"] = 9000;
        step["licence"] = "OpenRAIL++-M";
        plan["steps"] = Json::array({step});
        EXPECT_EQ(GetJsonBool(Result(Call("artgen_setup_report", plan)), "stale"), Optional<bool>(false));
        Json done = Report(static_cast<uint32_t>(*id));
        done["event"] = "done";
        done["ok"] = true;
        Call("artgen_setup_report", done);
        EXPECT_EQ(session.GetSetup().state, ArtGenSetupState::Done);
        ASSERT_EQ(session.GetSetup().plan.size(), 1u);
        EXPECT_EQ(session.GetSetup().plan[0].size, 9000);
        EXPECT_EQ(session.GetSetup().platform, "windows-nvidia");

        // The install keeps the plan the owner saw on screen, and reports its progress
        const uint32_t install = Require(session.RequestSetup(ArtGenSetupAction::Install));
        EXPECT_EQ(session.GetSetup().plan.size(), 1u);
        Call("artgen_poll", ReadyPoll());
        Json progress = Report(install);
        progress["event"] = "progress";
        progress["done"] = 4500;
        progress["total"] = 9000;
        Call("artgen_setup_report", progress);
        EXPECT_EQ(session.GetSetup().done, 4500);
        Json failed = Report(install);
        failed["event"] = "done";
        failed["ok"] = false;
        failed["error"] = "the download stopped; run again to resume";
        Call("artgen_setup_report", failed);
        EXPECT_EQ(session.GetSetup().state, ArtGenSetupState::Failed);
        EXPECT_EQ(session.GetSetup().error, "the download stopped; run again to resume");
    }

    TEST_F(ArtGenSessionTest, ASetupAndARunNeverRunTogether) {
        const uint32_t setup = Require(session.RequestSetup(ArtGenSetupAction::Start));
        Call("artgen_poll", ReadyPoll());
        EXPECT_FALSE(session.Request("wood", "a", 1, 1).has_value());
        EXPECT_FALSE(session.RequestSetup(ArtGenSetupAction::Stop).has_value());
        Json asked = Object();
        asked["action"] = "Stop";
        EXPECT_EQ(ErrorCode(Call("artgen_setup", asked)), "artgen_busy");
        Json done = Report(setup);
        done["event"] = "done";
        done["ok"] = true;
        Call("artgen_setup_report", done);

        ASSERT_TRUE(session.Request("wood", "a", 1, 1).has_value());
        EXPECT_FALSE(session.RequestSetup(ArtGenSetupAction::Plan).has_value());
    }

    TEST_F(ArtGenSessionTest, ACancelOfASetupGoesToTheServiceAndOldReportsAreStale) {
        const uint32_t first = Require(session.RequestSetup(ArtGenSetupAction::Install));
        Json cancel = Object();
        cancel["action"] = "cancel";
        EXPECT_EQ(FindJsonMember(Call("artgen_setup", cancel), "error"), nullptr);
        EXPECT_EQ(session.GetSetup().state, ArtGenSetupState::Failed);  // queued: the service never saw it

        const uint32_t second = Require(session.RequestSetup(ArtGenSetupAction::Install));
        Call("artgen_poll", ReadyPoll());
        session.CancelSetup();
        EXPECT_EQ(GetJsonInt(Result(Call("artgen_poll", ReadyPoll())), "setupCancel"), Optional<int64_t>(second));

        Json old = Report(first);
        old["event"] = "log";
        old["text"] = "late";
        EXPECT_EQ(GetJsonBool(Result(Call("artgen_setup_report", old)), "stale"), Optional<bool>(true));
        EXPECT_TRUE(session.GetSetup().log.empty());

        Json bad = Object();
        bad["action"] = "Dance";
        EXPECT_EQ(ErrorCode(Call("artgen_setup", bad)), "invalid_params");
        EXPECT_EQ(GetJsonString(Require(FindJsonMember(Result(Call("artgen_state")), "setup")), "state"), Optional<String>("Running"));
    }

    TEST_F(ArtGenSessionTest, ThePollSaysWhetherTheGeneratorWasStartedHere) {
        Call("artgen_poll", ReadyPoll());
        EXPECT_FALSE(session.GetServiceStatus().startedHere);
        Json poll = ReadyPoll();
        poll["startedHere"] = true;
        const uint64_t before = session.Version();
        Call("artgen_poll", poll);
        EXPECT_TRUE(session.GetServiceStatus().startedHere);
        EXPECT_GT(session.Version(), before);
    }

    TEST_F(ArtGenSessionTest, DisconnectTellsTheServiceToExitAndEndsWhatItWasDoing) {
        session.RequestServiceStop();  // not connected: nothing to tell
        EXPECT_EQ(FindJsonMember(Result(Call("artgen_poll", ReadyPoll())), "shutdown"), nullptr);

        ASSERT_TRUE(session.Request("wood", "queen", 1, 1).has_value());
        Call("artgen_poll", ReadyPoll());
        ASSERT_EQ(session.GetRun().state, ArtGenRunState::Running);
        session.RequestServiceStop();
        const Json answer = Call("artgen_poll", ReadyPoll());
        EXPECT_EQ(GetJsonBool(Result(answer), "shutdown"), Optional<bool>(true));
        EXPECT_FALSE(session.IsServiceConnected());
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Cancelled);

        // Started again, the service is told nothing more
        EXPECT_EQ(FindJsonMember(Result(Call("artgen_poll", ReadyPoll())), "shutdown"), nullptr);
        EXPECT_TRUE(session.IsServiceConnected());
    }

    namespace {

        Json DraftPreset() {
            return Json{{"preset", 1}, {"workflow", "sdxl_basic"}, {"prompt", "a {subject}"}, {"params", Json{{"steps", 9}}}};
        }

        Json CatalogPoll() {
            Json params = ReadyPoll();
            Json wood = Preset("wood");
            wood["source"] = "game";
            wood["preset"] = Json{{"preset", 1}, {"title", "Wood"}, {"workflow", "sdxl_basic"}, {"prompt", "carved {subject}"}};
            Json sprite = Preset("sprite");
            sprite["source"] = "engine";
            sprite["preset"] = Json{{"preset", 1}, {"workflow", "sdxl_cutout"}, {"prompt", "{subject}"}};
            params["presets"] = Json::array({wood, sprite});
            Json inputs = Json{{"steps", Json{{"default", 25}}}, {"sampler", Json{{"default", "euler"}, {"choices", Json::array({"euler", "ddim"})}}}};
            params["workflows"] = Json::array({Json{{"name", "sdxl_basic"}, {"source", "engine"}, {"inputs", inputs}}, Json{{"name", "broken"}, {"source", "game"}, {"error", "no inputs"}}});
            return params;
        }

    }  // namespace

    TEST_F(ArtGenSessionTest, ThePollCarriesThePresetsWithTheirContentAndTheWorkflowsWithTheirInputs) {
        Call("artgen_poll", CatalogPoll());
        const auto& status = session.GetServiceStatus();
        ASSERT_EQ(status.presets.size(), 2u);
        const ArtGenPreset* sprite = session.FindPreset("sprite");
        ASSERT_NE(sprite, nullptr);
        EXPECT_EQ(sprite->source, "engine");
        EXPECT_EQ(GetJsonString(sprite->data, "workflow"), Optional<String>("sdxl_cutout"));
        const ArtGenWorkflow* basic = session.FindWorkflow("sdxl_basic");
        ASSERT_NE(basic, nullptr);
        const ArtGenWorkflowInput* sampler = basic->FindInput("sampler");
        ASSERT_NE(sampler, nullptr);
        EXPECT_EQ(sampler->defaultValue, Json("euler"));
        EXPECT_EQ(sampler->choices, Json::array({"euler", "ddim"}));
        EXPECT_EQ(Require(basic->FindInput("steps")).defaultValue, Json(25));
        EXPECT_EQ(Require(session.FindWorkflow("broken")).error, "no inputs");

        // The same lists again change nothing the panel draws
        const uint64_t version = session.Version();
        Call("artgen_poll", CatalogPoll());
        EXPECT_EQ(session.Version(), version);
    }

    TEST_F(ArtGenSessionTest, ADraftGoesInlineWithTheNameItCameFrom) {
        Call("artgen_poll", CatalogPoll());
        Json params = Object();
        params["preset"] = DraftPreset();
        params["presetName"] = "wood";
        params["subject"] = "knight";
        params["count"] = 1;
        ASSERT_TRUE(GetJsonInt(Result(Call("artgen_generate", params)), "id").has_value());
        EXPECT_EQ(session.GetRun().presetData, DraftPreset());

        const Json poll = Call("artgen_poll", CatalogPoll());
        const Json* request = FindJsonMember(Result(poll), "request");
        ASSERT_NE(request, nullptr);
        EXPECT_EQ(Require(FindJsonMember(*request, "preset")), DraftPreset());
        EXPECT_EQ(GetJsonString(*request, "presetName"), Optional<String>("wood"));
        const Json state = Call("artgen_state");
        EXPECT_EQ(GetJsonBool(Require(FindJsonMember(Result(state), "run")), "draft"), Optional<bool>(true));
    }

    TEST_F(ArtGenSessionTest, ADraftThatIsNotAPresetIsRefusedHere) {
        Json noPrompt = DraftPreset();
        noPrompt.erase("prompt");
        auto refused = session.RequestDraft("wood", noPrompt, "knight", 1, Optional<int64_t>());
        ASSERT_FALSE(refused.has_value());
        EXPECT_NE(refused.error().find("\"prompt\" is required"), String::npos) << refused.error().c_str();
        Json params = Object();
        params["preset"] = Json::array();
        params["subject"] = "knight";
        EXPECT_EQ(ErrorCode(Call("artgen_generate", params)), "invalid_params");
    }

    TEST_F(ArtGenSessionTest, SaveWritesTheGamesPresetFileShowsItAndAsksTheServiceToLookAgain) {
        const std::filesystem::path root = TestTempDirectory("artgen_save");
        std::filesystem::remove_all(root);
        const std::string rootText = root.generic_string();  //-NoStd: std::filesystem gives a std::string
        session.SetGameRoot(FromStd(rootText));
        Call("artgen_poll", CatalogPoll());

        Json preset = DraftPreset();
        preset["title"] = "Mine";
        Json params = Object();
        params["name"] = "sprite";  // the engine starter's name: the game's own copy now
        params["preset"] = preset;
        const Json saved = Call("artgen_save_preset", params);
        const auto path = GetJsonString(Result(saved), "path");
        ASSERT_TRUE(path.has_value()) << DumpJson(saved).c_str();
        const std::filesystem::path file = root / "artgen" / "presets" / "sprite.json";
        EXPECT_EQ(*path, String(FromStd(file.generic_string())));
        const auto text = ReadTextFile(file);
        ASSERT_TRUE(text.has_value()) << text.error().c_str();
        EXPECT_EQ(text->find("{\n  \"preset\": 1,\n  \"title\": \"Mine\",\n  \"workflow\": \"sdxl_basic\""), 0u) << text->c_str();
        const auto reread = ParseJson(*text);
        ASSERT_TRUE(reread.has_value());
        EXPECT_EQ(*reread, preset);

        const ArtGenPreset* shown = session.FindPreset("sprite");
        ASSERT_NE(shown, nullptr);
        EXPECT_EQ(shown->source, "game");
        EXPECT_EQ(shown->title, "Mine");
        EXPECT_EQ(GetJsonBool(Result(Call("artgen_poll", CatalogPoll())), "refresh"), Optional<bool>(true));
        EXPECT_EQ(FindJsonMember(Result(Call("artgen_poll", CatalogPoll())), "refresh"), nullptr) << "asked once";
        std::filesystem::remove_all(root);
    }

    TEST_F(ArtGenSessionTest, SaveRefusesABadNameOrAShapeThatIsNotAPreset) {
        EXPECT_FALSE(session.SavePreset("../escape", DraftPreset()).has_value());
        EXPECT_FALSE(session.SavePreset("", DraftPreset()).has_value());
        Json badSize = DraftPreset();
        badSize["size"] = Json::array({0, 10});
        EXPECT_FALSE(session.SavePreset("ok", badSize).has_value());
        Json params = Object();
        params["name"] = "ok";
        EXPECT_EQ(ErrorCode(Call("artgen_save_preset", params)), "invalid_params");
    }

    TEST_F(ArtGenSessionTest, KindsComeWithThePresetsTheWorkflowsAndTheImages) {
        Json params = CatalogPoll();
        params["presets"][0]["kind"] = "audio";
        params["workflows"][0]["kind"] = "audio";
        Call("artgen_poll", params);
        EXPECT_EQ(Require(session.FindPreset("wood")).kind, "audio");
        EXPECT_EQ(Require(session.FindPreset("sprite")).kind, "image") << "image when the service says nothing";
        EXPECT_EQ(Require(session.FindWorkflow("sdxl_basic")).kind, "audio");

        const uint32_t id = Require(session.Request("wood", "knock", 2, Optional<int64_t>(1)));
        Call("artgen_poll", params);
        Json sound = Report(id);
        sound["seed"] = 1;
        sound["path"] = "D:/out/knock_1.flac";
        sound["kind"] = "audio";
        Call("artgen_image", sound);
        Json picture = Report(id);
        picture["seed"] = 2;
        picture["path"] = "D:/out/knock_2.png";
        Call("artgen_image", picture);
        ASSERT_EQ(session.GetRun().variants.size(), 2u);
        EXPECT_TRUE(session.GetRun().variants[0].IsAudio());
        EXPECT_FALSE(session.GetRun().variants[1].IsAudio());
        const Json state = Call("artgen_state");
        EXPECT_EQ(GetJsonString(Require(FindJsonMember(Require(FindJsonMember(Result(state), "run")), "variants"))[0], "kind"), Optional<String>("audio"));

        session.SetAcceptor(&FakeAcceptor);
        auto accepted = session.Accept(1, "knock", false);
        ASSERT_FALSE(accepted.has_value());
        EXPECT_EQ(accepted.error().code, "artgen_accept_failed");
        EXPECT_EQ(gAccepted.calls, 0) << "a sound never reaches the sprite acceptor";
    }

    TEST_F(ArtGenSessionTest, ASoundIsAcceptedAsAClipWithItsSettings) {
        const uint32_t id = Require(session.Request("wood", "knock", 1, Optional<int64_t>(7)));
        Call("artgen_poll", ReadyPoll());
        Json sound = Report(id);
        sound["seed"] = 7;
        sound["path"] = "D:/out/knock_7.flac";
        sound["kind"] = "audio";
        Call("artgen_image", sound);
        session.SetSoundAcceptor(&FakeSoundAcceptor);

        Json params = Object();
        params["seed"] = 7;
        params["clip"] = "assets/sounds/move.wav";
        params["sound"] = Json{{"mono", false}, {"peakDb", nullptr}, {"maxSeconds", 1.0}};
        const Json accepted = Call("artgen_accept", params);
        EXPECT_EQ(GetJsonString(Result(accepted), "clip"), Optional<String>("assets/sounds/move.wav")) << DumpJson(accepted).c_str();
        ASSERT_EQ(gSoundAccepted.calls, 1);
        EXPECT_EQ(gSoundAccepted.seed, 7);
        EXPECT_EQ(gSoundAccepted.target.clip, "assets/sounds/move.wav");
        EXPECT_FALSE(gSoundAccepted.settings.mono);
        EXPECT_FALSE(gSoundAccepted.settings.peakDb.has_value());
        EXPECT_FLOAT_EQ(gSoundAccepted.settings.maxSeconds, 1.0f);
        EXPECT_EQ(gSoundAccepted.settings.trimDb, ArtGenSoundSettings{}.trimDb) << "a missing key keeps the default";

        params["sound"] = Json{{"fadeInMs", -5}};
        EXPECT_EQ(ErrorCode(Call("artgen_accept", params)), "invalid_params");
        EXPECT_EQ(gSoundAccepted.calls, 1);
        params.erase("sound");
        params["useOnSelected"] = true;
        EXPECT_EQ(ErrorCode(Call("artgen_accept", params)), "artgen_accept_failed") << "no node is selected";
        EXPECT_EQ(session.AcceptSound(8, ArtGenSoundTarget{"x", ""}, ArtGenSoundSettings{}, false).error().code, "artgen_no_variant");
    }

    namespace {

        Json ImagePoll(const Json& woodAccept) {
            Json params = CatalogPoll();
            Json wood = params["presets"][0];
            wood["preset"]["accept"] = woodAccept;
            params["presets"][0] = wood;
            return params;
        }

        Json AcceptParams(int64_t seed) {
            return Json{{"seed", seed}, {"sprite", "icon"}};
        }

    }  // namespace

    TEST_F(ArtGenSessionTest, ASingleAcceptUsesTheSettingsOfTheSavedPresetOrTheDefaults) {
        session.SetAcceptor(&FakeAcceptor);
        RunWithImages({1});
        Call("artgen_poll", CatalogPoll());
        ASSERT_TRUE(Call("artgen_accept", AcceptParams(1)).contains("result"));
        EXPECT_EQ(gAccepted.settings, ArtGenImageSettings{}) << "a preset without the key";
        EXPECT_FALSE(gAccepted.hasFrame) << "a single image has no frame of a set";

        Call("artgen_poll", ImagePoll(Json{{"trim", false}, {"maxSize", 64}}));
        EXPECT_EQ(session.ImageSettingsOfRun().maxSize, 64);
        ASSERT_TRUE(Call("artgen_accept", AcceptParams(1)).contains("result"));
        EXPECT_FALSE(gAccepted.settings.trim);
        EXPECT_EQ(gAccepted.settings.maxSize, 64);
        EXPECT_EQ(gAccepted.settings.padding, ArtGenImageSettings::kDefaultPadding) << "a missing key keeps the default";

        Call("artgen_poll", ImagePoll(Json{{"maxSize", 5}}));
        ASSERT_TRUE(Call("artgen_accept", AcceptParams(1)).contains("result"));
        EXPECT_EQ(gAccepted.settings, ArtGenImageSettings{}) << "a malformed key is the defaults";
    }

    TEST_F(ArtGenSessionTest, ADraftAcceptsWithItsOwnKeyAndNeverTheSavedOnes) {
        session.SetAcceptor(&FakeAcceptor);
        Call("artgen_poll", ImagePoll(Json{{"maxSize", 64}}));
        Json draft = DraftPreset();
        draft["accept"] = Json{{"padding", 9}};
        ASSERT_TRUE(session.RequestDraft("wood", draft, "knight", 1, Optional<int64_t>(3)).has_value());
        Call("artgen_poll", ImagePoll(Json{{"maxSize", 64}}));
        Json image = Report(session.GetRun().id);
        image["seed"] = 3;
        image["path"] = "D:/out/knight_3.png";
        Call("artgen_image", image);
        ASSERT_TRUE(Call("artgen_accept", AcceptParams(3)).contains("result"));
        EXPECT_EQ(gAccepted.settings.padding, 9);
        EXPECT_EQ(gAccepted.settings.maxSize, ArtGenImageSettings::kDefaultMaxSize);
    }

    TEST_F(ArtGenSessionTest, TheImageParameterOverridesTheKeysItNames) {
        session.SetAcceptor(&FakeAcceptor);
        RunWithImages({1});
        Call("artgen_poll", ImagePoll(Json{{"padding", 10}, {"maxSize", 64}}));
        Json params = AcceptParams(1);
        params["image"] = Json{{"maxSize", 0}};
        ASSERT_TRUE(Call("artgen_accept", params).contains("result"));
        EXPECT_EQ(gAccepted.settings.maxSize, 0);
        EXPECT_EQ(gAccepted.settings.padding, 10) << "the preset's value of a key the call does not name";
        EXPECT_TRUE(gAccepted.settings.trim);
        EXPECT_EQ(gAccepted.calls, 1);

        params["image"] = Json{{"trim", false}, {"padding", 0}, {"maxSize", 32}};
        ASSERT_TRUE(Call("artgen_accept", params).contains("result"));
        EXPECT_EQ((gAccepted.settings), (ArtGenImageSettings{false, 0, 32}));

        for (const Json& bad : {Json("big"), Json{{"maxSize", 5}}, Json{{"padding", "2"}}, Json{{"trim", 1}}}) {
            params["image"] = bad;
            EXPECT_EQ(ErrorCode(Call("artgen_accept", params)), "invalid_params") << DumpJson(bad).c_str();
        }
        EXPECT_EQ(gAccepted.calls, 2) << "a bad image changes nothing";
    }

    namespace {

        Json AtlasPoll(const Json& woodAtlas) {
            Json params = CatalogPoll();
            Json wood = params["presets"][0];
            wood["preset"]["atlas"] = woodAtlas;
            params["presets"][0] = wood;
            return params;
        }

    }  // namespace

    TEST_F(ArtGenSessionTest, ASingleAcceptCarriesTheAtlasGroupOfTheSavedPreset) {
        session.SetAcceptor(&FakeAcceptor);
        RunWithImages({1});
        Call("artgen_poll", CatalogPoll());
        ASSERT_TRUE(Call("artgen_accept", AcceptParams(1)).contains("result"));
        EXPECT_TRUE(gAccepted.atlas.empty()) << "a preset without the key joins no group";

        Call("artgen_poll", AtlasPoll("pieces"));
        EXPECT_EQ(session.AtlasOfRun(), "pieces");
        ASSERT_TRUE(Call("artgen_accept", AcceptParams(1)).contains("result"));
        EXPECT_EQ(gAccepted.atlas, "pieces");

        Call("artgen_poll", AtlasPoll("not a name"));
        ASSERT_TRUE(Call("artgen_accept", AcceptParams(1)).contains("result"));
        EXPECT_TRUE(gAccepted.atlas.empty()) << "a malformed key is no group";
        Call("artgen_poll", AtlasPoll(5));
        EXPECT_TRUE(session.AtlasOfRun().empty());
    }

    TEST_F(ArtGenSessionTest, ADraftAcceptsIntoItsOwnAtlasGroupAndNeverTheSavedOne) {
        session.SetAcceptor(&FakeAcceptor);
        Call("artgen_poll", AtlasPoll("saved"));
        Json draft = DraftPreset();
        draft["atlas"] = "icons";
        ASSERT_TRUE(session.RequestDraft("wood", draft, "knight", 1, Optional<int64_t>(3)).has_value());
        Call("artgen_poll", AtlasPoll("saved"));
        Json image = Report(session.GetRun().id);
        image["seed"] = 3;
        image["path"] = "D:/out/knight_3.png";
        Call("artgen_image", image);
        ASSERT_TRUE(Call("artgen_accept", AcceptParams(3)).contains("result"));
        EXPECT_EQ(gAccepted.atlas, "icons");

        Json done = Report(session.GetRun().id);
        done["status"] = "ok";
        Call("artgen_done", done);
        draft.erase("atlas");
        ASSERT_TRUE(session.RequestDraft("wood", draft, "knight", 1, Optional<int64_t>(3)).has_value());
        image = Report(session.GetRun().id);
        image["seed"] = 3;
        image["path"] = "D:/out/knight_3.png";
        Call("artgen_poll", AtlasPoll("saved"));
        Call("artgen_image", image);
        ASSERT_TRUE(Call("artgen_accept", AcceptParams(3)).contains("result"));
        EXPECT_TRUE(gAccepted.atlas.empty()) << "a draft without the key has no group, whatever the saved preset says";
    }

    TEST_F(ArtGenSessionTest, TheAtlasParameterOverridesThePresetsGroup) {
        session.SetAcceptor(&FakeAcceptor);
        RunWithImages({1});
        Call("artgen_poll", AtlasPoll("pieces"));
        Json params = AcceptParams(1);
        params["atlas"] = "icons";
        ASSERT_TRUE(Call("artgen_accept", params).contains("result"));
        EXPECT_EQ(gAccepted.atlas, "icons");

        params["atlas"] = "";
        ASSERT_TRUE(Call("artgen_accept", params).contains("result"));
        EXPECT_TRUE(gAccepted.atlas.empty()) << "an empty string clears the preset's group";

        params["atlas"] = nullptr;
        ASSERT_TRUE(Call("artgen_accept", params).contains("result"));
        EXPECT_EQ(gAccepted.atlas, "pieces") << "null is the preset's";
        EXPECT_EQ(gAccepted.calls, 3);

        for (const Json& bad : {Json("a b"), Json(7), Json::array(), Json("../x")}) {
            params["atlas"] = bad;
            EXPECT_EQ(ErrorCode(Call("artgen_accept", params)), "invalid_params") << DumpJson(bad).c_str();
        }
        EXPECT_EQ(gAccepted.calls, 3) << "a bad group changes nothing";
    }

    TEST_F(ArtGenSessionTest, ASoundAcceptIgnoresTheAtlasGroup) {
        const uint32_t id = session.Request("wood", "knock", 1, Optional<int64_t>(7)).value();
        Call("artgen_poll", AtlasPoll("pieces"));
        Json sound = Report(id);
        sound["seed"] = 7;
        sound["path"] = "D:/out/knock_7.flac";
        sound["kind"] = "audio";
        Call("artgen_image", sound);
        session.SetSoundAcceptor(&FakeSoundAcceptor);
        Json params = Json{{"seed", 7}, {"name", "knock"}, {"atlas", "not a name"}};
        EXPECT_TRUE(Call("artgen_accept", params).contains("result"));
        EXPECT_EQ(gSoundAccepted.calls, 1);
    }

    TEST_F(ArtGenSessionTest, ASoundIgnoresTheImageParameter) {
        const uint32_t id = Require(session.Request("wood", "knock", 1, Optional<int64_t>(7)));
        Call("artgen_poll", ReadyPoll());
        Json sound = Report(id);
        sound["seed"] = 7;
        sound["path"] = "D:/out/knock_7.flac";
        sound["kind"] = "audio";
        Call("artgen_image", sound);
        session.SetSoundAcceptor(&FakeSoundAcceptor);
        Json params = Json{{"seed", 7}, {"name", "knock"}, {"image", "not even an object"}};
        EXPECT_TRUE(Call("artgen_accept", params).contains("result"));
        EXPECT_EQ(gSoundAccepted.calls, 1);
    }

    TEST_F(ArtGenSessionTest, ARequestWithAReferenceGoesOutInThePollAndIsInTheState) {
        Call("artgen_poll", ReadyPoll());
        Json params = Json{{"preset", "wood"}, {"subject", "rook"}, {"count", 1}, {"reference", Json{{"mode", "init"}, {"image", "assets/artgen/rook.png"}, {"weight", 0.3}}}};
        ASSERT_TRUE(GetJsonInt(Result(Call("artgen_generate", params)), "id").has_value());
        ASSERT_TRUE(session.GetRun().reference.has_value());
        EXPECT_EQ(session.GetRun().reference->image, "assets/artgen/rook.png");

        const Json state = Call("artgen_state");
        EXPECT_EQ(*FindJsonMember(*FindJsonMember(Result(state), "run"), "reference"), params["reference"]);

        const Json poll = Call("artgen_poll", ReadyPoll());
        const Json* request = FindJsonMember(Result(poll), "request");
        ASSERT_NE(request, nullptr);
        const Json* reference = FindJsonMember(*request, "reference");
        ASSERT_NE(reference, nullptr);
        EXPECT_EQ(*reference, params["reference"]);
    }

    TEST_F(ArtGenSessionTest, AStyleReferenceGoesOutInThePollAndIsInTheState) {
        Call("artgen_poll", ReadyPoll());
        Json params = Json{{"preset", "wood"}, {"subject", "rook"}, {"count", 1}, {"reference", Json{{"mode", "style"}, {"image", "assets/artgen/pawn.png"}, {"weight", 0.8}}}};
        ASSERT_TRUE(GetJsonInt(Result(Call("artgen_generate", params)), "id").has_value());
        EXPECT_EQ(*FindJsonMember(*FindJsonMember(Result(Call("artgen_state")), "run"), "reference"), params["reference"]);
        const Json poll = Call("artgen_poll", ReadyPoll());
        const Json* reference = FindJsonMember(*FindJsonMember(Result(poll), "request"), "reference");
        ASSERT_NE(reference, nullptr);
        EXPECT_EQ(*reference, params["reference"]);
    }

    TEST_F(ArtGenSessionTest, AStructureReferenceGoesOutInThePollAndIsInTheState) {
        Call("artgen_poll", ReadyPoll());
        Json params = Json{{"preset", "wood"}, {"subject", "rook"}, {"count", 1}, {"reference", Json{{"mode", "structure"}, {"image", "sketches/rook.png"}, {"weight", 0.8}}}};
        ASSERT_TRUE(GetJsonInt(Result(Call("artgen_generate", params)), "id").has_value());
        EXPECT_EQ(*FindJsonMember(*FindJsonMember(Result(Call("artgen_state")), "run"), "reference"), params["reference"]);
        const Json poll = Call("artgen_poll", ReadyPoll());
        const Json* reference = FindJsonMember(*FindJsonMember(Result(poll), "request"), "reference");
        ASSERT_NE(reference, nullptr);
        EXPECT_EQ(*reference, params["reference"]);
    }

    TEST_F(ArtGenSessionTest, ARequestWithoutAReferenceLeavesTheKeyOut) {
        Call("artgen_poll", ReadyPoll());
        ASSERT_TRUE(session.Request("wood", "rook", 1, Optional<int64_t>(1)).has_value());
        const Json poll = Call("artgen_poll", ReadyPoll());
        const Json* request = FindJsonMember(Result(poll), "request");
        ASSERT_NE(request, nullptr);
        EXPECT_EQ(FindJsonMember(*request, "reference"), nullptr);
        const Json state = Call("artgen_state");
        EXPECT_TRUE(FindJsonMember(*FindJsonMember(Result(state), "run"), "reference")->is_null());
    }

    TEST_F(ArtGenSessionTest, ADraftTakesAReferenceToo) {
        const auto id = session.RequestDraft("wood", Json{{"preset", 1}, {"workflow", "sdxl_basic"}, {"prompt", "{subject}"}}, "rook", 1, Optional<int64_t>(), false,
                                             ArtGenReference{"init", "artgen/out/x.png", Optional<double>()});
        ASSERT_TRUE(id.has_value()) << id.error().c_str();
        const Json poll = Call("artgen_poll", ReadyPoll());
        const Json* reference = FindJsonMember(*FindJsonMember(Result(poll), "request"), "reference");
        ASSERT_NE(reference, nullptr);
        EXPECT_EQ(GetJsonString(*reference, "image"), Optional<String>("artgen/out/x.png"));
        EXPECT_EQ(FindJsonMember(*reference, "weight"), nullptr);
    }

    TEST_F(ArtGenSessionTest, ANewRunClearsTheReferenceOfTheOldOne) {
        Call("artgen_poll", ReadyPoll());
        const uint32_t id = session.Request("wood", "rook", 1, Optional<int64_t>(1), false, ArtGenReference{"init", "a.png", Optional<double>(0.5)}).value();
        Call("artgen_poll", ReadyPoll());
        Json done = Report(id);
        done["status"] = "ok";
        Call("artgen_done", done);
        EXPECT_TRUE(session.GetRun().reference.has_value());
        ASSERT_TRUE(session.Request("wood", "rook", 1, Optional<int64_t>(2)).has_value());
        EXPECT_FALSE(session.GetRun().reference.has_value());
    }

    TEST_F(ArtGenSessionTest, TheSlotSurvivesARunAndIsClearedOnRequest) {
        const uint64_t before = session.Version();
        EXPECT_FALSE(session.GetReferenceSlot().has_value());
        session.SetReferenceSlot(ArtGenReference{"init", "a.png", Optional<double>(0.5)});
        ASSERT_TRUE(session.GetReferenceSlot().has_value());
        EXPECT_GT(session.Version(), before);

        const uint64_t afterSet = session.Version();
        session.SetReferenceSlot(ArtGenReference{"init", "a.png", Optional<double>(0.5)});
        EXPECT_EQ(session.Version(), afterSet) << "the same picture changes nothing";

        RunWithImages({1});
        ASSERT_TRUE(session.GetReferenceSlot().has_value()) << "a run does not take the slot by itself";
        EXPECT_EQ(session.GetReferenceSlot()->image, "a.png");

        session.ClearReferenceSlot();
        EXPECT_FALSE(session.GetReferenceSlot().has_value());
        const uint64_t afterClear = session.Version();
        session.ClearReferenceSlot();
        EXPECT_EQ(session.Version(), afterClear);
    }

    TEST_F(ArtGenSessionTest, AMalformedReferenceIsInvalidParamsAndChangesNothing) {
        Call("artgen_poll", ReadyPoll());
        Json params = Json{{"preset", "wood"}, {"subject", "rook"}, {"reference", Json{{"mode", "init"}, {"image", "a.png"}, {"weight", "x"}}}};
        const Json response = Call("artgen_generate", params);
        EXPECT_EQ(ErrorCode(response), "invalid_params");
        EXPECT_NE(DumpJson(response).find("reference.weight"), String::npos);
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Idle);

        for (const Json& bad : {Json("a.png"), Json::array(), Json{{"mode", "init"}}, Json{{"image", "a.png"}}, Json{{"mode", "init"}, {"image", ""}}}) {
            params["reference"] = bad;
            EXPECT_EQ(ErrorCode(Call("artgen_generate", params)), "invalid_params") << DumpJson(bad).c_str();
        }
        params["reference"] = Json();
        EXPECT_TRUE(Call("artgen_generate", params).contains("result")) << "null is no reference";
        EXPECT_FALSE(session.GetRun().reference.has_value());
    }

}  // namespace BECore::Tests
