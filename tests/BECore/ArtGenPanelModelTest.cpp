#include <BECore/Control/ControlService.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
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

        /// Four presets (the game's `wood` and `odd`, whose reference cannot be read, the engine's `sprite`, a broken one) and three workflows (a reference
        /// variant of the first and a broken one)
        constexpr StringView kCatalogPoll =
            R"({"generator":"fake","presets":[{"name":"wood","preset":{"preset":1,"prompt":"carved {subject}","title":"Wood","workflow":"sdxl_basic"},"source":"game","title":"wood"},)"
            R"({"name":"sprite","preset":{"preset":1,"prompt":"{subject}","title":"Sprite","workflow":"sdxl_basic"},"source":"engine","title":"sprite"},)"
            R"({"error":"no workflow 'x'","name":"broken","title":"broken"},)"
            R"({"name":"odd","preset":{"preset":1,"prompt":"x","reference":5,"workflow":"sdxl_basic"},"source":"game","title":"odd"}],"state":"ready",)"
            R"("workflows":[{"inputs":{"height":{"default":768},"sampler":{"choices":["euler","ddim"],"default":"euler"},"steps":{"default":25},"width":{"default":1024}},)"
            R"("name":"sdxl_basic","source":"engine"},)"
            R"({"inputs":{"height":{"default":768},"reference_image":{"default":""},"width":{"default":1024}},"name":"sdxl_basic_ref","source":"engine"},)"
            R"({"error":"no inputs","name":"broken","source":"game"}]})";

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

        const ArtGenPresetView& Preset() {
            model.Update();
            return model.GetView().preset;
        }

        const ArtGenPresetRefView& PresetRef() {
            model.Update();
            return model.GetView().presetRef;
        }

        /// The view's preset section, written as the panel's controls write it; the next `Update` applies it
        ArtGenPresetView& Edit() {
            return model.GetMutableView().preset;
        }

        /// The service listed the catalog and the model loaded its first preset
        void Catalog() {
            Call("artgen_poll", kCatalogPoll);
            model.Update();
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

    TEST_F(ArtGenPanelModelTest, TheFirstPresetThatLoadsIsShownAndABrokenOneCannotBePicked) {
        EXPECT_EQ(Preset().preview, "(no preset)");
        EXPECT_TRUE(Preset().noList);
        Catalog();
        EXPECT_FALSE(Preset().noList);
        EXPECT_EQ(Preset().index, 0);
        EXPECT_EQ(Preset().name, "wood");
        EXPECT_EQ(Preset().title, "Wood");
        EXPECT_EQ(Preset().prompt, "carved {subject}");
        EXPECT_EQ(Preset().preview, "wood");
        EXPECT_TRUE(Preset().savedHere);
        ASSERT_EQ(Preset().rows.size(), 4u);
        EXPECT_NE(Preset().rows[1].label.find(", engine"), String::npos);
        EXPECT_FALSE(Preset().rows[2].enabled);
        EXPECT_EQ(Preset().rows[2].tooltip, "no workflow 'x'");
        EXPECT_TRUE(Preset().rows[0].tooltip.empty());

        Edit().index = 2;
        EXPECT_EQ(Preset().index, 0) << "a broken preset is never loaded";
        EXPECT_EQ(Preset().name, "wood");
    }

    TEST_F(ArtGenPanelModelTest, PickingAnotherPresetDropsTheEditsAndPickingTheSameKeepsThem) {
        Catalog();
        Edit().title = "Carved";
        EXPECT_TRUE(Preset().dirty);
        EXPECT_EQ(Preset().preview, "wood *");
        EXPECT_EQ(Preset().stateText, "unsaved changes");
        EXPECT_FALSE(Preset().savedHere);

        Edit().index = 0;
        EXPECT_EQ(Preset().title, "Carved") << "the current one picked again changes nothing";

        Edit().index = 1;
        EXPECT_EQ(Preset().name, "sprite");
        EXPECT_EQ(Preset().title, "Sprite");
        EXPECT_FALSE(Preset().dirty);
        EXPECT_TRUE(Preset().engineStarter);
    }

    TEST_F(ArtGenPanelModelTest, ATypedNamePastTheFormIsCutAtACharacterBoundary) {
        Catalog();
        const size_t limit = ArtGenPresetForm::kNameSize - 1;
        // The two bytes of U+00E9 would straddle the limit: the whole character goes
        Edit().name = String(limit - 1, 'a') + "\xC3\xA9";
        EXPECT_EQ(Preset().name, String(limit - 1, 'a'));
        EXPECT_EQ(StringView(model.GetForm().name), StringView(Preset().name));
        EXPECT_EQ(Preset().preview, String(limit - 1, 'a') + " *");
    }

    TEST_F(ArtGenPanelModelTest, NewDuplicateAndRevert) {
        Catalog();
        Run(EditorCommandIds::kArtGenPreset, ArtGenPanelArgs::kDuplicate);
        EXPECT_EQ(Preset().stateText, "not saved yet");
        EXPECT_TRUE(Preset().dirty);
        EXPECT_FALSE(Preset().canRevert) << "a copy has no saved file to go back to";
        EXPECT_EQ(Preset().prompt, "carved {subject}");

        Edit().index = 0;
        Preset();
        Edit().prompt = "burnt {subject}";
        EXPECT_TRUE(Preset().canRevert);
        Run(EditorCommandIds::kArtGenPreset, ArtGenPanelArgs::kRevert);
        EXPECT_EQ(Preset().prompt, "carved {subject}");
        EXPECT_FALSE(Preset().dirty);

        Run(EditorCommandIds::kArtGenPreset, ArtGenPanelArgs::kNew);
        EXPECT_EQ(Preset().workflowPreview, "sdxl_basic");
        EXPECT_EQ(Preset().index, -1);
        EXPECT_NE(Preset().prompt, "carved {subject}") << "a command's change is never undone by the view made before it";
    }

    TEST_F(ArtGenPanelModelTest, SaveWritesTheGamesFileAndSaveAsTakesAValidName) {
        const Path root = TestTempDirectory("ArtGenPanelModelTest");
        MakeEmptyDirectory(root);
        session.SetGameRoot(root.View());
        Catalog();
        Edit().index = 1;
        EXPECT_TRUE(Preset().engineStarter);
        Run(EditorCommandIds::kArtGenPreset, ArtGenPanelArgs::kSave);
        EXPECT_TRUE(Preset().hasMessage);
        EXPECT_EQ(Preset().messageStyle, PanelTextStyle::Success);
        EXPECT_TRUE(Preset().savedHere) << "an engine starter saved is the game's own";
        EXPECT_FALSE(Preset().engineStarter);
        EXPECT_TRUE(PathExists(root / "artgen" / "presets" / "sprite.json"));

        Run(EditorCommandIds::kArtGenSaveAsOpened, "");
        EXPECT_EQ(Preset().saveAsName, "sprite");
        EXPECT_TRUE(Preset().saveAsValid);
        Edit().saveAsName = "bad name!";
        EXPECT_FALSE(Preset().saveAsValid);
        Run(EditorCommandIds::kArtGenPreset, ArtGenPanelArgs::kSaveAs);
        EXPECT_FALSE(PathExists(root / "artgen" / "presets" / "bad name!.json"));

        Edit().saveAsName = "sprite_two";
        Preset();
        Run(EditorCommandIds::kArtGenPreset, ArtGenPanelArgs::kSaveAs);
        EXPECT_EQ(Preset().name, "sprite_two");
        EXPECT_TRUE(PathExists(root / "artgen" / "presets" / "sprite_two.json"));
        RemoveDirectory(root);
    }

    TEST_F(ArtGenPanelModelTest, TheWorkflowListLeavesTheReferenceVariantOutAndAPickSetsIt) {
        Catalog();
        ASSERT_EQ(Preset().workflows.size(), 2u);
        EXPECT_EQ(Preset().workflows[0].label.find("sdxl_basic  ("), 0u);
        EXPECT_FALSE(Preset().workflows[1].enabled);
        EXPECT_EQ(Preset().workflows[1].tooltip, "no inputs");
        EXPECT_EQ(Preset().workflowIndex, 0);
        EXPECT_TRUE(Preset().knownWorkflow);

        Edit().workflowIndex = 1;
        EXPECT_EQ(Preset().workflowPreview, "sdxl_basic") << "a broken workflow is never set";
    }

    TEST_F(ArtGenPanelModelTest, TheSizeShowsTheWorkflowsDefaultsUntilSetAndResets) {
        Catalog();
        EXPECT_TRUE(Preset().hasSize);
        EXPECT_EQ(Preset().width, 1024);
        EXPECT_EQ(Preset().height, 768);
        EXPECT_FALSE(Preset().sizeOverridden);

        Edit().width = ArtGenPresetForm::kMaxImageSide + 1;
        EXPECT_EQ(Preset().width, ArtGenPresetForm::kMaxImageSide);
        EXPECT_EQ(Preset().height, 768);
        EXPECT_TRUE(Preset().sizeOverridden);
        EXPECT_TRUE(Preset().dirty);

        Run(EditorCommandIds::kArtGenPreset, ArtGenPanelArgs::kResetSize);
        EXPECT_EQ(Preset().width, 1024);
        EXPECT_FALSE(Preset().sizeOverridden);
    }

    TEST_F(ArtGenPanelModelTest, TheAcceptSettingsAreTheFormsAndDefaultsDropThem) {
        Catalog();
        const ArtGenImageSettingsView& accept = Preset().accept;
        EXPECT_EQ(accept.maxSizes.size(), ArtGenImageSettings::kMaxSizeOptions.size());
        EXPECT_EQ(accept.maxSizes[0], "original size");
        EXPECT_FALSE(accept.overridden);

        Edit().accept.trim = false;
        Edit().accept.maxSizeIndex = 0;
        Edit().accept.padding = ArtGenImageSettings::kMaxPadding + 5;
        Preset();
        EXPECT_FALSE(model.GetForm().GetImageAccept().trim);
        EXPECT_EQ(model.GetForm().GetImageAccept().maxSize, 0);
        EXPECT_EQ(model.GetForm().GetImageAccept().padding, ArtGenImageSettings::kMaxPadding);
        EXPECT_TRUE(Preset().accept.overridden);
        EXPECT_EQ(Preset().accept.maxSizeText, "original size");

        Run(EditorCommandIds::kArtGenPreset, ArtGenPanelArgs::kAcceptDefaults);
        EXPECT_FALSE(Preset().accept.overridden);
        EXPECT_TRUE(Preset().accept.trim);
        EXPECT_EQ(Preset().accept.maxSizeText, "256 px");
    }

    TEST_F(ArtGenPanelModelTest, ThePresetsReferenceTakesTheSlotChangesModeAndWeightAndIsRemoved) {
        Catalog();
        EXPECT_FALSE(PresetRef().has);
        EXPECT_FALSE(PresetRef().canTake);
        session.SetReferenceSlot(ArtGenReferenceOfSprite("knight", "style"));
        EXPECT_TRUE(PresetRef().canTake);
        Run(EditorCommandIds::kArtGenPreset, ArtGenPanelArgs::kTakeSlot);
        ASSERT_TRUE(PresetRef().has);
        EXPECT_EQ(PresetRef().mode, "style");
        EXPECT_EQ(PresetRef().modeTip, String(ArtGenReferenceModeOf("style").tip));
        EXPECT_FLOAT_EQ(PresetRef().weight, static_cast<float>(ArtGenReferenceDefaultWeight("style")));
        EXPECT_FALSE(PresetRef().imageTip.empty());

        model.GetMutableView().presetRef.weight = 0.9f;
        PresetRef();
        EXPECT_NEAR(model.GetForm().GetReference()->weight.value_or(0.0), 0.9, 1e-6);

        model.GetMutableView().presetRef.modeIndex = 0;
        EXPECT_EQ(PresetRef().mode, "init");
        EXPECT_FALSE(model.GetForm().GetReference()->weight.has_value()) << "a mode change drops the weight";
        EXPECT_FLOAT_EQ(PresetRef().weight, static_cast<float>(ArtGenReferenceDefaultWeight("init")));

        EXPECT_TRUE(PresetRef().canRemove);
        Run(EditorCommandIds::kArtGenPreset, ArtGenPanelArgs::kRemoveReference);
        EXPECT_FALSE(PresetRef().has);
        EXPECT_FALSE(PresetRef().canRemove);
    }

    TEST_F(ArtGenPanelModelTest, AnUnreadableReferenceCanOnlyBeRemoved) {
        Catalog();
        Edit().index = 3;
        EXPECT_EQ(Preset().name, "odd");
        EXPECT_TRUE(PresetRef().unreadable);
        EXPECT_FALSE(PresetRef().has);
        EXPECT_TRUE(PresetRef().canRemove);
        Run(EditorCommandIds::kArtGenPreset, ArtGenPanelArgs::kRemoveReference);
        EXPECT_FALSE(PresetRef().unreadable);
    }

    TEST_F(ArtGenPanelModelTest, ASteadyUpdateWithAPresetAllocatesNothing) {
        Catalog();
        Edit().title = "Carved";
        model.Update();
        ExpectNoAllocations([&] {
            for (int frame = 0; frame < kSteadyFrames; ++frame) {
                model.Update();
            }
        });
    }

}  // namespace BECore::Tests
