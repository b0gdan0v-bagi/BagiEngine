// Operations on a JsonView (Control/JsonViewOps.h): equality, copy into a writer and JSON Pointer. The DOM (Json.h) is the reference: every
// answer is checked against what `Json` gives for the same text.

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonViewOps.h>
#include <Common/AllocCheck.h>
#include <Common/JsonDoc.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        StringView View(const String& text) {
            return StringView(text.c_str(), text.size());
        }

        bool Equal(StringView a, StringView b) {
            const JsonDoc left(a);
            const JsonDoc right(b);
            EXPECT_TRUE(left.IsParsed()) << "not JSON: " << String(a.data(), a.size()).c_str();
            EXPECT_TRUE(right.IsParsed()) << "not JSON: " << String(b.data(), b.size()).c_str();
            const bool equal = JsonEquals(left.Root(), right.Root());
            EXPECT_EQ(JsonEquals(right.Root(), left.Root()), equal) << "equality is not symmetric";
            return equal;
        }

        /// What the DOM says of the same two texts
        bool DomEqual(StringView a, StringView b) {
            const auto left = ParseJson(a);
            const auto right = ParseJson(b);
            return left && right && *left == *right;
        }

        void ExpectSame(StringView a, StringView b, bool expected) {
            EXPECT_EQ(Equal(a, b), expected) << String(a.data(), a.size()).c_str() << " against " << String(b.data(), b.size()).c_str();
            EXPECT_EQ(DomEqual(a, b), expected) << "the DOM disagrees: " << String(a.data(), a.size()).c_str() << " against " << String(b.data(), b.size()).c_str();
        }

        String Written(JsonView value) {
            String out;
            JsonWriter writer(out);
            WriteJson(writer, value);
            EXPECT_TRUE(writer.IsComplete());
            return out;
        }

        JsonDoc PointerDocument() {
            return JsonDoc(R"({"":3,"0":"zero","a":{"b":[10,{"c":"deep"},20]},"a/b":1,"e\u0041":"escaped key","list":[1,2,3],"m~n":2,"s":"text"})");
        }

        /// The value at @p pointer by the view overload as JSON text; "absent" or "error: <message>"
        String ViewPointerResult(JsonView document, StringView pointer) {
            const auto found = FindJsonPointer(document, pointer);
            if (!found) {
                return Format("error: {}", found.error().message);
            }
            return found->has_value() ? Written(**found) : String("absent");
        }

        String DomPointerResult(const Json& document, StringView pointer) {
            const auto found = FindJsonPointer(document, pointer);
            if (!found) {
                return Format("error: {}", found.error().message);
            }
            return *found == nullptr ? String("absent") : DumpJson(**found);
        }

    }  // namespace

    TEST(JsonViewOpsTest, ObjectsAreEqualWhateverTheOrderOfTheirKeys) {
        ExpectSame(R"({"a":1,"b":2})", R"({"b":2,"a":1})", true);
        ExpectSame(R"({"a":{"x":[1,2],"y":null},"b":true})", R"({"b":true,"a":{"y":null,"x":[1,2]}})", true);
        ExpectSame(R"({})", R"({})", true);
        ExpectSame(R"({"a":1})", R"({"a":1,"b":2})", false);
        ExpectSame(R"({"a":1,"b":2})", R"({"a":1,"c":2})", false);
        ExpectSame(R"({"a":1})", R"({"a":2})", false);
    }

    TEST(JsonViewOpsTest, ArraysAreEqualItemByItem) {
        ExpectSame("[1,2,3]", "[1,2,3]", true);
        ExpectSame("[1,2,3]", "[3,2,1]", false);
        ExpectSame("[1,2]", "[1,2,3]", false);
        ExpectSame("[]", "[]", true);
        ExpectSame("[[1],[2,[3]]]", "[[1],[2,[3]]]", true);
        ExpectSame("[[1],[2,[3]]]", "[[1],[2,[4]]]", false);
    }

    TEST(JsonViewOpsTest, NumbersAreEqualByValue) {
        ExpectSame("1", "1.0", true);
        ExpectSame("1", "1e0", true);
        ExpectSame("1.0", "1E0", true);
        ExpectSame("-0", "0", true);
        ExpectSame("-0", "0.0", true);
        ExpectSame("100", "1e2", true);
        ExpectSame("1", "2", false);
        ExpectSame("0.1", "0.10000000000000001", true);
        ExpectSame("9223372036854775807", "9223372036854775808", false);
        ExpectSame("18446744073709551615", "18446744073709551615", true);
        ExpectSame("18446744073709551616", "18446744073709551616", true);
        ExpectSame("18446744073709551616", "1.8446744073709552e19", true);
    }

    TEST(JsonViewOpsTest, ASignedAndAnUnsignedBeyondInt64AreNeverEqual) {
        // The DOM wraps the unsigned around and calls -1 equal to 18446744073709551615
        EXPECT_FALSE(Equal("-1", "18446744073709551615"));
    }

    TEST(JsonViewOpsTest, StringsAreEqualByTheirDecodedText) {
        ExpectSame(R"("A")", R"("A")", true);
        ExpectSame(R"("A")", R"("A")", true);
        ExpectSame(R"("A")", R"("A")", true);
        ExpectSame(R"("a\nb")", "\"a\\u000ab\"", true);
        ExpectSame(R"("😀")", "\"\xF0\x9F\x98\x80\"", true);
        ExpectSame(R"("a")", R"("b")", false);
        ExpectSame(R"("a")", R"("ab")", false);
        ExpectSame(R"("Ab")", R"("A")", false);
        ExpectSame(R"("")", R"("")", true);
    }

    TEST(JsonViewOpsTest, AnEscapedKeyIsTheKeyItStandsFor) {
        ExpectSame(R"({"A":1})", R"({"A":1})", true);
        ExpectSame(R"({"A":1})", R"({"A":2})", false);
    }

    TEST(JsonViewOpsTest, ValuesOfOtherKindsAreNotEqual) {
        ExpectSame("null", "null", true);
        ExpectSame("true", "true", true);
        ExpectSame("true", "false", false);
        ExpectSame("null", "false", false);
        ExpectSame("0", "false", false);
        ExpectSame("0", R"("0")", false);
        ExpectSame("[]", "{}", false);
        ExpectSame("[]", "null", false);
        ExpectSame(R"({"a":null})", R"({"b":null})", false);
        ExpectSame(R"({"a":null})", "{}", false);
    }

    TEST(JsonViewOpsTest, ViewsThatAreNotValidAreEqualOnlyToEachOther) {
        const JsonDoc doc("null");

        EXPECT_TRUE(JsonEquals(JsonView(), JsonView()));
        EXPECT_FALSE(JsonEquals(JsonView(), doc.Root()));
        EXPECT_FALSE(JsonEquals(doc.Root(), JsonView()));
    }

    TEST(JsonViewOpsTest, OfTwoEqualKeysTheFirstCounts) {
        const JsonDoc left(R"({"a":1,"a":1})");
        const JsonDoc right(R"({"a":1,"a":2})");

        EXPECT_TRUE(JsonEquals(left.Root(), right.Root()));
        EXPECT_FALSE(JsonEquals(right.Root(), left.Root()));
    }

    TEST(JsonViewOpsTest, WriteJsonWritesWhatTheDomDumps) {
        const Vector<String> corpus = {
            "null",
            "true",
            "false",
            "0",
            "-0",
            "-17",
            "9223372036854775807",
            "-9223372036854775808",
            "18446744073709551615",
            "123456789012345678901234567890",
            "1.5",
            "1.0",
            "0.1",
            "1E5",
            "1e20",
            "-2.5e-7",
            "5e-324",
            R"("")",
            R"("plain")",
            R"("tab\tnewline\nquote\"slash\\\/")",
            R"("\u0001\u001f\u007f")",
            R"("é中😀")",
            "\"\xC3\xA9 and \xF0\x9F\x98\x80\"",
            "[]",
            "{}",
            R"([1,[2,[3,[]]],{"k":[null]}])",
            R"({"a":1,"b":[1.5,-2,18446744073709551615],"c":{"d":"x","e":{}},"s":"line\n"})",
            R"({"A":1,"a\"b":3,"é":2})",
        };

        for (const String& text : corpus) {
            const JsonDoc doc(View(text));
            ASSERT_TRUE(doc.IsParsed()) << text.c_str();
            const auto dom = ParseJson(View(text));
            ASSERT_TRUE(dom.has_value()) << text.c_str();
            EXPECT_EQ(Written(doc.Root()), DumpJson(*dom)) << text.c_str();
        }
    }

    TEST(JsonViewOpsTest, WriteJsonKeepsTheOrderOfTheDocument) {
        const JsonDoc doc(R"({ "b" : 1 , "a" : [ 2 , 3 ] })");

        EXPECT_EQ(Written(doc.Root()), R"({"b":1,"a":[2,3]})");
    }

    TEST(JsonViewOpsTest, WriteJsonOfAViewThatIsNotValidIsNull) {
        EXPECT_EQ(Written(JsonView()), "null");
    }

    TEST(JsonViewOpsTest, WriteJsonWritesASubValueInsideAnOpenContainer) {
        const JsonDoc doc(R"({"x":[1,{"y":2}]})");
        String out;
        JsonWriter writer(out);

        writer.BeginObject();
        writer.Key("copy");
        WriteJson(writer, *doc.Root().Find("x"));
        writer.EndObject();

        EXPECT_TRUE(writer.IsComplete());
        EXPECT_EQ(out, R"({"copy":[1,{"y":2}]})");
    }

    TEST(JsonViewOpsTest, WriteJsonOfADocumentDeeperThanTheWriterCutsItInsteadOfCrashing) {
        String text;
        const size_t depth = JsonWriter::kMaxDepth;
        for (size_t i = 0; i < depth; ++i) {
            text += '[';
        }
        for (size_t i = 0; i < depth; ++i) {
            text += ']';
        }
        const JsonDoc doc(View(text));
        ASSERT_TRUE(doc.IsParsed());
        String out;
        JsonWriter writer(out);

        writer.BeginArray();
        WriteJson(writer, doc.Root());
        writer.EndArray();

        EXPECT_TRUE(writer.IsComplete());
        EXPECT_NE(out.find("null"), String::npos);
    }

    namespace {

        String WrittenSorted(JsonView value, int indent) {
            String out;
            JsonWriter writer(out, indent);
            WriteJsonSorted(writer, value);
            EXPECT_TRUE(writer.IsComplete());
            return out;
        }

    }  // namespace

    TEST(JsonViewOpsTest, WriteJsonSortedWritesTheBytesTheDomDumps) {
        const Vector<String> corpus = {
            "null",
            "[]",
            "{}",
            R"([3,{"b":1,"a":[{"z":0,"y":{"d":1,"c":2}}]},"x"])",
            R"({"b":2,"a":1,"c":{"z":[],"y":{},"x":[{"q":1,"p":2}]}})",
            R"({"B":1,"a":2,"A":3,"":4,"ab":5,"a ":6})",
            R"({"é":1,"z":2,"\u00e9x":3,"e":4})",
            R"({"a\"b":1,"a\nb":2,"a\\b":3,"\u0001":4})",
            R"({"n":-0.5,"big":18446744073709551615,"f":1e20,"s":"tab\tq\"","t":true,"u":null})",
            R"({"\u0000":1,"\u0000a":2,"a":3})",
        };

        for (const String& text : corpus) {
            const JsonDoc doc(View(text));
            ASSERT_TRUE(doc.IsParsed()) << text.c_str();
            const auto dom = ParseJson(View(text));
            ASSERT_TRUE(dom.has_value()) << text.c_str();
            EXPECT_EQ(WrittenSorted(doc.Root(), JsonWriter::kCompact), DumpJson(*dom)) << text.c_str();
            EXPECT_EQ(WrittenSorted(doc.Root(), 2), DumpJson(*dom, 2)) << text.c_str();
        }
    }

    TEST(JsonViewOpsTest, WriteJsonSortedWritesTheFirstOfTwoEqualKeys) {
        const JsonDoc doc(R"({"b":1,"a":1,"b":2,"a\u0000":0,"a":3})");

        EXPECT_EQ(WrittenSorted(doc.Root(), JsonWriter::kCompact), "{\"a\":1,\"a\\u0000\":0,\"b\":1}");
    }

    TEST(JsonViewOpsTest, WriteJsonSortedOfAViewThatIsNotValidIsNull) {
        EXPECT_EQ(WrittenSorted(JsonView(), JsonWriter::kCompact), "null");
    }

    TEST(JsonViewOpsTest, WriteJsonSortedCutsAContainerThatWouldPassTheDepthOfTheWriter) {
        String text;
        for (size_t i = 0; i < JsonWriter::kMaxDepth; ++i) {
            text += "{\"k\":";
        }
        text += "1";
        for (size_t i = 0; i < JsonWriter::kMaxDepth; ++i) {
            text += '}';
        }
        const JsonDoc doc(View(text));
        ASSERT_TRUE(doc.IsParsed());
        String out;
        JsonWriter writer(out);

        writer.BeginArray();
        WriteJsonSorted(writer, doc.Root());
        writer.EndArray();

        EXPECT_TRUE(writer.IsComplete());
        // The array holds one level, so the nest is written one object short and its innermost object is the null
        size_t opened = 0;
        for (const char letter : out) {
            opened += letter == '{' ? 1 : 0;
        }
        EXPECT_EQ(opened, JsonWriter::kMaxDepth - 1);
        EXPECT_NE(out.find("null"), String::npos);
    }

    TEST(JsonViewOpsTest, PointerAnswersAsTheDomOverloadDoes) {
        const JsonDoc document = PointerDocument();
        const auto dom = ParseJson(document.Text());
        ASSERT_TRUE(dom.has_value());
        const Vector<String> pointers = {
            "",         "/",         "/a",       "/a/b/0",   "/a/b/1/c",    "/a~1b",      "/m~0n",     "/0", "/list/2", "/missing", "/a/missing/x", "/list/3", "/list/99999999999999999999999999",
            "/s/x",     "/list/0/x", "/eA",      "a",        "a/b",         "/a~",        "/a~2",      "~",  "/~x",     "/list/0x", "/list/01",     "/list/-", "/list/",
            "/list/+1", "/list/1.5", "/list/ 1", "/list/-1", "/missing/x~", "/list/9/x~", "/list/9/-",
        };

        for (const String& pointer : pointers) {
            EXPECT_EQ(ViewPointerResult(document.Root(), View(pointer)), DomPointerResult(*dom, View(pointer))) << pointer.c_str();
        }
    }

    TEST(JsonViewOpsTest, PointerNamesTheDocumentItselfWhenEmpty) {
        const JsonDoc document = PointerDocument();
        const auto found = FindJsonPointer(document.Root(), "");

        ASSERT_TRUE(found.has_value());
        ASSERT_TRUE(found->has_value());
        EXPECT_EQ(Written(**found), Written(document.Root()));
    }

    TEST(JsonViewOpsTest, PointerOfAnyLengthIsHandled) {
        const JsonDoc document = PointerDocument();
        String deep;
        for (int i = 0; i < 100000; ++i) {
            deep += "/a";
        }
        const String longKey = Format("/{}", String(1 << 20, 'k'));
        const String longDigits = Format("/list/{}", String(1 << 16, '9'));
        const String longBad = Format("/list/{}x", String(1 << 16, '9'));

        EXPECT_EQ(ViewPointerResult(document.Root(), View(deep)), "absent");
        EXPECT_EQ(ViewPointerResult(document.Root(), View(longKey)), "absent");
        EXPECT_EQ(ViewPointerResult(document.Root(), View(longDigits)), "absent");
        EXPECT_EQ(ViewPointerResult(document.Root(), View(longBad)).find("error"), 0u);
    }

    TEST(JsonViewOpsTest, PointerInAViewThatIsNotValidFindsNothing) {
        const auto found = FindJsonPointer(JsonView(), "/a");

        ASSERT_TRUE(found.has_value());
        EXPECT_FALSE(found->has_value());
        EXPECT_FALSE(FindJsonPointer(JsonView(), "a").has_value());
    }

    TEST(JsonViewOpsTest, CheckPointerJudgesTheSyntaxAsTheDomOverloadDoesOnAnEmptyObject) {
        const Json empty = Json::object();
        const Vector<String> pointers = {"", "/", "/a", "/a/0/b", "/a~0~1", "a", "a/b", "/a~", "/a~2", "~", "/~x", "/list/-", "/list/01"};

        for (const String& pointer : pointers) {
            const auto checked = CheckJsonPointer(View(pointer));
            const auto found = FindJsonPointer(empty, View(pointer));
            ASSERT_EQ(checked.has_value(), found.has_value()) << pointer.c_str();
            if (!checked) {
                EXPECT_EQ(checked.error().code, ControlErrors::kInvalidParams) << pointer.c_str();
                EXPECT_EQ(checked.error().message, found.error().message) << pointer.c_str();
            }
        }
    }

    TEST(JsonViewOpsTest, EqualityAndWritingOfAShapeAllocateNothingOnceWarm) {
        const JsonDoc left(R"({"a":[1,2.5,"text",true,null],"b":{"c":"d","e":-7},"f":18446744073709551615})");
        const JsonDoc right(R"({"f":18446744073709551615,"b":{"e":-7,"c":"d"},"a":[1,2.5,"text",true,null]})");
        String out;
        out.reserve(256);
        bool equal = false;
        const auto run = [&] {
            out.clear();
            JsonWriter writer(out);
            WriteJson(writer, left.Root());
            equal = JsonEquals(left.Root(), right.Root());
            const auto found = FindJsonPointer(left.Root(), "/b/c");
            EXPECT_TRUE(found.has_value() && found->has_value());
        };
        run();

        ExpectNoAllocations(run);

        EXPECT_TRUE(equal);
        EXPECT_EQ(out, R"({"a":[1,2.5,"text",true,null],"b":{"c":"d","e":-7},"f":18446744073709551615})");
    }

}  // namespace BECore::Tests
