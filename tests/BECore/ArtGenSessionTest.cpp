#include <BECore/ArtGenAnswerText.h>
#include <BECore/Control/ControlActivity.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/ControlTouches.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/File/File.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/JsonDoc.h>
#include <Common/JsonEdit.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenAnswers.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <gtest/gtest.h>
#include <initializer_list>

namespace BECore::Tests {

    namespace {

        constexpr int kFrameGuard = 100;

        constexpr StringView kReadyPoll = R"({"generator":"fake","presets":[{"name":"wood","title":"wood"},{"error":"no workflow 'x'","name":"broken","title":"broken"}],"state":"ready"})";

        constexpr StringView kDraftPreset = R"({"params":{"steps":9},"preset":1,"prompt":"a {subject}","workflow":"sdxl_basic"})";

        /// Two presets (the game's `wood` with its content, the engine's `sprite`) and two workflows (one broken)
        constexpr StringView kCatalogPoll =
            R"({"generator":"fake","presets":[{"name":"wood","preset":{"preset":1,"prompt":"carved {subject}","title":"Wood","workflow":"sdxl_basic"},"source":"game","title":"wood"},)"
            R"({"name":"sprite","preset":{"preset":1,"prompt":"{subject}","workflow":"sdxl_cutout"},"source":"engine","title":"sprite"}],"state":"ready",)"
            R"("workflows":[{"inputs":{"sampler":{"choices":["euler","ddim"],"default":"euler"},"steps":{"default":25}},"name":"sdxl_basic","source":"engine"},)"
            R"({"error":"no inputs","name":"broken","source":"game"}]})";

        /// The catalog poll with the members @p raw (JSON text) of the saved preset `wood` at @p key (`accept`, `atlas`)
        String CatalogWithWood(StringView key, StringView raw) {
            return JsonWith(kCatalogPoll, Format("/presets/0/preset/{}", key), raw);
        }

        bool HasResult(const ControlReply& reply) {
            return reply.View().Find("result").has_value();
        }

        /// The params of `artgen_accept` for the variant @p seed as the sprite `icon`, with the members @p more (`"atlas":"a"`)
        String AcceptParams(int64_t seed, StringView more = {}) {
            return more.empty() ? Format(R"({{"seed":{},"sprite":"icon"}})", seed) : Format(R"({{"seed":{},"sprite":"icon",{}}})", seed, more);
        }

        /// A report of the service about the run @p id with the members @p members (`"seed":1,"path":"x.png"`)
        String Report(uint32_t id, StringView members = {}) {
            return members.empty() ? Format(R"({{"id":{}}})", id) : Format(R"({{"id":{},{}}})", id, members);
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

        std::expected<ArtGenAcceptAnswer, ControlError> FakeSoundAcceptor(const ArtGenVariant& variant, const ArtGenSoundTarget& target, const ArtGenSoundSettings& settings, SceneNode* /*node*/) {
            gSoundAccepted.seed = variant.seed;
            gSoundAccepted.target = target;
            gSoundAccepted.settings = settings;
            ++gSoundAccepted.calls;
            ArtGenAcceptAnswer reply;
            reply.clip = target.clip.empty() ? target.name : target.clip;
            return reply;
        }

        std::expected<ArtGenAcceptAnswer, ControlError> FakeAcceptor(const ArtGenVariant& variant, StringView sprite, const ArtGenImagePlan& plan, SceneNode* node) {
            gAccepted.seed = variant.seed;
            gAccepted.settings = plan.settings;
            gAccepted.hasFrame = plan.frame.has_value();
            gAccepted.atlas = plan.atlas;
            EXPECT_FALSE(plan.deferPack) << "a single Accept packs at once";
            gAccepted.sprite = String(sprite);
            gAccepted.withNode = node != nullptr;
            ++gAccepted.calls;
            ArtGenAcceptAnswer reply;
            reply.sprite = String(sprite);
            return reply;
        }

        std::expected<ArtGenAcceptAnswer, ControlError> FailingAcceptor(const ArtGenVariant& /*variant*/, StringView /*sprite*/, const ArtGenImagePlan& /*plan*/, SceneNode* /*node*/) {
            return std::unexpected(ControlError{String("artgen_accept_failed"), String("cannot write")});
        }

        constexpr StringView kArtNodes = R"(<node name="Hero"><components>
  <component type="SpriteRendererComponent"/>
  <component type="AudioSourceComponent"/>
</components></node>)";

    }  // namespace

    class ArtGenSessionTest : public ::testing::Test {
    protected:
        ArtGenSessionTest() : session([this] { return now; }) {}

        /// One control call through the service, as the bridge makes it; the answer, empty when none came
        ControlReply Call(StringView method, StringView params = "{}") {
            ControlReply response;
            bool answered = false;
            service.Post(
                MakeRequestText(++_nextId, method, params),
                [&](const String& reply) {
                    response = ControlReply(StringView(reply.c_str(), reply.size()));
                    answered = true;
                },
                caller);
            for (int frame = 0; !answered && frame < kFrameGuard; ++frame) {
                service.Drain();
                service.EndFrame();
            }
            return response;
        }

        ControlReply Call(StringView method, const char* params) {
            return Call(method, StringView(params));
        }

        ControlReply Call(StringView method, const String& params) {
            return Call(method, StringView(params.c_str(), params.size()));
        }

        /// Runs a request to its end with images of @p seeds, as the service would
        uint32_t RunWithImages(std::initializer_list<int64_t> seeds, bool keep = false) {
            const uint32_t id = Require(session.Request("wood", "queen", static_cast<int32_t>(seeds.size()), *seeds.begin(), keep));
            Call("artgen_poll", kReadyPoll);
            for (int64_t seed : seeds) {
                Call("artgen_image", Report(id, Format(R"("path":"D:/out/queen_{}.png","seed":{})", seed, seed)));
            }
            Call("artgen_done", Report(id, R"("status":"ok")"));
            return id;
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
        IntrusivePtr<ControlClient> caller;  // who Call() asks as; none by default

    private:
        int64_t _nextId = 0;
    };

    TEST_F(ArtGenSessionTest, APollReportsTheServiceAndHandsTheRequestOverOnce) {
        EXPECT_FALSE(session.IsServiceConnected());
        ControlReply poll = Call("artgen_poll", kReadyPoll);
        EXPECT_EQ(StringOf(poll.Result(), "gameRoot"), "D:/Games/Chess");
        EXPECT_FALSE(poll.Result().Find("request").has_value());
        EXPECT_TRUE(session.IsServiceConnected());
        ASSERT_EQ(session.GetServiceStatus().presets.size(), 2u);

        const ControlReply generated = Call("artgen_generate", R"({"count":2,"preset":"wood","seed":7,"subject":"white knight"})");
        const auto id = IntOf(generated.Result(), "id");
        ASSERT_TRUE(id.has_value());
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Queued);

