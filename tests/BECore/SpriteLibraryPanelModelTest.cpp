#include <BECore/Resource/TextureLibrary.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/MountedFolderTest.h>
#include <Common/Printers.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/TextureEditor/SpriteLibraryPanelModel.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kSteadyFrames = 8;
        constexpr const char* kMount = "sprite_library_model";

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

    TEST_F(SpriteLibraryPanelModelTest, ASteadyUpdateAllocatesNothing) {
        SelectAndUpdate("rook");
        _model->Update();
        ExpectNoAllocations([&] {
            for (int frame = 0; frame < kSteadyFrames; ++frame) {
                _model->Update();
            }
        });
    }

}  // namespace BECore::Tests
