#include <BECore/Resource/TextureLibrary.h>
#include <Common/AllocCheck.h>
#include <Common/ArtGenTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/MountedFolderTest.h>
#include <Common/Printers.h>
#include <Widgets/ArtGen/ArtGenDrag.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/TextureEditor/SpriteLibraryPanelModel.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kSteadyFrames = 8;
        constexpr const char* kMount = "sprite_library_model";
        constexpr const char* kGamePrefix = "sprite_library_model_game";
        constexpr StringView kQueenPng = "assets/artgen/queen.png";
        constexpr StringView kRookPng = "assets/artgen/rook.png";

        SpriteEntry EntryOf(StringView name, StringView path, const Rect& rect) {
            SpriteEntry entry;
            entry.name = PoolString::Intern(name);
            entry.texturePath = PoolString::Intern(path);
            entry.srcRect = rect;
            return entry;
        }

    }  // namespace

    /// The Sprite Library's model over a library of its own (never the shipped file) and no ImGui
    class SpriteLibraryPanelModelTest : public MountedFolderTest {
    protected:
        SpriteLibraryPanelModelTest() : MountedFolderTest(kMount) {}

        void SetUp() override {
            MountedFolderTest::SetUp();
            WriteFile(_dir / "board.png", "png");
            WriteFile(_dir / "notes.txt", "text");
            _library.AddOrUpdate(EntryOf("queen", "assets/queen.png", Rect(0, 0, 8, 8)));
            _library.AddOrUpdate(EntryOf("rook", "assets/rook.png", Rect(1, 2, 3, 4)));
            _model = New<SpriteLibraryPanelModel>();
            _model->UseLibrary(&_library, [this](TextureLibrary&) {
                ++_saves;
                return _saveSucceeds;
            });
            _model->UseTextureMount(PoolString::Intern(kMount));
            _model->UseAtlasFiles(&_atlasFiles);
            _model->Initialize();
            _model->Update();
        }

        SpriteLibraryPanelView& View() {
            return const_cast<SpriteLibraryPanelView&>(_model->GetView());
        }

        bool Run(PoolString command) {
            return _model->RunCommand(command.ToStringView(), {});
        }

        void SelectAndUpdate(StringView name) {
            _model->SelectByName(PoolString::Intern(name));
            _model->Update();
        }

        const SpriteEntry& Entry(StringView name) const {
            const SpriteEntry* entry = _library.GetSprite(PoolString::Intern(name));
            EXPECT_NE(entry, nullptr);
            static const SpriteEntry kMissing;
            return entry != nullptr ? *entry : kMissing;
        }

        /// One frame in which a slot noted something: the model applies it after the walk
        void EndFrame() {
            _model->EndUpdate();
            _model->Update();
        }

        static SpriteLibraryDrop DropOf(ArtGenDragKind kind, int row, StringView suggestedName) {
            SpriteLibraryDrop drop;
            drop.row = row;
            drop.payload = MakeArtGenDragPayload(kind, 1, {}, {}, suggestedName);
            return drop;
        }

        /// Selects rook as a sheet of @p width x @p height that the preview has loaded, at @p path
        void SelectSheet(StringView path, int width, int height) {
            SpriteEntry rook = Entry("rook");
            rook.texturePath = PoolString::Intern(path);
            _library.AddOrUpdate(std::move(rook));
            SelectAndUpdate("rook");
            _model->NoteSheet(PoolString::Intern(path), width, height);
            _model->Update();
        }

        /// Puts queen and rook into the group "pieces" with their pictures in the game folder, unpacked
        void FillPieces() {
            AddPiece("queen", kQueenPng, 4);
            AddPiece("rook", kRookPng, 2);
        }

        void AddPiece(StringView name, StringView path, uint32_t size) {
            _game.SavePng(path, MakeSolid(size, size, kHigh, 0, 0));
            SpriteEntry entry = EntryOf(name, path, Rect(0, 0, static_cast<float>(size), static_cast<float>(size)));
            SpriteAtlasLink link;
            link.name = "pieces"_intern;
            entry.atlas = link;
            _library.AddOrUpdate(std::move(entry));
        }

        /// Makes @p name a packed member of an atlas, as Pack leaves it
        void Pack(StringView name) {
            SpriteEntry entry = Entry(name);
            SpriteAtlasLink link;
            link.name = "pieces"_intern;
            link.source = SpriteSource{};
            link.source->texturePath = entry.texturePath;
            entry.atlas = link;
            entry.texturePath = "assets/atlases/pieces.png"_intern;
            _library.AddOrUpdate(std::move(entry));
        }

        ArtGenGameFolder _game{kGamePrefix};
        MoveOnlyFunction<bool(PoolString)> _exists = [this](PoolString path) { return _game.Exists(path.ToStringView()); };
        SpriteAtlasFiles _atlasFiles{_game.Io(), _exists};
        TextureLibrary _library;
        IntrusivePtr<SpriteLibraryPanelModel> _model;
        int _saves = 0;
        bool _saveSucceeds = true;
    };

    TEST_F(SpriteLibraryPanelModelTest, NothingIsSelectedAtStartAndASelectionFillsTheForm) {
        EXPECT_FALSE(View().hasSelection);

        SelectAndUpdate("rook");

        EXPECT_TRUE(View().hasSelection);
        EXPECT_EQ(View().name, "rook");
        EXPECT_EQ(View().texture, "assets/rook.png");
        EXPECT_EQ(View().rectX, 1.0f);
        EXPECT_EQ(View().rectH, 4.0f);
        EXPECT_FALSE(View().packed);
        EXPECT_FALSE(View().dirty);
    }

    TEST_F(SpriteLibraryPanelModelTest, ARenameToAFreeNameRenamesTheEntryAndKeepsItSelected) {
        SelectAndUpdate("rook");
        View().name = "castle";
        _model->Update();

        EXPECT_TRUE(Run(EditorCommandIds::kSpriteLibraryRename));
        _model->Update();

        EXPECT_EQ(_library.GetSprite("rook"_intern), nullptr);
        EXPECT_EQ(Entry("castle").srcRect.w, 3.0f);
        EXPECT_EQ(_model->GetSelectedIndex(), 1);
        EXPECT_EQ(View().name, "castle");
        EXPECT_TRUE(View().dirty);
        EXPECT_FALSE(View().renameRefused);
    }

    TEST_F(SpriteLibraryPanelModelTest, ARenameToATakenNameWarnsUntilTheTypedNameOrTheSelectionChanges) {
        SelectAndUpdate("rook");
        View().name = "queen";
        _model->Update();
        Run(EditorCommandIds::kSpriteLibraryRename);
        _model->Update();

        EXPECT_TRUE(View().renameRefused);
        EXPECT_EQ(View().renameRefusedText, "A sprite named 'queen' already exists");
        EXPECT_EQ(View().name, "rook");
        EXPECT_NE(_library.GetSprite("rook"_intern), nullptr);
        EXPECT_FALSE(View().dirty);

        View().name = "rook2";
        _model->Update();
        EXPECT_FALSE(View().renameRefused);

        View().name = "queen";
        _model->Update();
        Run(EditorCommandIds::kSpriteLibraryRename);
        _model->Update();
        ASSERT_TRUE(View().renameRefused);
        SelectAndUpdate("queen");
        EXPECT_FALSE(View().renameRefused);
    }

    TEST_F(SpriteLibraryPanelModelTest, ARenameToAnEmptyNameIsRefusedWithoutAWarning) {
        SelectAndUpdate("rook");
        View().name.clear();
        _model->Update();
        Run(EditorCommandIds::kSpriteLibraryRename);
        _model->Update();

        EXPECT_FALSE(View().renameRefused);
        EXPECT_EQ(View().name, "rook");
        EXPECT_NE(_library.GetSprite("rook"_intern), nullptr);
    }

    TEST_F(SpriteLibraryPanelModelTest, ATextureRowOfTheListOrNoneIsWrittenIntoTheEntry) {
        ASSERT_EQ(View().textures.size(), 2U);
        EXPECT_EQ(View().textures[0], "(none)");
        EXPECT_EQ(View().textures[1], VirtualPath("board.png"));
        SelectAndUpdate("rook");

        View().texture = VirtualPath("board.png");
        _model->Update();
        EXPECT_EQ(Entry("rook").texturePath.ToStringView(), StringView(VirtualPath("board.png")));
        EXPECT_TRUE(View().dirty);

        View().texture = "(none)";
        _model->Update();
        EXPECT_TRUE(Entry("rook").texturePath.Empty());
        EXPECT_EQ(View().texture, "(none)");
    }

    TEST_F(SpriteLibraryPanelModelTest, RescanListsAnImageAddedToTheFolder) {
        WriteFile(_dir / "knight.png", "png");
        _model->Update();
        EXPECT_EQ(View().textures.size(), 2U);

        EXPECT_TRUE(Run(EditorCommandIds::kSpriteLibraryRescan));
        ASSERT_EQ(View().textures.size(), 3U);
        EXPECT_EQ(View().textures[2], VirtualPath("knight.png"));
    }

    TEST_F(SpriteLibraryPanelModelTest, ATextureThatIsNoRowChangesNothing) {
        SelectAndUpdate("rook");
        View().texture = "assets/not_listed.png";
        _model->Update();

        EXPECT_EQ(Entry("rook").texturePath, "assets/rook.png"_intern);
        EXPECT_EQ(View().texture, "assets/rook.png");
        EXPECT_FALSE(View().dirty);
    }

    TEST_F(SpriteLibraryPanelModelTest, AnEditedRectIsWrittenIntoTheEntry) {
        SelectAndUpdate("rook");
        View().rectX = 5.5f;
        View().rectW = 10.0f;
        _model->Update();

        EXPECT_EQ(Entry("rook").srcRect.x, 5.5f);
        EXPECT_EQ(Entry("rook").srcRect.y, 2.0f);
        EXPECT_EQ(Entry("rook").srcRect.w, 10.0f);
        EXPECT_TRUE(View().dirty);
    }

    TEST_F(SpriteLibraryPanelModelTest, APackedEntryIgnoresTheTextureAndTheRectAndTheViewGoesBack) {
        Pack("rook");
        SelectAndUpdate("rook");
        ASSERT_TRUE(View().packed);

        View().texture = "(none)";
        View().rectX = 50.0f;
        _model->Update();

        EXPECT_EQ(Entry("rook").texturePath, "assets/atlases/pieces.png"_intern);
        EXPECT_EQ(Entry("rook").srcRect.x, 1.0f);
        EXPECT_EQ(View().texture, "assets/atlases/pieces.png");
        EXPECT_EQ(View().rectX, 1.0f);
        EXPECT_FALSE(View().dirty);
    }

    TEST_F(SpriteLibraryPanelModelTest, AChangeOfTheLibraryUnderTheViewWinsOverTheStaleFields) {
        SelectAndUpdate("rook");
        _library.AddOrUpdate(EntryOf("rook", "assets/rook.png", Rect(7, 7, 7, 7)));
        _model->Update();

        EXPECT_EQ(Entry("rook").srcRect.x, 7.0f);
        EXPECT_EQ(View().rectX, 7.0f);
        EXPECT_FALSE(View().dirty);
    }

    TEST_F(SpriteLibraryPanelModelTest, AddSelectsANewUniqueEntryAndDeleteKeepsAValidSelection) {
        EXPECT_TRUE(Run(EditorCommandIds::kSpriteLibraryAdd));
        EXPECT_TRUE(Run(EditorCommandIds::kSpriteLibraryAdd));
        _model->Update();

        ASSERT_EQ(_library.GetAll().size(), 4U);
        EXPECT_EQ(View().name, "new_sprite_2");
        EXPECT_EQ(_model->GetSelectedIndex(), 3);
        EXPECT_TRUE(View().dirty);

        EXPECT_TRUE(Run(EditorCommandIds::kSpriteLibraryDelete));
        _model->Update();
        EXPECT_EQ(_library.GetSprite("new_sprite_2"_intern), nullptr);
        EXPECT_EQ(_model->GetSelectedIndex(), 2);
        EXPECT_EQ(View().name, "new_sprite");

        SelectAndUpdate("queen");
        Run(EditorCommandIds::kSpriteLibraryDelete);
        _model->Update();
        EXPECT_EQ(_model->GetSelectedIndex(), 0);
        EXPECT_EQ(View().name, "rook");
    }

    TEST_F(SpriteLibraryPanelModelTest, DeletingTheOnlyEntryLeavesNoSelection) {
        TextureLibrary single;
        single.AddOrUpdate(EntryOf("pawn", "", Rect()));
        _model->UseLibrary(&single, [](TextureLibrary&) { return true; });
        _model->Select(0);
        Run(EditorCommandIds::kSpriteLibraryDelete);
        _model->Update();

        EXPECT_TRUE(single.GetAll().empty());
        EXPECT_FALSE(View().hasSelection);
        _model->UseLibrary(&_library, [](TextureLibrary&) { return true; });
    }

    TEST_F(SpriteLibraryPanelModelTest, TextureSaveSavesOnlyWithUnsavedChanges) {
        EditorCommandEvent::Emit(EditorCommandIds::kTextureSave);
        EXPECT_EQ(_saves, 0);

        Run(EditorCommandIds::kSpriteLibraryAdd);
        EditorCommandEvent::Emit(EditorCommandIds::kTextureSave);
        _model->Update();
        EXPECT_EQ(_saves, 1);
        EXPECT_FALSE(View().dirty);
    }

    TEST_F(SpriteLibraryPanelModelTest, AFailedSaveKeepsTheUnsavedFlag) {
        _saveSucceeds = false;
        Run(EditorCommandIds::kSpriteLibraryAdd);
        EditorCommandEvent::Emit(EditorCommandIds::kTextureSave);
        _model->Update();

        EXPECT_EQ(_saves, 1);
        EXPECT_TRUE(View().dirty);
    }

    TEST_F(SpriteLibraryPanelModelTest, TextureSaveIsNotACommandOfThePanel) {
        Run(EditorCommandIds::kSpriteLibraryAdd);
        EXPECT_FALSE(Run(EditorCommandIds::kTextureSave));
        EXPECT_EQ(_saves, 0);
    }

    TEST_F(SpriteLibraryPanelModelTest, TheDocumentRowReportsTheUnsavedFlag) {
        Vector<EditorDocumentState> rows;
        _model->AppendDocuments(rows);
        ASSERT_EQ(rows.size(), 1U);
        EXPECT_EQ(rows[0].kind, EditorDocumentKind::Library);
        EXPECT_EQ(rows[0].name, "sprites");
        EXPECT_FALSE(rows[0].dirty);

        Run(EditorCommandIds::kSpriteLibraryAdd);
        rows.clear();
        _model->AppendDocuments(rows);
        ASSERT_EQ(rows.size(), 1U);
        EXPECT_TRUE(rows[0].dirty);
    }

    TEST_F(SpriteLibraryPanelModelTest, AClickNotedByTheListSelectsOnlyAfterTheWalk) {
        _model->NoteListClick(1);
        EXPECT_EQ(_model->GetSelectedIndex(), -1);

        EndFrame();

        EXPECT_EQ(_model->GetSelectedIndex(), 1);
        EXPECT_EQ(View().name, "rook");
    }

    TEST_F(SpriteLibraryPanelModelTest, ADragNotedByThePreviewWritesTheRectAndTheUnsavedFlag) {
        SelectAndUpdate("rook");
        _model->NoteDraggedRect(Rect(2, 3, 10, 20));
        EXPECT_EQ(Entry("rook").srcRect.x, 1.0f);

        EndFrame();

        EXPECT_EQ(Entry("rook").srcRect.w, 10.0f);
        EXPECT_EQ(View().rectH, 20.0f);
        EXPECT_TRUE(View().dirty);
    }

    TEST_F(SpriteLibraryPanelModelTest, ADragOverAPackedEntryChangesNothing) {
        Pack("rook");
        SelectAndUpdate("rook");
        _model->NoteDraggedRect(Rect(2, 3, 10, 20));
        EndFrame();

        EXPECT_EQ(Entry("rook").srcRect.x, 1.0f);
        EXPECT_FALSE(View().dirty);
    }

    TEST_F(SpriteLibraryPanelModelTest, ADropOnTheFreeAreaOpensTheDialogWithTheSuggestedName) {
        _model->NoteDrop(DropOf(ArtGenDragKind::Image, -1, "knight"));
        EndFrame();

        EXPECT_TRUE(View().dropOpen);
        EXPECT_EQ(View().dropName, "knight");
        EXPECT_FALSE(View().dropRefused);
        EXPECT_FALSE(View().dropTaken);
    }

    TEST_F(SpriteLibraryPanelModelTest, ATakenNameInTheDialogNamesTheFreeOneAndARefusedOneTurnsAcceptOff) {
        _model->NoteDrop(DropOf(ArtGenDragKind::Image, -1, "knight"));
        EndFrame();

        View().dropName = "queen";
        _model->Update();
        EXPECT_TRUE(View().dropTaken);
        EXPECT_EQ(View().dropTakenText, "Taken: it will be added as 'queen_2'");
        EXPECT_FALSE(View().dropRefused);

        View().dropName = "bad name";
        _model->Update();
        EXPECT_TRUE(View().dropRefused);
        EXPECT_FALSE(View().dropReason.empty());
        EXPECT_FALSE(View().dropTaken);

        Run(EditorCommandIds::kSpriteLibraryDropAccept);
        EXPECT_TRUE(View().dropOpen) << "a refused name is never accepted";
    }

    TEST_F(SpriteLibraryPanelModelTest, CancelClosesTheDialogAndAddsNothing) {
        _model->NoteDrop(DropOf(ArtGenDragKind::Image, -1, "knight"));
        EndFrame();

        EXPECT_TRUE(Run(EditorCommandIds::kSpriteLibraryDropCancel));
        _model->Update();

        EXPECT_FALSE(View().dropOpen);
        EXPECT_EQ(_library.GetAll().size(), 2U);
        EXPECT_FALSE(View().hasDropMessage);
    }

    TEST_F(SpriteLibraryPanelModelTest, AcceptClosesTheDialogAndShowsTheAnswerOfTheGeneratePanel) {
        _model->NoteDrop(DropOf(ArtGenDragKind::Image, -1, "knight"));
        EndFrame();

        EXPECT_TRUE(Run(EditorCommandIds::kSpriteLibraryDropAccept));
        _model->Update();

        EXPECT_FALSE(View().dropOpen);
        EXPECT_TRUE(View().hasDropMessage);
        EXPECT_TRUE(View().dropFailed) << "no Generate panel answers in this test";
        EXPECT_EQ(View().dropMessage, ArtGenDropResult{}.message);
    }

    TEST_F(SpriteLibraryPanelModelTest, ARefusedDropOnARowSetsTheErrorAnswer) {
        _model->NoteDrop(DropOf(ArtGenDragKind::Audio, 1, "rook"));
        EndFrame();

        EXPECT_FALSE(View().dropOpen);
        EXPECT_TRUE(View().hasDropMessage);
        EXPECT_TRUE(View().dropFailed);
        EXPECT_EQ(View().dropMessage, kArtGenSoundIsNotASprite);
        EXPECT_EQ(Entry("rook").texturePath, "assets/rook.png"_intern);
    }

    TEST_F(SpriteLibraryPanelModelTest, TheSliceSectionNeedsTheSheetOfTheSelectedEntryLoaded) {
        SelectAndUpdate("rook");
        EXPECT_FALSE(View().slice.canSlice);

        _model->NoteSheet("assets/queen.png"_intern, 60, 20);
        _model->Update();
        EXPECT_FALSE(View().slice.canSlice) << "the preview's sheet is another entry's";

        _model->NoteSheet("assets/rook.png"_intern, 60, 20);
        _model->Update();
        EXPECT_TRUE(View().slice.canSlice);

        Pack("rook");
        _model->Update();
        EXPECT_FALSE(View().slice.canSlice);
    }

    TEST_F(SpriteLibraryPanelModelTest, TheGridSummarySaysWhatAddSpritesWouldDo) {
        SelectSheet("assets/rook.png", 60, 20);

        EXPECT_EQ(View().slice.summary, "12 cells: 12 to add, 0 to replace, 0 skipped");
        EXPECT_FALSE(View().slice.hasError);
        EXPECT_TRUE(View().slice.canAdd);
    }

    TEST_F(SpriteLibraryPanelModelTest, AGridOfCellsSmallerThanAPixelIsAnErrorAndCannotBeAdded) {
        SelectSheet("assets/rook.png", 60, 20);
        View().slice.rows = 40;
        _model->Update();

        EXPECT_TRUE(View().slice.hasError);
        EXPECT_EQ(View().slice.summary, SheetGridErrorText(SheetGridError::CellTooSmall));
        EXPECT_FALSE(View().slice.canAdd);
    }

    TEST_F(SpriteLibraryPanelModelTest, TheFieldsAreClampedToTheOldRangesInTheView) {
        SelectSheet("assets/rook.png", 60, 20);
        View().slice.columns = 0;
        View().slice.rows = 1000;
        View().slice.alphaThreshold = 300;
        View().slice.startIndex = -5;
        View().slice.prefix = String(100, 'p');
        _model->Update();

        EXPECT_EQ(View().slice.columns, 1);
        EXPECT_EQ(View().slice.rows, kMaxSheetGridAxis);
        EXPECT_EQ(View().slice.alphaThreshold, kMaxIslandAlphaThreshold);
        EXPECT_EQ(View().slice.startIndex, 0);
        EXPECT_EQ(View().slice.prefix.size(), 64U);
    }

    TEST_F(SpriteLibraryPanelModelTest, AddSpritesAddsTheCellsAndSelectsTheFirst) {
        SelectSheet("assets/rook.png", 60, 20);

        EXPECT_TRUE(Run(EditorCommandIds::kSpriteLibrarySliceAdd));
        _model->Update();

        EXPECT_EQ(_library.GetAll().size(), 14U);
        EXPECT_EQ(View().name, "sprite_00");
        EXPECT_EQ(Entry("sprite_11").srcRect.x, 50.0f);
        EXPECT_TRUE(View().dirty);
    }

    TEST_F(SpriteLibraryPanelModelTest, TheConflictPolicyDecidesWhatATakenNameDoes) {
        SelectSheet("assets/rook.png", 60, 20);
        View().slice.columns = 2;
        View().slice.rows = 1;
        View().slice.names = "queen, rook";
        _model->Update();
        EXPECT_EQ(View().slice.summary, "2 cells: 0 to add, 2 to replace, 0 skipped");

        View().slice.policy = SheetConflictPolicy::Skip;
        _model->Update();
        EXPECT_EQ(View().slice.summary, "2 cells: 0 to add, 0 to replace, 2 skipped");
        EXPECT_FALSE(View().slice.canAdd);
    }

    TEST_F(SpriteLibraryPanelModelTest, TheIslandsAreFoundOnlyByTheCommandAndAnEditedFieldClearsThem) {
        const RgbaImage sheet = MakeSolid(8, 4, kHigh, 0, 0);
        ASSERT_TRUE(SaveRgbaPng(_dir / "sheet.png", sheet).has_value());
        SelectSheet(VirtualPath("sheet.png"), 8, 4);
        View().slice.mode = SheetSliceMode::Islands;
        _model->Update();
        EXPECT_EQ(View().slice.summary, "Press Find islands");
        EXPECT_FALSE(View().slice.canAdd);

        EXPECT_TRUE(Run(EditorCommandIds::kSpriteLibrarySliceFindIslands));
        _model->Update();
        EXPECT_EQ(View().slice.summary, "1 islands: 1 to add, 0 to replace, 0 skipped");
        EXPECT_TRUE(View().slice.canAdd);

        View().slice.padding = 1;
        _model->Update();
        EXPECT_EQ(View().slice.summary, "Press Find islands");
    }

    TEST_F(SpriteLibraryPanelModelTest, AGroupNameIsAppliedOnCommitAndABadOneIsTheStatusError) {
        SelectAndUpdate("rook");
        EXPECT_FALSE(View().atlas.hasGroup);
        EXPECT_FALSE(View().atlas.canPack);

        View().atlas.name = "pieces";
        _model->Update();
        EXPECT_FALSE(Entry("rook").atlas.has_value()) << "the field is applied on its commit only";
        EXPECT_TRUE(Run(EditorCommandIds::kSpriteLibraryAtlasName));
        _model->Update();

        ASSERT_TRUE(Entry("rook").atlas.has_value());
        EXPECT_EQ(Entry("rook").atlas->name, "pieces"_intern);
        EXPECT_TRUE(View().dirty);
        EXPECT_TRUE(View().atlas.hasGroup);
        EXPECT_EQ(View().atlas.summary, "Atlas 'pieces': 1 sprites, 0 packed");
        EXPECT_TRUE(View().atlas.canPack);
        EXPECT_FALSE(View().atlas.canUnpack);

        View().atlas.name = "bad name";
        Run(EditorCommandIds::kSpriteLibraryAtlasName);
        _model->Update();
        EXPECT_EQ(Entry("rook").atlas->name, "pieces"_intern);
        EXPECT_TRUE(View().atlas.hasStatus);
        EXPECT_TRUE(View().atlas.statusError);
        EXPECT_FALSE(View().atlas.status.empty());

        SelectAndUpdate("queen");
        EXPECT_FALSE(View().atlas.hasStatus) << "another sprite starts with no status";
    }

    TEST_F(SpriteLibraryPanelModelTest, TheSettingsFollowTheGroupsPackRecordWhenTheSelectionMovesToIt) {
        SpriteEntry queen = Entry("queen");
        SpriteAtlasLink link;
        link.name = "ui"_intern;
        AtlasPackRecord record;
        record.padding = 3;
        record.sizeMode = "Tight"_intern;
        record.maxSide = kDefaultAtlasMaxSide;
        link.pack = record;
        queen.atlas = link;
        _library.AddOrUpdate(std::move(queen));

        SelectAndUpdate("rook");
        EXPECT_EQ(View().atlas.padding, kDefaultAtlasPadding);
        EXPECT_EQ(View().atlas.sizeMode, AtlasSizeMode::PowerOfTwo);

        SelectAndUpdate("queen");
        EXPECT_EQ(View().atlas.name, "ui");
        EXPECT_EQ(View().atlas.padding, 3);
        EXPECT_EQ(View().atlas.sizeMode, AtlasSizeMode::Tight);
    }

    TEST_F(SpriteLibraryPanelModelTest, ThePaddingIsClampedInTheView) {
        SelectAndUpdate("rook");
        View().atlas.padding = 100;
        _model->Update();
        EXPECT_EQ(View().atlas.padding, kMaxAtlasPadding);
        EXPECT_EQ(_model->GetAtlasSettings().padding, kMaxAtlasPadding);
    }

    TEST_F(SpriteLibraryPanelModelTest, PackAndUnpackChangeTheGroupAndSayWhatTheyDid) {
        FillPieces();
        SelectAndUpdate("queen");

        EXPECT_TRUE(Run(EditorCommandIds::kSpriteLibraryPack));
        _model->Update();

        EXPECT_TRUE(IsPackedSprite(Entry("queen")));
        EXPECT_TRUE(IsPackedSprite(Entry("rook")));
        EXPECT_TRUE(_game.Exists("assets/atlases/pieces.png"));
        EXPECT_TRUE(View().dirty);
        EXPECT_FALSE(View().atlas.statusError);
        EXPECT_EQ(View().atlas.status.find("Packed 2 sprites into assets/atlases/pieces.png"), 0U);
        EXPECT_TRUE(View().atlas.packed);
        EXPECT_EQ(View().atlas.packedText, "Packed on 'pieces': Unpack to change");
        EXPECT_TRUE(View().atlas.canUnpack);
        EXPECT_TRUE(View().packed);

        EXPECT_TRUE(Run(EditorCommandIds::kSpriteLibraryUnpack));
        _model->Update();

        EXPECT_FALSE(IsPackedSprite(Entry("queen")));
        EXPECT_EQ(Entry("queen").texturePath.ToStringView(), kQueenPng);
        EXPECT_EQ(View().atlas.status.find("Unpacked 2 sprites"), 0U);
        EXPECT_FALSE(View().atlas.packed);
        EXPECT_FALSE(View().atlas.canUnpack);
    }

    TEST_F(SpriteLibraryPanelModelTest, AFailedPackIsTheStatusErrorAndChangesNothing) {
        FillPieces();
        RemoveFileIfPresent(_game.Resolve(kRookPng));
        SelectAndUpdate("queen");

        Run(EditorCommandIds::kSpriteLibraryPack);
        _model->Update();

        EXPECT_FALSE(IsPackedSprite(Entry("queen")));
        EXPECT_TRUE(View().atlas.statusError);
        EXPECT_FALSE(View().dirty);
    }

    TEST_F(SpriteLibraryPanelModelTest, ThePreviewShowsThePlanWhileNothingIsPackedAndKeepsTheChoice) {
        FillPieces();
        SelectAndUpdate("queen");
        EXPECT_TRUE(View().preview.hasGroup);
        EXPECT_FALSE(View().preview.anyPacked);
        EXPECT_EQ(View().preview.mode, AtlasPreviewMode::Plan);

        Run(EditorCommandIds::kSpriteLibraryPack);
        _model->Update();
        EXPECT_TRUE(View().preview.anyPacked);
        EXPECT_EQ(View().preview.mode, AtlasPreviewMode::Current) << "the choice it had before";

        View().preview.mode = AtlasPreviewMode::Plan;
        _model->Update();
        EXPECT_EQ(View().preview.mode, AtlasPreviewMode::Plan);

        SelectAndUpdate("rook");
        EXPECT_EQ(View().preview.mode, AtlasPreviewMode::Plan);
    }

    TEST_F(SpriteLibraryPanelModelTest, ARectClickedOnTheAtlasPreviewSelectsItsSpriteAfterTheWalk) {
        SelectAndUpdate("queen");
        _model->NotePreviewClick("rook"_intern);
        EXPECT_EQ(View().name, "queen");

        EndFrame();

        EXPECT_EQ(View().name, "rook");
    }

    TEST_F(SpriteLibraryPanelModelTest, ASteadyUpdateAllocatesNothing) {
        SelectSheet("assets/rook.png", 60, 20);
        _model->Update();
        ExpectNoAllocations([&] {
            for (int frame = 0; frame < kSteadyFrames; ++frame) {
                _model->Update();
            }
        });
    }

}  // namespace BECore::Tests
