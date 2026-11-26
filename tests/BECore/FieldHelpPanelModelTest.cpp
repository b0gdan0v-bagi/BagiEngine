// The model of the Field Help panel over help files in layers of its own: the rows, where an edit goes, Save and Revert.

#include <BECore/FieldHelpTest.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Events/ConfigEvents.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/Feedback/ToastEvents.h>
#include <Widgets/SceneEditor/FieldHelpEvents.h>
#include <Widgets/SceneEditor/FieldHelpPanelModel.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kProbe = "HelpProbeComponent";
        constexpr StringView kBase = "HelpProbeBase";
        constexpr StringView kEngineFile = "FieldHelpConfig.xml";
        constexpr StringView kGameFile = "GameFieldHelpConfig.xml";
        constexpr int kSteadyFrames = 10;

        String Entry(StringView component, StringView field, StringView text) {
            return Format("<item component=\"{}\" field=\"{}\" text=\"{}\"/>", component, field, text);
        }

        String HelpFile(StringView entries) {
            return Format("<root><entries>{}</entries></root>", entries);
        }

        struct HelpEvents : SubscriptionHolder {
            HelpEvents() {
                Subscribe<FieldHelpSavedEvent, &HelpEvents::OnSaved>(this);
                Subscribe<EditorToastEvent, &HelpEvents::OnToast>(this);
            }

            void OnSaved(const FieldHelpSavedEvent& /*event*/) {
                ++saved;
            }

            void OnToast(const EditorToastEvent& event) {
                toasts.push_back(event.text);
            }

            int saved = 0;
            Vector<String> toasts;
        };

        class FieldHelpPanelModelTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                MakeDirectory(_engine / "config");
                MakeDirectory(_game / "config");
            }

            [[nodiscard]] Path EngineFile() const {
                return _engine / "config" / kEngineFile;
            }

            [[nodiscard]] Path GameFile() const {
                return _game / "config" / kGameFile;
            }

            /// The model over the layers as they are now, showing the probe component; without @p game the engine's layer is the only one
            void Start(bool game = true) {
                _files.AddContentRoot(_engine);
                if (game) {
                    _files.AddContentRoot(_game);
                }
                _files.Initialize();
                _model = New<FieldHelpPanelModel>();
                _model->SetFileSystem(_files);
                _model->Initialize();
                _model->Update();
                Choose(kProbe);
            }

            FieldHelpPanelView& View() {
                return _model->GetView();
            }

            void Choose(StringView component) {
                for (size_t i = 0; i < View().classes.size(); ++i) {
                    if (StringView(View().classes[i]) == component) {
                        View().classIndex = static_cast<int32_t>(i);
                        _model->Update();
                        return;
                    }
                }
                FAIL() << "no class " << String(component).c_str();
            }

            const FieldHelpRow& Row(StringView field) {
                for (const FieldHelpRow& row : View().rows) {
                    if (StringView(row.name) == field) {
                        return row;
                    }
                }
                ADD_FAILURE() << "no row " << String(field).c_str();
                static const FieldHelpRow none;
                return none;
            }

            void Command(PoolString id, StringView argument = {}) {
                EXPECT_TRUE(_model->RunCommand(id.ToStringView(), argument));
            }

            void Select(StringView field) {
                Command(EditorCommandIds::kFieldHelpSelect, field);
            }

            /// What the multiline field does, and the frame after it
            void Type(StringView text) {
                View().description.assign(text.data(), text.size());
                _model->Update();
            }

            void Save() {
                Command(EditorCommandIds::kFieldHelpSave);
            }

            /// What the table of another reader finds on disk
            void ExpectOnDisk(StringView field, StringView text, FieldHelpSource source) {
                FieldHelp help(_files);
                help.Reload();
                const FieldHelpAnswer answer = help.Find(kProbe, field);
                EXPECT_EQ(answer.text, text) << String(field).c_str();
                EXPECT_EQ(answer.source, source) << String(field).c_str();
            }

            /// The `editor_state` rows of the two help files
            static Vector<EditorDocumentState> Documents() {
                Vector<EditorDocumentState> found;
                for (EditorDocumentState& state : CoreManager::GetEditorDocuments().Collect()) {
                    if (state.name.find("FieldHelpConfig") != String::npos) {
                        found.push_back(std::move(state));
                    }
                }
                return found;
            }

            TestTempFolder _folder{"FieldHelpPanel"};
            Path _engine{_folder.PathOf("engine")};
            Path _game{_folder.PathOf("game")};
            FileSystem _files;
            IntrusivePtr<FieldHelpPanelModel> _model;
            HelpEvents _events;
        };

    }  // namespace

    TEST_F(FieldHelpPanelModelTest, ARowSaysWhatAFieldIsAndWhereItsDescriptionComesFrom) {
        const String longLine(100, 'a');
        WriteFile(EngineFile(), HelpFile(Entry(kBase, "active", "from the engine") + Entry(kProbe, "title", Format("{}&#10;second", longLine))));
        WriteFile(GameFile(), HelpFile(Entry(kProbe, "count", "first&#10;second")));
        ASSERT_NO_FATAL_FAILURE(Start());

        EXPECT_TRUE(View().hasRows);
        const FieldHelpRow& speed = Row("speed");
        EXPECT_EQ(speed.type, "float");
        EXPECT_EQ(speed.range, "0 .. 10");
        EXPECT_EQ(speed.defaultText, "1");
        EXPECT_EQ(speed.description, "How fast it goes");
        EXPECT_EQ(speed.source, "attribute");
        EXPECT_EQ(Row("active").source, "engine");
        EXPECT_EQ(Row("count").source, "game");
        EXPECT_EQ(Row("count").range, "");
        EXPECT_EQ(Row("count").description, "first...") << "a cell shows one line";
        EXPECT_EQ(Row("count").fullDescription, "first\nsecond");
        EXPECT_LT(Row("title").description.size(), longLine.size());
        EXPECT_EQ(StringView(Row("title").fullDescription).substr(0, longLine.size()), StringView(longLine));

        Choose(kBase);
        EXPECT_EQ(Row("speed").source, "attribute");
        for (const FieldHelpRow& row : View().rows) {
            EXPECT_NE(row.name, "count") << "the rows are of the chosen class";
        }
    }

    TEST_F(FieldHelpPanelModelTest, ANewEntryGoesToTheGameWhenTheGameHasALayerAndToTheChosenFile) {
        ASSERT_NO_FATAL_FAILURE(Start());
        EXPECT_EQ(View().newEntryFile, FieldHelpFile::Game);
        EXPECT_EQ(Row("count").source, "none");

        Select("count");
        EXPECT_TRUE(View().hasSelection);
        EXPECT_EQ(View().description, "");
        EXPECT_TRUE(Row("count").selected);
        Type("how many");
        EXPECT_EQ(Row("count").source, "game");
        EXPECT_EQ(Row("count").description, "how many");
        EXPECT_TRUE(View().dirty);

        View().newEntryFile = FieldHelpFile::Engine;
        Type("how many of them");
        EXPECT_EQ(Row("count").source, "game") << "an entry stays in the file that holds it";
        Select("title");
        Type("the title");
        EXPECT_EQ(Row("title").source, "engine");
    }

    TEST_F(FieldHelpPanelModelTest, WithTheEnginesLayerAloneANewEntryIsTheEngines) {
        ASSERT_NO_FATAL_FAILURE(Start(false));

        EXPECT_EQ(View().newEntryFile, FieldHelpFile::Engine);
    }

    TEST_F(FieldHelpPanelModelTest, AnEmptiedEntryIsRemovedAndTheTextAfterItGoesWhereItWas) {
        WriteFile(EngineFile(), HelpFile(Entry(kProbe, "active", "engine's")));
        WriteFile(GameFile(), HelpFile(Entry(kProbe, "active", "game's")));
        ASSERT_NO_FATAL_FAILURE(Start());
        View().newEntryFile = FieldHelpFile::Engine;
        Select("active");
        EXPECT_EQ(View().description, "game's");

        Type("");
        EXPECT_EQ(Row("active").source, "engine") << "what is behind the entry answers again";
        EXPECT_EQ(View().description, "") << "the field is not filled again under the writer";
        Type("game's, rewritten");
        EXPECT_EQ(Row("active").source, "game");
        Save();

        ExpectOnDisk("active", "game's, rewritten", FieldHelpSource::Game);
        EXPECT_NE(ReadFile(EngineFile()).find("engine's"), String::npos);
    }

    TEST_F(FieldHelpPanelModelTest, ANewEntryThatIsEmptiedAgainLeavesNothingToSave) {
        ASSERT_NO_FATAL_FAILURE(Start());
        Select("count");
        Type("x");
        Type("");

        EXPECT_FALSE(View().dirty);
        EXPECT_TRUE(Documents().empty());
        View().newEntryFile = FieldHelpFile::Engine;
        Type("y");
        EXPECT_EQ(Row("count").source, "engine") << "it is a new entry again";
    }

    TEST_F(FieldHelpPanelModelTest, AnInheritedTextIsShownAndAnEditOfItIsTheClasssOwnEntry) {
        WriteFile(EngineFile(), HelpFile(Entry(kBase, "active", "of the base")));
        ASSERT_NO_FATAL_FAILURE(Start());
        Select("active");
        EXPECT_EQ(View().description, "of the base");
        EXPECT_FALSE(View().dirty) << "a selection changes nothing";

        Type("of the probe");
        Save();

        ExpectOnDisk("active", "of the probe", FieldHelpSource::Game);
        EXPECT_NE(ReadFile(EngineFile()).find("of the base"), String::npos);
    }

    TEST_F(FieldHelpPanelModelTest, SaveMakesTheGameFileAndTellsTheEditor) {
        ASSERT_NO_FATAL_FAILURE(Start());
        Select("count");
        View().description = "how many";

        // The click of Save arrives before the next `Update`
        Save();

        EXPECT_EQ(_events.saved, 1);
        EXPECT_FALSE(View().dirty);
        EXPECT_FALSE(PathExists(EngineFile())) << "only a changed file is written";
        ExpectOnDisk("count", "how many", FieldHelpSource::Game);
        Save();
        EXPECT_EQ(_events.saved, 1) << "there was nothing to write";
        EXPECT_TRUE(_events.toasts.empty());
    }

    TEST_F(FieldHelpPanelModelTest, AnUnsavedFileIsADocumentOfTheEditor) {
        ASSERT_NO_FATAL_FAILURE(Start());
        EXPECT_TRUE(Documents().empty());

        Select("count");
        Type("how many");

        const Vector<EditorDocumentState> documents = Documents();
        ASSERT_EQ(documents.size(), 1u);
        const EditorDocumentState& row = documents.front();
        EXPECT_EQ(row.id, "config:GameFieldHelpConfig");
        EXPECT_EQ(row.kind, EditorDocumentKind::Config);
        EXPECT_TRUE(row.dirty);
        EXPECT_EQ(row.path, "config/GameFieldHelpConfig.xml");
        EXPECT_EQ(StringView(row.file), _files.WriteTarget(GameFile()).View()) << "also before the first save";

        Save();
        EXPECT_TRUE(Documents().empty());
    }

    TEST_F(FieldHelpPanelModelTest, RevertDropsTheChangesAndReadsTheFiles) {
        WriteFile(GameFile(), HelpFile(Entry(kProbe, "count", "saved")));
        ASSERT_NO_FATAL_FAILURE(Start());
        Select("count");
        Type("typed");
        WriteFile(GameFile(), HelpFile(Entry(kProbe, "count", "changed on disk")));

        Command(EditorCommandIds::kFieldHelpRevert);

        EXPECT_FALSE(View().dirty);
        EXPECT_EQ(View().description, "changed on disk");
        EXPECT_EQ(Row("count").description, "changed on disk");
        EXPECT_EQ(_events.saved, 0);
    }

    TEST_F(FieldHelpPanelModelTest, AChangeOnDiskIsReadOnlyWhileNothingIsUnsaved) {
        ASSERT_NO_FATAL_FAILURE(Start());
        Select("count");
        WriteFile(GameFile(), HelpFile(Entry(kProbe, "count", "from outside")));
        ConfigEvents::ConfigChangedEvent::Emit(PoolString("SomeOtherConfig"_intern));
        _model->Update();
        EXPECT_EQ(View().description, "");

        ConfigEvents::ConfigChangedEvent::Emit(FieldHelp::ConfigName(FieldHelpFile::Game));
        _model->Update();
        EXPECT_EQ(View().description, "from outside");

        Type("typed");
        WriteFile(GameFile(), HelpFile(Entry(kProbe, "count", "again")));
        ConfigEvents::ConfigChangedEvent::Emit(FieldHelp::ConfigName(FieldHelpFile::Game));
        _model->OnOpenChanged(true);
        _model->Update();
        EXPECT_EQ(View().description, "typed");
        EXPECT_TRUE(View().dirty);
    }

    TEST_F(FieldHelpPanelModelTest, ASaveThatFailsIsAToastAndTheChangeWaits) {
        const String broken = "<root><entries>";
        WriteFile(GameFile(), broken);
        ASSERT_NO_FATAL_FAILURE(Start());
        Select("count");
        Type("how many");

        Save();

        EXPECT_TRUE(View().dirty);
        EXPECT_EQ(_events.saved, 0);
        ASSERT_EQ(_events.toasts.size(), 1u);
        EXPECT_NE(_events.toasts.front().find("GameFieldHelpConfig.xml"), String::npos) << _events.toasts.front().c_str();
        EXPECT_EQ(ReadFile(GameFile()), broken);
    }

    TEST_F(FieldHelpPanelModelTest, OnlyItsOwnCommandsAndTheFieldsOfTheClassAreTaken) {
        ASSERT_NO_FATAL_FAILURE(Start());

        EXPECT_FALSE(_model->RunCommand("activity.clear", {}));
        Select("no such field");
        EXPECT_FALSE(View().hasSelection);
        Select("count");
        Choose(kBase);
        EXPECT_FALSE(View().hasSelection) << "another class has no selection yet";
    }

    TEST_F(FieldHelpPanelModelTest, ASettledFrameAllocatesNothing) {
        WriteFile(GameFile(), HelpFile(Entry(kProbe, "count", "how many")));
        ASSERT_NO_FATAL_FAILURE(Start());
        Select("count");
        _model->Update();

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                _model->Update();
            }
        });
    }

}  // namespace BECore::Tests
