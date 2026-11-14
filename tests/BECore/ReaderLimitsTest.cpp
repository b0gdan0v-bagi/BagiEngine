// The limits of the readers (Reflection/ReaderLimits.h): what a deserializer refuses with an error instead of allocating or recursing for it.
// The fuzz targets (tests/Fuzz) found them; the regression seeds are tests/Fuzz/corpus/*/fix-*.

#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Fuzz/FuzzProbeAccess.h>

namespace BECore::Tests {

    namespace {

        using Fuzz::ProbeState;

        /// "<a><a>...<a/>...</a></a>" nested @p depth deep
        String NestedXml(size_t depth, const char* tag) {
            String xml;
            for (size_t i = 0; i < depth; ++i) {
                xml += "<";
                xml += tag;
                xml += ">";
            }
            for (size_t i = 0; i < depth; ++i) {
                xml += "</";
                xml += tag;
                xml += ">";
            }
            return xml;
        }

        /// A <tree> of ProbeNode nested @p depth levels: every level is a `children` array with one `item`
        String NestedProbeTree(size_t depth) {
            String xml = "<root><tree id=\"1\">";
            for (size_t i = 0; i < depth; ++i) {
                xml += "<children><item id=\"2\">";
            }
            for (size_t i = 0; i < depth; ++i) {
                xml += "</item></children>";
            }
            xml += "</tree></root>";
            return xml;
        }

        size_t TreeDepth(const FuzzProbeData::ProbeNode& node) {
            size_t deepest = 0;
            for (const FuzzProbeData::ProbeNode& child : node.children) {
                deepest = std::max(deepest, TreeDepth(child));
            }
            return deepest + 1;
        }

        bool HasErrorContaining(const IDeserializer& reader, StringView part) {
            for (const DeserializeError& error : reader.GetErrors()) {
                if (error.errorMessage.ToStringView().find(part) != StringView::npos) {
                    return true;
                }
            }
            return false;
        }

        std::filesystem::path SparseFileOfSize(const char* stem, uintmax_t size) {
            const std::filesystem::path path = TestTempPath(stem);
            {
                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                file << "x";
            }
            std::error_code ec;
            std::filesystem::resize_file(path, size, ec);
            EXPECT_FALSE(ec) << ec.message();
            return path;
        }

    }  // namespace

    // ---------------------------------------------------------------------------------------------
    // XmlDeserializer
    // ---------------------------------------------------------------------------------------------

    TEST(ReaderLimitsTest, XmlStringOverTheLimitIsRefused) {
        String big = "<root>";
        big.append(ReaderLimits::kMaxXmlDocumentBytes, ' ');
        big += "</root>";

        XmlDeserializer reader;
        EXPECT_FALSE(reader.LoadFromString(big));
        EXPECT_TRUE(HasErrorContaining(reader, "larger than the limit"));
    }

    TEST(ReaderLimitsTest, XmlFileThatIsNotARegularFileOrIsTooBigIsRefusedBeforeItIsRead) {
        XmlDeserializer folder;
        EXPECT_FALSE(folder.LoadFromFile(std::filesystem::temp_directory_path()));
        EXPECT_TRUE(HasErrorContaining(folder, "not a regular file"));

        const std::filesystem::path big = SparseFileOfSize("BagiReaderLimitsTest_BigXml", ReaderLimits::kMaxXmlDocumentBytes + 1);
        XmlDeserializer reader;
        const bool loaded = reader.LoadFromFile(big);
        std::error_code ignored;
        std::filesystem::remove(big, ignored);
        EXPECT_FALSE(loaded);
        EXPECT_TRUE(HasErrorContaining(reader, "larger than the limit"));

        // A missing file keeps its message
        XmlDeserializer missing;
        EXPECT_FALSE(missing.LoadFromFile(TestTempPath("BagiReaderLimitsTest_NoSuchXml")));
        EXPECT_TRUE(HasErrorContaining(missing, "Failed to parse XML file"));
    }

    TEST(ReaderLimitsTest, XmlRefusedLoadLeavesNothingReadable) {
        XmlDeserializer reader;
        ASSERT_TRUE(reader.LoadFromString("<root><a><b x=\"1\"/></a></root>"));
        ASSERT_TRUE(reader.BeginObject("a"));  // left open on purpose

        // The document is gone with its nodes, whether the new text does not parse or is too big
        EXPECT_FALSE(reader.LoadFromString("<"));
        int32_t value = 0;
        EXPECT_FALSE(reader.BeginObject("a"));
        EXPECT_FALSE(reader.Read("x", value));
        EXPECT_FALSE(reader.BeginObject("b"));

        EXPECT_FALSE(reader.LoadFromFile(std::filesystem::temp_directory_path()));
        EXPECT_FALSE(reader.BeginObject("a"));

        // A later load starts at the root again, with no open object left over
        reader.ClearErrors();
        ASSERT_TRUE(reader.LoadFromString("<root><a><b x=\"1\"/></a></root>"));
        EXPECT_TRUE(reader.BeginObject("a"));
        int32_t missing = 0;
        reader.Read("nothing", missing);
        ASSERT_EQ(reader.GetErrors().size(), 1u);
        EXPECT_EQ(reader.GetErrors()[0].path.ToStringView(), "a.nothing");
    }

