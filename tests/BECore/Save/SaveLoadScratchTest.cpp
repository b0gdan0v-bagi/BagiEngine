#include <BECore/Reflection/ReaderLimits.h>
#include <BECore/Save/SaveGame.h>
#include <BECore/Save/SaveTestSupport.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>

namespace BECore::Tests {

    namespace {

        constexpr SaveSchema kSchema{"ScratchGame", 1, 1};
        constexpr StringView kScoreKey = "score";
        constexpr StringView kBlobKey = "blob";
        constexpr int32_t kScore = 4321;
        constexpr size_t kMidBlobBytes = 100 * 1024;          // under the scratch bound
        constexpr size_t kOverScratchBlobBytes = 300 * 1024;  // a slot over the bound the loads read into scratch

        void WriteMidState(ISerializer& serializer) {
            serializer.WriteAttribute(kScoreKey, kScore);
            serializer.WriteAttribute(kBlobKey, String(kMidBlobBytes, 'x'));
        }

        void WriteBigState(ISerializer& serializer) {
            serializer.WriteAttribute(kScoreKey, kScore);
            serializer.WriteAttribute(kBlobKey, String(kOverScratchBlobBytes, 'x'));
        }

    }  // namespace

    class SaveLoadScratchTest : public SaveFolderTest {
    protected:
        void SetUp() override {
            ASSERT_NO_FATAL_FAILURE(SaveFolderTest::SetUp());
            _service = MakeUnique<SaveGameService>(_fileSystem);
            ASSERT_TRUE(_service->Initialize(_saves));
        }

        UniquePtr<SaveGameService> _service;
    };

    // The slot is big enough that its bytes on the heap would dominate the count: the rest (paths, messages, the reader's tables) is far below it
    TEST_F(SaveLoadScratchTest, ALoadKeepsTheBytesOfASlotOffTheHeap) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        ASSERT_TRUE(_service->Save("mid", kSchema, WriteMidState).has_value());
        int32_t score = 0;
        const auto read = [&](IDeserializer& deserializer) { deserializer.ReadAttribute(kScoreKey, score); };
        ASSERT_TRUE(_service->Load("mid", kSchema, read).has_value());  // gives the thread's scratch arena its chunk

        std::expected<SaveInfo, SaveError> info = std::unexpected(SaveError{});
        const AllocCount count = CountAllocations([&] { info = _service->Load("mid", kSchema, read); });

        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(score, kScore);
        EXPECT_LT(count.bytes, kMidBlobBytes / 2);
    }

    TEST_F(SaveLoadScratchTest, APeekKeepsTheBytesOfASlotOffTheHeap) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        ASSERT_TRUE(_service->Save("mid", kSchema, WriteMidState).has_value());
        ASSERT_TRUE(_service->Peek("mid").has_value());

        std::expected<SaveInfo, SaveError> info = std::unexpected(SaveError{});
        const AllocCount count = CountAllocations([&] { info = _service->Peek("mid"); });

        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->game, kSchema.game);
        EXPECT_LT(count.bytes, kMidBlobBytes / 2);
    }

    TEST_F(SaveLoadScratchTest, ASlotOverTheScratchBoundStillLoadsAndPeeks) {
        ASSERT_TRUE(_service->Save("big", kSchema, WriteBigState).has_value());
        ASSERT_GT(_service->ReadSlot("big")->size(), kOverScratchBlobBytes);

        String blob;
        int32_t score = 0;
        const auto info = _service->Load("big", kSchema, [&](IDeserializer& deserializer) {
            deserializer.ReadAttribute(kScoreKey, score);
            deserializer.ReadAttribute(kBlobKey, blob);
        });

        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(score, kScore);
        EXPECT_EQ(blob.size(), kOverScratchBlobBytes);
        const auto peeked = _service->Peek("big");
        ASSERT_TRUE(peeked.has_value());
        EXPECT_EQ(peeked->game, kSchema.game);
    }

}  // namespace BECore::Tests
