#include <BECore/Save/SaveGame.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>

namespace BECore::Tests {

    namespace {

        Vector<uint8_t> Bytes(std::initializer_list<uint8_t> values) {
            return Vector<uint8_t>(values);
        }

        Vector<String> Names(const Vector<PoolString>& slots) {
            Vector<String> names;
            for (const PoolString& slot : slots) {
                names.emplace_back(slot.ToStringView().data(), slot.ToStringView().size());
            }
            return names;
        }

        bool HasTempFile(const std::filesystem::path& directory) {
            for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                if (entry.path().extension().string() == ToStd(kSaveTempExtension)) {
                    return true;
                }
            }
            return false;
        }

        /// The kind of a failed result; a success is a test failure of its own (the value returned for it is only a placeholder)
        template <typename T>
        SaveErrorKind KindOf(const std::expected<T, SaveError>& result) {
            EXPECT_FALSE(result.has_value()) << "a failure was expected";
            return result.has_value() ? SaveErrorKind::NotInitialized : result.error().kind;
        }

    }  // namespace

    /// A local FileSystem and a temp folder per test: no global services, so the tests run beside each other
    class SaveGameFilesTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _base = TestTempDirectory("BagiSaveGameTest");
            std::filesystem::remove_all(_base);
            std::filesystem::create_directories(_base);
            _saves = _base / "saves";
        }

        void TearDown() override {
            std::error_code ec;
            std::filesystem::remove_all(_base, ec);
        }

        std::filesystem::path _base;
        std::filesystem::path _saves;
        FileSystem _fileSystem;
    };

    TEST_F(SaveGameFilesTest, WrittenBytesAreReadBack) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_saves));

        const auto bytes = Bytes({0, 1, 2, 255, 0, 10, 13});
        ASSERT_TRUE(service.WriteSlot("slot1", bytes).has_value());

        const auto read = service.ReadSlot("slot1");
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(*read, bytes);
        EXPECT_TRUE(std::filesystem::is_regular_file(_saves / "slot1.save"));
    }

    TEST_F(SaveGameFilesTest, EmptyBytesAreAValidSave) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_saves));

        ASSERT_TRUE(service.WriteSlot("empty", {}).has_value());
        const auto read = service.ReadSlot("empty");
        ASSERT_TRUE(read.has_value());
        EXPECT_TRUE(read->empty());
    }

    TEST_F(SaveGameFilesTest, SecondWriteReplacesTheFirst) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_saves));

        ASSERT_TRUE(service.WriteSlot("auto", Bytes({1, 2, 3, 4, 5})).has_value());
        ASSERT_TRUE(service.WriteSlot("auto", Bytes({9})).has_value());

        const auto read = service.ReadSlot("auto");
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(*read, Bytes({9}));
    }

    TEST_F(SaveGameFilesTest, NoTempFileIsLeftAfterAWrite) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_saves));

        ASSERT_TRUE(service.WriteSlot("a", Bytes({1})).has_value());
        ASSERT_TRUE(service.WriteSlot("a", Bytes({2})).has_value());
        EXPECT_FALSE(HasTempFile(_saves));
    }

    TEST_F(SaveGameFilesTest, FailedWriteLeavesNoTempFileAndKeepsTheTarget) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_saves));

        // A folder where the save file should go: the rename cannot replace it
        std::filesystem::create_directories(_saves / "blocked.save");

        const auto result = service.WriteSlot("blocked", Bytes({1, 2, 3}));
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().kind, SaveErrorKind::WriteFailed);
        EXPECT_FALSE(result.error().detail.empty());
        EXPECT_TRUE(std::filesystem::is_directory(_saves / "blocked.save"));
        EXPECT_FALSE(HasTempFile(_saves));
    }

    TEST_F(SaveGameFilesTest, ReadOfAFolderInPlaceOfASaveFails) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_saves));
        std::filesystem::create_directories(_saves / "folder.save");

        EXPECT_EQ(KindOf(service.ReadSlot("folder")), SaveErrorKind::ReadFailed);
        EXPECT_FALSE(service.Exists("folder"));
        EXPECT_FALSE(service.Delete("folder"));
    }

    TEST_F(SaveGameFilesTest, ExistsAndDelete) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_saves));

        EXPECT_FALSE(service.Exists("one"));
        EXPECT_FALSE(service.Delete("one"));

        ASSERT_TRUE(service.WriteSlot("one", Bytes({1})).has_value());
        EXPECT_TRUE(service.Exists("one"));

        EXPECT_TRUE(service.Delete("one"));
        EXPECT_FALSE(service.Exists("one"));
        EXPECT_FALSE(std::filesystem::exists(_saves / "one.save"));
        EXPECT_FALSE(service.Delete("one"));
    }

    TEST_F(SaveGameFilesTest, ListIsSortedAndHasSavesOnly) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_saves));
        EXPECT_TRUE(service.List().empty());

        ASSERT_TRUE(service.WriteSlot("beta", Bytes({1})).has_value());
        ASSERT_TRUE(service.WriteSlot("alpha", Bytes({1})).has_value());
        ASSERT_TRUE(service.WriteSlot("Gamma_2", Bytes({1})).has_value());
        ASSERT_TRUE(service.WriteSlot("a-b", Bytes({1})).has_value());

        // Not saves: a leftover temporary file, another extension, a name that is not a slot name, a file in a subfolder
        std::ofstream(_saves / "leftover.save.tmp") << "x";
        std::ofstream(_saves / "notes.txt") << "x";
        std::ofstream(_saves / "bad name.save") << "x";
        std::filesystem::create_directories(_saves / "sub");
        std::ofstream(_saves / "sub" / "deep.save") << "x";

        const Vector<String> expected = {"Gamma_2", "a-b", "alpha", "beta"};
        EXPECT_EQ(Names(service.List()), expected);
    }

    TEST_F(SaveGameFilesTest, ReadOfAMissingSlotIsNotFound) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_saves));

        const auto read = service.ReadSlot("nothing");
        ASSERT_FALSE(read.has_value());
        EXPECT_EQ(read.error().kind, SaveErrorKind::NotFound);
        EXPECT_FALSE(read.error().detail.empty());
    }

    TEST_F(SaveGameFilesTest, FileOfTheWorkingDirectoryIsNotASave) {
        // ResolvePath falls back to paths relative to the working directory; "saves/stray.save" there is not in the save folder
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_base / "other"));
        std::filesystem::create_directories(_base / "saves");
        std::ofstream(_base / "saves" / "stray.save") << "x";

        // Changing the working directory is safe here: nothing else in this process uses a relative path meanwhile (the
        // test is single threaded), and the old directory is restored before any check
        const std::filesystem::path previous = std::filesystem::current_path();
        std::filesystem::current_path(_base);
        const bool exists = service.Exists("stray");
        const bool deleted = service.Delete("stray");
        const auto read = service.ReadSlot("stray");
        std::filesystem::current_path(previous);

        EXPECT_FALSE(exists);
        EXPECT_FALSE(deleted);
        EXPECT_EQ(KindOf(read), SaveErrorKind::NotFound);
        EXPECT_TRUE(std::filesystem::exists(_base / "saves" / "stray.save"));
    }

    TEST_F(SaveGameFilesTest, BadSlotNamesAreRefusedByEveryCall) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_saves));

        const String tooLong(kMaxSlotNameLength + 1, 'a');
        const String longest(kMaxSlotNameLength, 'a');
        const String names[] = {"", "../x", "a/b", "a\\b", "a.b", "a b", "x.save", "..", tooLong, "caf\xC3\xA9"};

        for (const String& name : names) {
            SCOPED_TRACE(name.c_str());
            EXPECT_FALSE(SaveGameService::IsValidSlotName(name));

            const auto write = service.WriteSlot(name, Bytes({1}));
            ASSERT_FALSE(write.has_value());
            EXPECT_EQ(write.error().kind, SaveErrorKind::InvalidSlotName);
            EXPECT_FALSE(write.error().detail.empty());

            EXPECT_EQ(KindOf(service.ReadSlot(name)), SaveErrorKind::InvalidSlotName);
            EXPECT_FALSE(service.Exists(name));
            EXPECT_FALSE(service.Delete(name));
        }

        // The longest name is fine, and nothing was written for the bad ones
        EXPECT_TRUE(SaveGameService::IsValidSlotName(longest));
        EXPECT_TRUE(SaveGameService::IsValidSlotName("A_z-09"));
        EXPECT_TRUE(service.WriteSlot(longest, Bytes({1})).has_value());
        EXPECT_EQ(service.List().size(), 1u);
    }

    TEST_F(SaveGameFilesTest, EveryCallIsNotInitializedBeforeInitialize) {
        SaveGameService service(_fileSystem);
        EXPECT_FALSE(service.IsInitialized());

        const auto write = service.WriteSlot("a", Bytes({1}));
        ASSERT_FALSE(write.has_value());
        EXPECT_EQ(write.error().kind, SaveErrorKind::NotInitialized);
        EXPECT_FALSE(write.error().detail.empty());

        const auto read = service.ReadSlot("a");
        ASSERT_FALSE(read.has_value());
        EXPECT_EQ(read.error().kind, SaveErrorKind::NotInitialized);

        EXPECT_FALSE(service.Exists("a"));
        EXPECT_FALSE(service.Delete("a"));
        EXPECT_TRUE(service.List().empty());
    }

    TEST_F(SaveGameFilesTest, InitializeCreatesTheFolderAndMountsIt) {
        SaveGameService service(_fileSystem);
        const std::filesystem::path nested = _base / "a" / "b" / "saves";
        ASSERT_TRUE(service.Initialize(nested));

        EXPECT_TRUE(service.IsInitialized());
        EXPECT_TRUE(std::filesystem::is_directory(nested));
        EXPECT_EQ(_fileSystem.GetMountLayers(PoolString::Intern(kSaveMountPoint)).size(), 1u);
        EXPECT_TRUE(std::filesystem::equivalent(_fileSystem.GetMountedPath(PoolString::Intern(kSaveMountPoint)), nested));
        EXPECT_TRUE(std::filesystem::equivalent(service.GetDirectory(), nested));
    }

    TEST_F(SaveGameFilesTest, InitializeOnAFileIsFalse) {
        const std::filesystem::path file = _base / "not_a_folder";
        std::ofstream(file) << "x";

        SaveGameService service(_fileSystem);
        EXPECT_FALSE(service.Initialize(file));
        EXPECT_FALSE(service.IsInitialized());
        EXPECT_EQ(KindOf(service.ReadSlot("a")), SaveErrorKind::NotInitialized);
        EXPECT_FALSE(service.Initialize({}));
    }

    TEST_F(SaveGameFilesTest, SecondInitializeMountsTheNewFolder) {
        SaveGameService service(_fileSystem);
        const std::filesystem::path first = _base / "first";
        const std::filesystem::path second = _base / "second";

        ASSERT_TRUE(service.Initialize(first));
        ASSERT_TRUE(service.WriteSlot("shared", Bytes({1})).has_value());
        ASSERT_TRUE(service.WriteSlot("only_first", Bytes({1})).has_value());

        ASSERT_TRUE(service.Initialize(second));
        EXPECT_EQ(_fileSystem.GetMountLayers(PoolString::Intern(kSaveMountPoint)).size(), 1u);
        EXPECT_FALSE(service.Exists("only_first"));
        EXPECT_EQ(KindOf(service.ReadSlot("shared")), SaveErrorKind::NotFound);
        EXPECT_TRUE(service.List().empty());

        ASSERT_TRUE(service.WriteSlot("shared", Bytes({2, 2})).has_value());
        EXPECT_TRUE(std::filesystem::exists(second / "shared.save"));
        const auto read = service.ReadSlot("shared");
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(*read, Bytes({2, 2}));

        // The first folder keeps its files
        EXPECT_TRUE(std::filesystem::exists(first / "only_first.save"));
    }

    TEST_F(SaveGameFilesTest, FailedSecondInitializeChangesNothing) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_saves));
        ASSERT_TRUE(service.WriteSlot("keep", Bytes({7})).has_value());

        const std::filesystem::path file = _base / "not_a_folder";
        std::ofstream(file) << "x";
        EXPECT_FALSE(service.Initialize(file));

        EXPECT_TRUE(service.IsInitialized());
        const auto read = service.ReadSlot("keep");
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(*read, Bytes({7}));
    }

    TEST_F(SaveGameFilesTest, ErrorKindNamesAreStable) {
        EXPECT_EQ(EnumUtils<SaveErrorKind>::ToString(SaveErrorKind::NotFound), "NotFound");
        EXPECT_EQ(EnumUtils<SaveErrorKind>::ToString(SaveErrorKind::VersionTooOld), "VersionTooOld");
    }

}  // namespace BECore::Tests
