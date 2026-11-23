// The reflected leaf JsonPayload (Control/JsonPayload.h): a generated `WriteJson` writes it as one value at the sorted place of its key, byte for byte
// the text `DumpJson` gives for the same document.

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonPayload.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/Control/JsonWriteReflect.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/JsonPayloadTypes.h>
#include <Common/AllocCheck.h>
#include <Common/AssertBridge.h>
#include <Generated/JsonPayloadTypes.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using JsonPayloadTestData::PayloadBase;
        using JsonPayloadTestData::PayloadBetween;
        using JsonPayloadTestData::PayloadDerived;
        using JsonPayloadTestData::PayloadOptional;
        using JsonPayloadTestData::PayloadRow;
        using JsonPayloadTestData::PayloadRows;

        constexpr Array<int, 2> kIndents = {JsonWriter::kCompact, 2};

        /// A document with nested containers and keys that the DOM sorts
        Json MakeDocument() {
            return Json::parse(R"({"b":[1,2.5,"x"],"a":{"z":null,"y":true},"c":"text"})");
        }

        template <typename T>
        String Written(const T& object, int indent = JsonWriter::kCompact) {
            String out;
            JsonWriter writer(out, indent);
            object.WriteJson(writer);
            EXPECT_TRUE(writer.IsComplete());
            return out;
        }

        /// The writing form's callback: one object, the way a game's `WriteState` does
        std::expected<void, ControlError> WriteSmallObject(JsonWriter& writer) {
            writer.BeginObject();
            writer.Member("n", 1);
            writer.Member("on", true);
            writer.EndObject();
            return {};
        }

        const String kSmallObjectText = R"({"n":1,"on":true})";

    }  // namespace

    TEST(JsonPayloadTest, ABorrowedDocumentIsWrittenAtTheSortedPlaceOfItsKey) {
        const Json document = MakeDocument();
        PayloadBetween answer;
        answer.alpha = "A";
        answer.zulu = "Z";
        answer.middle = JsonPayload::Borrowed(document);
        for (const int indent : kIndents) {
            const Json expected = {{"alpha", "A"}, {"middle", document}, {"zulu", "Z"}};
            EXPECT_EQ(Written(answer, indent), DumpJson(expected, indent)) << "indent " << indent;
        }
    }

    TEST(JsonPayloadTest, AnOwnedDocumentIsMovedInAndWrittenTheSame) {
        Json document = MakeDocument();
        const String reference = DumpJson(document);
        PayloadBetween answer;
        answer.middle = JsonPayload::Owned(std::move(document));
        EXPECT_TRUE(document.is_null()) << "moved from, so not copied";  // NOLINT(bugprone-use-after-move): a moved-from Json is null
        EXPECT_EQ(Written(answer), String("{\"alpha\":\"\",\"middle\":") + reference + ",\"zulu\":\"\"}");

        // Moving the payload moves the document with it
        PayloadBetween other;
        other.middle = std::move(answer.middle);
        EXPECT_EQ(Written(other), String("{\"alpha\":\"\",\"middle\":") + reference + ",\"zulu\":\"\"}");
    }

    TEST(JsonPayloadTest, ReadyTextIsWrittenAsItIsAlsoWhenIndented) {
        const String text = R"({"k":[1,2]})";
        PayloadBetween answer;
        answer.middle = JsonPayload::Text(text);
        EXPECT_EQ(Written(answer), R"({"alpha":"","middle":{"k":[1,2]},"zulu":""})");
        EXPECT_EQ(Written(answer, 2), "{\n  \"alpha\": \"\",\n  \"middle\": {\"k\":[1,2]},\n  \"zulu\": \"\"\n}");
    }

    TEST(JsonPayloadTest, AWritingCallbackStreamsTheValue) {
        const auto write = [](JsonWriter& writer) { return WriteSmallObject(writer); };
        PayloadBetween answer;
        answer.middle = JsonPayload::Streamed(write);
        EXPECT_EQ(Written(answer), "{\"alpha\":\"\",\"middle\":" + kSmallObjectText + ",\"zulu\":\"\"}");
    }

    TEST(JsonPayloadTest, AnAbsentOptionalWritesNoKey) {
        PayloadOptional answer;
        const Json document = MakeDocument();
        answer.always = JsonPayload::Borrowed(document);
        EXPECT_EQ(Written(answer), String("{\"always\":") + DumpJson(document) + ",\"version\":0}");

        const String text = "[1]";
        answer.maybe = JsonPayload::Text(text);
        EXPECT_EQ(Written(answer), String("{\"always\":") + DumpJson(document) + ",\"maybe\":[1],\"version\":0}");
    }

    TEST(JsonPayloadTest, AnEmptyPayloadWritesNullAndIsAnExpect) {
        size_t expects = 0;
        AssertBridge::Capture capture([&](const AssertEvent&) { ++expects; });
        PayloadBetween answer;
        String out;
        JsonWriter writer(out);
        answer.WriteJson(writer);
        EXPECT_EQ(out, R"({"alpha":"","middle":null,"zulu":""})");
        EXPECT_TRUE(JsonPayload().IsEmpty());
    }

    TEST(JsonPayloadTest, ARowOfAListAndAnOptionalRowHoldTheirPayloads) {
        const Json first = MakeDocument();
        const String second = "7";
        const auto write = [](JsonWriter& writer) { return WriteSmallObject(writer); };
        PayloadRows answer;
        answer.rows.resize(3);
        answer.rows[0].ok = true;
        answer.rows[0].result = JsonPayload::Borrowed(first);
        answer.rows[1].result = JsonPayload::Text(second);
        answer.rows[2].ok = true;
        answer.rows[2].result = JsonPayload::Streamed(write);
        answer.last = PayloadRow{};
        answer.last->result = JsonPayload::Text(second);

        const String expected =
            "{\"last\":{\"ok\":false,\"result\":7},\"rows\":[{\"ok\":true,\"result\":" + DumpJson(first) + "},{\"ok\":false,\"result\":7},{\"ok\":true,\"result\":" + kSmallObjectText + "}]}";
        EXPECT_EQ(Written(answer), expected);
    }

    TEST(JsonPayloadTest, AClassWithAParentMergesItsPayloadKeysWithTheParents) {
        const String id = "\"r1\"";
        const String data = "{\"q\":1}";
        PayloadDerived answer;
        answer.name = "n";
        answer.id = JsonPayload::Text(id);
        answer.data = JsonPayload::Text(data);
        answer.zeta = 9;
        EXPECT_EQ(Written(answer), R"({"data":{"q":1},"id":"r1","name":"n","zeta":9})");
    }

    TEST(JsonPayloadTest, ANestedValueIsCutLikeTheDocumentWriterWhenTheWriterIsDeep) {
        // The payload writes through the document writer, so the depth rule of WriteJson(writer, json) holds inside a struct
        Json deep = Json::array();
        Json* cursor = &deep;
        for (size_t level = 0; level < JsonWriter::kMaxDepth + 4; ++level) {
            cursor->push_back(Json::array());
            cursor = &(*cursor)[0];
        }
        PayloadBetween answer;
        answer.middle = JsonPayload::Borrowed(deep);
        String out;
        JsonWriter writer(out);
        answer.WriteJson(writer);
        EXPECT_TRUE(writer.IsComplete());
    }

    TEST(JsonPayloadTest, AFailedWritingCallbackInvalidatesTheWriter) {
        size_t expects = 0;
        AssertBridge::Capture capture([&](const AssertEvent&) { ++expects; });
        const auto fail = [](JsonWriter& /*writer*/) -> std::expected<void, ControlError> { return std::unexpected(MakeControlError("boom", "no")); };
        PayloadBetween answer;
        answer.middle = JsonPayload::Streamed(fail);
        String out;
        JsonWriter writer(out);
        answer.WriteJson(writer);
        EXPECT_FALSE(writer.IsValid());
    }

    TEST(JsonPayloadTest, TheLeafIsOpaqueToReflectionAndHasNoFormOutsideTheJsonWire) {
        static_assert(DescribeType<JsonPayload>().kind == TypeKind::Opaque);
        static_assert(DescribeType<Optional<JsonPayload>>().kind == TypeKind::Optional);
        static_assert(DescribeType<Optional<JsonPayload>>().element->kind == TypeKind::Opaque);

        // The generated pair compiles for a struct with a payload; it saves nothing of the document
        const Json document = MakeDocument();
        PayloadBetween answer;
        answer.middle = JsonPayload::Borrowed(document);
        JsonSerializer serializer;
        answer.Serialize(serializer);
    }

    TEST(JsonPayloadTest, TheTextAndTheWritingFormAllocateNothingIntoAKeptString) {
        const String text = R"({"k":[1,2]})";
        const auto write = [](JsonWriter& writer) { return WriteSmallObject(writer); };
        PayloadBetween byText;
        byText.middle = JsonPayload::Text(text);
        PayloadBetween byWriter;
        byWriter.middle = JsonPayload::Streamed(write);

        String out;
        out.reserve(256);
        const auto writeBoth = [&] {
            out.clear();
            JsonWriter first(out);
            byText.WriteJson(first);
            out.clear();
            JsonWriter second(out);
            byWriter.WriteJson(second);
        };
        writeBoth();
        ExpectNoAllocations(writeBoth);
    }

}  // namespace BECore::Tests
