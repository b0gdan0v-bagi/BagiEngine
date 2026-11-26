#include <BECore/FileSystem/FakeFileWatchBackend.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/FileWatcher.h>
#include <BECore/Widgets/EditorDocuments.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/AssetBrowser/AssetBrowserPanelModel.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// The Asset Browser's model over a content root on disk and a watcher on a fake backend, whose clock the test moves
    class AssetBrowserPanelModelTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _dir = TestTempDirectory("assetbrowser");
            MakeEmptyDirectory(_dir);
            Write("assets/board.png");
            Write("assets/click.wav");
            Write("assets/pieces/rook.png");
            Write("config/GameConfig.xml", "<GameConfig/>");
            _fileSystem.AddContentRoot(_dir);
            _fileSystem.Initialize();
            _backend = New<FakeFileWatchBackend>();
            _watcher.Start(_backend);
            _model.SetFileSystem(_fileSystem);
            _model.SetWatcher(_watcher);
            _model.SetEviction([this](StringView path) { _evicted.push_back(String(path)); });
            _documentSource = _documents.Add([this](Vector<EditorDocumentState>& out) {
                EditorDocumentState scene;
                scene.id = "scene:Main";
                scene.dirty = _sceneDirty;
                out.push_back(std::move(scene));
            });
            _model.SetDocuments(_documents);
        }

        void TearDown() override {
            RemoveDirectory(_dir);
        }

        /// What the widget does with a model it has made: `Initialize`, then the first frame
        void Open() {
            _model.Initialize();
            _model.Update();
        }

        void Write(StringView file, StringView text = "x") {
            WriteFile(_dir / file, text);
        }

        /// Reports @p paths as the platform does and lets the watcher's settle window pass; the model is not updated
        void Notify(std::initializer_list<StringView> paths) {
            for (const StringView path : paths) {
                _backend->Emit(_dir / path);
            }
            _watcher.Update(_now);
            _now += 2.0 * kFileWatchSettleSeconds;
            _watcher.Update(_now);
        }

        AssetBrowserView& View() {
            return const_cast<AssetBrowserView&>(_model.GetView());
        }

        [[nodiscard]] Vector<String> Shown() const {
            Vector<String> paths;
            for (const uint32_t index : _model.GetShownFiles()) {
                paths.push_back(String(At(_model.GetCatalog().GetFiles(), index).path.ToStringView()));
            }
            return paths;
        }

        bool Command(PoolString command) {
            return _model.RunCommand(command.ToStringView(), {});
        }

        [[nodiscard]] Vector<String> Users() const {
            Vector<String> lines;
            for (const AssetUseRow& row : _model.GetView().change.users) {
                lines.push_back(row.text);
            }
            return lines;
        }

        [[nodiscard]] bool OnDisk(StringView file) const {
            return IsFile(_dir / file);
        }

        Path _dir;
        EditorDocuments _documents;
        EditorDocumentsHandle _documentSource;
        bool _sceneDirty = false;
        FileSystem _fileSystem;
        FileWatcher _watcher{_fileSystem};
        IntrusivePtr<FakeFileWatchBackend> _backend;
        Vector<String> _evicted;
        AssetBrowserPanelModel _model;
        double _now = 1.0;
    };

    TEST_F(AssetBrowserPanelModelTest, ItStartsInTheAssetsFolder) {
        Open();

        EXPECT_EQ(View().folder, "assets");
        EXPECT_EQ(Shown(), (Vector<String>{"assets/board.png", "assets/click.wav"}));
        EXPECT_TRUE(View().showFiles);
        EXPECT_FALSE(View().showEmpty);
        EXPECT_FALSE(View().hasSelection);
    }

    TEST_F(AssetBrowserPanelModelTest, TheShownListFollowsTheSearchTheFilterAndTheFolderWithoutListingTheFilesAgain) {
        Open();
        const uint32_t revision = _model.GetCatalogRevision();

        View().search = "rook";
        _model.Update();
        EXPECT_EQ(Shown(), Vector<String>{"assets/pieces/rook.png"});

        View().typeFilter = AssetTypeFilter::Sound;
        _model.Update();
        EXPECT_TRUE(Shown().empty());
        EXPECT_TRUE(View().showEmpty);
        EXPECT_FALSE(View().showFiles);

        View().search.clear();
        _model.Update();
        EXPECT_EQ(Shown(), Vector<String>{"assets/click.wav"});

        View().typeFilter = AssetTypeFilter::All;
        _model.SelectFolder("assets/pieces");
        EXPECT_EQ(Shown(), Vector<String>{"assets/pieces/rook.png"});
        EXPECT_EQ(_model.GetCatalogRevision(), revision);
    }

    TEST_F(AssetBrowserPanelModelTest, OnlyAListedFileCanBeSelected) {
        Open();

        _model.SelectAsset("config/GameConfig.xml");
        EXPECT_TRUE(View().hasSelection);
        EXPECT_EQ(View().selectedPath, "config/GameConfig.xml");
        EXPECT_EQ(View().selectedKind, AssetKind::Config);

        _model.SelectAsset("assets/pieces");
        EXPECT_FALSE(View().hasSelection);
        EXPECT_TRUE(View().selectedPath.empty());
        EXPECT_EQ(View().selectedKind, AssetKind::None);
    }

    TEST_F(AssetBrowserPanelModelTest, ManyNoticesListTheFilesOnceAndOnlyInTheNextUpdate) {
        Open();
        const uint32_t revision = _model.GetCatalogRevision();

        Write("assets/added.png");
        Write("assets/also.ttf");
        Notify({"assets/added.png", "assets/also.ttf"});
        EXPECT_EQ(_model.GetCatalogRevision(), revision);

        _model.Update();
        EXPECT_EQ(_model.GetCatalogRevision(), revision + 1);
        EXPECT_EQ(Shown(), (Vector<String>{"assets/added.png", "assets/also.ttf", "assets/board.png", "assets/click.wav"}));
    }

    TEST_F(AssetBrowserPanelModelTest, ARemovedSelectionIsDroppedAndAGoneFolderBecomesItsParent) {
        Open();
        _model.SelectFolder("assets/pieces");
        _model.SelectAsset("assets/pieces/rook.png");

        RemoveFileIfPresent(_dir / "assets/pieces/rook.png");
        Notify({"assets/pieces/rook.png"});
        _model.Update();

        EXPECT_FALSE(View().hasSelection);
        EXPECT_TRUE(View().selectedPath.empty());
        EXPECT_EQ(View().folder, "assets");
        EXPECT_EQ(Shown(), (Vector<String>{"assets/board.png", "assets/click.wav"}));
    }

    TEST_F(AssetBrowserPanelModelTest, ASelectedFileThatChangesItsKindStaysSelected) {
        Open();
        _model.SelectAsset("config/GameConfig.xml");

        Write("config/GameConfig.xml", "<node name=\"Hud\"/>");
        Notify({"config/GameConfig.xml"});
        _model.Update();

        EXPECT_EQ(View().selectedPath, "config/GameConfig.xml");
        EXPECT_EQ(View().selectedKind, AssetKind::Node);
    }

    TEST_F(AssetBrowserPanelModelTest, AChangedOrRemovedTextureIsEvictedAtOnceAndNothingElseIs) {
        Open();

        Write("assets/board.png", "changed");
        Write("assets/click.wav", "changed");
        Write("config/GameConfig.xml", "<GameConfig changed=\"1\"/>");
        RemoveFileIfPresent(_dir / "assets/pieces/rook.png");
        Notify({"assets/board.png", "assets/click.wav", "config/GameConfig.xml", "assets/pieces/rook.png"});

        EXPECT_EQ(_evicted, (Vector<String>{"assets/board.png", "assets/pieces/rook.png"}));
    }

    TEST_F(AssetBrowserPanelModelTest, AFileTheCatalogDoesNotListChangesNothing) {
        Open();
        const uint32_t revision = _model.GetCatalogRevision();

        Write("config/imgui.ini");
        Write("assets/board.png.tmp");
        Notify({"config/imgui.ini", "assets/board.png.tmp", "assets/gone.tmp"});
        _model.Update();

        EXPECT_EQ(_model.GetCatalogRevision(), revision);
        EXPECT_TRUE(_evicted.empty());
    }

    TEST_F(AssetBrowserPanelModelTest, AFolderThatWentAwayAndIsReportedAsItselfEvictsItsTextures) {
        Open();

        RemoveDirectory(_dir / "assets/pieces");
        Notify({"assets/pieces"});
        _model.Update();

        EXPECT_EQ(_evicted, Vector<String>{"assets/pieces/rook.png"});
        EXPECT_FALSE(_model.GetCatalog().HasFolder("assets/pieces"));
    }

    TEST_F(AssetBrowserPanelModelTest, AFolderThatAppearedAndIsReportedAsItselfIsListed) {
        Open();

        Write("assets/moved/queen.png");
        Notify({"assets/moved"});
        _model.Update();

        EXPECT_NE(_model.GetCatalog().Find("assets/moved/queen.png"), nullptr);
    }

    TEST_F(AssetBrowserPanelModelTest, WithoutAWatcherOnlyRescanSeesAChange) {
        _watcher.Stop();
        Open();
        Write("assets/added.png");
        _model.Update();
        EXPECT_EQ(_model.GetCatalog().Find("assets/added.png"), nullptr);

        EXPECT_TRUE(_model.RunCommand("assetBrowser.rescan", ""));

        EXPECT_NE(_model.GetCatalog().Find("assets/added.png"), nullptr);
        EXPECT_EQ(Shown().size(), 3u);
        EXPECT_FALSE(_model.RunCommand("fileChanges.rescan", ""));
    }

    TEST_F(AssetBrowserPanelModelTest, TheButtonsAreOffWhileADocumentIsUnsaved) {
        Open();
        _sceneDirty = true;
        _model.SelectAsset("assets/board.png");

        EXPECT_FALSE(View().canChange);
        EXPECT_NE(View().deleteHint.find("scene:Main"), String::npos) << View().deleteHint.c_str();
        Command(EditorCommandIds::kAssetBrowserDelete);
        EXPECT_FALSE(View().change.deleteOpen);
        EXPECT_TRUE(View().hasMessage);

        // A save by a hotkey: the command reaches the model, which looks again in its next update
        _sceneDirty = false;
        Command(EditorCommandIds::kSceneSave);
        _model.Update();
        EXPECT_TRUE(View().canChange);

        // And a save with no command is seen when the pointer comes back to the panel
        _sceneDirty = true;
        _model.NotePointerOver(true);
        _model.Update();
        EXPECT_FALSE(View().canChange);
    }

    TEST_F(AssetBrowserPanelModelTest, AConfigIsOfferedNoDelete) {
        Open();
        _model.SelectFolder("config");
        _model.SelectAsset("config/GameConfig.xml");

        EXPECT_FALSE(View().canChange);
        Command(EditorCommandIds::kAssetBrowserDelete);
        Command(EditorCommandIds::kAssetBrowserConfirm);

        EXPECT_FALSE(View().change.deleteOpen);
        EXPECT_TRUE(OnDisk("config/GameConfig.xml"));
    }

    TEST_F(AssetBrowserPanelModelTest, TheDialogListsTheUsersAndCancelChangesNothing) {
        Write("config/GameConfig.xml", "<GameConfig\n  icon=\"assets/board.png\"/>");
        Open();
        _model.SelectAsset("assets/board.png");

        Command(EditorCommandIds::kAssetBrowserDelete);

        EXPECT_TRUE(View().change.deleteOpen);
        EXPECT_TRUE(View().change.inUse);
        EXPECT_FALSE(View().change.unused);
        EXPECT_EQ(Users(), (Vector<String>{"config/GameConfig.xml, line 2"}));

        Command(EditorCommandIds::kAssetBrowserCancel);
        Command(EditorCommandIds::kAssetBrowserConfirm);

        EXPECT_FALSE(View().change.deleteOpen);
        EXPECT_TRUE(OnDisk("assets/board.png"));
        EXPECT_TRUE(_evicted.empty());
    }

    TEST_F(AssetBrowserPanelModelTest, AConfirmedDeleteRemovesTheFileFromTheDiskAndTheList) {
        Open();
        _model.SelectAsset("assets/board.png");

        Command(EditorCommandIds::kAssetBrowserDelete);
        ASSERT_TRUE(View().change.unused);
        Command(EditorCommandIds::kAssetBrowserConfirm);

        EXPECT_FALSE(OnDisk("assets/board.png"));
        EXPECT_FALSE(View().change.deleteOpen);
        EXPECT_FALSE(View().hasSelection);
        EXPECT_FALSE(View().hasMessage);
        EXPECT_EQ(_evicted, (Vector<String>{"assets/board.png"}));
        EXPECT_EQ(Shown(), (Vector<String>{"assets/click.wav"}));
    }

    TEST_F(AssetBrowserPanelModelTest, AConfirmedRenameSelectsTheRenamedFile) {
        Open();
        _model.SelectAsset("assets/board.png");

        Command(EditorCommandIds::kAssetBrowserRename);
        ASSERT_TRUE(View().change.renameOpen);
        EXPECT_EQ(View().change.newName, "board");
        EXPECT_FALSE(View().change.canConfirm);
        EXPECT_FALSE(View().change.nameRefused);

        View().change.newName = "click.x y";
        _model.Update();
        EXPECT_FALSE(View().change.canConfirm);
        EXPECT_TRUE(View().change.nameRefused);

        View().change.newName = "table";
        _model.Update();
        ASSERT_TRUE(View().change.canConfirm);
        Command(EditorCommandIds::kAssetBrowserConfirm);

        EXPECT_FALSE(OnDisk("assets/board.png"));
        EXPECT_TRUE(OnDisk("assets/table.png"));
        EXPECT_FALSE(View().change.renameOpen);
        EXPECT_EQ(View().selectedPath, "assets/table.png");
        EXPECT_EQ(_evicted, (Vector<String>{"assets/board.png"}));
    }

    TEST_F(AssetBrowserPanelModelTest, AChangeBetweenTheDialogAndTheConfirmIsShownAndNothingHappens) {
        Open();
        _model.SelectAsset("assets/board.png");
        Command(EditorCommandIds::kAssetBrowserDelete);
        ASSERT_TRUE(View().change.unused);

        _sceneDirty = true;
        Command(EditorCommandIds::kAssetBrowserConfirm);
        EXPECT_TRUE(View().change.deleteOpen);
        EXPECT_TRUE(View().change.hasProblem);
        EXPECT_TRUE(OnDisk("assets/board.png"));

        // The save that cleared it wrote a use of the file
        _sceneDirty = false;
        Write("config/GameConfig.xml", "<GameConfig icon=\"assets/board.png\"/>");
        Command(EditorCommandIds::kAssetBrowserConfirm);
        EXPECT_TRUE(View().change.deleteOpen);
        EXPECT_TRUE(View().change.inUse);
        EXPECT_EQ(Users(), (Vector<String>{"config/GameConfig.xml, line 1"}));
        EXPECT_TRUE(OnDisk("assets/board.png"));

        // The file itself was written over
        Write("assets/board.png", "another picture");
        Command(EditorCommandIds::kAssetBrowserConfirm);
        EXPECT_TRUE(OnDisk("assets/board.png"));

        Command(EditorCommandIds::kAssetBrowserConfirm);
        EXPECT_FALSE(OnDisk("assets/board.png"));
    }

    TEST_F(AssetBrowserPanelModelTest, ClosingThePanelDropsTheQuestion) {
        Open();
        _model.SelectAsset("assets/board.png");
        Command(EditorCommandIds::kAssetBrowserDelete);

        _model.OnOpenChanged(false);
        Command(EditorCommandIds::kAssetBrowserConfirm);

        EXPECT_FALSE(View().change.deleteOpen);
        EXPECT_TRUE(OnDisk("assets/board.png"));
    }

    TEST_F(AssetBrowserPanelModelTest, ASteadyUpdateAllocatesNothing) {
        Open();
        View().search = "a name that is longer than any small string buffer";
        _model.Update();

        ExpectNoAllocations([&] {
            for (int frame = 0; frame < 3; ++frame) {
                _model.Update();
            }
        });
    }

}  // namespace BECore::Tests
