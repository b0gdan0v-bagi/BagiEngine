// The content loader (Reflection/BinaryContent.h): a cooked version 3 file read straight into a pool block, the rules it refuses by
// and the compact stream it keeps. Streams are made by BinaryStreamBuilder; texts come from FreshText so the shared pool is fresh for them.

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/BinaryContent.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/SchemaHash.h>
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

        constexpr StringView kStampRoot = "StampRoot";
        constexpr StringView kStampSource = "config/Content.bin";

        /// A document with every kind of entry: scalars, a String, PoolStrings (one read as a String), an object, an array of objects and a repeated name
        Vector<Streams::Entry> DocumentOf(const Vector<String>& texts) {
            return {Streams::Number("count", uint32_t{7}),
                    Streams::Text("label", "hello"),
                    Streams::Pooled("kind", texts[0]),
                    Streams::Pooled("kindAsString", texts[1]),
                    Streams::Object("inner", {Streams::Number("a", int32_t{-3}), Streams::Pooled("p", texts[0])}),
                    Streams::Array("items", {Streams::Element({Streams::Number("id", int32_t{1}), Streams::Pooled("tag", texts[2])}),
                                             Streams::Element({Streams::Number("id", int32_t{2}), Streams::Pooled("tag", texts[0])})}),
                    Streams::Number("dup", int32_t{1}),
                    Streams::Number("dup", int32_t{2})};
        }

        /// What a reader says about DocumentOf: one line, so two readers over the same document compare as two strings
        String Digest(BinaryDeserializer& reader) {
            uint32_t count = 0;
            String label;
            PoolString kind;
            String kindAsString;
            int32_t dup = 0;
            const bool hasDup = reader.HasKey("dup");
            reader.Read("count", count);
            reader.Read("label", label);
            reader.Read("kind", kind);
            reader.Read("kindAsString", kindAsString);
            reader.Read("dup", dup);
            String inner;
            if (reader.BeginObject("inner")) {
                int32_t a = 0;
                PoolString p;
                reader.Read("a", a);
                reader.Read("p", p);
                inner = Format("{}/{}", a, p.ToStringView());
                reader.EndObject();
            }
            String items;
            size_t elements = 0;
            if (reader.BeginArray("items", "item", elements)) {
                for (size_t i = 0; i < elements; ++i) {
                    if (reader.BeginArrayElement()) {
                        int32_t id = 0;
                        PoolString tag;
                        reader.Read("id", id);
                        reader.Read("tag", tag);
                        items += Format("[{}:{}]", id, tag.ToStringView());
                        reader.EndArrayElement();
                    }
                }
                reader.EndArray();
            }
            EXPECT_FALSE(reader.HasErrors());
            return Format("{} {} {} {} {} {} {} {}", count, label, kind.ToStringView(), kindAsString, dup, hasDup, inner, items);
        }

        String DigestOfStream(const Vector<uint8_t>& stream) {
            BinaryDeserializer reader;
            EXPECT_TRUE(reader.LoadFromView(stream.data(), stream.size()));
            return Digest(reader);
        }

        String DigestOfContent(const BinaryContent& content) {
            BinaryDeserializer reader;
            EXPECT_TRUE(reader.LoadFromContent(content));
            return Digest(reader);
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

    TEST(BinaryContentTest, ACookedFileWithANonAsciiNameIsRead) {
        const Cooked cooked = Cook(Fresh(2));
        TestTempFolder folder("BinaryContentTest");
        const Path file = Path(folder.PathOf("")) / "caf\xC3\xA9" / "Cook\xC3\xA9.bin";
        WriteFile(AsPath(String(file.View())), StringView(reinterpret_cast<const char*>(cooked.bytes.data()), cooked.bytes.size()));

        const auto loaded = LoadContentFile(file, Verify(true));
        ASSERT_TRUE(loaded.has_value()) << loaded.error().c_str();
        EXPECT_EQ(loaded->version, BF::PooledVersion);
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

    TEST(BinaryContentTest, AContentReadsLikeTheFullStream) {
        const Vector<String> texts = Fresh(3);
        Streams::Options options;
        options.pool = texts;
        const Vector<uint8_t> stream = Streams::Build(DocumentOf(texts), options);
        const auto content = LoadContentBytes(View(stream), Verify(true));
        ASSERT_TRUE(content.has_value()) << content.error().c_str();

        const String fromContent = DigestOfContent(*content);
        EXPECT_EQ(fromContent, DigestOfStream(stream));
        EXPECT_TRUE(Contains(fromContent, texts[2])) << fromContent.c_str();
    }

    TEST(BinaryContentTest, APoolStringReadFromAContentInternsNothingAndIsTheAdoptedEntry) {
        const Vector<String> texts = Fresh(3);
        Streams::Options options;
        options.pool = texts;
        const auto content = LoadContentBytes(View(Streams::Build(DocumentOf(texts), options)), Verify(true));
        ASSERT_TRUE(content.has_value());

        BinaryDeserializer reader;
        const PoolStringStats before = PoolString::GetStats();
        ASSERT_TRUE(reader.LoadFromContent(*content));
        EXPECT_TRUE(reader.HasKey("kind"));
        const PoolStringStats peeked = PoolString::GetStats();
        EXPECT_EQ(peeked.stringCount, before.stringCount);
        EXPECT_EQ(peeked.batchCalls, before.batchCalls);

        PoolString kind;
        String asString;
        ASSERT_TRUE(reader.Read("kind", kind));
        ASSERT_TRUE(reader.Read("kindAsString", asString));
        const PoolStringStats after = PoolString::GetStats();
        EXPECT_EQ(after.stringCount, before.stringCount);
        EXPECT_EQ(after.internCalls, before.internCalls);
        EXPECT_EQ(after.batchCalls, before.batchCalls);
        EXPECT_EQ(kind, content->strings[0]);
        EXPECT_EQ(asString, texts[1]);
    }

    TEST(BinaryContentTest, ACompactStreamThatDoesNotAddUpIsRefused) {
        const Vector<String> texts = Fresh(2);
        Streams::Options options;
        options.pool = texts;
        const Vector<uint8_t> stream = Streams::Build({Streams::Pooled("k", texts[0]), Streams::Pooled("m", texts[1])}, options);
        const auto good = LoadContentBytes(View(stream), Verify(true));
        ASSERT_TRUE(good.has_value());

        const auto refused = [](const BinaryContent& content) {
            BinaryDeserializer reader;
            const bool loaded = reader.LoadFromContent(content);
            EXPECT_EQ(loaded, !reader.HasErrors());
            return !loaded;
        };
        EXPECT_FALSE(refused(*good));

        BinaryContent shorter = *good;
        shorter.bytes.pop_back();
        EXPECT_TRUE(refused(shorter));

        BinaryContent longer = *good;
        longer.bytes.push_back(0);
        EXPECT_TRUE(refused(longer));

        BinaryContent otherSection = *good;
        otherSection.section.bytes += 8;
        EXPECT_TRUE(refused(otherSection));

        BinaryContent fewerStrings = *good;
        fewerStrings.strings.pop_back();
        EXPECT_TRUE(refused(fewerStrings));

        BinaryContent otherCount = *good;
        otherCount.section.count += 1;
        EXPECT_TRUE(refused(otherCount));

        // The first root entry is the PoolString "k": u8 tag, u32 name index, u32 string index
        BinaryContent outOfRange = *good;
        BF::Store<uint32_t>(outOfRange.bytes.data() + good->section.entriesOffset + sizeof(uint8_t) + sizeof(uint32_t), static_cast<uint32_t>(good->strings.size()));
        EXPECT_TRUE(refused(outOfRange));

        BinaryContent cutNameTable = *good;
        BF::Store<uint32_t>(cutNameTable.bytes.data() + BF::HeaderSize + sizeof(uint32_t), 0);
        EXPECT_TRUE(refused(cutNameTable));
    }

    TEST(BinaryContentTest, TheContentStampIsCheckedOverAContent) {
        const Vector<String> texts = Fresh(1);
        const uint64_t layout = ComputeLayoutHash(kStampRoot).value_or(0);
        Streams::Options options;
        options.pool = texts;
        const auto stamped = [&](uint64_t hash) { return Streams::Object(BF::ContentStampName, {Streams::Text("root", kStampRoot), Streams::Number("layout", hash)}); };

        const auto content = LoadContentBytes(View(Streams::Build({stamped(layout), Streams::Pooled("kind", texts[0])}, options)), Verify(true));
        ASSERT_TRUE(content.has_value());
        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromContent(*content));
        const auto stamp = CheckContentStamp(reader, kStampSource);
        ASSERT_TRUE(stamp.has_value()) << String(stamp.error()).c_str();
        EXPECT_EQ(stamp->layout, layout);
        PoolString kind;
        EXPECT_TRUE(reader.Read("kind", kind));
        EXPECT_EQ(kind.ToStringView(), StringView(texts[0]));

        const auto other = LoadContentBytes(View(Streams::Build({stamped(layout + 1), Streams::Pooled("kind", texts[0])}, options)), Verify(true));
        ASSERT_TRUE(other.has_value());
        BinaryDeserializer otherReader;
        ASSERT_TRUE(otherReader.LoadFromContent(*other));
        const auto refused = CheckContentStamp(otherReader, kStampSource);
        ASSERT_FALSE(refused.has_value());
        EXPECT_TRUE(Contains(refused.error(), "cook"));
    }

    TEST(BinaryContentTest, AContentOfVersionTwoReadsLikeItsStream) {
        const Vector<String> texts = Fresh(3);
        Streams::Options options;
        options.version = BF::TabledVersion;
        const Vector<uint8_t> stream = Streams::Build(DocumentOf(texts), options);
        const auto content = LoadContentBytes(View(stream), Verify(true));
        ASSERT_TRUE(content.has_value());
        EXPECT_EQ(DigestOfContent(*content), DigestOfStream(stream));
    }

    TEST(BinaryContentTest, ASaveWithStaleHashesStillLoadsThroughTheHashingLoads) {
        const Vector<String> texts = Fresh(3);
        Streams::Options options;
        options.pool = texts;
        for (const String& text : texts) {
            options.hashOverrides.push_back({text, kWrongHash});
        }
        const Vector<uint8_t> save = Streams::Build(DocumentOf(texts), options);

        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromBuffer(save));
        PoolString kind;
        ASSERT_TRUE(reader.Read("kind", kind));
        const Optional<PoolString> found = PoolString::Find(StringView(texts[0]));
        ASSERT_TRUE(found.has_value()) << "the reader hashed the text itself";
        EXPECT_EQ(kind, *found);
    }

}  // namespace BECore::Tests
