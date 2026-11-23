// The pool section of a version 3 stream (Reflection/BinaryFormat.h): what ParsePoolSection accepts and refuses, on bytes made by hand.

#include <BECore/Reflection/BinaryFormat.h>
#include <Common/Printers.h>

namespace BECore::Tests {

    namespace {

        namespace BF = BinaryFormat;

        constexpr size_t kHashBytes = sizeof(uint64_t);
        constexpr size_t kSizeAt = offsetof(Details::PoolStringEntry, size);

        void PutU32(Vector<uint8_t>& out, size_t at, uint32_t value) {
            BF::Store<uint32_t>(out.data() + at, value);
        }

        /// A section as a writer lays it out: the head with the real checksum, then one entry per text (zero padded)
        Vector<uint8_t> MakeSection(const Vector<String>& texts) {
            size_t entryBytes = 0;
            for (const String& text : texts) {
                entryBytes += Details::PoolEntryBytes(text.size());
            }
            Vector<uint8_t> bytes(BF::PoolSectionHeadSize + entryBytes, 0);
            size_t at = BF::PoolSectionHeadSize;
            for (const String& text : texts) {
                Details::ConstructPoolEntry(bytes.data() + at, StringUtils::GetHash(text), text);
                at += Details::PoolEntryBytes(text.size());
            }
            PutU32(bytes, 0, static_cast<uint32_t>(texts.size()));
            PutU32(bytes, sizeof(uint32_t), static_cast<uint32_t>(entryBytes));
            PutU32(bytes, 2 * sizeof(uint32_t), Details::PoolHashFunctionId());
            BF::Store<uint64_t>(bytes.data() + 4 * sizeof(uint32_t), BF::PoolSectionChecksum(bytes.data() + BF::PoolSectionHeadSize, entryBytes));
            return bytes;
        }

        bool Parse(const Vector<uint8_t>& bytes, BF::PoolSection& section, size_t& end, size_t limit) {
            return BF::ParsePoolSection(bytes.data(), 0, limit, section, end);
        }

        bool Parse(const Vector<uint8_t>& bytes) {
            BF::PoolSection section;
            size_t end = 0;
            return Parse(bytes, section, end, bytes.size());
        }

    }  // namespace

    TEST(BinaryPoolSectionTest, AGoodSectionParses) {
        const Vector<String> texts = {"a", "name", "a text of more than eight bytes", ""};
        const Vector<uint8_t> bytes = MakeSection(texts);

        BF::PoolSection section;
        size_t end = 0;
        ASSERT_TRUE(Parse(bytes, section, end, bytes.size()));
        EXPECT_EQ(section.count, texts.size());
        EXPECT_EQ(section.entriesOffset, BF::PoolSectionHeadSize);
        EXPECT_EQ(section.bytes, bytes.size() - BF::PoolSectionHeadSize);
        EXPECT_EQ(section.hashFunction, Details::PoolHashFunctionId());
        EXPECT_EQ(end, bytes.size());
        EXPECT_EQ(section.checksum, BF::PoolSectionChecksum(bytes.data() + section.entriesOffset, section.bytes));
    }

    TEST(BinaryPoolSectionTest, AnEmptySectionParses) {
        EXPECT_TRUE(Parse(MakeSection({})));
    }

    TEST(BinaryPoolSectionTest, ASectionInTheMiddleOfAStreamEndsWhereItsBytesEnd) {
        Vector<uint8_t> stream(5, 0xEE);
        const Vector<uint8_t> section = MakeSection({"x", "y"});
        stream.insert(stream.end(), section.begin(), section.end());
        stream.insert(stream.end(), 7, 0xEE);

        BF::PoolSection parsed;
        size_t end = 0;
        ASSERT_TRUE(BF::ParsePoolSection(stream.data(), 5, stream.size(), parsed, end));
        EXPECT_EQ(parsed.headOffset, 5u);
        EXPECT_EQ(end, 5 + section.size());
    }

    TEST(BinaryPoolSectionTest, ATruncatedHeadIsRefused) {
        const Vector<uint8_t> bytes = MakeSection({"a"});
        BF::PoolSection section;
        size_t end = 0;
        EXPECT_FALSE(Parse(bytes, section, end, BF::PoolSectionHeadSize - 1));
        EXPECT_FALSE(Parse(bytes, section, end, 0));
    }

    TEST(BinaryPoolSectionTest, ABytesCountThatIsNotAMultipleOfEightIsRefused) {
        Vector<uint8_t> bytes = MakeSection({"a"});
        PutU32(bytes, sizeof(uint32_t), static_cast<uint32_t>(bytes.size() - BF::PoolSectionHeadSize - 1));
        EXPECT_FALSE(Parse(bytes));
    }

    TEST(BinaryPoolSectionTest, ABytesCountPastTheLimitIsRefused) {
        Vector<uint8_t> bytes = MakeSection({"a"});
        BF::PoolSection section;
        size_t end = 0;
        EXPECT_FALSE(Parse(bytes, section, end, bytes.size() - 8));

        PutU32(bytes, sizeof(uint32_t), UINT32_MAX - 7);
        EXPECT_FALSE(Parse(bytes));
    }

