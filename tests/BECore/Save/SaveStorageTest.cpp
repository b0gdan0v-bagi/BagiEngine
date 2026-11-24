#include <BECore/Save/SaveGame.h>
#include <BECore/Save/SaveStorage.h>
#include <BECore/Save/SaveTestSupport.h>
#include <Common/DestroyTally.h>
#include <Common/Printers.h>
#include <Common/TestFiles.h>

namespace BECore::Tests {

    namespace {

        constexpr SaveSchema kSchema{"StorageGame", 1, 1};

        struct TinyState {
            int32_t value = 0;

            void Serialize(ISerializer& serializer) const {
                serializer.WriteAttribute("value", value);
            }
        };

    }  // namespace

    /// Counts the calls of NotifySaveChanged() with a fake handler
    class SaveStorageTest : public SaveFolderTest {
    protected:
        void SetUp() override {
            ASSERT_NO_FATAL_FAILURE(SaveFolderTest::SetUp());
            SetSaveChangedHandler([this] { ++_notifications; });
        }

        void TearDown() override {
            SetSaveChangedHandler({});
            SaveFolderTest::TearDown();
        }

        int _notifications = 0;
    };

    TEST_F(SaveStorageTest, SuccessfulWriteNotifiesOnce) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_base / "saves"));
        EXPECT_EQ(_notifications, 0) << "Initialize writes no save";

        ASSERT_TRUE(service.WriteSlot("one", Bytes({1, 2, 3})).has_value());
        EXPECT_EQ(_notifications, 1);
        ASSERT_TRUE(service.WriteSlot("one", Bytes({4})).has_value());
        EXPECT_EQ(_notifications, 2) << "an overwrite is a change too";
    }

    TEST_F(SaveStorageTest, SuccessfulDeleteNotifiesOnce) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_base / "saves"));
        ASSERT_TRUE(service.WriteSlot("one", Bytes({1})).has_value());
        _notifications = 0;

        EXPECT_TRUE(service.Delete("one"));
        EXPECT_EQ(_notifications, 1);
    }

    TEST_F(SaveStorageTest, DeleteOfMissingSlotDoesNotNotify) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_base / "saves"));

        EXPECT_FALSE(service.Delete("nothing"));
        EXPECT_FALSE(service.Delete("bad name"));
        EXPECT_EQ(_notifications, 0);
    }

    TEST_F(SaveStorageTest, FailedWritesDoNotNotify) {
        SaveGameService service(_fileSystem);
        EXPECT_FALSE(service.WriteSlot("one", Bytes({1})).has_value()) << "before Initialize";
        EXPECT_EQ(_notifications, 0);

        ASSERT_TRUE(service.Initialize(_base / "saves"));
        EXPECT_FALSE(service.WriteSlot("../escape", Bytes({1})).has_value()) << "bad slot name";
        EXPECT_FALSE(service.WriteSlot("", Bytes({1})).has_value());

        const Vector<uint8_t> tooBig(ReaderLimits::kMaxBinaryStreamBytes + 1);
        EXPECT_FALSE(service.WriteSlot("big", tooBig).has_value()) << "over the size limit";
        EXPECT_EQ(_notifications, 0);
    }

    TEST_F(SaveStorageTest, RenameFailureDoesNotNotify) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_base / "saves"));
        // A non-empty folder in place of the save: the rename of the temporary file over it fails
        const Path blocker = _base / "saves" / "one.save";
        MakeDirectory(blocker);
        WriteFile(blocker / "inside", "x");

        EXPECT_FALSE(service.WriteSlot("one", Bytes({1})).has_value());
        EXPECT_EQ(_notifications, 0);
    }

    TEST_F(SaveStorageTest, EnvelopeSaveNotifiesOnce) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_base / "saves"));

        TinyState state;
        state.value = 7;
        ASSERT_TRUE(service.Save("slot", kSchema, state).has_value());
        EXPECT_EQ(_notifications, 1);
    }

    TEST_F(SaveStorageTest, ReadsDoNotNotify) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_base / "saves"));
        ASSERT_TRUE(service.WriteSlot("one", Bytes({1})).has_value());
        _notifications = 0;

        EXPECT_TRUE(service.ReadSlot("one").has_value());
        EXPECT_TRUE(service.Exists("one"));
        EXPECT_EQ(service.List().size(), 1u);
        EXPECT_EQ(_notifications, 0);
    }

    TEST_F(SaveStorageTest, EmptyHandlerRestoresTheDefault) {
        SaveGameService service(_fileSystem);
        ASSERT_TRUE(service.Initialize(_base / "saves"));

        SetSaveChangedHandler({});
        // The default does nothing off the browser: the call must neither crash nor reach the old fake
        ASSERT_TRUE(service.WriteSlot("one", Bytes({1})).has_value());
        NotifySaveChanged();
        EXPECT_EQ(_notifications, 0);
    }

    TEST_F(SaveStorageTest, AMoveOnlyHandlerIsCalledAndDestroyedWhenReplaced) {
        int destroyed = 0;
        int calls = 0;
        SetSaveChangedHandler([token = DestroyTally(destroyed), &calls] { ++calls; });
        NotifySaveChanged();
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(destroyed, 0);

        SetSaveChangedHandler({});
        EXPECT_EQ(destroyed, 1);
    }

#ifndef __EMSCRIPTEN__
    TEST_F(SaveStorageTest, DesktopHasNoPersistentDirectoryAndNoState) {
        EXPECT_TRUE(PersistentSaveDirectory().Empty());
        EXPECT_EQ(GetSaveStorageState(), SaveStorageState::NotApplicable);
    }
#endif

}  // namespace BECore::Tests