    TEST(ReaderLimitsTest, XmlTextIsUtf8WhateverItStartsWith) {
        // The first bytes of a UTF-16 and of a UTF-32 text: not decoded, and the view into the middle of a buffer is not read as words
        const char utf16[] = "\xFF\xFE<\0r\0/\0>\0";
        const char utf32[] = "\xFF\xFE\0\0<\0\0\0r\0\0\0/\0\0\0>\0\0\0";
        for (const StringView text : {StringView(utf16, sizeof(utf16) - 1), StringView(utf32, sizeof(utf32) - 1)}) {
            Vector<char> buffer(text.size() + 1);
            std::memcpy(buffer.data() + 1, text.data(), text.size());

            XmlDeserializer reader;
            EXPECT_FALSE(reader.LoadFromString(StringView(buffer.data() + 1, text.size())));
            EXPECT_TRUE(reader.HasErrors());
        }

        // A byte order mark of UTF-8 is not text
        XmlDeserializer withMark;
        EXPECT_TRUE(withMark.LoadFromString("\xEF\xBB\xBF<root a=\"1\"/>"));
    }

    TEST(ReaderLimitsTest, XmlNestingStopsAtTheLimit) {
        XmlDeserializer reader;
        const String xml = NestedXml(ReaderLimits::kMaxXmlNesting + 20, "a");
        ASSERT_TRUE(reader.LoadFromString(xml));

        // The root is the first entry of the stack
        size_t opened = 0;
        while (reader.BeginObject("a")) {
            ++opened;
        }
        EXPECT_EQ(opened, ReaderLimits::kMaxXmlNesting - 1);
        EXPECT_TRUE(HasErrorContaining(reader, "deeper than the limit"));
        for (size_t i = 0; i < opened; ++i) {
            reader.EndObject();
        }

        // Arrays count too: the array and its element are an entry each
        reader.ClearErrors();
        size_t arrays = 0;
        size_t count = 0;
        while (reader.BeginArray("a", "item", count) && reader.BeginArrayElement()) {
            ++arrays;
        }
        EXPECT_LE(arrays, ReaderLimits::kMaxXmlNesting / 2);
        EXPECT_TRUE(reader.HasErrors());
    }

    TEST(ReaderLimitsTest, XmlRecursiveTypeReadsDeepDocumentsWithoutRunningOutOfStack) {
        XmlDeserializer reader;
        ASSERT_TRUE(reader.LoadFromString(NestedProbeTree(20000)));

        ProbeState state;
        state.Deserialize(reader);
        EXPECT_LE(TreeDepth(state.tree), ReaderLimits::kMaxXmlNesting);
        EXPECT_TRUE(reader.HasErrors());
    }

    TEST(ReaderLimitsTest, XmlArrayOverTheLimitIsRefused) {
        const auto listOf = [](size_t items) {
            String xml = "<root><list>";
            for (size_t i = 0; i < items; ++i) {
                xml += "<i/>";
            }
            xml += "</list></root>";
            return xml;
        };

        XmlDeserializer atLimit;
        ASSERT_TRUE(atLimit.LoadFromString(listOf(ReaderLimits::kMaxArrayElements)));
        size_t count = 0;
        EXPECT_TRUE(atLimit.BeginArray("list", "i", count));
        EXPECT_EQ(count, ReaderLimits::kMaxArrayElements);
        EXPECT_FALSE(atLimit.HasErrors());

        XmlDeserializer beyond;
        ASSERT_TRUE(beyond.LoadFromString(listOf(ReaderLimits::kMaxArrayElements + 1)));
        count = 7;
        EXPECT_FALSE(beyond.BeginArray("list", "i", count));
        EXPECT_EQ(count, 7u);
        EXPECT_TRUE(HasErrorContaining(beyond, "more elements than the limit"));
    }

    TEST(ReaderLimitsTest, XmlErrorsAreBoundedAndCut) {
        XmlDeserializer reader;
        ASSERT_TRUE(reader.LoadFromString("<root/>"));

        int32_t value = 0;
        const String longName(ReaderLimits::kMaxErrorTextBytes * 4, 'n');
        for (size_t i = 0; i < ReaderLimits::kMaxRecordedErrors * 4; ++i) {
            reader.Read(StringView(longName), value);
        }

        const auto& errors = reader.GetErrors();
        ASSERT_EQ(errors.size(), ReaderLimits::kMaxRecordedErrors);
        EXPECT_EQ(errors.front().path.Length(), ReaderLimits::kMaxErrorTextBytes);
        EXPECT_EQ(errors.back().errorMessage.ToStringView(), "Too many errors: the rest are not recorded");

        // ClearErrors starts the count again
        reader.ClearErrors();
        reader.Read("one", value);
        EXPECT_EQ(reader.GetErrors().size(), 1u);
    }

