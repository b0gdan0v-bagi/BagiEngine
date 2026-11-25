#include <BECore/Control/ControlService.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/ArtGen/ArtGenPanelModel.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kFrameGuard = 100;
        constexpr int kSteadyFrames = 8;
        constexpr uint16_t kPort = 4567;

        constexpr StringView kReadyPoll = R"({"generator":"fake","state":"ready"})";
        constexpr StringView kNotInstalledPoll = R"({"generator":"fake","reason":"no ComfyUI under D:/ComfyUI","state":"not_installed"})";
        constexpr StringView kStartedHerePoll = R"({"generator":"fake","startedHere":true,"state":"ready"})";

        /// A report of the service about the setup or run @p id with the members @p members
        String Report(uint32_t id, StringView members) {
            return Format(R"({{"id":{},{}}})", id, members);
        }

    }  // namespace

    /// The Generate panel's model over a session, a control service and a clock of its own; the bridge and the control API's start are fakes
    class ArtGenPanelModelTest : public ::testing::Test {
    protected:
        ArtGenPanelModelTest() : session([this] { return now; }) {}

        void SetUp() override {
            ArtGenPanelServices services;
            services.session = &session;
            services.control = &service;
            services.controlStatus = [this] { return control; };
            services.clock = [this] { return now; };
            services.launchBridge = [this](uint16_t port, String& error) {
                launchedPorts.push_back(port);
                error = launchError;
                return launchError.empty();
            };
            services.startControl = [this] { ++controlStarts; };
            model.SetServices(std::move(services));
            model.Initialize();
            session.SetGameRoot("D:/Games/Chess");
        }

        /// One control call through the service, as the bridge makes it
        ControlReply Call(StringView method, StringView params = "{}") {
            ControlReply response;
            bool answered = false;
            service.Post(MakeRequestText(++_nextId, method, params), [&](const String& reply) {
                response = ControlReply(StringView(reply.c_str(), reply.size()));
                answered = true;
            });
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

        bool Run(PoolString command, StringView argument) {
            return model.RunCommand(command.ToStringView(), argument);
        }

        const ArtGenServiceView& Service() {
            model.Update();
            return model.GetView().service;
        }

        const ArtGenSetupView& SetupView() {
            model.Update();
            return model.GetView().setup;
        }

        void Listen() {
            control.listening = true;
            control.port = kPort;
        }

        double now = 100.0;
        ControlServerStatus control;
        Vector<uint16_t> launchedPorts;
        String launchError;
        int controlStarts = 0;
        ControlService service;
        ArtGenSession session;
        ArtGenPanelModel model;

    private:
        int64_t _nextId = 0;
    };

    TEST_F(ArtGenPanelModelTest, StartWithTheControlApiOffTurnsItOnAndStartsTheBridgeOnceItListens) {
        EXPECT_EQ(Service().line, ArtGenServiceLine::ControlOff);
        EXPECT_TRUE(Service().canStart);
        EXPECT_FALSE(Service().controlFailed);

        EXPECT_TRUE(Run(EditorCommandIds::kArtGenService, ArtGenPanelArgs::kStart));
        EXPECT_EQ(controlStarts, 1);
        EXPECT_TRUE(launchedPorts.empty());
        EXPECT_TRUE(Service().controlFailed) << "the control API is still off: the line says it did not start";

        Listen();
        EXPECT_EQ(Service().line, ArtGenServiceLine::Starting);
        ASSERT_EQ(launchedPorts.size(), 1u);
        EXPECT_EQ(launchedPorts[0], kPort);
        EXPECT_FALSE(Service().canStart);
        model.Update();
        EXPECT_EQ(launchedPorts.size(), 1u) << "the bridge starts once";
    }

    TEST_F(ArtGenPanelModelTest, ABridgeThatDoesNotPollInTimeSaysSoWithThePort) {
        Listen();
        EXPECT_EQ(Service().line, ArtGenServiceLine::NotConnected);
        EXPECT_EQ(Service().byHandText, Format("Or by hand: python engine/tools/artgen/artgen_bridge.py --port {}", kPort));

        Run(EditorCommandIds::kArtGenService, ArtGenPanelArgs::kStart);
        EXPECT_EQ(Service().line, ArtGenServiceLine::Starting);
        now += ArtGenPanelModel::kBridgeStartSeconds;
        EXPECT_EQ(Service().line, ArtGenServiceLine::NotConnected);
        EXPECT_TRUE(Service().canStart);
        EXPECT_NE(Service().byHandText.find("It did not connect"), String::npos);
        EXPECT_NE(Service().byHandText.find(Format("--port {}", kPort)), String::npos);
    }

    TEST_F(ArtGenPanelModelTest, ABridgeThatCannotStartShowsWhy) {
        Listen();
        launchError = "no python";
        Run(EditorCommandIds::kArtGenService, ArtGenPanelArgs::kStart);
        EXPECT_EQ(Service().line, ArtGenServiceLine::NotConnected);
        EXPECT_TRUE(Service().hasBridgeError);
        EXPECT_EQ(Service().bridgeError, "no python");
        EXPECT_EQ(Service().byHandText.find("It did not connect"), String::npos);
    }

    TEST_F(ArtGenPanelModelTest, APollingServiceShowsItsGeneratorReadyOrWhyNot) {
        Listen();
        Run(EditorCommandIds::kArtGenService, ArtGenPanelArgs::kStart);
        Call("artgen_poll", kReadyPoll);
        EXPECT_EQ(Service().line, ArtGenServiceLine::Ready);
        EXPECT_EQ(Service().statusText, "Service ready (fake)");
        EXPECT_EQ(Service().statusStyle, PanelTextStyle::Success);
        EXPECT_FALSE(Service().hasReason);
        EXPECT_FALSE(Service().startedHere);

        Call("artgen_poll", kNotInstalledPoll);
        EXPECT_EQ(Service().line, ArtGenServiceLine::NotReady);
        EXPECT_EQ(Service().statusText, "Generator not_installed");
        EXPECT_EQ(Service().statusStyle, PanelTextStyle::Warning);
        EXPECT_TRUE(Service().hasReason);
        EXPECT_EQ(Service().reason, "no ComfyUI under D:/ComfyUI");

        Call("artgen_poll", kStartedHerePoll);
        EXPECT_TRUE(Service().startedHere);

        now += ArtGenSession::kServiceTimeoutSeconds + 1.0;
        EXPECT_EQ(Service().line, ArtGenServiceLine::NotConnected) << "a service that stopped polling is gone, and the start window is over";
    }

    TEST_F(ArtGenPanelModelTest, DisconnectAsksTheServiceToStop) {
        Listen();
        Call("artgen_poll", kReadyPoll);
        EXPECT_TRUE(Run(EditorCommandIds::kArtGenService, ArtGenPanelArgs::kDisconnect));
        EXPECT_TRUE(BoolOf(Call("artgen_poll", kReadyPoll).Result(), "shutdown").value_or(false));
    }

    TEST_F(ArtGenPanelModelTest, ThePlanListsItsStepsWithTheirLicencesAndInstallShowsProgress) {
        Listen();
        Call("artgen_poll", kNotInstalledPoll);
        EXPECT_TRUE(SetupView().notInstalled);
        EXPECT_TRUE(SetupView().canAct);

        EXPECT_TRUE(Run(EditorCommandIds::kArtGenSetup, ArtGenPanelArgs::kPlan));
        EXPECT_TRUE(SetupView().busy);
        EXPECT_EQ(SetupView().doing, "Looking at this machine...");
        EXPECT_FALSE(SetupView().cancelShown);
        const uint32_t plan = session.GetSetup().id;
        Call("artgen_poll", kNotInstalledPoll);
        Call("artgen_setup_report", Report(plan, R"("downloadBytes":6500000000,"event":"plan","platform":"windows-nvidia","root":"D:/ComfyUI","steps":[)"
                                                 R"({"dest":"D:/ComfyUI/models","licence":"OpenRAIL++-M","size":6500000000,"state":"download","what":"sd_xl_base"},)"
                                                 R"({"state":"install","what":"ComfyUI"}])"));
        Call("artgen_setup_report", Report(plan, R"("event":"done","ok":true)"));
        ASSERT_TRUE(SetupView().planShown);
        EXPECT_FALSE(SetupView().notInstalled);
        EXPECT_EQ(SetupView().platformText.find("This machine: windows-nvidia."), 0u);
        EXPECT_EQ(SetupView().rootText, "Into D:/ComfyUI:");
        ASSERT_EQ(SetupView().plan.size(), 2u);
        EXPECT_EQ(SetupView().plan[0].what, "sd_xl_base");
        EXPECT_EQ(SetupView().plan[0].size, "6.50 GB");
        EXPECT_EQ(SetupView().plan[0].tooltip, "Licence: OpenRAIL++-M\nD:/ComfyUI/models");
        EXPECT_EQ(SetupView().plan[1].tooltip, "") << "a step without a licence has no tooltip";
        EXPECT_EQ(SetupView().plan[1].size, "");
        EXPECT_TRUE(SetupView().fromSource);
        EXPECT_EQ(SetupView().downloadSize, "6.5 GB");

        Run(EditorCommandIds::kArtGenSetup, ArtGenPanelArgs::kInstall);
        const uint32_t install = session.GetSetup().id;
        Call("artgen_poll", kNotInstalledPoll);
        Call("artgen_setup_report", Report(install, R"("done":2000000,"event":"progress","total":8000000)"));
        EXPECT_TRUE(SetupView().busy);
        EXPECT_TRUE(SetupView().hasProgress);
        EXPECT_FLOAT_EQ(SetupView().progress, 0.25f);
        EXPECT_EQ(SetupView().progressText, "2 / 8 MB");
        EXPECT_TRUE(SetupView().cancelShown);
        EXPECT_FALSE(SetupView().cancelRequested);

        Run(EditorCommandIds::kArtGenSetup, ArtGenPanelArgs::kCancel);
        EXPECT_TRUE(SetupView().cancelRequested);
        Call("artgen_setup_report", Report(install, R"("error":"the download stopped","event":"done","ok":false)"));
        EXPECT_FALSE(SetupView().busy);
        EXPECT_TRUE(SetupView().failed);
        EXPECT_EQ(SetupView().error, "the download stopped");
    }

    TEST_F(ArtGenPanelModelTest, ADoneStartSaysWhatItDidAndARefusedRequestShowsWhy) {
        Listen();
        Call("artgen_poll", R"({"generator":"fake","state":"not_running"})");
        EXPECT_TRUE(SetupView().notRunning);
        Run(EditorCommandIds::kArtGenSetup, ArtGenPanelArgs::kStart);
        const uint32_t start = session.GetSetup().id;
        Call("artgen_poll", kReadyPoll);
        Call("artgen_setup_report", Report(start, R"("event":"log","text":"ComfyUI is already running: used as it is")"));
        Call("artgen_setup_report", Report(start, R"("event":"done","ok":true)"));
        EXPECT_TRUE(SetupView().hasDoneLog);
        EXPECT_EQ(SetupView().doneLog, "ComfyUI is already running: used as it is");

        ASSERT_TRUE(session.Request("wood", "queen", 1, Optional<int64_t>(1)).has_value());
        EXPECT_FALSE(SetupView().canAct) << "Set up and Start are off while a run is in work";
        Run(EditorCommandIds::kArtGenSetup, ArtGenPanelArgs::kStop);
        EXPECT_TRUE(SetupView().hasSetupError);
        EXPECT_FALSE(SetupView().setupError.empty());

        Run(EditorCommandIds::kArtGenSetup, ArtGenPanelArgs::kDismiss);
        EXPECT_FALSE(SetupView().hasSetupError);
        EXPECT_FALSE(SetupView().hasDoneLog) << "Not now forgets the finished action";
    }

    TEST_F(ArtGenPanelModelTest, AnUnknownArgumentOrCommandDoesNothing) {
        Listen();
        Call("artgen_poll", kNotInstalledPoll);
        EXPECT_TRUE(Run(EditorCommandIds::kArtGenSetup, "explode"));
        EXPECT_TRUE(Run(EditorCommandIds::kArtGenService, ""));
        EXPECT_FALSE(SetupView().busy);
        EXPECT_TRUE(launchedPorts.empty());
        EXPECT_EQ(controlStarts, 0);
        EXPECT_FALSE(Run(EditorCommandIds::kViewGenerate, ArtGenPanelArgs::kStart)) << "the panel's own toggle is the widget's";
    }

    TEST_F(ArtGenPanelModelTest, ASteadyUpdateAllocatesNothing) {
        Listen();
        Call("artgen_poll", kNotInstalledPoll);
        Run(EditorCommandIds::kArtGenSetup, ArtGenPanelArgs::kPlan);
        model.Update();
        ExpectNoAllocations([&] {
            for (int frame = 0; frame < kSteadyFrames; ++frame) {
                model.Update();
            }
        });
    }

}  // namespace BECore::Tests
