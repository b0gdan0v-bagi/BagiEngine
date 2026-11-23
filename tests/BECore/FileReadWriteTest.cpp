#include <BECore/File/File.h>
#include <BECore/Memory/ScratchArena.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <array>
#include <cstring>

namespace BECore::Tests {

    namespace {

        constexpr StringView kRawBytes{"a\0b\r\n\xFF\r", 7};
        constexpr size_t kBigFileBytes = 64 * 1024;
        constexpr size_t kScratchBound = 4096;

        Span<const uint8_t> AsBytes(StringView text) {
            return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
        }

        String AsText(const Vector<uint8_t>& bytes) {
            return String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }

        bool Exists(const String& path) {
            return GetFileInfo(path).has_value();
        }

    }  // namespace

    class FileReadWriteTest : public ::testing::Test {
    protected:
        TestTempFolder _folder{"BagiFileReadWriteTest"};
    };

    TEST_F(FileReadWriteTest, BytesRoundTripIncludingZeroAndLineEnds) {
        const String path = _folder.PathOf("raw.bin");

        ASSERT_TRUE(WriteFileAtomically(path, AsBytes(kRawBytes)).has_value());

        const auto bytes = BECore::ReadFileBytes(path);
        ASSERT_TRUE(bytes.has_value()) << bytes.error();
        EXPECT_EQ(AsText(*bytes), String(kRawBytes));
    }