    TEST(ReaderLimitsTest, XmlStrictErrorQuotesAShortPieceOfTheValue) {
        const String junk(1000, 'z');
        const String xml = "<root a=\"" + junk + "\"/>";
        XmlDeserializer reader;
        reader.SetStrictValues(true);
        ASSERT_TRUE(reader.LoadFromString(xml));

        int32_t value = 0;
        EXPECT_FALSE(reader.ReadAttribute("a", value));
        ASSERT_EQ(reader.GetErrors().size(), 1u);
        const StringView message = reader.GetErrors()[0].errorMessage.ToStringView();
        EXPECT_LT(message.size(), 100u);
        EXPECT_NE(message.find("..."), StringView::npos);
    }

    // ---------------------------------------------------------------------------------------------
    // BinaryDeserializer
    // ---------------------------------------------------------------------------------------------

    TEST(ReaderLimitsTest, BinaryFileThatIsNotARegularFileOrIsTooBigIsRefusedBeforeItIsRead) {
        BinaryDeserializer folder;
        EXPECT_FALSE(folder.LoadFromFile(std::filesystem::temp_directory_path()));
        EXPECT_TRUE(HasErrorContaining(folder, "not a regular file"));

        const std::filesystem::path big = SparseFileOfSize("BagiReaderLimitsTest_BigBinary", ReaderLimits::kMaxBinaryStreamBytes + 1);
        BinaryDeserializer reader;
        const bool loaded = reader.LoadFromFile(big);
        std::error_code ignored;
        std::filesystem::remove(big, ignored);
        EXPECT_FALSE(loaded);
        EXPECT_TRUE(HasErrorContaining(reader, "larger than the limit"));
    }

    TEST(ReaderLimitsTest, BinaryBufferOverTheLimitIsRefused) {
        BinarySerializer writer;
        writer.WriteAttribute("a", int32_t{1});
        Vector<uint8_t> bytes = writer.GetData();
        bytes.resize(ReaderLimits::kMaxBinaryStreamBytes + 1);
        BinaryFormat::Store<uint32_t>(bytes.data() + BinaryFormat::PayloadSizeOffset, static_cast<uint32_t>(bytes.size() - BinaryFormat::HeaderSize));

        BinaryDeserializer reader;
        EXPECT_FALSE(reader.LoadFromBuffer(bytes));
        EXPECT_TRUE(HasErrorContaining(reader, "larger than the limit"));
    }

    TEST(ReaderLimitsTest, BinaryArrayOverTheLimitIsRefused) {
        const auto saveArray = [](size_t elements) {
            BinarySerializer writer;
            size_t count = elements;
            if (writer.BeginArray("list", "item", count)) {
                for (size_t i = 0; i < elements; ++i) {
                    writer.BeginArrayElement();
                    writer.EndArrayElement();
                }
                writer.EndArray();
            }
            return writer.GetData();
        };

        BinaryDeserializer atLimit;
        ASSERT_TRUE(atLimit.LoadFromBuffer(saveArray(ReaderLimits::kMaxArrayElements))) << (atLimit.GetErrors().empty() ? "" : atLimit.GetErrors()[0].errorMessage.CStr());
        size_t count = 0;
        EXPECT_TRUE(atLimit.BeginArray("list", "item", count));
        EXPECT_EQ(count, ReaderLimits::kMaxArrayElements);

        BinaryDeserializer beyond;
        EXPECT_FALSE(beyond.LoadFromBuffer(saveArray(ReaderLimits::kMaxArrayElements + 1)));
        EXPECT_TRUE(beyond.HasErrors());
    }

    TEST(ReaderLimitsTest, BinaryErrorsAreBounded) {
        BinarySerializer writer;
        writer.WriteAttribute("a", int32_t{1});
        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromBuffer(writer.GetData()));

        int32_t value = 0;
        for (size_t i = 0; i < ReaderLimits::kMaxRecordedErrors * 4; ++i) {
            reader.Read("missing", value);
        }
        ASSERT_EQ(reader.GetErrors().size(), ReaderLimits::kMaxRecordedErrors);
        EXPECT_EQ(reader.GetErrors().back().errorMessage.ToStringView(), "Too many errors: the rest are not recorded");
    }

    TEST(ReaderLimitsTest, BinaryRefusedLoadForgetsTheOpenObjects) {
        BinarySerializer writer;
        if (writer.BeginObject("a")) {
            writer.WriteAttribute("x", int32_t{1});
            writer.EndObject();
        }
        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromBuffer(writer.GetData()));
        ASSERT_TRUE(reader.BeginObject("a"));  // left open on purpose

        EXPECT_FALSE(reader.LoadFromBuffer(nullptr, 0));
        ASSERT_TRUE(reader.LoadFromBuffer(writer.GetData()));
        reader.ClearErrors();
        int32_t value = 0;
        reader.Read("nothing", value);
        ASSERT_EQ(reader.GetErrors().size(), 1u);
        EXPECT_EQ(reader.GetErrors()[0].path.ToStringView(), "nothing");
    }

}  // namespace BECore::Tests
