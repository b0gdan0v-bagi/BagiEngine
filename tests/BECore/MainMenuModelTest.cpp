#include <BECore/EditorLayoutHostProbe.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/MainMenuModel.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kAlpha = "Alpha";
        constexpr StringView kBeta = "Beta";
        constexpr StringView kUnsaved = "Gamma";
        constexpr StringView kTraversal = "../preferences";
        constexpr StringView kLayoutText = "text";

    }  // namespace

    /// The menu bar's model over a fake host with a folder of its own and no ImGui
    class MainMenuModelTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _directory = TestTempDirectory("MainMenuModel");
            MakeEmptyDirectory(_directory);
            _host.emplace(_directory);
            _handle = EditorLayoutHostRegistry::GetInstance().Register(*_host);
            _model = New<MainMenuModel>();
        }

        void TearDown() override {
            _model.Reset();
            _handle.Reset();
            _host.reset();
            RemoveDirectory(_directory);
        }

        void Save(StringView name) {
            ASSERT_TRUE(_host->layouts->Save(name, kLayoutText).has_value());
        }

        template <typename TId>
        bool Command(const TId& id, StringView argument = {}) {
            return _model->RunCommand(id.View(), argument);
        }

        const MainMenuView& View() const {
            return _model->GetView();
        }

        Path _directory;
        Optional<ProbeEditorLayoutHost> _host;
        EditorLayoutHostHandle _handle;
        IntrusivePtr<MainMenuModel> _model;
    };

    TEST_F(MainMenuModelTest, TheFirstUpdateReadsTheFolderAndTheMenuOpeningReadsItAgain) {
        ASSERT_NO_FATAL_FAILURE(Save(kBeta));
        ASSERT_NO_FATAL_FAILURE(Save(kAlpha));
        _model->Update();
        EXPECT_TRUE(View().hasStore);
        EXPECT_TRUE(View().hasLayouts);
        ASSERT_EQ(View().layouts.size(), 2u);
        EXPECT_EQ(View().layouts[0], kAlpha) << "the names are sorted";

        // A layout saved elsewhere shows only when a menu opens: a menu that is open does not scan on every draw
        ASSERT_NO_FATAL_FAILURE(Save(kUnsaved));
        _model->Update();
        EXPECT_EQ(View().layouts.size(), 2u);
        EXPECT_TRUE(Command(EditorCommandIds::kLayoutsMenuOpened));
        EXPECT_EQ(View().layouts.size(), 3u);
    }

    TEST_F(MainMenuModelTest, DeleteRemovesTheFileAndTheNameFromTheList) {
        ASSERT_NO_FATAL_FAILURE(Save(kAlpha));
        ASSERT_NO_FATAL_FAILURE(Save(kBeta));
        _model->Update();

        EXPECT_TRUE(Command(EditorCommandIds::kLayoutsDelete, kAlpha));

        EXPECT_FALSE(_host->layouts->Exists(kAlpha));
        EXPECT_TRUE(_host->layouts->Exists(kBeta));
        ASSERT_EQ(View().layouts.size(), 1u);
        EXPECT_EQ(View().layouts[0], kBeta);
        EXPECT_TRUE(View().hasLayouts);

        EXPECT_TRUE(Command(EditorCommandIds::kLayoutsDelete, kBeta));
        EXPECT_FALSE(View().hasLayouts);
    }

    TEST_F(MainMenuModelTest, LoadHandsTheNameToTheHost) {
        ASSERT_NO_FATAL_FAILURE(Save(kAlpha));
        _model->Update();

        EXPECT_TRUE(Command(EditorCommandIds::kLayoutsLoad, kAlpha));

        ASSERT_EQ(_host->loadedLayouts.size(), 1u);
        EXPECT_EQ(_host->loadedLayouts[0], kAlpha);
    }

    TEST_F(MainMenuModelTest, AnEmptyUnsavedOrPathLikeNameDoesNothing) {
        ASSERT_NO_FATAL_FAILURE(Save(kAlpha));
        _model->Update();

        for (const StringView name : {StringView(), kUnsaved, kTraversal}) {
            EXPECT_TRUE(Command(EditorCommandIds::kLayoutsLoad, name));
            EXPECT_TRUE(Command(EditorCommandIds::kLayoutsDelete, name));
        }

        EXPECT_TRUE(_host->loadedLayouts.empty());
        EXPECT_TRUE(_host->layouts->Exists(kAlpha));
        EXPECT_EQ(View().layouts.size(), 1u);
    }

    TEST_F(MainMenuModelTest, WithoutALayoutsFolderThereIsNoStoreAndTheCommandsDoNothing) {
        _host->layouts.reset();
        _model->Update();

        EXPECT_FALSE(View().hasStore);
        EXPECT_FALSE(View().hasLayouts);
        EXPECT_TRUE(View().layouts.empty());
        EXPECT_TRUE(Command(EditorCommandIds::kLayoutsMenuOpened));
        EXPECT_TRUE(Command(EditorCommandIds::kLayoutsLoad, kAlpha));
        EXPECT_TRUE(_host->loadedLayouts.empty());
    }

    TEST_F(MainMenuModelTest, ACommandOfAnotherOwnerIsNotTaken) {
        EXPECT_FALSE(Command(EditorCommandIds::kFileQuit));
        EXPECT_FALSE(Command(EditorCommandIds::kViewSaveLayout));
    }

}  // namespace BECore::Tests