    TEST_F(FileReadWriteTest, EmptyFileReadsAsEmptyAndEmptyWriteMakesOne) {
        const String path = _folder.PathOf("empty.bin");

        ASSERT_TRUE(WriteFileAtomically(path, AsBytes({})).has_value());

        const auto info = GetFileInfo(path);
        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->size, 0u);
        const auto bytes = BECore::ReadFileBytes(path);
        ASSERT_TRUE(bytes.has_value());
        EXPECT_TRUE(bytes->empty());
        const auto text = BECore::ReadTextFile(path);
        ASSERT_TRUE(text.has_value());
        EXPECT_TRUE(text->empty());
    }

    TEST_F(FileReadWriteTest, MissingFileAndFolderAreErrorsThatNameThePath) {
        const String missing = _folder.PathOf("missing.bin");
        const String folder = _folder.PathOf("folder");
        ASSERT_TRUE(CreateDirectories(folder).has_value());

        const auto fromMissing = BECore::ReadFileBytes(missing);
        const auto fromFolder = BECore::ReadTextFile(folder);

        ASSERT_FALSE(fromMissing.has_value());
        EXPECT_NE(fromMissing.error().find(missing.c_str()), String::npos) << fromMissing.error();
        ASSERT_FALSE(fromFolder.has_value());
        EXPECT_NE(fromFolder.error().find(folder.c_str()), String::npos) << fromFolder.error();
    }

    TEST_F(FileReadWriteTest, FileOverTheLimitIsRefusedAndTheLimitIsTheInclusiveBound) {
        const String path = _folder.Write("four.bin", "abcd");

        const auto over = BECore::ReadFileBytes(path, 3);
        const auto overText = BECore::ReadTextFile(path, 3);
        const auto exact = BECore::ReadFileBytes(path, 4);

        ASSERT_FALSE(over.has_value());
        EXPECT_NE(over.error().find("limit is 3"), String::npos) << over.error();
        EXPECT_NE(over.error().find('4'), String::npos) << over.error();
        EXPECT_FALSE(overText.has_value());
        ASSERT_TRUE(exact.has_value());
        EXPECT_EQ(AsText(*exact), "abcd");
    }

    TEST_F(FileReadWriteTest, TextKeepsLineEndsAndUtf8ByteForByte) {
        const String path = _folder.PathOf("text.txt");
        const StringView text = "line one\r\nстрока два é\n\r\n";

        ASSERT_TRUE(WriteFileAtomically(path, text).has_value());

        const auto read = BECore::ReadTextFile(path);
        ASSERT_TRUE(read.has_value()) << read.error();
        EXPECT_EQ(*read, String(text));
    }

    TEST_F(FileReadWriteTest, SuccessfulReplaceLeavesTheNewBytesAndNoTempFile) {
        const String path = _folder.Write("target.txt", "old content that is longer");

        ASSERT_TRUE(WriteFileAtomically(path, StringView{"new"}).has_value());

        EXPECT_EQ(BECore::ReadTextFile(path).value_or(String{"<error>"}), "new");
        EXPECT_FALSE(Exists(Format("{}{}", path, kAtomicTempSuffix)));
    }

    TEST_F(FileReadWriteTest, TempPathThatIsAFolderFailsTheWriteAndKeepsTheOldFile) {
        const String path = _folder.Write("target.txt", "old");
        const String tempPath = Format("{}{}", path, kAtomicTempSuffix);
        ASSERT_TRUE(CreateDirectories(tempPath).has_value());

        const auto written = WriteFileAtomically(path, StringView{"new"});

        ASSERT_FALSE(written.has_value());
        EXPECT_NE(written.error().find("Cannot"), String::npos) << written.error();
        EXPECT_EQ(BECore::ReadTextFile(path).value_or(String{"<error>"}), "old");
        const auto temp = GetFileInfo(tempPath);
        ASSERT_TRUE(temp.has_value());
        EXPECT_EQ(temp->kind, FileKind::Directory);
    }

    TEST_F(FileReadWriteTest, TargetThatIsAFolderFailsTheRenameAndLeavesNoTempFile) {
        const String path = _folder.PathOf("target");
        ASSERT_TRUE(CreateDirectories(path).has_value());

        const auto written = WriteFileAtomically(path, StringView{"new"});

        ASSERT_FALSE(written.has_value());
        EXPECT_NE(written.error().find(path.c_str()), String::npos) << written.error();
        EXPECT_FALSE(Exists(Format("{}{}", path, kAtomicTempSuffix)));
        const auto target = GetFileInfo(path);
        ASSERT_TRUE(target.has_value());
        EXPECT_EQ(target->kind, FileKind::Directory);
    }

    TEST_F(FileReadWriteTest, WriteMakesTheFoldersAboveTheFile) {
        const String path = Format("{}/{}", _folder.PathOf("a"), "b/c/file.txt");

        ASSERT_TRUE(WriteFileAtomically(path, StringView{"deep"}).has_value());

        EXPECT_EQ(BECore::ReadTextFile(path).value_or(String{"<error>"}), "deep");
    }

    TEST_F(FileReadWriteTest, NameWithNonAsciiCharactersWorks) {
        const String path = Format("{}/{}", _folder.PathOf("base"), "Файл_é/данные_é.txt");

        ASSERT_TRUE(WriteFileAtomically(path, StringView{"é"}).has_value());

        EXPECT_EQ(BECore::ReadTextFile(path).value_or(String{"<error>"}), "é");
    }

    // The tracker counts what the layer allocates; libc's FILE buffer and SDL's own stream are not part of the promise
    TEST_F(FileReadWriteTest, WholeFileReadMakesNoSecondCopy) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        const String path = _folder.PathOf("big.bin");
        const String content(kBigFileBytes, 'x');
        ASSERT_TRUE(WriteFileAtomically(path, StringView{content}).has_value());

        std::expected<Vector<uint8_t>, String> bytes;
        const AllocCount forBytes = CountAllocations([&] { bytes = BECore::ReadFileBytes(path); });
        std::expected<String, String> text;
        const AllocCount forText = CountAllocations([&] { text = BECore::ReadTextFile(path); });

        ASSERT_TRUE(bytes.has_value());
        ASSERT_TRUE(text.has_value());
        EXPECT_LT(forBytes.bytes, 2 * kBigFileBytes);
        EXPECT_LT(forText.bytes, 2 * kBigFileBytes);
    }

    TEST_F(FileReadWriteTest, ScratchReadGivesTheBytesOfTheHeapReadInArenaOrOnHeap) {
        const String small = _folder.PathOf("small.bin");
        const String empty = _folder.PathOf("empty.bin");
        const String exact = _folder.PathOf("exact.bin");
        const String over = _folder.PathOf("over.bin");
        const String exactContent(kScratchBound, 'a');
        const String overContent(kScratchBound + 1, 'b');
        ASSERT_TRUE(WriteFileAtomically(small, AsBytes(kRawBytes)).has_value());
        ASSERT_TRUE(WriteFileAtomically(empty, AsBytes({})).has_value());
        ASSERT_TRUE(WriteFileAtomically(exact, StringView{exactContent}).has_value());
        ASSERT_TRUE(WriteFileAtomically(over, StringView{overContent}).has_value());

        ScratchScope scratch;
        for (const auto& [path, onHeap] : {Pair<String, bool>{small, false}, {empty, false}, {exact, false}, {over, true}}) {
            const auto expected = BECore::ReadFileBytes(path);
            const auto scratchRead = ReadFileBytesScratch(path, scratch.GetArena(), kScratchBound);
            ASSERT_TRUE(expected.has_value() && scratchRead.has_value()) << path;
            EXPECT_EQ(scratchRead->IsOnHeap(), onHeap) << path;
            const Span<const uint8_t> bytes = scratchRead->Bytes();
            ASSERT_EQ(bytes.size(), expected->size()) << path;
            EXPECT_TRUE(bytes.empty() || std::memcmp(bytes.data(), expected->data(), bytes.size()) == 0) << path;
        }
    }

    TEST_F(FileReadWriteTest, ScratchReadOfAMissingFileNamesThePathAndOverTheLimitIsRefusedBeforeTheArenaIsAsked) {
        const String missing = _folder.PathOf("missing.bin");
        const String big = _folder.PathOf("big.bin");
        ASSERT_TRUE(WriteFileAtomically(big, StringView{String(kScratchBound, 'x')}).has_value());

        ScratchScope scratch;
        const size_t usedBefore = scratch.GetArena().GetBytesUsed();
        const auto none = ReadFileBytesScratch(missing, scratch.GetArena(), kScratchBound);
        ASSERT_FALSE(none.has_value());
        EXPECT_NE(none.error().find("missing.bin"), String::npos) << none.error();

        const auto refused = ReadFileBytesScratch(big, scratch.GetArena(), kScratchBound, kScratchBound - 1);
        ASSERT_FALSE(refused.has_value());
        EXPECT_NE(refused.error().find("big.bin"), String::npos) << refused.error();
        EXPECT_EQ(scratch.GetArena().GetBytesUsed(), usedBefore);
    }

    TEST_F(FileReadWriteTest, ScratchReadOfASmallFileAllocatesNothingOnceTheArenaHasItsChunk) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        const String path = _folder.PathOf("small.bin");
        ASSERT_TRUE(WriteFileAtomically(path, AsBytes(kRawBytes)).has_value());
        {
            ScratchScope warm;  // the first chunk of the thread's arena is the arena's cost, not the read's
            static_cast<void>(ReadFileBytesScratch(path, warm.GetArena(), kScratchBound));
        }

        // SDL allocates the stream itself (counted by the tracker), so the bound is a read into a stack buffer: the bytes cost nothing more
        const AllocCount forStream = CountAllocations([&] {
            auto file = File::Open(path, FileMode::Read);
            ASSERT_TRUE(file.has_value());
            Array<uint8_t, kRawBytes.size()> buffer{};
            ASSERT_TRUE(file->Read(buffer.data(), buffer.size()).has_value());
        });
        AllocCount forScratch;
        {
            ScratchScope scratch;
            std::expected<ScratchFileBytes, String> read;
            forScratch = CountAllocations([&] { read = ReadFileBytesScratch(path, scratch.GetArena(), kScratchBound); });
            ASSERT_TRUE(read.has_value());
        }
        EXPECT_EQ(forScratch.count, forStream.count);
        EXPECT_EQ(forScratch.bytes, forStream.bytes);
    }

}  // namespace BECore::Tests
