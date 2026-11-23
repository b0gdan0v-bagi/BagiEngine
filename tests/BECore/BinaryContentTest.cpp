// The content loader (Reflection/BinaryContent.h): a cooked version 3 file read straight into a pool block, the rules it refuses by
// and the compact stream it keeps. Streams are made by BinaryStreamBuilder; texts come from FreshText so the shared pool is fresh for them.

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/BinaryContent.h>
#include <Common/BinaryStreams.h>
#include <Common/FreshText.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        namespace BF = BinaryFormat;
        namespace Streams = BinaryStreams;

        constexpr size_t kHeadBytes = BF::PoolSectionHeadSize;
        constexpr size_t kCountAt = 0;
        constexpr size_t kBytesAt = sizeof(uint32_t);
        constexpr size_t kReservedAt = 3 * sizeof(uint32_t);
        constexpr size_t kChecksumAt = 4 * sizeof(uint32_t);
        constexpr uint64_t kWrongHash = 0x1234;

        /// A stream whose pool section holds `texts` in this order, one PoolString entry each
        struct Cooked {
            Vector<String> texts;
            Vector<uint8_t> bytes;

            size_t HeadOffset() const {
                return Streams::LayoutOf(bytes).entriesOffset;
            }
            size_t EntriesOffset() const {
                return HeadOffset() + kHeadBytes;
            }
            size_t SectionBytes() const {
                return BF::Load<uint32_t>(bytes.data() + HeadOffset() + kBytesAt);
            }
            size_t EntryOffset(size_t index) const {
                size_t offset = EntriesOffset();
                for (size_t i = 0; i < index; ++i) {
                    offset += Details::PoolEntryBytes(texts[i].size());
                }
                return offset;
            }
            /// The checksum a writer would have put after a test changed an entry
            void FixChecksum() {
                BF::Store<uint64_t>(bytes.data() + HeadOffset() + kChecksumAt, BF::PoolSectionChecksum(bytes.data() + EntriesOffset(), SectionBytes()));
            }
            void PokeHead(size_t at, uint32_t value) {
                BF::Store<uint32_t>(bytes.data() + HeadOffset() + at, value);
            }
        };

        std::filesystem::path AsPath(const String& path) {
            return std::filesystem::path(ToStd(StringView(path)));
        }

        Vector<String> Fresh(size_t count) {
            Vector<String> texts;
            for (size_t i = 0; i < count; ++i) {
                texts.push_back(FreshText("content_text"));
            }
            return texts;
        }

        Cooked Cook(const Vector<String>& texts, Streams::Options options = {}) {
            Vector<Streams::Entry> root;
            for (size_t i = 0; i < texts.size(); ++i) {
                root.push_back(Streams::Pooled(Format("key{}", i), texts[i]));
            }
            options.pool = texts;
            return Cooked{texts, Streams::Build(root, options)};
        }

        Span<const uint8_t> View(const Vector<uint8_t>& bytes) {
            return Span<const uint8_t>(bytes.data(), bytes.size());
        }

        BinaryContentOptions Verify(bool verifyHashes) {
            BinaryContentOptions options;
            options.verifyHashes = verifyHashes;
            return options;
        }

        std::expected<BinaryContent, String> Load(const Cooked& cooked, bool verifyHashes = true) {
            return LoadContentBytes(View(cooked.bytes), Verify(verifyHashes));
        }

        /// The message of a refusal; a load that succeeds fails the test
        String RefusalOf(const Cooked& cooked, bool verifyHashes = true) {
            const auto loaded = Load(cooked, verifyHashes);
            EXPECT_FALSE(loaded.has_value());
            return loaded.has_value() ? String() : loaded.error();
        }

        bool Contains(const String& text, StringView part) {
            return StringView(text).find(part) != StringView::npos;
        }

        /// Nothing of the file reached the pool as an entry
        void ExpectNotAdopted(const PoolStringStats& before, const Vector<String>& texts) {
            const PoolStringStats after = PoolString::GetStats();
            EXPECT_EQ(after.adopted.blocks, before.adopted.blocks);
            EXPECT_EQ(after.adopted.strings, before.adopted.strings);
            EXPECT_EQ(after.stringCount, before.stringCount);
            for (const String& text : texts) {
                EXPECT_FALSE(PoolString::Find(StringView(text)).has_value()) << text.c_str();
            }
        }

    }  // namespace

    TEST(BinaryContentTest, ACookedFileIsReadStraightIntoAPoolBlock) {
        const Cooked cooked = Cook(Fresh(3));
        TestTempFolder folder("BinaryContentTest");
        const String path = folder.PathOf("cooked.bin");
        WriteFile(AsPath(path), StringView(reinterpret_cast<const char*>(cooked.bytes.data()), cooked.bytes.size()));

        const PoolStringStats before = PoolString::GetStats();
        const auto loaded = LoadContentFile(AsPath(path), Verify(true));
        ASSERT_TRUE(loaded.has_value()) << loaded.error().c_str();
        const PoolStringStats after = PoolString::GetStats();

        EXPECT_EQ(loaded->version, BF::PooledVersion);
        EXPECT_EQ(after.adopted.blocks, before.adopted.blocks + 1);
        EXPECT_EQ(after.adopted.strings, before.adopted.strings + cooked.texts.size());
        EXPECT_EQ(after.misses, before.misses) << "the texts were adopted, not interned";
        ASSERT_EQ(loaded->strings.size(), cooked.texts.size());
        for (size_t i = 0; i < cooked.texts.size(); ++i) {
            const Optional<PoolString> found = PoolString::Find(StringView(cooked.texts[i]));
            ASSERT_TRUE(found.has_value());
            EXPECT_EQ(loaded->strings[i], *found);
            EXPECT_EQ(loaded->strings[i].ToStringView(), StringView(cooked.texts[i]));
        }
        EXPECT_EQ(loaded->section.count, cooked.texts.size());
        EXPECT_EQ(loaded->section.bytes, cooked.SectionBytes());
        EXPECT_EQ(loaded->bytes.size(), cooked.bytes.size() - cooked.SectionBytes());
        EXPECT_EQ(loaded->section.entriesOffset, cooked.EntriesOffset());
    }

    TEST(BinaryContentTest, TheCompactStreamIsTheFileWithoutTheEntriesOfTheSection) {
        const Cooked cooked = Cook(Fresh(2));
        const auto loaded = Load(cooked);
        ASSERT_TRUE(loaded.has_value());

        Vector<uint8_t> expected(cooked.bytes.begin(), cooked.bytes.begin() + cooked.EntriesOffset());
        expected.insert(expected.end(), cooked.bytes.begin() + cooked.EntriesOffset() + cooked.SectionBytes(), cooked.bytes.end());
        EXPECT_EQ(loaded->bytes, expected);
    }

    TEST(BinaryContentTest, AWrongHashIsRefusedWhenHashesAreVerifiedAndTrustedWhenNot) {
        const Vector<String> texts = Fresh(3);
        Streams::Options options;
        options.hashOverrides.push_back({texts[1], kWrongHash});
        const Cooked cooked = Cook(texts, options);

        const PoolStringStats before = PoolString::GetStats();
        const String message = RefusalOf(cooked, true);
        EXPECT_TRUE(Contains(message, "WrongHash")) << message.c_str();
        ExpectNotAdopted(before, texts);

        // The contract of the switch: a loader that does not verify takes the file's word
        const auto trusted = Load(cooked, false);
        ASSERT_TRUE(trusted.has_value());
        EXPECT_EQ(trusted->strings.size(), texts.size());
    }

    TEST(BinaryContentTest, AFileCookedUnderAnotherHashFunctionHasItsHashesComputedHere) {
        const Vector<String> texts = Fresh(3);
        Streams::Options options;
        for (const String& text : texts) {
            options.hashOverrides.push_back({text, kWrongHash});
        }
        options.hashFunction = Details::PoolHashFunctionId() + 1;
        const Cooked cooked = Cook(texts, options);

        const PoolStringStats before = PoolString::GetStats();
        const auto loaded = Load(cooked, true);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().c_str();
        const PoolStringStats after = PoolString::GetStats();

        EXPECT_EQ(after.stringCount, before.stringCount + texts.size());
        for (size_t i = 0; i < texts.size(); ++i) {
            const Optional<PoolString> found = PoolString::Find(StringView(texts[i]));
            ASSERT_TRUE(found.has_value()) << "the entry is where a lookup of its text looks";
            EXPECT_EQ(loaded->strings[i], *found);
        }
    }

    TEST(BinaryContentTest, AnotherHashFunctionDoesNotExcuseAnOverLongText) {
        const Vector<String> texts = {FreshText("content_long") + String(ReaderLimits::kMaxPoolStringBytes, 'x')};
        Streams::Options options;
        options.hashFunction = Details::PoolHashFunctionId() + 1;
        const Cooked cooked = Cook(texts, options);

        const PoolStringStats before = PoolString::GetStats();
        EXPECT_FALSE(Load(cooked).has_value());
        ExpectNotAdopted(before, texts);
    }

    TEST(BinaryContentTest, ADamagedChecksumIsRefusedAndTheMessageSaysToCookAgain) {
        const Vector<String> texts = Fresh(2);
        Streams::Options options;
        options.checksum = uint64_t{42};
        const Cooked cooked = Cook(texts, options);

        const PoolStringStats before = PoolString::GetStats();
        const String message = RefusalOf(cooked);
        EXPECT_TRUE(Contains(message, "cook")) << message.c_str();
        ExpectNotAdopted(before, texts);
    }

    TEST(BinaryContentTest, ADamagedEntryWithTheChecksumRecomputedIsRefusedByTheStructure) {
        const Vector<String> texts = Fresh(2);
        Cooked cooked = Cook(texts);
        // The NUL after the text of the first entry
        cooked.bytes[cooked.EntryOffset(0) + offsetof(Details::PoolStringEntry, data) + texts[0].size()] = 'x';
        cooked.FixChecksum();

        const PoolStringStats before = PoolString::GetStats();
        const String message = RefusalOf(cooked);
        EXPECT_TRUE(Contains(message, "MissingNul")) << message.c_str();
        ExpectNotAdopted(before, texts);
    }

    TEST(BinaryContentTest, ATextOverTheLimitIsRefused) {
        const Vector<String> texts = {FreshText("content_long") + String(ReaderLimits::kMaxPoolStringBytes, 'x')};
        const Cooked cooked = Cook(texts);

        const PoolStringStats before = PoolString::GetStats();
        const String message = RefusalOf(cooked);
        EXPECT_TRUE(Contains(message, "TextTooLong")) << message.c_str();
        ExpectNotAdopted(before, texts);
    }

    TEST(BinaryContentTest, ANonZeroReservedFieldIsRefused) {
        const Vector<String> texts = Fresh(2);
        Cooked cooked = Cook(texts);
        cooked.PokeHead(kReservedAt, 1);

        const PoolStringStats before = PoolString::GetStats();
        EXPECT_FALSE(Load(cooked).has_value());
        ExpectNotAdopted(before, texts);
    }

    TEST(BinaryContentTest, ACountTheEntriesDoNotReproduceIsRefused) {
        const Vector<String> texts = Fresh(3);
        for (const uint32_t count : {uint32_t{2}, uint32_t{4}, uint32_t{0}, UINT32_MAX}) {
            Cooked cooked = Cook(texts);
            cooked.PokeHead(kCountAt, count);

            const PoolStringStats before = PoolString::GetStats();
            EXPECT_FALSE(Load(cooked).has_value()) << "count " << count;
            ExpectNotAdopted(before, texts);
        }
    }

    TEST(BinaryContentTest, EntriesWithoutBytesAndBytesWithoutEntriesAreRefused) {
        Cooked cooked = Cook(Fresh(1));
        cooked.PokeHead(kBytesAt, 0);
        EXPECT_FALSE(Load(cooked).has_value());

        Vector<Streams::Entry> root = {Streams::Number("n", uint32_t{1})};
        Streams::Options options;
        options.pool = {};
        Cooked empty{{}, Streams::Build(root, options)};
        empty.PokeHead(kCountAt, 1);
        EXPECT_FALSE(Load(empty).has_value());
    }

    TEST(BinaryContentTest, ASectionOfNoEntriesNeedsNoBlock) {
        Vector<Streams::Entry> root = {Streams::Number("n", uint32_t{1})};
        const Cooked cooked{{}, Streams::Build(root)};
        ASSERT_EQ(cooked.SectionBytes(), 0u);

        const PoolStringStats before = PoolString::GetStats();
        const auto loaded = Load(cooked);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().c_str();
        EXPECT_TRUE(loaded->strings.empty());
        EXPECT_EQ(loaded->bytes, cooked.bytes);
        EXPECT_EQ(PoolString::GetStats().blockCount, before.blockCount);
    }

    TEST(BinaryContentTest, ATruncatedFileAndTrailingBytesAreRefused) {
        const Cooked cooked = Cook(Fresh(2));

        Cooked shorter = cooked;
        shorter.bytes.pop_back();
        EXPECT_FALSE(Load(shorter).has_value());

        Cooked longer = cooked;
        longer.bytes.push_back(0);
        EXPECT_FALSE(Load(longer).has_value());

        for (const size_t size : {size_t{0}, BF::HeaderSize - 1, BF::HeaderSize}) {
            EXPECT_FALSE(LoadContentBytes(Span<const uint8_t>(cooked.bytes.data(), size)).has_value()) << size;
        }
    }

    TEST(BinaryContentTest, ANameTableThatDoesNotFitTheFileIsRefused) {
        Cooked cooked = Cook(Fresh(1));
        BF::Store<uint32_t>(cooked.bytes.data() + BF::HeaderSize + sizeof(uint32_t), UINT32_MAX);
        EXPECT_FALSE(Load(cooked).has_value());
        BF::Store<uint32_t>(cooked.bytes.data() + BF::HeaderSize, UINT32_MAX);
        EXPECT_FALSE(Load(cooked).has_value());
    }

    TEST(BinaryContentTest, APathThatIsNoRegularFileOrIsMissingIsRefused) {
        TestTempFolder folder("BinaryContentTest");
        EXPECT_FALSE(LoadContentFile(AsPath(folder.PathOf("")), Verify(true)).has_value());
        EXPECT_FALSE(LoadContentFile(AsPath(folder.PathOf("missing.bin")), Verify(true)).has_value());
    }

    TEST(BinaryContentTest, AFileOverTheStreamLimitIsRefused) {
        TestTempFolder folder("BinaryContentTest");
        const String path = folder.PathOf("big.bin");
        WriteFile(AsPath(path), String(ReaderLimits::kMaxBinaryStreamBytes + 1, '\0'));
        EXPECT_FALSE(LoadContentFile(AsPath(path), Verify(true)).has_value());
    }

    TEST(BinaryContentTest, TextsThePoolAlreadyHadKeepTheirOlderEntries) {
        const Vector<String> texts = Fresh(3);
        const PoolString older = PoolString::Intern(StringView(texts[1]));
        const Cooked cooked = Cook(texts);

        const PoolStringStats before = PoolString::GetStats();
        const auto loaded = Load(cooked);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().c_str();
        const PoolStringStats after = PoolString::GetStats();

        EXPECT_EQ(after.adopted.blocks, before.adopted.blocks + 1);
        EXPECT_EQ(after.deadStrings, before.deadStrings + 1);
        EXPECT_EQ(after.deadBytes, before.deadBytes + Details::PoolEntryBytes(texts[1].size()));
        EXPECT_EQ(after.adopted.strings, before.adopted.strings + 2);
        EXPECT_EQ(loaded->strings[1], older);
        EXPECT_EQ(loaded->strings[0].ToStringView(), StringView(texts[0]));
    }

    TEST(BinaryContentTest, TheFileLoaderAndTheMemoryLoaderAgree) {
        TestTempFolder folder("BinaryContentTest");
        const Vector<String> texts = Fresh(2);
        Cooked damaged = Cook(texts);
        damaged.PokeHead(kReservedAt, 1);
        Streams::Options hashed;
        hashed.hashOverrides.push_back({texts[0], kWrongHash});
        const Cooked wrongHash = Cook(texts, hashed);

        for (const Cooked* cooked : {static_cast<const Cooked*>(&damaged), &wrongHash}) {
            const String path = folder.PathOf("agree.bin");
            WriteFile(AsPath(path), StringView(reinterpret_cast<const char*>(cooked->bytes.data()), cooked->bytes.size()));
            const auto fromFile = LoadContentFile(AsPath(path), Verify(true));
            const auto fromMemory = LoadContentBytes(View(cooked->bytes), Verify(true));
            ASSERT_EQ(fromFile.has_value(), fromMemory.has_value());
            if (!fromFile.has_value()) {
                EXPECT_EQ(fromFile.error(), fromMemory.error());
            }
        }

        const Cooked good = Cook(Fresh(3));
        const String path = folder.PathOf("agree.bin");
        WriteFile(AsPath(path), StringView(reinterpret_cast<const char*>(good.bytes.data()), good.bytes.size()));
        const auto fromFile = LoadContentFile(AsPath(path), Verify(true));
        const auto fromMemory = LoadContentBytes(View(good.bytes), Verify(true));
        ASSERT_TRUE(fromFile.has_value());
        ASSERT_TRUE(fromMemory.has_value());
        EXPECT_EQ(fromFile->bytes, fromMemory->bytes);
        EXPECT_EQ(fromFile->strings, fromMemory->strings);
        EXPECT_EQ(fromFile->section.count, fromMemory->section.count);
    }

    TEST(BinaryContentTest, FilesOfVersionOneAndTwoLoadWholeWithNoStrings) {
        const Vector<String> texts = Fresh(2);
        for (const uint16_t version : {BF::LegacyVersion, BF::TabledVersion}) {
            Streams::Options options;
            options.version = version;
            const Cooked cooked = [&] {
                Vector<Streams::Entry> root;
                for (const String& text : texts) {
                    root.push_back(Streams::Pooled(Format("k{}", text), text));
                }
                return Cooked{texts, Streams::Build(root, options)};
            }();

            const PoolStringStats before = PoolString::GetStats();
            const auto loaded = Load(cooked);
            ASSERT_TRUE(loaded.has_value()) << loaded.error().c_str();
            EXPECT_EQ(loaded->version, version);
            EXPECT_EQ(loaded->bytes, cooked.bytes);
            EXPECT_TRUE(loaded->strings.empty());
            EXPECT_EQ(PoolString::GetStats().blockCount, before.blockCount);
        }
    }

}  // namespace BECore::Tests
