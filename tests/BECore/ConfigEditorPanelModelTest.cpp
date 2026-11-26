#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/XmlDocument.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Logger/ConsoleSink.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/FreshText.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/ConfigEditor/ConfigEditorPanelModel.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        const PoolString kPanelConfig = "PanelConfig"_intern;
        const PoolString kOtherPanelConfig = "OtherPanelConfig"_intern;
        constexpr StringView kPanelText = "<PanelConfig value=\"one\"/>\n";
        constexpr StringView kOtherPanelText = "<OtherPanelConfig value=\"other\"/>\n";
        constexpr StringView kValueKey = "root@value";

        /// A config with a registered reflected type, so it opens as objects
        const PoolString kTypedConfig = "LoggerConfig"_intern;
        constexpr StringView kTypedText = "<root>\n    <!-- the console -->\n    <sinks>\n        <sink type=\"ConsoleSink\" priority=\"1\"/>\n    </sinks>\n</root>\n";

    }  // namespace

    /// The Config Editor's model over a manager of its own on a `config` folder in a temporary directory: the engine's files are never written
    class ConfigEditorPanelModelTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("cfg_editor_model");
            MakeEmptyDirectory(_dir);
            WriteFile(_dir / "PanelConfig.xml", kPanelText);
            WriteFile(_dir / "OtherPanelConfig.xml", kOtherPanelText);
            WriteMoreConfigs();
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
            _manager.Initialize();

            _model = New<ConfigEditorPanelModel>();
            _model->Initialize();
            _model->UseConfigManager(&_manager);
            _model->Update();
        }

        void TearDown() override {
            _model.Reset();
            RemoveDirectory(_dir);
        }

        virtual void WriteMoreConfigs() {}

        const ConfigEditorView& View() const {
            return _model->GetView();
        }

        bool SelectText(StringView name) {
            return _model->RunCommand(PoolString(EditorCommandIds::kConfigEditorSelect).ToStringView(), name);
        }

        void Select(PoolString name) {
            ASSERT_TRUE(SelectText(name.ToStringView()));
        }

        void Run(PoolString command) {
            ASSERT_TRUE(_model->RunCommand(command.ToStringView(), ""));
        }

        void StageNewValue(StringView value) {
            const XmlNode root = _manager.GetConfig(kPanelConfig);
            _model->GetStagedChanges().Set(String{kValueKey.data(), kValueKey.size()}, root.GetPugiNode().attribute("value"), value);
            _model->Update();
        }

        String ValueOnDisk() const {
            return ReadFile(_dir / "PanelConfig.xml");
        }

        Path _dir;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        IntrusivePtr<ConfigEditorPanelModel> _model;
    };

    TEST_F(ConfigEditorPanelModelTest, WithNoSelectionTheViewListsTheConfigsAndShowsNoDetail) {
        EXPECT_TRUE(View().noSelection);
        EXPECT_FALSE(View().hasSelection);
        EXPECT_FALSE(View().showTree);
        EXPECT_FALSE(View().configMissing);
        EXPECT_EQ(View().configsCount, "(2)");
        ASSERT_EQ(View().configs.size(), 2u);
        EXPECT_EQ(View().configs[0].name, "OtherPanelConfig");
        EXPECT_EQ(View().configs[1].name, "PanelConfig");
        EXPECT_FALSE(View().configs[0].selected);
        EXPECT_FALSE(View().configs[1].selected);
    }

    TEST_F(ConfigEditorPanelModelTest, ASelectionShowsTheNameThePathAndTheTree) {
        Select(kPanelConfig);

        EXPECT_TRUE(View().hasSelection);
        EXPECT_FALSE(View().noSelection);
        EXPECT_EQ(View().configName, "PanelConfig");
        EXPECT_TRUE(View().hasPath);
        EXPECT_TRUE(StringView(View().filePathText).starts_with("("));
        EXPECT_NE(View().filePathText.find("PanelConfig.xml"), String::npos);
        EXPECT_TRUE(View().showTree);
        EXPECT_FALSE(View().configMissing);
        EXPECT_FALSE(View().hasStaged);
        EXPECT_TRUE(View().configs[1].selected);
        EXPECT_FALSE(View().configs[0].selected);
    }

    TEST_F(ConfigEditorPanelModelTest, ASelectedConfigWithNoDocumentShowsTheEmptyStateInsteadOfTheTree) {
        _model->SelectConfig(PoolString::Intern(FreshText("NoSuchConfig")));
        _model->Update();

        EXPECT_TRUE(View().configMissing);
        EXPECT_FALSE(View().showTree);
        EXPECT_FALSE(View().hasPath);
    }

    TEST_F(ConfigEditorPanelModelTest, ANameThatIsNoLoadedConfigSelectsNothingAndIsNeverInterned) {
        const String stranger = FreshText("Stranger");

        EXPECT_TRUE(SelectText(stranger));
        EXPECT_FALSE(PoolString::Find(StringView(stranger)).has_value());
        EXPECT_TRUE(SelectText(""));

        EXPECT_FALSE(View().hasSelection);
    }

    TEST_F(ConfigEditorPanelModelTest, SwitchingWithStagedEditsAsksFirstAndDiscardSwitches) {
        Select(kPanelConfig);
        StageNewValue("two");

        Select(kOtherPanelConfig);

        EXPECT_TRUE(View().switchConfirmOpen);
        EXPECT_EQ(View().configName, "PanelConfig") << "the selection waits for the answer";
        EXPECT_TRUE(View().hasStaged);

        Run(EditorCommandIds::kConfigEditorSwitchDiscard);

        EXPECT_FALSE(View().switchConfirmOpen);
        EXPECT_EQ(View().configName, "OtherPanelConfig");
        EXPECT_FALSE(View().hasStaged);
        EXPECT_TRUE(_model->GetStagedChanges().IsEmpty());
    }

    TEST_F(ConfigEditorPanelModelTest, CancellingASwitchKeepsTheSelectionAndTheEdits) {
        Select(kPanelConfig);
        StageNewValue("two");
        Select(kOtherPanelConfig);

        Run(EditorCommandIds::kConfigEditorSwitchCancel);

        EXPECT_FALSE(View().switchConfirmOpen);
        EXPECT_EQ(View().configName, "PanelConfig");
        EXPECT_TRUE(View().hasStaged);
        Run(EditorCommandIds::kConfigEditorSwitchDiscard);
        EXPECT_EQ(View().configName, "PanelConfig") << "a discard after a cancel has nothing pending";
    }

    TEST_F(ConfigEditorPanelModelTest, ReloadWithNothingStagedReadsTheFileAgain) {
        Select(kPanelConfig);
        WriteFile(_dir / "PanelConfig.xml", "<PanelConfig value=\"changed\"/>\n");

        Run(EditorCommandIds::kConfigEditorReload);

        EXPECT_FALSE(View().reloadConfirmOpen);
        EXPECT_EQ(_manager.GetConfig(kPanelConfig).GetPugiNode().attribute("value").as_string(), String("changed"));
    }

    TEST_F(ConfigEditorPanelModelTest, ReloadWithStagedEditsAsksFirstAndDiscardReadsTheFile) {
        Select(kPanelConfig);
        StageNewValue("two");
        WriteFile(_dir / "PanelConfig.xml", "<PanelConfig value=\"changed\"/>\n");

        Run(EditorCommandIds::kConfigEditorReload);

        EXPECT_TRUE(View().reloadConfirmOpen);
        EXPECT_NE(View().reloadMessage.find("PanelConfig"), String::npos);
        EXPECT_EQ(_manager.GetConfig(kPanelConfig).GetPugiNode().attribute("value").as_string(), String("one")) << "nothing is read before the answer";

        Run(EditorCommandIds::kConfigEditorReloadDiscard);

        EXPECT_FALSE(View().reloadConfirmOpen);
        EXPECT_FALSE(View().hasStaged);
        EXPECT_FALSE(View().changedOnDisk) << "the panel's own reload is not news to it";
        EXPECT_EQ(_manager.GetConfig(kPanelConfig).GetPugiNode().attribute("value").as_string(), String("changed"));
    }

    TEST_F(ConfigEditorPanelModelTest, CancellingAReloadKeepsTheEdits) {
        Select(kPanelConfig);
        StageNewValue("two");
        Run(EditorCommandIds::kConfigEditorReload);

        Run(EditorCommandIds::kConfigEditorReloadCancel);

        EXPECT_FALSE(View().reloadConfirmOpen);
        EXPECT_TRUE(View().hasStaged);
    }

    TEST_F(ConfigEditorPanelModelTest, SaveWritesTheStagedEditAndClearsTheFlag) {
        Select(kPanelConfig);
        StageNewValue("two");
        ASSERT_TRUE(View().hasStaged);

        Run(EditorCommandIds::kConfigSave);

        EXPECT_NE(ValueOnDisk().find("two"), String::npos);
        EXPECT_FALSE(View().hasStaged);
        EXPECT_FALSE(View().changedOnDisk);
    }

    TEST_F(ConfigEditorPanelModelTest, SaveWithNothingStagedWritesNothing) {
        Select(kPanelConfig);
        WriteFile(_dir / "PanelConfig.xml", "<PanelConfig value=\"by hand\"/>\n");

        Run(EditorCommandIds::kConfigSave);

        EXPECT_NE(ValueOnDisk().find("by hand"), String::npos);
    }

    TEST_F(ConfigEditorPanelModelTest, AChangeOfTheSelectedConfigOnDiskShowsTheNoticeWhenEditsWereDropped) {
        Select(kPanelConfig);
        StageNewValue("two");

        EXPECT_TRUE(_model->DropStagedForChange(kPanelConfig));
        _model->Update();

        EXPECT_TRUE(View().changedOnDisk);
        EXPECT_FALSE(View().hasStaged);
    }

    TEST_F(ConfigEditorPanelModelTest, ACommandOfAnotherPanelIsNotTheModels) {
        EXPECT_FALSE(_model->RunCommand("gamePanel.reset", ""));
        EXPECT_FALSE(_model->RunCommand("", ""));
    }

    TEST_F(ConfigEditorPanelModelTest, TheTreeSlotLivesAsLongAsTheModel) {
        EXPECT_NE(PanelSlots::GetInstance().Find("configTree"), nullptr);

        _model.Reset();

        EXPECT_EQ(PanelSlots::GetInstance().Find("configTree"), nullptr);
    }

    TEST_F(ConfigEditorPanelModelTest, ASteadyUpdateAllocatesNothing) {
        Select(kPanelConfig);
        StageNewValue("two");

        ExpectNoAllocations([&] {
            for (int frame = 0; frame < 20; ++frame) {
                _model->Update();
            }
        });
    }

    class ConfigEditorTypedModelTest : public ConfigEditorPanelModelTest {
    protected:
        void WriteMoreConfigs() override {
            WriteFile(_dir / "LoggerConfig.xml", kTypedText);
        }

        /// The one sink of the open document
        ConsoleSink& Sink() {
            ReflectedConfigDocument* document = _model->GetDocument();
            EXPECT_NE(document, nullptr);
            return *static_cast<ConsoleSink*>(document->GetLists().front().elements.front().object.Get());
        }

        /// What an edit in the panel does: the object changes and the document is told
        void SetSinkPriority(int priority) {
            Sink().SetPriority(priority);
            _model->GetDocument()->NoteChange();
            _model->Update();
        }

        bool ReportsDirty() {
            Vector<EditorDocumentState> documents;
            _model->AppendDocuments(documents);
            return documents.size() == 1 && documents.front().dirty;
        }

        String TypedOnDisk() const {
            return ReadFile(_dir / "LoggerConfig.xml");
        }
    };

    TEST_F(ConfigEditorTypedModelTest, AConfigOfARegisteredTypeOpensAsObjectsAndAnotherSaysWhyItIsText) {
        Select(kTypedConfig);

        EXPECT_NE(_model->GetDocument(), nullptr);
        EXPECT_TRUE(View().showTree);
        EXPECT_FALSE(View().textMode);
        EXPECT_TRUE(View().textModeNote.empty());

        Select(kPanelConfig);

        EXPECT_EQ(_model->GetDocument(), nullptr);
        EXPECT_TRUE(View().textMode);
        EXPECT_NE(View().textModeNote.find("no reflected type"), String::npos);
    }

    TEST_F(ConfigEditorTypedModelTest, AnEditedObjectIsUnsavedAndSaveWritesItAndOpensTheConfigAgain) {
        Select(kTypedConfig);
        SetSinkPriority(4);

        EXPECT_TRUE(View().hasStaged);
        EXPECT_TRUE(ReportsDirty());

        Run(EditorCommandIds::kConfigSave);

        EXPECT_NE(TypedOnDisk().find("priority=\"4\""), String::npos);
        EXPECT_NE(TypedOnDisk().find("<!-- the console -->"), String::npos);
        EXPECT_FALSE(View().hasStaged);
        EXPECT_FALSE(ReportsDirty());
        EXPECT_EQ(Sink().GetPriority(), 4);
    }

    TEST_F(ConfigEditorTypedModelTest, AReloadWithAnEditedObjectAsksFirstAndDiscardReadsTheFile) {
        Select(kTypedConfig);
        SetSinkPriority(4);

        Run(EditorCommandIds::kConfigEditorReload);

        EXPECT_TRUE(View().reloadConfirmOpen);
        EXPECT_EQ(Sink().GetPriority(), 4);

        Run(EditorCommandIds::kConfigEditorReloadDiscard);

        EXPECT_EQ(Sink().GetPriority(), 1);
        EXPECT_FALSE(View().hasStaged);
        EXPECT_EQ(TypedOnDisk(), kTypedText);
    }

    TEST_F(ConfigEditorTypedModelTest, AChangeOnDiskDropsTheEditsAndOpensTheNewDocument) {
        Select(kTypedConfig);
        SetSinkPriority(4);
        String changed(kTypedText);
        changed.replace(changed.find("priority=\"1\""), StringView("priority=\"1\"").size(), "priority=\"9\"");
        WriteFile(_dir / "LoggerConfig.xml", changed);

        _manager.ReloadConfig(kTypedConfig);
        _model->Update();

        EXPECT_TRUE(View().changedOnDisk);
        EXPECT_FALSE(View().hasStaged);
        EXPECT_EQ(Sink().GetPriority(), 9);
    }

    TEST_F(ConfigEditorTypedModelTest, ASaveThatCannotWriteTheFileKeepsTheConfigUnsaved) {
        Select(kTypedConfig);
        SetSinkPriority(4);
        // A directory where the file was: nothing can be written there
        RemoveFileIfPresent(_dir / "LoggerConfig.xml");
        MakeEmptyDirectory(_dir / "LoggerConfig.xml");

        Run(EditorCommandIds::kConfigSave);

        EXPECT_TRUE(View().hasStaged);
        EXPECT_TRUE(ReportsDirty());
    }

}  // namespace BECore::Tests
