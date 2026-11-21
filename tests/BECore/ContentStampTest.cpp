#include "ContentStampTest.h"

#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/SchemaHash.h>
#include <Common/BinaryStreams.h>
#include <Generated/ContentStampTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace StampTestData;
    namespace Streams = BinaryStreams;

    namespace {

        constexpr StringView kRootName = "StampRoot";
        constexpr StringView kSource = "config/Stamped.bin";
        const PoolString kKind = "stamp-test-kind"_intern;

        StampRoot MakeRoot() {
            StampRoot root;
            root.id = 7;
            root.label = "text";
            root.kind = kKind;
            root.items = {1, 2, 3};
            return root;
        }

        void ExpectSameRoot(const StampRoot& a, const StampRoot& b) {
            EXPECT_EQ(a.id, b.id);
            EXPECT_EQ(a.label, b.label);
            EXPECT_EQ(a.kind, b.kind);
            EXPECT_EQ(a.items, b.items);
        }

        uint64_t LayoutOfRoot() {
            return ComputeLayoutHash(kRootName).value_or(0);
        }

        /// The entries of a stamp as the writer makes them
        Streams::Entry StampEntry(StringView root, uint64_t layout) {
            return Streams::Object(BinaryFormat::ContentStampName, {Streams::Text("root", root), Streams::Number("layout", layout)});
        }

        BinaryDeserializer Open(const Vector<uint8_t>& bytes) {
            BinaryDeserializer reader;
            EXPECT_TRUE(reader.LoadFromBuffer(bytes));
            return reader;
        }

        /// A check that refuses, with the message
        String Refusal(const Vector<Streams::Entry>& root, uint16_t version = BinaryFormat::TabledVersion) {
            Streams::Options options;
            options.version = version;
            BinaryDeserializer reader = Open(Streams::Build(root, options));
            const auto stamp = CheckContentStamp(reader, kSource);
            EXPECT_FALSE(stamp.has_value());
            EXPECT_FALSE(reader.HasErrors()) << "a refused stamp leaves the reader clean";
            return stamp.has_value() ? String() : stamp.error();
        }

        bool Contains(const String& text, StringView part) {
            return StringView(text).find(part) != StringView::npos;
        }

    }  // namespace

    TEST(ContentStamp, WrittenStampChecksAndTheRootLoadsWithAndWithoutLookingAtIt) {
        BinarySerializer writer;
        ASSERT_TRUE(WriteContentStamp(writer, kRootName));
        const StampRoot written = MakeRoot();
        written.Serialize(writer);

        BinaryDeserializer checked = Open(writer.GetData());
        const auto stamp = CheckContentStamp(checked, kSource);
        ASSERT_TRUE(stamp.has_value()) << String(stamp.error()).c_str();
        EXPECT_EQ(stamp->rootType, kRootName);
        EXPECT_EQ(stamp->layout, LayoutOfRoot());
        StampRoot afterCheck;
        afterCheck.Deserialize(checked);
        EXPECT_FALSE(checked.HasErrors());
        ExpectSameRoot(afterCheck, written);

        BinaryDeserializer blind = Open(writer.GetData());
        StampRoot ignored;
        ignored.Deserialize(blind);
        EXPECT_FALSE(blind.HasErrors());
        ExpectSameRoot(ignored, written);
    }

    TEST(ContentStamp, UnknownTypeWritesNothing) {
        BinarySerializer stamped;
        EXPECT_FALSE(WriteContentStamp(stamped, "StampNoSuchType"));
        BinarySerializer untouched;
        EXPECT_EQ(stamped.GetData(), untouched.GetData());
    }

    TEST(ContentStamp, ReadingTheStampInternsNothing) {
        BinarySerializer writer;
        ASSERT_TRUE(WriteContentStamp(writer, kRootName));
        BinaryDeserializer reader = Open(writer.GetData());
        const PoolStringStats before = PoolString::GetStats();
        const auto stamp = CheckContentStamp(reader, kSource);
        const PoolStringStats after = PoolString::GetStats();
        ASSERT_TRUE(stamp.has_value());
        EXPECT_EQ(after.internCalls, before.internCalls);
        EXPECT_EQ(after.stringCount, before.stringCount);
    }

    TEST(ContentStamp, BothVersionsCarryIt) {
        for (const uint16_t version : {BinaryFormat::LegacyVersion, BinaryFormat::TabledVersion}) {
            Streams::Options options;
            options.version = version;
            BinaryDeserializer reader = Open(Streams::Build({StampEntry(kRootName, LayoutOfRoot()), Streams::Number("id", static_cast<int32_t>(5))}, options));
            const auto stamp = CheckContentStamp(reader, kSource);
            ASSERT_TRUE(stamp.has_value()) << "version " << version;
            int32_t id = 0;
            EXPECT_TRUE(reader.Read("id", id));
            EXPECT_EQ(id, 5);
        }
    }

    TEST(ContentStamp, ADocumentWithoutOneIsRefusedAndReadStampSaysSo) {
        const Vector<Streams::Entry> save = {Streams::Number("id", static_cast<int32_t>(5))};
        BinaryDeserializer reader = Open(Streams::Build(save));
        const auto stamp = ReadContentStamp(reader);
        ASSERT_FALSE(stamp.has_value());
        EXPECT_TRUE(Contains(stamp.error(), "no content stamp"));
        EXPECT_FALSE(reader.HasErrors());

        const String message = Refusal(save);
        EXPECT_TRUE(Contains(message, kSource));
        EXPECT_TRUE(Contains(message, "cook"));
    }

    TEST(ContentStamp, StampOfAnUnknownTypeNamesTheTypeAndTheFile) {
        const String message = Refusal({StampEntry("StampNoSuchType", 0x1234)});
        EXPECT_TRUE(Contains(message, kSource));
        EXPECT_TRUE(Contains(message, "StampNoSuchType"));
        EXPECT_TRUE(Contains(message, "cook"));
    }

    TEST(ContentStamp, ChangedHashIsRefusedWithBothHashes) {
        const uint64_t expected = LayoutOfRoot();
        const uint64_t recorded = expected ^ 1U;
        const String message = Refusal({StampEntry(kRootName, recorded)});
        EXPECT_TRUE(Contains(message, kSource));
        EXPECT_TRUE(Contains(message, kRootName));
        EXPECT_TRUE(Contains(message, Format("{:016x}", expected)));
        EXPECT_TRUE(Contains(message, Format("{:016x}", recorded)));
        EXPECT_TRUE(Contains(message, "cook"));
    }

    TEST(ContentStamp, StampOfTheWrongShapeIsRefusedWithoutAnErrorOnTheReader) {
        const Streams::Entry layout = Streams::Number("layout", LayoutOfRoot());
        // `root` is a number, `layout` is a text, a key is missing, the stamp is no object
        EXPECT_FALSE(Refusal({Streams::Object(BinaryFormat::ContentStampName, {Streams::Number("root", static_cast<int32_t>(3)), layout})}).empty());
        EXPECT_FALSE(Refusal({Streams::Object(BinaryFormat::ContentStampName, {Streams::Text("root", kRootName), Streams::Text("layout", "x")})}).empty());
        EXPECT_FALSE(Refusal({Streams::Object(BinaryFormat::ContentStampName, {Streams::Text("root", kRootName)})}).empty());
        EXPECT_TRUE(Contains(Refusal({Streams::Number(BinaryFormat::ContentStampName, static_cast<int32_t>(1))}), "not an object"));
    }

    TEST(ContentStamp, ReadingTheStampKeepsTheErrorsTheReaderHad) {
        BinaryDeserializer reader = Open(Streams::Build({Streams::Number("a", static_cast<int32_t>(1))}));
        String wrong;
        EXPECT_FALSE(reader.Read("a", wrong));
        ASSERT_TRUE(reader.HasErrors());
        const size_t errors = reader.GetErrors().size();
        EXPECT_FALSE(ReadContentStamp(reader).has_value());
        EXPECT_EQ(reader.GetErrors().size(), errors);
    }

}  // namespace BECore::Tests