        poll = Call("artgen_poll", kReadyPoll);
        const JsonView request = MemberOf(poll.Result(), "request");
        ASSERT_TRUE(request.IsObject());
        EXPECT_EQ(IntOf(request, "id"), id);
        EXPECT_EQ(StringOf(request, "preset"), "wood");
        EXPECT_EQ(StringOf(request, "subject"), "white knight");
        EXPECT_EQ(IntOf(request, "count"), Optional<int64_t>(2));
        EXPECT_EQ(IntOf(request, "seed"), Optional<int64_t>(7));
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Running);

        poll = Call("artgen_poll", kReadyPoll);
        EXPECT_FALSE(poll.Result().Find("request").has_value());
    }

    TEST_F(ArtGenSessionTest, ProgressImagesAndDoneFillTheRun) {
        const uint32_t id = Require(session.Request("wood", "rook", 2, Optional<int64_t>()));
        Call("artgen_poll", kReadyPoll);
        EXPECT_FALSE(Call("artgen_poll", kReadyPoll).Result().Find("request").has_value());

        EXPECT_EQ(BoolOf(Call("artgen_progress", Report(id, R"("image":0,"step":5,"steps":25)")).Result(), "stale"), Optional<bool>(false));
        EXPECT_EQ(session.GetRun().step, 5);
        EXPECT_EQ(session.GetRun().steps, 25);

        for (int64_t seed : {11, 12}) {
            Call("artgen_image", Report(id, Format(R"("path":"D:/Games/Chess/artgen/out/rook_{}.png","provenance":"D:/Games/Chess/artgen/out/rook_{}.artgen.json","seed":{})", seed, seed, seed)));
        }
        ASSERT_EQ(session.GetRun().variants.size(), 2u);
        EXPECT_EQ(session.GetRun().variants[1].seed, 12);
        EXPECT_EQ(session.GetRun().variants[1].path, "D:/Games/Chess/artgen/out/rook_12.png");

        Call("artgen_done", Report(id, R"("status":"ok")"));
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Done);
        EXPECT_FALSE(session.IsBusy());

        const ControlReply state = Call("artgen_state");
        const JsonView run = MemberOf(state.Result(), "run");
        ASSERT_TRUE(run.IsObject());
        EXPECT_EQ(StringOf(run, "state"), "Done");
        EXPECT_EQ(MemberOf(run, "variants").Size(), 2u);
    }

    TEST_F(ArtGenSessionTest, AFailureKeepsItsReason) {
        const uint32_t id = Require(session.Request("wood", "rook", 1, 3));
        Call("artgen_poll", kReadyPoll);
        Call("artgen_done", Report(id, R"("error":"ComfyUI failed: out of memory","status":"failed")"));
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Failed);
        EXPECT_EQ(session.GetRun().error, "ComfyUI failed: out of memory");
    }

    TEST_F(ArtGenSessionTest, ReportsAboutAnOlderRunAreStale) {
        const uint32_t first = Require(session.Request("wood", "a", 1, 1));
        Call("artgen_poll", kReadyPoll);
        Call("artgen_done", Report(first, R"("status":"cancelled")"));
        const uint32_t second = Require(session.Request("wood", "b", 1, 1));
        EXPECT_NE(first, second);
        Call("artgen_poll", kReadyPoll);

        EXPECT_EQ(BoolOf(Call("artgen_image", Report(first, R"("path":"x.png","seed":1)")).Result(), "stale"), Optional<bool>(true));
        EXPECT_TRUE(session.GetRun().variants.empty());
        EXPECT_EQ(session.GetRun().subject, "b");
    }

    TEST_F(ArtGenSessionTest, CancelBeforeTheServiceTookItNeedsNoService) {
        Require(session.Request("wood", "a", 1, 1));
        Call("artgen_cancel");
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Cancelled);
        EXPECT_FALSE(Call("artgen_poll", kReadyPoll).Result().Find("request").has_value());
    }

    TEST_F(ArtGenSessionTest, CancelOfTheRunAnswersAnEmptyObject) {
        Require(session.Request("wood", "a", 1, 1));
        const ControlReply response = Call("artgen_cancel");
        ASSERT_TRUE(response.Result().IsObject());
        EXPECT_EQ(response.Result().Size(), 0u);
        EXPECT_EQ(AnswerString(ArtGenCancelAnswer{}), "{}") << "an answer of Optionals only writes {}";
    }

    TEST_F(ArtGenSessionTest, CancelOfARunningRequestGoesToTheServiceOnTheNextPoll) {
        const uint32_t id = Require(session.Request("wood", "a", 3, 1));
        Call("artgen_poll", kReadyPoll);
        session.Cancel();
        EXPECT_TRUE(session.GetRun().cancelRequested);
        EXPECT_EQ(IntOf(Call("artgen_poll", kReadyPoll).Result(), "cancel"), Optional<int64_t>(id));
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Running);  // until the service says it stopped
        Call("artgen_done", Report(id, R"("status":"cancelled")"));
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Cancelled);
        EXPECT_EQ(Call("artgen_cancel").ErrorCode(), "artgen_no_run");
    }

    TEST_F(ArtGenSessionTest, RequestsThatCannotRunAreRefused) {
        Call("artgen_poll", kReadyPoll);
        EXPECT_FALSE(session.Request("wood", "a", 0, 1).has_value());
        EXPECT_FALSE(session.Request("wood", "a", ArtGenSession::kMaxCount + 1, 1).has_value());
        EXPECT_FALSE(session.Request("wood", "a", 1, -1).has_value());
        EXPECT_FALSE(session.Request("", "a", 1, 1).has_value());
        EXPECT_FALSE(session.Request("broken", "a", 1, 1).has_value());

        EXPECT_EQ(Call("artgen_generate", R"({"preset":"wood"})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("artgen_generate", R"({"count":"four","preset":"wood","subject":"a"})").ErrorCode(), "invalid_params");
        constexpr StringView kWithoutCount = R"({"preset":"wood","subject":"a"})";
        EXPECT_TRUE(IntOf(Call("artgen_generate", kWithoutCount).Result(), "id").has_value());
        EXPECT_EQ(session.GetRun().count, 4);
        EXPECT_EQ(Call("artgen_generate", kWithoutCount).ErrorCode(), "artgen_busy");
    }

    TEST_F(ArtGenSessionTest, TheServiceIsGoneWhenItStopsPolling) {
        Call("artgen_poll", kReadyPoll);
        now += ArtGenSession::kServiceTimeoutSeconds - 0.5;
        EXPECT_TRUE(session.IsServiceConnected());
        now += 1.0;
        EXPECT_FALSE(session.IsServiceConnected());
    }

    TEST_F(ArtGenSessionTest, ANotReadyGeneratorIsShownWithItsReason) {
        const uint64_t before = session.Version();
        Call("artgen_poll", R"({"reason":"ComfyUI is installed in D:/ComfyUI but not started","state":"not_running"})");
        EXPECT_GT(session.Version(), before);
        EXPECT_EQ(session.GetServiceStatus().state, "not_running");
        EXPECT_EQ(session.GetServiceStatus().reason, "ComfyUI is installed in D:/ComfyUI but not started");
        EXPECT_TRUE(session.GetServiceStatus().presets.empty());
    }

    TEST_F(ArtGenSessionTest, ASecondRegistrationOnTheSameServiceChangesNothing) {
        session.RegisterControl(service);
        Call("artgen_poll", kReadyPoll);
        EXPECT_EQ(session.PollCount(), 1u);
        EXPECT_TRUE(Call("artgen_state").Result().Find("service").has_value());
    }

    TEST_F(ArtGenSessionTest, AcceptGoesToTheAcceptorWithTheVariantOfThatSeed) {
        RunWithImages({21, 22});
        constexpr StringView kParams = R"({"seed":22,"sprite":"white_queen"})";
        EXPECT_EQ(Call("artgen_accept", kParams).ErrorCode(), "artgen_accept_failed");  // no acceptor yet
        session.SetAcceptor(&FakeAcceptor);
        const ControlReply reply = Call("artgen_accept", kParams);
        EXPECT_EQ(StringOf(reply.Result(), "sprite"), "white_queen");
        EXPECT_EQ(gAccepted.seed, 22);
        EXPECT_EQ(gAccepted.sprite, "white_queen");
        EXPECT_FALSE(gAccepted.withNode);
    }

    TEST_F(ArtGenSessionTest, AcceptRefusesAnUnknownSeedOrAMissingSelection) {
        session.SetAcceptor(&FakeAcceptor);
        RunWithImages({5});
        EXPECT_EQ(Call("artgen_accept", R"({"seed":6,"sprite":"q"})").ErrorCode(), "artgen_no_variant");
        EXPECT_EQ(Call("artgen_accept", R"({"seed":5,"sprite":"q","useOnSelected":true})").ErrorCode(), "artgen_accept_failed");
        EXPECT_EQ(Call("artgen_accept", R"({"seed":5,"sprite":"q","useOnSelected":"yes"})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("artgen_accept", R"({"seed":5,"useOnSelected":"yes"})").ErrorCode(), "invalid_params");
        EXPECT_EQ(gAccepted.calls, 0);
    }

    /// A session whose selected node is in the active scene and has what an accept puts art on
    class ArtGenSessionTouchTest : public ArtGenSessionTest {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            ArtGenSessionTest::SetUp();
            XmlDeserializer deserializer;
            const String xml = SceneConfigText(kArtNodes);
            ASSERT_TRUE(deserializer.LoadFromString(xml.c_str()));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            EngineEnvironment::UseSceneManager(_manager);
            Scene* scene = _manager->GetActiveScene();
            ASSERT_NE(scene, nullptr);
            ASSERT_FALSE(scene->GetRootNode().GetChildren().empty());
            hero = scene->GetRootNode().GetChildren().front();
            session.SetSelectedNode(hero);
            session.SetAcceptor(&FakeAcceptor);
            session.SetSoundAcceptor(&FakeSoundAcceptor);
            caller = service.OpenLocalClient("tool");
        }

        void TearDown() override {
            session.SetSelectedNode({});
            hero.Reset();
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
        }

        /// The last report is of the selected node, with @p component and @p field, by the caller, and its call is listed with the node
        void ExpectTouched(StringView component, StringView field) {
            ASSERT_GT(service.Touches().Size(), 0u);
            const ControlTouch& touch = service.Touches().At(service.Touches().Size() - 1);
            EXPECT_EQ(touch.nodeId, hero->GetRuntimeId());
            EXPECT_EQ(StringView(touch.component), component);
            EXPECT_EQ(StringView(touch.field), field);
            EXPECT_EQ(touch.client.Get(), caller.Get());
            EXPECT_EQ(service.Activity().At(service.Activity().Size() - 1).node, hero->GetRuntimeId());
        }

        IntrusivePtr<SceneNode> hero;

    private:
        IntrusivePtr<SceneManager> _manager;
    };

    TEST_F(ArtGenSessionTouchTest, AnImageAcceptedOntoTheSelectedNodeTouchesTheSpriteItWrites) {
        RunWithImages({5});

        ASSERT_TRUE(Call("artgen_accept", R"({"seed":5,"sprite":"q"})").IsOk());
        EXPECT_EQ(service.Touches().Size(), 0u) << "no node was asked for";

        ASSERT_TRUE(Call("artgen_accept", R"({"seed":5,"sprite":"q","useOnSelected":true})").IsOk());
        EXPECT_EQ(service.Touches().Size(), 1u);
        ExpectTouched("SpriteRendererComponent", "spriteId");
    }

    TEST_F(ArtGenSessionTouchTest, ASoundAcceptedOntoTheSelectedNodeTouchesTheClip) {
        const uint32_t id = Require(session.Request("wood", "knock", 1, Optional<int64_t>(7)));
        Call("artgen_poll", kReadyPoll);
        Call("artgen_image", Report(id, R"("kind":"audio","path":"D:/out/knock_7.flac","seed":7)"));

        ASSERT_TRUE(Call("artgen_accept", R"({"name":"knock","seed":7,"useOnSelected":true})").IsOk());
        EXPECT_EQ(service.Touches().Size(), 1u);
        ExpectTouched("AudioSourceComponent", "clip");
    }

    TEST_F(ArtGenSessionTouchTest, AFailedAcceptAndThePanelsOwnAcceptTouchNothing) {
        RunWithImages({5});
        session.SetAcceptor(&FailingAcceptor);
        EXPECT_EQ(Call("artgen_accept", R"({"seed":5,"sprite":"q","useOnSelected":true})").ErrorCode(), "artgen_accept_failed");

        session.SetAcceptor(&FakeAcceptor);
        ASSERT_TRUE(session.Accept(5, "q", true).has_value());  // outside any control call, as the panel's button
        EXPECT_TRUE(gAccepted.withNode);

        EXPECT_EQ(service.Touches().Size(), 0u);
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
        const auto id = IntOf(Call("artgen_setup", R"({"action":"Plan"})").Result(), "id");
        ASSERT_TRUE(id.has_value());
        EXPECT_EQ(session.GetSetup().state, ArtGenSetupState::Queued);

        const ControlReply poll = Call("artgen_poll", kReadyPoll);
        const JsonView setup = MemberOf(poll.Result(), "setup");
        ASSERT_TRUE(setup.IsObject());
        EXPECT_EQ(IntOf(setup, "id"), id);
        EXPECT_EQ(StringOf(setup, "action"), "Plan");
        EXPECT_EQ(session.GetSetup().state, ArtGenSetupState::Running);
        EXPECT_FALSE(Call("artgen_poll", kReadyPoll).Result().Find("setup").has_value());

        const auto plan = Report(static_cast<uint32_t>(*id), R"("downloadBytes":9000,"event":"plan","platform":"windows-nvidia","root":"D:/ComfyUI",)"
                                                             R"("steps":[{"licence":"OpenRAIL++-M","size":9000,"state":"download","what":"checkpoints/sd_xl_base_1.0.safetensors"}])");
        EXPECT_EQ(BoolOf(Call("artgen_setup_report", plan).Result(), "stale"), Optional<bool>(false));
        Call("artgen_setup_report", Report(static_cast<uint32_t>(*id), R"("event":"done","ok":true)"));
        EXPECT_EQ(session.GetSetup().state, ArtGenSetupState::Done);
        ASSERT_EQ(session.GetSetup().plan.size(), 1u);
        EXPECT_EQ(session.GetSetup().plan[0].size, 9000);
        EXPECT_EQ(session.GetSetup().platform, "windows-nvidia");

        // The install keeps the plan the owner saw on screen, and reports its progress
        const uint32_t install = Require(session.RequestSetup(ArtGenSetupAction::Install));
        EXPECT_EQ(session.GetSetup().plan.size(), 1u);
        Call("artgen_poll", kReadyPoll);
        Call("artgen_setup_report", Report(install, R"("done":4500,"event":"progress","total":9000)"));
        EXPECT_EQ(session.GetSetup().done, 4500);
        Call("artgen_setup_report", Report(install, R"("error":"the download stopped; run again to resume","event":"done","ok":false)"));
        EXPECT_EQ(session.GetSetup().state, ArtGenSetupState::Failed);
        EXPECT_EQ(session.GetSetup().error, "the download stopped; run again to resume");
    }

    TEST_F(ArtGenSessionTest, ASetupAndARunNeverRunTogether) {
        const uint32_t setup = Require(session.RequestSetup(ArtGenSetupAction::Start));
        Call("artgen_poll", kReadyPoll);
        EXPECT_FALSE(session.Request("wood", "a", 1, 1).has_value());
        EXPECT_FALSE(session.RequestSetup(ArtGenSetupAction::Stop).has_value());
        EXPECT_EQ(Call("artgen_setup", R"({"action":"Stop"})").ErrorCode(), "artgen_busy");
        Call("artgen_setup_report", Report(setup, R"("event":"done","ok":true)"));

        ASSERT_TRUE(session.Request("wood", "a", 1, 1).has_value());
        EXPECT_FALSE(session.RequestSetup(ArtGenSetupAction::Plan).has_value());
    }

    TEST_F(ArtGenSessionTest, ACancelOfASetupGoesToTheServiceAndOldReportsAreStale) {
        const uint32_t first = Require(session.RequestSetup(ArtGenSetupAction::Install));
        EXPECT_FALSE(Call("artgen_setup", R"({"action":"cancel"})").View().Find("error").has_value());
        EXPECT_EQ(session.GetSetup().state, ArtGenSetupState::Failed);  // queued: the service never saw it

        const uint32_t second = Require(session.RequestSetup(ArtGenSetupAction::Install));
        Call("artgen_poll", kReadyPoll);
        session.CancelSetup();
        EXPECT_EQ(IntOf(Call("artgen_poll", kReadyPoll).Result(), "setupCancel"), Optional<int64_t>(second));

        EXPECT_EQ(BoolOf(Call("artgen_setup_report", Report(first, R"("event":"log","text":"late")")).Result(), "stale"), Optional<bool>(true));
        EXPECT_TRUE(session.GetSetup().log.empty());

        EXPECT_EQ(Call("artgen_setup", R"({"action":"Dance"})").ErrorCode(), "invalid_params");
        EXPECT_EQ(StringOf(MemberOf(Call("artgen_state").Result(), "setup"), "state"), "Running");
    }

    TEST_F(ArtGenSessionTest, ThePollSaysWhetherTheGeneratorWasStartedHere) {
        Call("artgen_poll", kReadyPoll);
        EXPECT_FALSE(session.GetServiceStatus().startedHere);
        const uint64_t before = session.Version();
        Call("artgen_poll", JsonWith(kReadyPoll, "/startedHere", "true"));
        EXPECT_TRUE(session.GetServiceStatus().startedHere);
        EXPECT_GT(session.Version(), before);
    }

    TEST_F(ArtGenSessionTest, DisconnectTellsTheServiceToExitAndEndsWhatItWasDoing) {
        session.RequestServiceStop();  // not connected: nothing to tell
        EXPECT_FALSE(Call("artgen_poll", kReadyPoll).Result().Find("shutdown").has_value());

        ASSERT_TRUE(session.Request("wood", "queen", 1, 1).has_value());
        Call("artgen_poll", kReadyPoll);
        ASSERT_EQ(session.GetRun().state, ArtGenRunState::Running);
        session.RequestServiceStop();
        const ControlReply answer = Call("artgen_poll", kReadyPoll);
        EXPECT_EQ(BoolOf(answer.Result(), "shutdown"), Optional<bool>(true));
        EXPECT_FALSE(session.IsServiceConnected());
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Cancelled);

        // Started again, the service is told nothing more
        EXPECT_FALSE(Call("artgen_poll", kReadyPoll).Result().Find("shutdown").has_value());
        EXPECT_TRUE(session.IsServiceConnected());
    }

    TEST_F(ArtGenSessionTest, ThePollCarriesThePresetsWithTheirContentAndTheWorkflowsWithTheirInputs) {
        Call("artgen_poll", kCatalogPoll);
        const auto& status = session.GetServiceStatus();
        ASSERT_EQ(status.presets.size(), 2u);
        const ArtGenPreset* sprite = session.FindPreset("sprite");
        ASSERT_NE(sprite, nullptr);
        EXPECT_EQ(sprite->source, "engine");
        EXPECT_EQ(sprite->data, R"({"preset":1,"prompt":"{subject}","workflow":"sdxl_cutout"})");
        const ArtGenWorkflow* basic = session.FindWorkflow("sdxl_basic");
        ASSERT_NE(basic, nullptr);
        const ArtGenWorkflowInput* sampler = basic->FindInput("sampler");
        ASSERT_NE(sampler, nullptr);
        EXPECT_EQ(sampler->defaultValue, ArtGenInputValue::FromText("euler"));
        ASSERT_EQ(sampler->choices.size(), 2u);
        EXPECT_EQ(sampler->choices[0], ArtGenInputValue::FromText("euler"));
        EXPECT_EQ(sampler->choices[1], ArtGenInputValue::FromText("ddim"));
        EXPECT_EQ(Require(basic->FindInput("steps")).defaultValue, ArtGenInputValue::FromInteger(25));
        EXPECT_EQ(Require(session.FindWorkflow("broken")).error, "no inputs");

        // The same lists again change nothing the panel draws
        const uint64_t version = session.Version();
        Call("artgen_poll", kCatalogPoll);
        EXPECT_EQ(session.Version(), version);
    }

    TEST_F(ArtGenSessionTest, TheInputsOfAWorkflowAreListedInTheOrderOfTheirNamesWhateverTheDocumentSays) {
        const String poll = R"({"workflows":[{"name":"w","inputs":{"steps":{"default":1},"cfg":{"default":2},"sampler":{"default":"x"}}}]})";
        ASSERT_TRUE(session.HandlePoll(JsonDoc(poll)).has_value());
        const ArtGenWorkflow& workflow = Require(session.FindWorkflow("w"));
        ASSERT_EQ(workflow.inputs.size(), 3u);
        EXPECT_EQ(workflow.inputs[0].name, "cfg");
        EXPECT_EQ(workflow.inputs[1].name, "sampler");
        EXPECT_EQ(workflow.inputs[2].name, "steps");
    }

    TEST_F(ArtGenSessionTest, AListOfThePollThatIsTheSameBytesIsNotReadAgainAndOneFormattedDifferentlyIs) {
        const String compact = R"({"presets":[{"name":"wood"}]})";
        ASSERT_TRUE(session.HandlePoll(JsonDoc(compact)).has_value());
        ASSERT_EQ(session.GetServiceStatus().presets.size(), 1u);
        const uint64_t version = session.Version();
        ASSERT_TRUE(session.HandlePoll(JsonDoc(compact)).has_value());
        EXPECT_EQ(session.Version(), version);
        ASSERT_TRUE(session.HandlePoll(JsonDoc(R"({"presets": [ {"name": "wood"} ]})")).has_value());
        EXPECT_GT(session.Version(), version);
        EXPECT_EQ(session.GetServiceStatus().presets.size(), 1u);
    }

    TEST_F(ArtGenSessionTest, ACountThatIsNotAFiniteNumberIsInvalidParams) {
        Call("artgen_poll", kReadyPoll);
        const auto refused = session.HandleGenerate(JsonDoc(R"({"preset":"wood","subject":"knight","count":1e999})"));
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().code, "invalid_params");
    }

    TEST_F(ArtGenSessionTest, ADraftGoesInlineWithTheNameItCameFrom) {
        Call("artgen_poll", kCatalogPoll);
        const String params = Format(R"({{"count":1,"preset":{},"presetName":"wood","subject":"knight"}})", kDraftPreset);
        ASSERT_TRUE(IntOf(Call("artgen_generate", params).Result(), "id").has_value());
        EXPECT_EQ(session.GetRun().presetData, kDraftPreset);

        const ControlReply poll = Call("artgen_poll", kCatalogPoll);
        const JsonView request = MemberOf(poll.Result(), "request");
        ASSERT_TRUE(request.IsObject());
        EXPECT_EQ(RawOf(request, "preset"), kDraftPreset);
        EXPECT_EQ(StringOf(request, "presetName"), "wood");
        const ControlReply state = Call("artgen_state");
        EXPECT_EQ(BoolOf(MemberOf(state.Result(), "run"), "draft"), Optional<bool>(true));
    }

    TEST_F(ArtGenSessionTest, TheRequestOfAPollIsCompactSortedWithTheDraftsTextInIt) {
        const String draft = R"({"preset":1,"prompt":"a {subject}","workflow":"sdxl_basic"})";
        ASSERT_TRUE(session.RequestDraft("wood", draft, "knight", 2, Optional<int64_t>(3), false, ArtGenReference{"init", "x.png", Optional<double>(0.25)}).has_value());
        const auto reply = session.HandlePoll(JsonDoc(kReadyPoll));
        ASSERT_TRUE(reply.has_value());
        EXPECT_EQ(*reply, Format(R"({{"gameRoot":"D:/Games/Chess","request":{{"count":2,"id":{},"preset":{},"presetName":"wood","reference":{{"image":"x.png","mode":"init","weight":0.25}},)"
                                 R"("seed":3,"subject":"knight"}}}})",
                                 session.GetRun().id, draft));

        Call("artgen_done", Report(session.GetRun().id, R"("status":"ok")"));
        ASSERT_TRUE(session.Request("wood", "rook", 1, Optional<int64_t>()).has_value());
        const auto byName = session.HandlePoll(JsonDoc(kReadyPoll));
        ASSERT_TRUE(byName.has_value());
        EXPECT_EQ(*byName, Format(R"({{"gameRoot":"D:/Games/Chess","request":{{"count":1,"id":{},"preset":"wood","seed":null,"subject":"rook"}}}})", session.GetRun().id));
    }

    TEST_F(ArtGenSessionTest, ADraftTextThatIsNotAPresetObjectIsRefused) {
        for (const StringView text : {StringView(""), StringView("not json"), StringView("[1]"), StringView("3")}) {
            const auto refused = session.RequestDraft("wood", text, "knight", 1, Optional<int64_t>());
            ASSERT_FALSE(refused.has_value()) << String(text).c_str();
            EXPECT_NE(refused.error().find("a preset is a JSON object"), String::npos) << refused.error().c_str();
            EXPECT_FALSE(session.SavePreset("ok", text).has_value()) << String(text).c_str();
        }
    }

    TEST_F(ArtGenSessionTest, ADraftThatIsNotAPresetIsRefusedHere) {
        auto refused = session.RequestDraft("wood", JsonWithout(kDraftPreset, "/prompt"), "knight", 1, Optional<int64_t>());
        ASSERT_FALSE(refused.has_value());
        EXPECT_NE(refused.error().find("\"prompt\" is required"), String::npos) << refused.error().c_str();
        EXPECT_EQ(Call("artgen_generate", R"({"preset":[],"subject":"knight"})").ErrorCode(), "invalid_params");
    }

    TEST_F(ArtGenSessionTest, SaveWritesTheGamesPresetFileShowsItAndAsksTheServiceToLookAgain) {
        const Path root(TestTempDirectory("artgen_save"));
        RemoveDirectory(root);
        session.SetGameRoot(root.View());
        Call("artgen_poll", kCatalogPoll);

        const String preset = JsonWith(kDraftPreset, "/title", R"("Mine")");
        // "sprite" is the engine starter's name: the game's own copy now
        const ControlReply saved = Call("artgen_save_preset", Format(R"({{"name":"sprite","preset":{}}})", preset));
        const String path = StringOf(saved.Result(), "path");
        ASSERT_FALSE(path.empty()) << String(saved.Text()).c_str();
        const Path file = root / "artgen" / "presets" / "sprite.json";
        EXPECT_EQ(path, String(file.View()));
        const auto text = ReadTextFile(file);
        ASSERT_TRUE(text.has_value()) << text.error().c_str();
        EXPECT_EQ(text->find("{\n  \"preset\": 1,\n  \"title\": \"Mine\",\n  \"workflow\": \"sdxl_basic\""), 0u) << text->c_str();
        const JsonDoc reread{StringView(text->c_str(), text->size())};
        ASSERT_TRUE(reread.IsParsed());
        EXPECT_TRUE(JsonEquals(reread.Root(), JsonDoc(StringView(preset.c_str(), preset.size())).Root()));

        const ArtGenPreset* shown = session.FindPreset("sprite");
        ASSERT_NE(shown, nullptr);
        EXPECT_EQ(shown->source, "game");
        EXPECT_EQ(shown->title, "Mine");
        EXPECT_EQ(BoolOf(Call("artgen_poll", kCatalogPoll).Result(), "refresh"), Optional<bool>(true));
        EXPECT_FALSE(Call("artgen_poll", kCatalogPoll).Result().Find("refresh").has_value()) << "asked once";
        RemoveDirectory(root);
    }

    TEST_F(ArtGenSessionTest, SaveIntoAGameWhoseRootHasANonAsciiNameWritesTheFile) {
        const Path parent(TestTempDirectory("artgen_save_utf8"));
        RemoveDirectory(parent);
        const Path root = parent / "caf\xC3\xA9 game";
        session.SetGameRoot(root.View());
        Call("artgen_poll", kCatalogPoll);

        const ControlReply saved = Call("artgen_save_preset", Format(R"({{"name":"mine","preset":{}}})", kDraftPreset));
        const String path = StringOf(saved.Result(), "path");
        ASSERT_FALSE(path.empty()) << String(saved.Text()).c_str();
        EXPECT_EQ(path, Format("{}/artgen/presets/mine.json", root.View()));
        EXPECT_TRUE(ReadTextFile(path).has_value());
        RemoveDirectory(parent);
    }

    TEST_F(ArtGenSessionTest, SaveRefusesABadNameOrAShapeThatIsNotAPreset) {
        EXPECT_FALSE(session.SavePreset("../escape", kDraftPreset).has_value());
        EXPECT_FALSE(session.SavePreset("", kDraftPreset).has_value());
        EXPECT_FALSE(session.SavePreset("ok", JsonWith(kDraftPreset, "/size", "[0,10]")).has_value());
        EXPECT_EQ(Call("artgen_save_preset", R"({"name":"ok"})").ErrorCode(), "invalid_params");
    }

    TEST_F(ArtGenSessionTest, KindsComeWithThePresetsTheWorkflowsAndTheImages) {
        const String params = JsonWith(JsonWith(kCatalogPoll, "/presets/0/kind", R"("audio")"), "/workflows/0/kind", R"("audio")");
        Call("artgen_poll", params);
        EXPECT_EQ(Require(session.FindPreset("wood")).kind, "audio");
        EXPECT_EQ(Require(session.FindPreset("sprite")).kind, "image") << "image when the service says nothing";
        EXPECT_EQ(Require(session.FindWorkflow("sdxl_basic")).kind, "audio");

        const uint32_t id = Require(session.Request("wood", "knock", 2, Optional<int64_t>(1)));
        Call("artgen_poll", params);
        Call("artgen_image", Report(id, R"("kind":"audio","path":"D:/out/knock_1.flac","seed":1)"));
        Call("artgen_image", Report(id, R"("path":"D:/out/knock_2.png","seed":2)"));
        ASSERT_EQ(session.GetRun().variants.size(), 2u);
        EXPECT_TRUE(session.GetRun().variants[0].IsAudio());
        EXPECT_FALSE(session.GetRun().variants[1].IsAudio());
        const ControlReply state = Call("artgen_state");
        EXPECT_EQ(StringOf(ItemAt(MemberOf(MemberOf(state.Result(), "run"), "variants"), 0), "kind"), "audio");

        session.SetAcceptor(&FakeAcceptor);
        auto accepted = session.Accept(1, "knock", false);
        ASSERT_FALSE(accepted.has_value());
        EXPECT_EQ(accepted.error().code, "artgen_accept_failed");
        EXPECT_EQ(gAccepted.calls, 0) << "a sound never reaches the sprite acceptor";
    }

    TEST_F(ArtGenSessionTest, ASoundIsAcceptedAsAClipWithItsSettings) {
        const uint32_t id = Require(session.Request("wood", "knock", 1, Optional<int64_t>(7)));
        Call("artgen_poll", kReadyPoll);
        Call("artgen_image", Report(id, R"("kind":"audio","path":"D:/out/knock_7.flac","seed":7)"));
        session.SetSoundAcceptor(&FakeSoundAcceptor);

        const ControlReply accepted = Call("artgen_accept", R"({"clip":"assets/sounds/move.wav","seed":7,"sound":{"maxSeconds":1.0,"mono":false,"peakDb":null}})");
        EXPECT_EQ(StringOf(accepted.Result(), "clip"), "assets/sounds/move.wav") << String(accepted.Text()).c_str();
        ASSERT_EQ(gSoundAccepted.calls, 1);
        EXPECT_EQ(gSoundAccepted.seed, 7);
        EXPECT_EQ(gSoundAccepted.target.clip, "assets/sounds/move.wav");
        EXPECT_FALSE(gSoundAccepted.settings.mono);
        EXPECT_FALSE(gSoundAccepted.settings.peakDb.has_value());
        EXPECT_FLOAT_EQ(gSoundAccepted.settings.maxSeconds, 1.0f);
        EXPECT_EQ(gSoundAccepted.settings.trimDb, ArtGenSoundSettings{}.trimDb) << "a missing key keeps the default";

        EXPECT_EQ(Call("artgen_accept", R"({"clip":"assets/sounds/move.wav","seed":7,"sound":{"fadeInMs":-5}})").ErrorCode(), "invalid_params");
        EXPECT_EQ(gSoundAccepted.calls, 1);
        EXPECT_EQ(Call("artgen_accept", R"({"clip":"assets/sounds/move.wav","seed":7,"useOnSelected":true})").ErrorCode(), "artgen_accept_failed") << "no node is selected";
        EXPECT_EQ(session.AcceptSound(8, ArtGenSoundTarget{"x", ""}, ArtGenSoundSettings{}, false).error().code, "artgen_no_variant");
    }

    TEST_F(ArtGenSessionTest, ASingleAcceptUsesTheSettingsOfTheSavedPresetOrTheDefaults) {
        session.SetAcceptor(&FakeAcceptor);
        RunWithImages({1});
        Call("artgen_poll", kCatalogPoll);
        ASSERT_TRUE(HasResult(Call("artgen_accept", AcceptParams(1))));
        EXPECT_EQ(gAccepted.settings, ArtGenImageSettings{}) << "a preset without the key";
        EXPECT_FALSE(gAccepted.hasFrame) << "a single image has no frame of a set";

        Call("artgen_poll", CatalogWithWood("accept", R"({"maxSize":64,"trim":false})"));
        EXPECT_EQ(session.ImageSettingsOfRun().maxSize, 64);
        ASSERT_TRUE(HasResult(Call("artgen_accept", AcceptParams(1))));
        EXPECT_FALSE(gAccepted.settings.trim);
        EXPECT_EQ(gAccepted.settings.maxSize, 64);
        EXPECT_EQ(gAccepted.settings.padding, ArtGenImageSettings::kDefaultPadding) << "a missing key keeps the default";

        Call("artgen_poll", CatalogWithWood("accept", R"({"maxSize":5})"));
        ASSERT_TRUE(HasResult(Call("artgen_accept", AcceptParams(1))));
        EXPECT_EQ(gAccepted.settings, ArtGenImageSettings{}) << "a malformed key is the defaults";
    }

    TEST_F(ArtGenSessionTest, ADraftAcceptsWithItsOwnKeyAndNeverTheSavedOnes) {
        session.SetAcceptor(&FakeAcceptor);
        const String saved = CatalogWithWood("accept", R"({"maxSize":64})");
        Call("artgen_poll", saved);
        const String draft = JsonWith(kDraftPreset, "/accept", R"({"padding":9})");
        ASSERT_TRUE(session.RequestDraft("wood", draft, "knight", 1, Optional<int64_t>(3)).has_value());
        Call("artgen_poll", saved);
        Call("artgen_image", Report(session.GetRun().id, R"("path":"D:/out/knight_3.png","seed":3)"));
        ASSERT_TRUE(HasResult(Call("artgen_accept", AcceptParams(3))));
        EXPECT_EQ(gAccepted.settings.padding, 9);
        EXPECT_EQ(gAccepted.settings.maxSize, ArtGenImageSettings::kDefaultMaxSize);
    }

    TEST_F(ArtGenSessionTest, TheImageParameterOverridesTheKeysItNames) {
        session.SetAcceptor(&FakeAcceptor);
        RunWithImages({1});
        Call("artgen_poll", CatalogWithWood("accept", R"({"maxSize":64,"padding":10})"));
        ASSERT_TRUE(HasResult(Call("artgen_accept", AcceptParams(1, R"("image":{"maxSize":0})"))));
        EXPECT_EQ(gAccepted.settings.maxSize, 0);
        EXPECT_EQ(gAccepted.settings.padding, 10) << "the preset's value of a key the call does not name";
        EXPECT_TRUE(gAccepted.settings.trim);
        EXPECT_EQ(gAccepted.calls, 1);

        ASSERT_TRUE(HasResult(Call("artgen_accept", AcceptParams(1, R"("image":{"maxSize":32,"padding":0,"trim":false})"))));
        EXPECT_EQ((gAccepted.settings), (ArtGenImageSettings{false, 0, 32}));

        for (const StringView bad : {StringView(R"("big")"), StringView(R"({"maxSize":5})"), StringView(R"({"padding":"2"})"), StringView(R"({"trim":1})")}) {
            EXPECT_EQ(Call("artgen_accept", AcceptParams(1, Format(R"("image":{})", bad))).ErrorCode(), "invalid_params") << String(bad).c_str();
        }
        EXPECT_EQ(gAccepted.calls, 2) << "a bad image changes nothing";
    }

    TEST_F(ArtGenSessionTest, ASingleAcceptCarriesTheAtlasGroupOfTheSavedPreset) {
        session.SetAcceptor(&FakeAcceptor);
        RunWithImages({1});
        Call("artgen_poll", kCatalogPoll);
        ASSERT_TRUE(HasResult(Call("artgen_accept", AcceptParams(1))));
        EXPECT_TRUE(gAccepted.atlas.empty()) << "a preset without the key joins no group";

        Call("artgen_poll", CatalogWithWood("atlas", R"("pieces")"));
        EXPECT_EQ(session.AtlasOfRun(), "pieces");
        ASSERT_TRUE(HasResult(Call("artgen_accept", AcceptParams(1))));
        EXPECT_EQ(gAccepted.atlas, "pieces");

        Call("artgen_poll", CatalogWithWood("atlas", R"("not a name")"));
        ASSERT_TRUE(HasResult(Call("artgen_accept", AcceptParams(1))));
        EXPECT_TRUE(gAccepted.atlas.empty()) << "a malformed key is no group";
        Call("artgen_poll", CatalogWithWood("atlas", "5"));
        EXPECT_TRUE(session.AtlasOfRun().empty());
    }

    TEST_F(ArtGenSessionTest, ADraftAcceptsIntoItsOwnAtlasGroupAndNeverTheSavedOne) {
        session.SetAcceptor(&FakeAcceptor);
        const String saved = CatalogWithWood("atlas", R"("saved")");
        Call("artgen_poll", saved);
        ASSERT_TRUE(session.RequestDraft("wood", JsonWith(kDraftPreset, "/atlas", R"("icons")"), "knight", 1, Optional<int64_t>(3)).has_value());
        Call("artgen_poll", saved);
        constexpr StringView kImage = R"("path":"D:/out/knight_3.png","seed":3)";
        Call("artgen_image", Report(session.GetRun().id, kImage));
        ASSERT_TRUE(HasResult(Call("artgen_accept", AcceptParams(3))));
        EXPECT_EQ(gAccepted.atlas, "icons");

        Call("artgen_done", Report(session.GetRun().id, R"("status":"ok")"));
        ASSERT_TRUE(session.RequestDraft("wood", kDraftPreset, "knight", 1, Optional<int64_t>(3)).has_value());
        const String image = Report(session.GetRun().id, kImage);
        Call("artgen_poll", saved);
        Call("artgen_image", image);
        ASSERT_TRUE(HasResult(Call("artgen_accept", AcceptParams(3))));
        EXPECT_TRUE(gAccepted.atlas.empty()) << "a draft without the key has no group, whatever the saved preset says";
    }

    TEST_F(ArtGenSessionTest, TheAtlasParameterOverridesThePresetsGroup) {
        session.SetAcceptor(&FakeAcceptor);
        RunWithImages({1});
        Call("artgen_poll", CatalogWithWood("atlas", R"("pieces")"));
        ASSERT_TRUE(HasResult(Call("artgen_accept", AcceptParams(1, R"("atlas":"icons")"))));
        EXPECT_EQ(gAccepted.atlas, "icons");

        ASSERT_TRUE(HasResult(Call("artgen_accept", AcceptParams(1, R"("atlas":"")"))));
        EXPECT_TRUE(gAccepted.atlas.empty()) << "an empty string clears the preset's group";

        ASSERT_TRUE(HasResult(Call("artgen_accept", AcceptParams(1, R"("atlas":null)"))));
        EXPECT_EQ(gAccepted.atlas, "pieces") << "null is the preset's";
        EXPECT_EQ(gAccepted.calls, 3);

        for (const StringView bad : {StringView(R"("a b")"), StringView("7"), StringView("[]"), StringView(R"("../x")")}) {
            EXPECT_EQ(Call("artgen_accept", AcceptParams(1, Format(R"("atlas":{})", bad))).ErrorCode(), "invalid_params") << String(bad).c_str();
        }
        EXPECT_EQ(gAccepted.calls, 3) << "a bad group changes nothing";
    }

    TEST_F(ArtGenSessionTest, ASoundAcceptIgnoresTheAtlasGroup) {
        const uint32_t id = session.Request("wood", "knock", 1, Optional<int64_t>(7)).value();
        Call("artgen_poll", CatalogWithWood("atlas", R"("pieces")"));
        Call("artgen_image", Report(id, R"("kind":"audio","path":"D:/out/knock_7.flac","seed":7)"));
        session.SetSoundAcceptor(&FakeSoundAcceptor);
        EXPECT_TRUE(HasResult(Call("artgen_accept", R"({"atlas":"not a name","name":"knock","seed":7})")));
        EXPECT_EQ(gSoundAccepted.calls, 1);
    }

    TEST_F(ArtGenSessionTest, ASoundIgnoresTheImageParameter) {
        const uint32_t id = Require(session.Request("wood", "knock", 1, Optional<int64_t>(7)));
        Call("artgen_poll", kReadyPoll);
        Call("artgen_image", Report(id, R"("kind":"audio","path":"D:/out/knock_7.flac","seed":7)"));
        session.SetSoundAcceptor(&FakeSoundAcceptor);
        EXPECT_TRUE(HasResult(Call("artgen_accept", R"({"image":"not even an object","name":"knock","seed":7})")));
        EXPECT_EQ(gSoundAccepted.calls, 1);
    }

    TEST_F(ArtGenSessionTest, ARequestWithAReferenceGoesOutInThePollAndIsInTheState) {
        Call("artgen_poll", kReadyPoll);
        constexpr StringView kReference = R"({"image":"assets/artgen/rook.png","mode":"init","weight":0.3})";
        ASSERT_TRUE(IntOf(Call("artgen_generate", Format(R"({{"count":1,"preset":"wood","reference":{},"subject":"rook"}})", kReference)).Result(), "id").has_value());
        ASSERT_TRUE(session.GetRun().reference.has_value());
        EXPECT_EQ(session.GetRun().reference->image, "assets/artgen/rook.png");

        const ControlReply state = Call("artgen_state");
        EXPECT_EQ(RawOf(MemberOf(state.Result(), "run"), "reference"), kReference);

        const ControlReply poll = Call("artgen_poll", kReadyPoll);
        const JsonView request = MemberOf(poll.Result(), "request");
        ASSERT_TRUE(request.IsObject());
        EXPECT_EQ(RawOf(request, "reference"), kReference);
    }

    TEST_F(ArtGenSessionTest, AStyleReferenceGoesOutInThePollAndIsInTheState) {
        Call("artgen_poll", kReadyPoll);
        constexpr StringView kReference = R"({"image":"assets/artgen/pawn.png","mode":"style","weight":0.8})";
        ASSERT_TRUE(IntOf(Call("artgen_generate", Format(R"({{"count":1,"preset":"wood","reference":{},"subject":"rook"}})", kReference)).Result(), "id").has_value());
        EXPECT_EQ(RawOf(MemberOf(Call("artgen_state").Result(), "run"), "reference"), kReference);
        const ControlReply poll = Call("artgen_poll", kReadyPoll);
        EXPECT_EQ(RawOf(MemberOf(poll.Result(), "request"), "reference"), kReference);
    }

    TEST_F(ArtGenSessionTest, AStructureReferenceGoesOutInThePollAndIsInTheState) {
        Call("artgen_poll", kReadyPoll);
        constexpr StringView kReference = R"({"image":"sketches/rook.png","mode":"structure","weight":0.8})";
        ASSERT_TRUE(IntOf(Call("artgen_generate", Format(R"({{"count":1,"preset":"wood","reference":{},"subject":"rook"}})", kReference)).Result(), "id").has_value());
        EXPECT_EQ(RawOf(MemberOf(Call("artgen_state").Result(), "run"), "reference"), kReference);
        const ControlReply poll = Call("artgen_poll", kReadyPoll);
        EXPECT_EQ(RawOf(MemberOf(poll.Result(), "request"), "reference"), kReference);
    }

    TEST_F(ArtGenSessionTest, ARequestWithoutAReferenceLeavesTheKeyOut) {
        Call("artgen_poll", kReadyPoll);
        ASSERT_TRUE(session.Request("wood", "rook", 1, Optional<int64_t>(1)).has_value());
        const ControlReply poll = Call("artgen_poll", kReadyPoll);
        const JsonView request = MemberOf(poll.Result(), "request");
        ASSERT_TRUE(request.IsObject());
        EXPECT_FALSE(request.Find("reference").has_value());
        const ControlReply state = Call("artgen_state");
        const JsonView run = MemberOf(state.Result(), "run");
        ASSERT_TRUE(run.IsObject());
        EXPECT_FALSE(run.Find("reference").has_value()) << "none is no member, not null";
    }

    TEST_F(ArtGenSessionTest, TheReferenceOfAnAnswerIsWrittenAsTheFileWritesIt) {
        const ArtGenReference withWeight{"style", "assets/artgen/pawn.png", Optional<double>(0.8)};
        const ArtGenReference withoutWeight{"init", "sketches/rook.png", Optional<double>()};
        const auto fileText = [](const ArtGenReference& reference) {
            String text;
            JsonWriter writer(text);
            WriteArtGenReference(writer, reference);
            return text;
        };
        EXPECT_EQ(AnswerString(MakeArtGenReferenceAnswer(withWeight)), fileText(withWeight));
        EXPECT_EQ(AnswerString(MakeArtGenReferenceAnswer(withoutWeight)), fileText(withoutWeight));
    }

    TEST_F(ArtGenSessionTest, TheStateOfARunHasItsSeedOnlyWhenTheRequestNamedOne) {
        Call("artgen_poll", kReadyPoll);
        Require(session.Request("wood", "rook", 1, Optional<int64_t>(5)));
        EXPECT_EQ(IntOf(MemberOf(Call("artgen_state").Result(), "run"), "seed"), Optional<int64_t>(5));
        session.Cancel();
        Require(session.Request("wood", "rook", 1, Optional<int64_t>()));
        EXPECT_FALSE(MemberOf(Call("artgen_state").Result(), "run").Find("seed").has_value());
    }

    TEST_F(ArtGenSessionTest, ADraftTakesAReferenceToo) {
        const auto id = session.RequestDraft("wood", R"({"preset":1,"prompt":"{subject}","workflow":"sdxl_basic"})", "rook", 1, Optional<int64_t>(), false,
                                             ArtGenReference{"init", "artgen/out/x.png", Optional<double>()});
        ASSERT_TRUE(id.has_value()) << id.error().c_str();
        const ControlReply poll = Call("artgen_poll", kReadyPoll);
        const JsonView reference = MemberOf(MemberOf(poll.Result(), "request"), "reference");
        ASSERT_TRUE(reference.IsObject());
        EXPECT_EQ(StringOf(reference, "image"), "artgen/out/x.png");
        EXPECT_FALSE(reference.Find("weight").has_value());
    }

    TEST_F(ArtGenSessionTest, ANewRunClearsTheReferenceOfTheOldOne) {
        Call("artgen_poll", kReadyPoll);
        const uint32_t id = session.Request("wood", "rook", 1, Optional<int64_t>(1), false, ArtGenReference{"init", "a.png", Optional<double>(0.5)}).value();
        Call("artgen_poll", kReadyPoll);
        Call("artgen_done", Report(id, R"("status":"ok")"));
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
        Call("artgen_poll", kReadyPoll);
        const auto generate = [this](StringView reference) { return Call("artgen_generate", Format(R"({{"preset":"wood","reference":{},"subject":"rook"}})", reference)); };
        const ControlReply response = generate(R"({"image":"a.png","mode":"init","weight":"x"})");
        EXPECT_EQ(response.ErrorCode(), "invalid_params");
        EXPECT_NE(String(response.Text()).find("reference.weight"), String::npos);
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Idle);

        for (const StringView bad : {StringView(R"("a.png")"), StringView("[]"), StringView(R"({"mode":"init"})"), StringView(R"({"image":"a.png"})"), StringView(R"({"image":"","mode":"init"})")}) {
            EXPECT_EQ(generate(bad).ErrorCode(), "invalid_params") << String(bad).c_str();
        }
        EXPECT_TRUE(HasResult(generate("null"))) << "null is no reference";
        EXPECT_FALSE(session.GetRun().reference.has_value());
    }

}  // namespace BECore::Tests