    TEST(BinaryPoolSectionTest, AWrongCountIsRefused) {
        Vector<uint8_t> bytes = MakeSection({"a", "b"});
        PutU32(bytes, 0, 1);
        EXPECT_FALSE(Parse(bytes));
        PutU32(bytes, 0, 3);
        EXPECT_FALSE(Parse(bytes));
        PutU32(bytes, 0, UINT32_MAX);
        EXPECT_FALSE(Parse(bytes));
    }

    TEST(BinaryPoolSectionTest, ASizeThatRunsPastTheSectionIsRefused) {
        Vector<uint8_t> bytes = MakeSection({"abc"});
        PutU32(bytes, BF::PoolSectionHeadSize + kSizeAt, 1000);
        EXPECT_FALSE(Parse(bytes));
    }

    TEST(BinaryPoolSectionTest, AMissingNulIsRefused) {
        Vector<uint8_t> bytes = MakeSection({"abc"});
        bytes[BF::PoolSectionHeadSize + offsetof(Details::PoolStringEntry, data) + 3] = 'x';
        EXPECT_FALSE(Parse(bytes));
    }

    TEST(BinaryPoolSectionTest, ATextOverTheLimitIsRefused) {
        const String longest(ReaderLimits::kMaxPoolStringBytes, 'a');
        EXPECT_TRUE(Parse(MakeSection({longest})));
        EXPECT_FALSE(Parse(MakeSection({longest + "a"})));
    }

    TEST(BinaryPoolSectionTest, ANonZeroReservedWordIsRefused) {
        Vector<uint8_t> bytes = MakeSection({"a"});
        PutU32(bytes, 3 * sizeof(uint32_t), 1);
        EXPECT_FALSE(Parse(bytes));
    }

    TEST(BinaryPoolSectionTest, AWalkThatDoesNotEndAtTheBytesCountIsRefused) {
        // The only entry's padded size is 16 bytes, so 8 more bytes of `bytes` leave a tail too small for a header
        Vector<uint8_t> bytes = MakeSection({"a"});
        bytes.insert(bytes.end(), 8, 0);
        PutU32(bytes, sizeof(uint32_t), static_cast<uint32_t>(bytes.size() - BF::PoolSectionHeadSize));
        EXPECT_FALSE(Parse(bytes));

        // An entry whose size ends before `bytes` but leaves a whole entry's worth of zeros: count says one, the walk finds two
        Vector<uint8_t> twoEntries = MakeSection({"a"});
        twoEntries.insert(twoEntries.end(), 16, 0);
        PutU32(twoEntries, sizeof(uint32_t), static_cast<uint32_t>(twoEntries.size() - BF::PoolSectionHeadSize));
        EXPECT_FALSE(Parse(twoEntries));
        PutU32(twoEntries, 0, 2);
        EXPECT_TRUE(Parse(twoEntries));
    }

    TEST(BinaryPoolSectionTest, TheChecksumDependsOnEveryByteAndOnTheOrderOfTheWords) {
        const Vector<uint8_t> bytes = MakeSection({"first text", "second text", "third"});
        const uint8_t* entries = bytes.data() + BF::PoolSectionHeadSize;
        const size_t size = bytes.size() - BF::PoolSectionHeadSize;
        const uint64_t original = BF::PoolSectionChecksum(entries, size);

        Vector<uint8_t> flipped(entries, entries + size);
        for (size_t i = 0; i < size; ++i) {
            flipped[i] ^= 0x01;
            EXPECT_NE(BF::PoolSectionChecksum(flipped.data(), size), original) << "byte " << i;
            flipped[i] ^= 0x01;
        }

        Vector<uint8_t> swapped(entries, entries + size);
        std::swap_ranges(swapped.begin(), swapped.begin() + kHashBytes, swapped.begin() + kHashBytes);
        ASSERT_NE(Vector<uint8_t>(entries, entries + size), swapped);
        EXPECT_NE(BF::PoolSectionChecksum(swapped.data(), size), original);
    }

    TEST(BinaryPoolSectionTest, AStringIndexIsCheckedAgainstThePoolCountNotTheNameTable) {
        const Vector<uint8_t> names = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 'n'};  // count 1, charBytes 1, end[0] 1, "n"
        BF::StringTable table;
        size_t tableEnd = 0;
        ASSERT_TRUE(BF::ParseStringTable(names.data(), 0, names.size(), table, tableEnd));

        // tag PoolString, name 0, index 5
        Vector<uint8_t> entry = {BF::Tag::PoolString, 0, 0, 0, 0, 5, 0, 0, 0};
        BF::EntryView view;
        EXPECT_FALSE(BF::ParseEntryV3(entry.data(), 0, entry.size(), table, 5, view));
        EXPECT_TRUE(BF::ParseEntryV3(entry.data(), 0, entry.size(), table, 6, view));
        EXPECT_EQ(view.name, "n");
        EXPECT_FALSE(BF::ParseEntryV2(entry.data(), 0, entry.size(), table, view));
    }

}  // namespace BECore::Tests
