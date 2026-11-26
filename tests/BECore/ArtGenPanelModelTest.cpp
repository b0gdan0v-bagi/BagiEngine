#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/AudioDecode.h>
#include <BECore/Control/ControlService.h>
#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <Common/AllocCheck.h>
#include <Common/ArtGenTestSupport.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/ArtGen/ArtGenDrag.h>
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

        /// What the fake acceptor was called with (a function pointer cannot capture, so the test reads it here)
        struct AcceptCall {
            int64_t seed = -1;
            String sprite;
            ArtGenImageSettings settings;
            int calls = 0;
        };
        AcceptCall gAccepted;

        std::expected<ArtGenAcceptAnswer, ControlError> FakeAcceptor(const ArtGenVariant& variant, StringView sprite, const ArtGenImagePlan& plan, SceneNode* /*node*/) {
            gAccepted.seed = variant.seed;
            gAccepted.sprite = String(sprite);
            gAccepted.settings = plan.settings;
            ++gAccepted.calls;
            ArtGenAcceptAnswer reply;
            reply.sprite = String(sprite);
            reply.texturePath = Format("assets/artgen/{}.png", sprite);
            reply.reloadedSprites = 1;
            return reply;
        }

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
            reply.clip = target.clip.empty() ? Format("assets/artgen/{}.wav", target.name) : target.clip;
            reply.reloadedSources = 0;
            return reply;
        }

        constexpr uint32_t kSoundRate = 22050;
        constexpr size_t kSoundFrames = 2205;
        constexpr float kSoundLevel = 0.5f;

        /// A folder the session takes as the game root, removed with the object
        class TempGame {
        public:
            TempGame() : _root(TestTempDirectory("ArtGenPanelModelTest")) {
                MakeEmptyDirectory(_root);
            }
            ~TempGame() {
                RemoveDirectory(_root);
            }
            TempGame(const TempGame&) = delete;
            TempGame& operator=(const TempGame&) = delete;

            [[nodiscard]] const Path& Root() const {
                return _root;
            }

            /// A `kSide` square PNG at @p name, opaque or with nothing drawn; its real path
            String SavePng(StringView name, bool drawn) const {
                RgbaImage image = MakeSolid(kSide, kSide, kHigh, kLow, kLow);
                if (!drawn) {
                    std::fill(image.pixels.begin(), image.pixels.end(), uint8_t{0});
                }
                const Path file = _root / name;
                EXPECT_TRUE(SaveRgbaPng(file, image).has_value());
                return String(file.View());
            }

            /// A short mono tone as a WAV at @p name; its real path
            String SaveSound(StringView name) const {
                Vector<float> samples(kSoundFrames, kSoundLevel);
                for (size_t i = 1; i < samples.size(); i += 2) {
                    samples[i] = -kSoundLevel;
                }
                const AudioFormat format{kSoundRate, 1};
                auto bytes = EncodeWav(Span<const float>(samples.data(), samples.size()), format, WavSampleFormat::Pcm16);
                EXPECT_TRUE(bytes.has_value());
                const Path file = _root / name;
                if (bytes) {
                    EXPECT_TRUE(WriteFileAtomically(file.View(), Span<const uint8_t>(bytes->data(), bytes->size())).has_value());
                }
                return String(file.View());
            }

            static constexpr uint32_t kSide = 8;

        private:
            Path _root;
        };

        /// A game folder mounted under a virtual path, so the resource manager loads the sound variants from it as the editor does from the service's
        /// folder
        class SoundFolder {
        public:
            SoundFolder() {
                EXPECT_TRUE(CoreManager::GetFileSystem().Mount(kMount, _game.Root()));
            }
            ~SoundFolder() {
                CoreManager::GetResourceManager().ClearCache();
                CoreManager::GetFileSystem().Unmount(kMount);
            }
            SoundFolder(const SoundFolder&) = delete;
            SoundFolder& operator=(const SoundFolder&) = delete;

            /// A tone at @p name; its virtual path
            String Save(StringView name) const {
                _game.SaveSound(name);
                return Format("{}/{}", kMount.ToStringView(), name);
            }

        private:
            static inline const PoolString kMount = "artgen_panel_sounds"_intern;
            TempGame _game;
        };

    }  // namespace

    /// The Generate panel's model over a session, a control service and a clock of its own; the bridge and the control API's start are fakes
    class ArtGenPanelModelTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

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

        const ArtGenRunView& RunView() {
            model.Update();
            return model.GetView().run;
        }

        const ArtGenRefView& Ref() {
            model.Update();
            return model.GetView().ref;
        }

        const ArtGenAcceptView& Accept() {
            model.Update();
            return model.GetView().accept;
        }

        /// A run of the preset `wood` to its end with images at the paths @p files, seeds 1, 2, ...; its id
        uint32_t RunWithImages(const Vector<String>& files) {
            const uint32_t id = Require(session.Request("wood", "White Knight", static_cast<int32_t>(files.size()), Optional<int64_t>(1)));
            Call("artgen_poll", kCatalogPoll);
            for (size_t i = 0; i < files.size(); ++i) {
                Call("artgen_image", Report(id, Format(R"("path":"{}","seed":{})", files[i], i + 1)));
            }
            Call("artgen_done", Report(id, R"("status":"ok")"));
            return id;
        }

        static int32_t ModeRow(StringView mode) {
            for (size_t i = 0; i < kArtGenReferenceModes.size(); ++i) {
                if (kArtGenReferenceModes[i].mode == mode) {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        /// A run of the preset `wood` to its end with the sound variants at @p files, seeds 1, 2, ...
        void RunWithSounds(const Vector<String>& files) {
            const uint32_t id = Require(session.Request("wood", "Knight Move", static_cast<int32_t>(files.size()), Optional<int64_t>(1)));
            Call("artgen_poll", kCatalogPoll);
            for (size_t i = 0; i < files.size(); ++i) {
                Call("artgen_image", Report(id, Format(R"("kind":"audio","path":"{}","seed":{})", files[i], i + 1)));
            }
            Call("artgen_done", Report(id, R"("status":"ok")"));
        }

        const ArtGenSoundAcceptView& SoundAccept() {
            model.Update();
            return model.GetView().soundAccept;
        }

        /// A click of the variants slot on the tile of @p seed, applied at the end of the frame
        void Pick(int64_t seed) {
            model.Update();
            model.NotePick(seed);
            model.EndUpdate();
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

    TEST_F(ArtGenPanelModelTest, UseFileFillsTheSlotOnlyWithAFileThatExists) {
        const TempGame game;
        session.SetGameRoot(game.Root().View());
        model.GetMutableView().ref.typedPath = "sketch.png";
        Ref();
        EXPECT_TRUE(Run(EditorCommandIds::kArtGenReference, ArtGenPanelArgs::kUseFile));
        EXPECT_TRUE(Ref().fileMissing);
        EXPECT_FALSE(Ref().has);
        model.GetMutableView().ref.typedPath = "sketch2.png";
        EXPECT_FALSE(Ref().fileMissing) << "the note goes when the text changes";

        game.SavePng("sketch2.png", true);
        Run(EditorCommandIds::kArtGenReference, ArtGenPanelArgs::kUseFile);
        ASSERT_TRUE(Ref().has);
        EXPECT_EQ(Ref().name, "sketch2.png");
        EXPECT_EQ(Ref().mode, "structure") << "an empty slot takes a file as the shape";
        EXPECT_FLOAT_EQ(Ref().weight, static_cast<float>(ArtGenReferenceDefaultWeight("structure")));

        model.GetMutableView().ref.weight = 1.2f;
        Ref();
        EXPECT_NEAR(session.GetReferenceSlot()->weight.value_or(0.0), 1.2, 1e-6);
        model.GetMutableView().ref.modeIndex = ModeRow("init");
        EXPECT_EQ(Ref().mode, "init");
        EXPECT_FALSE(session.GetReferenceSlot()->weight.has_value()) << "a mode change drops the weight";

        Run(EditorCommandIds::kArtGenReference, ArtGenPanelArgs::kClear);
        EXPECT_FALSE(Ref().has);
        EXPECT_FALSE(session.GetReferenceSlot().has_value());
    }

    TEST_F(ArtGenPanelModelTest, GenerateNeedsASubjectAndSendsASavedPresetByNameAndAnEditedOneAsADraft) {
        Catalog();
        EXPECT_FALSE(RunView().canGenerate);
        Run(EditorCommandIds::kArtGenRun, ArtGenPanelArgs::kGenerate);
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Idle);

        model.GetMutableView().run.subject = "queen";
        model.GetMutableView().run.count = 99;
        model.GetMutableView().run.randomSeed = false;
        model.GetMutableView().run.seed = -5;
        EXPECT_TRUE(RunView().canGenerate);
        EXPECT_EQ(RunView().count, ArtGenSession::kMaxCount);
        EXPECT_EQ(RunView().seed, 0);
        Run(EditorCommandIds::kArtGenRun, ArtGenPanelArgs::kGenerate);
        EXPECT_EQ(session.GetRun().preset, "wood");
        EXPECT_TRUE(session.GetRun().presetData.empty());
        EXPECT_EQ(session.GetRun().count, ArtGenSession::kMaxCount);
        EXPECT_EQ(session.GetRun().seed, Optional<int64_t>(0));
        EXPECT_TRUE(RunView().singleRunInWork);
        EXPECT_FALSE(RunView().canGenerate);

        Run(EditorCommandIds::kArtGenRun, ArtGenPanelArgs::kCancel);
        EXPECT_FALSE(session.IsRunInWork());
        Edit().title = "Carved";
        Preset();
        Run(EditorCommandIds::kArtGenRun, ArtGenPanelArgs::kGenerate);
        EXPECT_FALSE(session.GetRun().presetData.empty()) << "an edited preset goes inline";
        EXPECT_FALSE(RunView().hasRequestError);
    }

    TEST_F(ArtGenPanelModelTest, TheRunStateShowsItsProgressAndItsEnd) {
        Catalog();
        const uint32_t id = Require(session.Request("wood", "queen", 2, Optional<int64_t>(1)));
        EXPECT_EQ(RunView().state, ArtGenRunState::Queued);
        Call("artgen_poll", kCatalogPoll);
        Call("artgen_progress", Report(id, R"("image":0,"step":5,"steps":25)"));
        EXPECT_EQ(RunView().state, ArtGenRunState::Running);
        EXPECT_FLOAT_EQ(RunView().progress, 0.1f);
        EXPECT_EQ(RunView().progressText, "image 1 of 2, step 5 of 25");
        Call("artgen_image", Report(id, R"("path":"D:/out/queen_1.png","seed":1)"));
        Call("artgen_done", Report(id, R"("error":"out of memory","status":"failed")"));
        EXPECT_EQ(RunView().state, ArtGenRunState::Failed);
        EXPECT_EQ(RunView().failedText, "Failed: out of memory");
        EXPECT_TRUE(RunView().hasImages);
        EXPECT_FALSE(RunView().hasSounds);
    }

    TEST_F(ArtGenPanelModelTest, ANotedPickSelectsAtTheEndOfTheFrameAndFillsTheName) {
        const TempGame game;
        Catalog();
        RunWithImages({game.SavePng("a.png", true), game.SavePng("b.png", false)});
        EXPECT_EQ(RunView().doneText, "2 image(s) of \"White Knight\"");
        EXPECT_FALSE(Accept().hasPick);

        model.Update();
        model.NotePick(1);
        EXPECT_FALSE(model.GetSelectedSeed().has_value()) << "a click is applied after the window";
        model.EndUpdate();
        EXPECT_EQ(model.GetSelectedSeed(), Optional<int64_t>(1));
        EXPECT_TRUE(Accept().hasPick);
        EXPECT_EQ(Accept().seedText, "seed 1");
        EXPECT_EQ(Accept().sprite, "white_knight");
        EXPECT_TRUE(Accept().spriteValid);
        EXPECT_EQ(Accept().summary.find(Format("drawn {} x {}", TempGame::kSide, TempGame::kSide)), 0u);
        EXPECT_EQ(Accept().summaryStyle, PanelTextStyle::Disabled);

        Pick(2);
        EXPECT_EQ(Accept().summary, "Nothing is drawn: Accept would refuse this picture");
        EXPECT_EQ(Accept().summaryStyle, PanelTextStyle::Error);

        model.Update();
        model.NoteDice(2);
        model.EndUpdate();
        EXPECT_FALSE(RunView().randomSeed);
        EXPECT_EQ(RunView().seed, 2);
    }

    TEST_F(ArtGenPanelModelTest, AcceptWritesTheMessageAndTheSpriteBecomesAReference) {
        const TempGame game;
        session.SetAcceptor(&FakeAcceptor);
        gAccepted = AcceptCall{};
        Catalog();
        RunWithImages({game.SavePng("a.png", true)});
        Pick(1);
        model.GetMutableView().accept.sprite = "bad name";
        EXPECT_TRUE(Accept().nameWarning);
        Run(EditorCommandIds::kArtGenAccept, ArtGenPanelArgs::kImage);
        EXPECT_EQ(gAccepted.calls, 0) << "an invalid name is never accepted";

        model.GetMutableView().accept.sprite = "icon";
        model.GetMutableView().accept.useOnSelected = true;
        EXPECT_FALSE(Accept().useOnSelected) << "cleared without a selected node";
        Run(EditorCommandIds::kArtGenAccept, ArtGenPanelArgs::kImage);
        EXPECT_EQ(gAccepted.calls, 1);
        EXPECT_EQ(gAccepted.seed, 1);
        EXPECT_EQ(gAccepted.sprite, "icon");
        EXPECT_EQ(Accept().message, "Accepted as 'icon' (assets/artgen/icon.png); 1 sprite(s) reloaded");
        EXPECT_EQ(Accept().messageStyle, PanelTextStyle::Success);
        EXPECT_TRUE(Accept().hasAccepted);

        Run(EditorCommandIds::kArtGenReference, ArtGenPanelArgs::kUseShape);
        ASSERT_TRUE(session.GetReferenceSlot().has_value());
        EXPECT_EQ(session.GetReferenceSlot()->mode, "structure");
        EXPECT_EQ(session.GetReferenceSlot()->image, ArtGenReferenceOfSprite("icon").image);

        Pick(1);
        EXPECT_TRUE(Accept().hasMessage) << "picking the same variant keeps the answer";
    }

    TEST_F(ArtGenPanelModelTest, ADroppedVariantIsAnsweredWhileThePanelIsClosed) {
        const TempGame game;
        session.SetAcceptor(&FakeAcceptor);
        gAccepted = AcceptCall{};
        Catalog();
        RunWithImages({game.SavePng("a.png", true)});
        Edit().accept.maxSizeIndex = 0;
        model.Update();

        ArtGenDropResult result;
        ArtGenVariantDropEvent::Emit(MakeArtGenDragPayload(ArtGenDragKind::Image, 1, {}, {}, "knight"), StringView("knight"), &result);
        EXPECT_TRUE(result.ok);
        EXPECT_EQ(gAccepted.sprite, "knight");
        EXPECT_EQ(gAccepted.settings.maxSize, 0) << "the form's accept settings, as the Accept button";
        EXPECT_EQ(Accept().message, result.message);

        ArtGenDropResult sound;
        ArtGenVariantDropEvent::Emit(MakeArtGenDragPayload(ArtGenDragKind::Audio, 1, {}, {}, "knight"), StringView("knight"), &sound);
        EXPECT_FALSE(sound.ok);
        EXPECT_EQ(sound.message, kArtGenSoundIsNotASprite);
    }

    TEST_F(ArtGenPanelModelTest, ASteadyUpdateWithARunAndAPickAllocatesNothing) {
        const TempGame game;
        Catalog();
        RunWithImages({game.SavePng("a.png", true)});
        Pick(1);
        model.GetMutableView().run.subject = "queen";
        model.Update();
        ExpectNoAllocations([&] {
            for (int frame = 0; frame < kSteadyFrames; ++frame) {
                model.Update();
                model.EndUpdate();
            }
        });
    }

    TEST_F(ArtGenPanelModelTest, TheSoundSettingsAreClampedAndAChangeDropsThePreview) {
        const SoundFolder sounds;
        Catalog();
        RunWithSounds({sounds.Save("move_1.wav")});
        EXPECT_TRUE(RunView().hasSounds);
        Pick(1);
        ASSERT_TRUE(SoundAccept().hasPick);
        ASSERT_TRUE(SoundAccept().canListen) << "the picked sound is loaded";
        EXPECT_TRUE(Run(EditorCommandIds::kArtGenListen, ""));
        EXPECT_TRUE(SoundAccept().hasPreview);
        EXPECT_NE(SoundAccept().previewText.find("Hz, mono"), String::npos);

        ArtGenSoundView& sound = model.GetMutableView().sound;
        sound.fadeIn = ArtGenSoundSettings::kMaxFadeMs + 1.0f;
        model.Update();
        EXPECT_FLOAT_EQ(model.GetView().sound.fadeIn, ArtGenSoundSettings::kMaxFadeMs);
        EXPECT_FALSE(SoundAccept().hasPreview) << "what was heard is no longer what Accept writes";
        EXPECT_NE(model.GetPlayingRow(), ArtGenPanelModel::kPreviewRow);

        sound.trim = false;
        sound.peakDb = ArtGenSoundSettings::kMaxPeakDb + 3.0f;
        sound.rateIndex = 1;
        model.Update();
        EXPECT_FALSE(model.GetView().sound.trim);
        EXPECT_FLOAT_EQ(model.GetView().sound.peakDb, ArtGenSoundSettings::kMaxPeakDb);
        EXPECT_EQ(model.GetView().sound.rates[1], Format("{} Hz", ArtGenSoundSettings::kRates[1]));
        sound.trim = true;
        model.Update();
        EXPECT_FLOAT_EQ(model.GetView().sound.trimDb, ArtGenSoundSettings::kDefaultTrimDb) << "switched on, the trim starts from the default";
    }

    TEST_F(ArtGenPanelModelTest, ASoundIsAcceptedUnderANameOrOverAClipOfTheGame) {
        const SoundFolder sounds;
        session.SetSoundAcceptor(&FakeSoundAcceptor);
        gSoundAccepted = SoundAcceptCall{};
        Catalog();
        RunWithSounds({sounds.Save("move_1.wav")});
        Pick(1);
        EXPECT_EQ(Accept().sprite, "knight_move");
        EXPECT_TRUE(SoundAccept().canAccept);
        EXPECT_NE(SoundAccept().acceptTip.find("assets/artgen/<name>.wav"), String::npos);

        model.GetMutableView().soundAccept.replaceClip = true;
        model.GetMutableView().soundAccept.clipPath = "sounds/move.wav";
        EXPECT_FALSE(SoundAccept().clipValid);
        EXPECT_FALSE(SoundAccept().canAccept);
        Run(EditorCommandIds::kArtGenAccept, ArtGenPanelArgs::kSound);
        EXPECT_EQ(gSoundAccepted.calls, 0);

        model.GetMutableView().soundAccept.clipPath = "assets/sounds/move.wav";
        EXPECT_TRUE(SoundAccept().canAccept);
        EXPECT_NE(SoundAccept().acceptTip.find("over the game's clip assets/sounds/move.wav"), String::npos);
        Run(EditorCommandIds::kArtGenAccept, ArtGenPanelArgs::kSound);
        EXPECT_EQ(gSoundAccepted.calls, 1);
        EXPECT_EQ(gSoundAccepted.target.clip, "assets/sounds/move.wav");
        EXPECT_EQ(Accept().message, "Accepted as assets/sounds/move.wav; 0 source(s) reloaded");
    }

    TEST_F(ArtGenPanelModelTest, TheSetTabsSoundStopsInAFrameThatDidNotDrawItAndOnClose) {
        DecodedAudio audio;
        audio.format = AudioFormat{kSoundRate, 1};
        audio.samples.assign(kSoundFrames, kSoundLevel);
        const IntrusivePtr<AudioClip> clip = AudioClip::Create("artgen-panel-test"_intern, std::move(audio));

        model.PlaySetTabSound(*clip);
        EXPECT_EQ(model.GetPlayingRow(), ArtGenPanelModel::kSetTabRow);
        model.Update();
        model.EndUpdate();
        EXPECT_EQ(model.GetPlayingRow(), -1) << "another tab was shown";

        model.PlaySetTabSound(*clip);
        model.OnOpenChanged(false);
        EXPECT_EQ(model.GetPlayingRow(), -1);
    }

    TEST_F(ArtGenPanelModelTest, ASteadyUpdateWithSoundsAllocatesNothing) {
        const SoundFolder sounds;
        Catalog();
        RunWithSounds({sounds.Save("move_1.wav")});
        Pick(1);
        model.GetMutableView().soundAccept.replaceClip = true;
        model.Update();
        ExpectNoAllocations([&] {
            for (int frame = 0; frame < kSteadyFrames; ++frame) {
                model.Update();
                model.EndUpdate();
            }
        });
    }

    TEST_F(ArtGenPanelModelTest, TheProgressSourceFollowsARunTheServiceWorksOn) {
        EditorProgressRegistry& progress = EditorProgressRegistry::GetInstance();
        Catalog();
        EXPECT_FALSE(progress.Collect().first);

        const uint32_t id = Require(session.Request("wood", "queen", 2, Optional<int64_t>(1)));
        const EditorProgressSummary queued = progress.Collect();
        ASSERT_TRUE(queued.first);
        EXPECT_FALSE(queued.first->fraction) << "nothing is made yet";

        Call("artgen_poll", kCatalogPoll);
        Call("artgen_progress", Report(id, R"("image":0,"step":5,"steps":25)"));
        const EditorProgressSummary running = progress.Collect();
        ASSERT_TRUE(running.first);
        ASSERT_TRUE(running.first->fraction);
        EXPECT_FLOAT_EQ(*running.first->fraction, 0.1f);

        now += ArtGenSession::kServiceTimeoutSeconds + 1.0;
        EXPECT_FALSE(progress.Collect().first) << "a silent service works on nothing";

        Call("artgen_poll", kCatalogPoll);
        EXPECT_TRUE(progress.Collect().first);
        Call("artgen_done", Report(id, R"("status":"ok")"));
        EXPECT_FALSE(progress.Collect().first);
    }

    TEST_F(ArtGenPanelModelTest, TheProgressSourceFollowsASetupAction) {
        EditorProgressRegistry& progress = EditorProgressRegistry::GetInstance();
        Call("artgen_poll", R"({"state":"not_installed"})");
        const uint32_t id = Require(session.RequestSetup(ArtGenSetupAction::Install));
        Call("artgen_poll", R"({"state":"not_installed"})");
        const EditorProgressSummary starting = progress.Collect();
        ASSERT_TRUE(starting.first);
        EXPECT_FALSE(starting.first->fraction) << "no total reported yet";

        Call("artgen_setup_report", Report(id, R"("done":2000000,"event":"progress","total":8000000)"));
        const EditorProgressSummary downloading = progress.Collect();
        ASSERT_TRUE(downloading.first);
        ASSERT_TRUE(downloading.first->fraction);
        EXPECT_FLOAT_EQ(*downloading.first->fraction, 0.25f);

        Call("artgen_setup_report", Report(id, R"("event":"done","ok":true)"));
        EXPECT_FALSE(progress.Collect().first);
    }

}  // namespace BECore::Tests
