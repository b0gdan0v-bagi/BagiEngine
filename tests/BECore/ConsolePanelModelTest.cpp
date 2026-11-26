#include <BECore/Widgets/PanelAssets/PanelBinding.h>
#include <BECore/Widgets/PanelAssets/PanelBindingReader.h>
#include <BECore/Widgets/PanelAssets/PanelBindingWriter.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/Console/ConsolePanelModel.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kTime = "12:34:56.789";
        constexpr StringView kChessFile = "a/src/Chess/Game/X.cpp";
        constexpr StringView kVulkanFile = "a/src/Modules/Vulkan/src/X.cpp";
        constexpr int kSteadyFrames = 8;

        void AddLine(LogHistory& history, LogLevel level, StringView message, StringView file = {}) {
            const LogSite site{file.empty() ? nullptr : file.data(), file.empty() ? 0U : 1U};
            history.Add(level, message, site, kTime);
        }

        String Rows(const LogHistory& history, const ConsoleLogView& view) {
            String text;
            for (const uint32_t row : view.GetRows()) {
                if (!text.empty()) {
                    text += "|";
                }
                text += history.At(row).message;
            }
            return text;
        }

    }  // namespace

    /// The Console's model over a history of its own and no ImGui
    class ConsolePanelModelTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            AddLine(_history, LogLevel::Debug, "Texture loaded", kVulkanFile);
            AddLine(_history, LogLevel::Info, "Move played", kChessFile);
            AddLine(_history, LogLevel::Warning, "texture missing", kVulkanFile);
            AddLine(_history, LogLevel::Error, "Bad move", kChessFile);
            _model = New<ConsolePanelModel>();
            _model->SetHistory(&_history);
            _model->Initialize();
            _model->Update();
        }

        ConsolePanelView& View() {
            return const_cast<ConsolePanelView&>(_model->GetView());
        }

        String Listed() {
            return Rows(_history, _model->GetLogView());
        }

        LogHistory _history{16};
        IntrusivePtr<ConsolePanelModel> _model;
    };

    TEST_F(ConsolePanelModelTest, EveryLineIsListedAtStartAndTheLabelsCarryTheCounts) {
        EXPECT_TRUE(View().hasHistory);
        EXPECT_EQ(Listed(), "Texture loaded|Move played|texture missing|Bad move");
        EXPECT_EQ(View().debugLabel, "Debug 1");
        EXPECT_EQ(View().infoLabel, "Info 1");
        EXPECT_EQ(View().warningLabel, "Warning 1");
        EXPECT_EQ(View().errorLabel, "Error 1");
    }

    TEST_F(ConsolePanelModelTest, AToggleOfTheViewFiltersTheRowsInTheNextUpdate) {
        View().showDebug = false;
        View().showWarning = false;
        _model->Update();
        EXPECT_EQ(Listed(), "Move played|Bad move");

        View().showDebug = true;
        _model->Update();
        EXPECT_EQ(Listed(), "Texture loaded|Move played|Bad move");
    }

    TEST_F(ConsolePanelModelTest, TheErrorToggleCoversFatalAndItsCountSumsBoth) {
        AddLine(_history, LogLevel::Fatal, "Boom", kChessFile);
        View().showError = false;
        _model->Update();

        EXPECT_EQ(View().errorLabel, "Error 2");
        EXPECT_EQ(Listed(), "Texture loaded|Move played|texture missing");
    }

    TEST_F(ConsolePanelModelTest, TheCategoryListStartsWithAllAndAnIndexSelectsACategory) {
        ASSERT_EQ(View().categories.size(), _history.GetCategoryCount() + 1);
        EXPECT_EQ(View().categories[0], "All");
        const auto indexOf = [&](StringView name) {
            for (size_t i = 0; i < View().categories.size(); ++i) {
                if (StringView(View().categories[i]) == name) {
                    return static_cast<int32_t>(i);
                }
            }
            return int32_t{-1};
        };
        const int32_t chess = indexOf("Chess");
        ASSERT_GT(chess, 0) << "the history has no Chess category";

        View().category = chess;
        _model->Update();
        EXPECT_EQ(Listed(), "Move played|Bad move");

        View().category = 0;
        _model->Update();
        EXPECT_EQ(Listed(), "Texture loaded|Move played|texture missing|Bad move");
    }

    TEST_F(ConsolePanelModelTest, ACategoryIndexPastTheListGoesBackToAll) {
        View().category = 1000;
        _model->Update();
        EXPECT_EQ(View().category, 0);
        EXPECT_EQ(Listed(), "Texture loaded|Move played|texture missing|Bad move");
    }

    TEST_F(ConsolePanelModelTest, TheCategoryListGrowsWithTheHistoryAndKeepsTheSelection) {
        const size_t before = View().categories.size();
        View().category = 1;
        AddLine(_history, LogLevel::Info, "Sound started", "a/src/Modules/FreshAudioModule/X.cpp");
        _model->Update();

        EXPECT_EQ(View().categories.size(), before + 1);
        EXPECT_EQ(View().category, 1);
    }

    TEST_F(ConsolePanelModelTest, TheFilterTextNarrowsTheRowsAndIsCutAtTheLimit) {
        View().filterText = "TEXTURE";
        _model->Update();
        EXPECT_EQ(Listed(), "Texture loaded|texture missing");

        // 'é' is two bytes: a cut at the limit lands inside it and must step back
        String text;
        for (size_t i = 0; i < kMaxConsoleFilterLength - 1; ++i) {
            text += 'a';
        }
        text += "\xC3\xA9";
        View().filterText = text;
        _model->Update();
        EXPECT_EQ(Listed(), "") << "a filter text cannot match a 127-letter line";
    }

    TEST_F(ConsolePanelModelTest, AnUnchangedFilterDoesNotRebuildTheRows) {
        View().filterText = "move";
        _model->Update();
        const uint32_t rebuilds = _model->GetLogView().GetRebuildCount();

        _model->Update();
        View().filterText = "move";
        _model->Update();
        EXPECT_EQ(_model->GetLogView().GetRebuildCount(), rebuilds);

        View().filterText = "moves";
        _model->Update();
        EXPECT_EQ(_model->GetLogView().GetRebuildCount(), rebuilds + 1);
    }

    TEST_F(ConsolePanelModelTest, PauseFreezesTheRowsAndTheCommandTogglesIt) {
        EXPECT_TRUE(_model->RunCommand(PoolString(EditorCommandIds::kConsolePause).ToStringView(), ""));
        EXPECT_TRUE(View().paused);
        _model->Update();
        AddLine(_history, LogLevel::Info, "Late line");
        _model->Update();
        EXPECT_EQ(Listed(), "Texture loaded|Move played|texture missing|Bad move");

        EXPECT_TRUE(_model->RunCommand(PoolString(EditorCommandIds::kConsolePause).ToStringView(), ""));
        EXPECT_FALSE(View().paused);
        _model->Update();
        EXPECT_EQ(Listed(), "Texture loaded|Move played|texture missing|Bad move|Late line");
    }

    TEST_F(ConsolePanelModelTest, ClearEmptiesTheHistoryAndTheLabelsFollow) {
        EXPECT_TRUE(_model->RunCommand(PoolString(EditorCommandIds::kConsoleClear).ToStringView(), ""));
        _model->Update();

        EXPECT_EQ(_history.Size(), 0u);
        EXPECT_EQ(Listed(), "");
        EXPECT_EQ(View().errorLabel, "Error 0");
        EXPECT_EQ(View().title, "Console");
    }

    TEST_F(ConsolePanelModelTest, ClearOutputEmptiesTheOutputAreaAndAStrangerCommandIsNotTaken) {
        _model->GetSession().GetOutput().Append(ConsoleOutputKind::Note, "hello");
        ASSERT_EQ(_model->GetSession().GetOutput().Size(), 1u);

        EXPECT_TRUE(_model->RunCommand(PoolString(EditorCommandIds::kConsoleClearOutput).ToStringView(), ""));
        EXPECT_EQ(_model->GetSession().GetOutput().Size(), 0u);
        EXPECT_FALSE(_model->RunCommand("console.nobody", ""));
    }

    TEST_F(ConsolePanelModelTest, TheTitleCountsErrorsAndWarningsAndChangesOnlyWithThem) {
        EXPECT_EQ(View().title, "Console (1 error, 1 warning)");

        AddLine(_history, LogLevel::Info, "More info");
        _model->Update();
        EXPECT_EQ(View().title, "Console (1 error, 1 warning)");

        AddLine(_history, LogLevel::Error, "Another");
        _model->Update();
        EXPECT_EQ(View().title, "Console (2 errors, 1 warning)");
    }

    TEST_F(ConsolePanelModelTest, TheFirstPreferencesEventGivesTheHistoryAndLaterOnesOnlyTheEditor) {
        EditorPreferencesChangedEvent first;
        first.externalEditor = "code -g {file}:{line}";
        first.consoleHistory = {"ping", "list_methods"};
        EditorPreferencesChangedEvent::Emit(first);

        EXPECT_EQ(_model->GetExternalEditor(), "code -g {file}:{line}");
        ASSERT_EQ(_model->GetSession().GetHistory().GetEntries().size(), 2u);

        EditorPreferencesChangedEvent later;
        later.externalEditor = "vim {file}";
        later.consoleHistory = {"step"};
        EditorPreferencesChangedEvent::Emit(later);

        EXPECT_EQ(_model->GetExternalEditor(), "vim {file}");
        EXPECT_EQ(_model->GetSession().GetHistory().GetEntries().size(), 2u);
    }

    TEST_F(ConsolePanelModelTest, TheFieldsTheAssetBindsAreWrittenAndReadThroughTheBindingsWithNoCommand) {
        PanelDataSources sources;
        const PanelSourceHandle handle = sources.Register("console", [this] { return _model->GetObject(); });
        PanelBindingReader reader;
        PanelBindingWriter writer(reader);
        const auto binding = [](StringView text) { return *ParseBinding(text); };

        EXPECT_EQ(writer.WriteBool(sources, binding("console.showInfo"), false), PanelReadStatus::Ok);
        EXPECT_EQ(writer.WriteText(sources, binding("console.filterText"), "move"), PanelReadStatus::Ok);
        _model->Update();
        EXPECT_EQ(Listed(), "Bad move");

        String scratch;
        StringView title;
        EXPECT_EQ(reader.ReadText(sources, binding("console.title"), scratch, title), PanelReadStatus::Ok);
        EXPECT_EQ(title, "Console (1 error, 1 warning)");
        double lost = -1.0;
        EXPECT_EQ(reader.ReadNumber(sources, binding("console.lost"), lost), PanelReadStatus::Ok);
        EXPECT_GE(lost, 0.0);
    }

    TEST_F(ConsolePanelModelTest, ASteadyUpdateAllocatesNothing) {
        View().filterText = "move";
        for (int i = 0; i < kSteadyFrames; ++i) {
            _model->Update();
        }
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                _model->Update();
            }
        });
    }

}  // namespace BECore::Tests
