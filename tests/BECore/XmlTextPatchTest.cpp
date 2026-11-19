#include <BECore/Config/XmlTextPatch.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using AttributeEdit = XmlTextPatch::AttributeEdit;
        using ElementEdit = XmlTextPatch::ElementEdit;

        AttributeEdit Set(const String& name, const String& value) {
            return AttributeEdit{name, value};
        }

        AttributeEdit Remove(const String& name) {
            return AttributeEdit{name, std::nullopt};
        }

        /// The @p nth element called @p name in document order
        pugi::xml_node FindElement(const pugi::xml_document& doc, StringView name, size_t nth = 0) {
            size_t seen = 0;
            pugi::xml_node found;
            const auto visit = [&](auto&& self, pugi::xml_node node) -> void {
                for (pugi::xml_node child : node.children()) {
                    if (child.type() == pugi::node_element && StringView{child.name()} == name && seen++ == nth) {
                        found = child;
                    }
                    self(self, child);
                }
            };
            visit(visit, doc);
            return found;
        }

        struct Target {
            StringView element;
            size_t nth = 0;
            Vector<AttributeEdit> edits;
        };

        struct Result {
            bool ok = false;
            String text;
            String error;
        };

        Result Patch(const String& text, const Vector<Target>& targets) {
            pugi::xml_document doc;
            Result result;
            if (!XmlTextPatch::Parse(text, doc, result.error)) {
                return result;
            }
            Vector<ElementEdit> edits;
            for (const Target& target : targets) {
                edits.push_back({FindElement(doc, target.element, target.nth), target.edits});
            }
            result.ok = XmlTextPatch::Apply(text, edits, result.text, result.error);
            return result;
        }

        Result PatchOne(const String& text, StringView element, const Vector<AttributeEdit>& edits) {
            return Patch(text, {{element, 0, edits}});
        }

        String Prefix(StringView head, StringView tail) {
            String text{head};
            text.append(tail.data(), tail.size());
            return text;
        }

        constexpr StringView kUtf8Bom = "\xEF\xBB\xBF";

        /// xorshift: a seeded sequence that is the same on every platform
        class Random {
        public:
            explicit Random(uint32_t seed) : _state(seed) {}

            uint32_t Next() {
                _state ^= _state << 13;
                _state ^= _state >> 17;
                _state ^= _state << 5;
                return _state;
            }

            size_t Below(size_t bound) {
                return Next() % bound;
            }

        private:
            uint32_t _state;
        };

    }  // namespace

    TEST(XmlTextPatchTest, SetKeepsTheQuoteAndTheOrder) {
        const Result result = PatchOne("<a x=\"1\" y='2' z=\"3\"/>", "a", {Set("y", "20"), Set("x", "10")});
        ASSERT_TRUE(result.ok) << result.error.c_str();
        EXPECT_EQ(result.text, "<a x=\"10\" y='20' z=\"3\"/>");
    }

    TEST(XmlTextPatchTest, SetWritesEntitiesAndTheQuoteOfItsKind) {
        const Result result = PatchOne("<a x=\"1\" y='2'/>", "a", {Set("x", "a&b<\"q\">'"), Set("y", "a&b<\"q\">'")});
        ASSERT_TRUE(result.ok) << result.error.c_str();
        EXPECT_EQ(result.text, "<a x=\"a&amp;b&lt;&quot;q&quot;>'\" y='a&amp;b&lt;\"q\">&apos;'/>");
    }

    TEST(XmlTextPatchTest, WhitespaceCharactersAreWrittenAsReferences) {
        const Result result = PatchOne("<a x=\"1\"/>", "a", {Set("x", "a\nb\r\tc")});
        ASSERT_TRUE(result.ok) << result.error.c_str();
        EXPECT_EQ(result.text, "<a x=\"a&#10;b&#13;&#9;c\"/>");
        pugi::xml_document doc;
        String error;
        ASSERT_TRUE(XmlTextPatch::Parse(result.text, doc, error));
        EXPECT_STREQ(doc.child("a").attribute("x").value(), "a\nb\r\tc");
    }

    TEST(XmlTextPatchTest, ValueThatHoldsAGreaterThanSignIsScannedThrough) {
        const Result result = PatchOne("<a t=\"x>y\" u='p=\"q\"' v=\"1\">text</a>", "a", {Set("v", "2"), Set("u", "r")});
        ASSERT_TRUE(result.ok) << result.error.c_str();
        EXPECT_EQ(result.text, "<a t=\"x>y\" u='r' v=\"2\">text</a>");
    }

    TEST(XmlTextPatchTest, SpacingAroundEqualsAndAfterTheNameIsKept) {
        const Result result = PatchOne("<a  x = \"1\"\t y\n=\n'2' ></a>", "a", {Set("x", "5"), Set("y", "6")});
        ASSERT_TRUE(result.ok) << result.error.c_str();
        EXPECT_EQ(result.text, "<a  x = \"5\"\t y\n=\n'6' ></a>");
    }

    TEST(XmlTextPatchTest, AddToATagWithNoAttribute) {
        EXPECT_EQ(PatchOne("<a>t</a>", "a", {Set("x", "1")}).text, "<a x=\"1\">t</a>");
        EXPECT_EQ(PatchOne("<a/>", "a", {Set("x", "1")}).text, "<a x=\"1\"/>");
        EXPECT_EQ(PatchOne("<a />", "a", {Set("x", "1")}).text, "<a x=\"1\" />");
        EXPECT_EQ(PatchOne("<a ></a>", "a", {Set("x", "1")}).text, "<a x=\"1\" ></a>");
    }

    TEST(XmlTextPatchTest, AddToATagWithAnAttribute) {
        EXPECT_EQ(PatchOne("<a x=\"1\"></a>", "a", {Set("y", "2")}).text, "<a x=\"1\" y=\"2\"></a>");
        EXPECT_EQ(PatchOne("<a x=\"1\"/>", "a", {Set("y", "2")}).text, "<a x=\"1\" y=\"2\"/>");
        EXPECT_EQ(PatchOne("<a x='1' />", "a", {Set("y", "2")}).text, "<a x='1' y=\"2\" />");
        EXPECT_EQ(PatchOne("<a x=\"1\"></a>", "a", {Set("y", "2"), Set("z", "3")}).text, "<a x=\"1\" y=\"2\" z=\"3\"></a>");
    }

    TEST(XmlTextPatchTest, AddToATagWrittenOneAttributePerLine) {
        const String text = "<root>\n  <a x=\"1\"\n     y=\"2\"\n     z=\"3\"/>\n</root>\n";
        const Result result = PatchOne(text, "a", {Set("w", "4")});
        ASSERT_TRUE(result.ok) << result.error.c_str();
        EXPECT_EQ(result.text, "<root>\n  <a x=\"1\"\n     y=\"2\"\n     z=\"3\"\n     w=\"4\"/>\n</root>\n");
    }

    TEST(XmlTextPatchTest, AddToATagWrittenOneAttributePerLineInACrlfFile) {
        const Result result = PatchOne("<a\r\n  x=\"1\"\r\n  y=\"2\"\r\n/>\r\n", "a", {Set("z", "3")});
        ASSERT_TRUE(result.ok) << result.error.c_str();
        EXPECT_EQ(result.text, "<a\r\n  x=\"1\"\r\n  y=\"2\"\r\n  z=\"3\"\r\n/>\r\n");
    }

    TEST(XmlTextPatchTest, AddToATagWhoseLastAttributeSharesItsLineUsesASpace) {
        const Result result = PatchOne("<a x=\"1\"\n   y=\"2\" z=\"3\"></a>", "a", {Set("w", "4")});
        ASSERT_TRUE(result.ok) << result.error.c_str();
        EXPECT_EQ(result.text, "<a x=\"1\"\n   y=\"2\" z=\"3\" w=\"4\"></a>");
    }

    TEST(XmlTextPatchTest, RemoveTakesTheAttributeAndTheWhitespaceBefore) {
        EXPECT_EQ(PatchOne("<a x=\"1\" y=\"2\" z=\"3\"/>", "a", {Remove("x")}).text, "<a y=\"2\" z=\"3\"/>");
        EXPECT_EQ(PatchOne("<a x=\"1\" y=\"2\" z=\"3\"/>", "a", {Remove("y")}).text, "<a x=\"1\" z=\"3\"/>");
        EXPECT_EQ(PatchOne("<a x=\"1\" y=\"2\" z=\"3\" />", "a", {Remove("z")}).text, "<a x=\"1\" y=\"2\" />");
        EXPECT_EQ(PatchOne("<a x=\"1\"/>", "a", {Remove("x")}).text, "<a/>");
        EXPECT_EQ(PatchOne("<a x=\"1\" y=\"2\"></a>", "a", {Remove("x"), Remove("y")}).text, "<a></a>");
    }

    TEST(XmlTextPatchTest, RemoveInATagWrittenOneAttributePerLine) {
        const String text = "<a\n  x=\"1\"\n  y=\"2\"\n  z=\"3\"\n/>";
        EXPECT_EQ(PatchOne(text, "a", {Remove("x")}).text, "<a\n  y=\"2\"\n  z=\"3\"\n/>");
        EXPECT_EQ(PatchOne(text, "a", {Remove("y")}).text, "<a\n  x=\"1\"\n  z=\"3\"\n/>");
        EXPECT_EQ(PatchOne(text, "a", {Remove("z")}).text, "<a\n  x=\"1\"\n  y=\"2\"\n/>");
    }

    TEST(XmlTextPatchTest, RemovingAnAbsentAttributeChangesNothing) {
        const Result result = PatchOne("<a x=\"1\"/>", "a", {Remove("q")});
        ASSERT_TRUE(result.ok) << result.error.c_str();
        EXPECT_EQ(result.text, "<a x=\"1\"/>");
    }

    TEST(XmlTextPatchTest, RemoveTheLastAndAddInOneCall) {
        const Result result = PatchOne("<a\n  x=\"1\"\n  y=\"2\"/>", "a", {Remove("y"), Set("z", "3")});
        ASSERT_TRUE(result.ok) << result.error.c_str();
        EXPECT_EQ(result.text, "<a\n  x=\"1\"\n  z=\"3\"/>");
    }

    TEST(XmlTextPatchTest, NamesThatContainDashesAndColons) {
        const Result result = PatchOne("<ns:a data-x=\"1\" xml:lang=\"en\"/>", "ns:a", {Set("data-x", "2"), Remove("xml:lang"), Set("a.b", "3")});
        ASSERT_TRUE(result.ok) << result.error.c_str();
        EXPECT_EQ(result.text, "<ns:a data-x=\"2\" a.b=\"3\"/>");
    }

    TEST(XmlTextPatchTest, EverythingOutsideTheTagIsIdentical) {
        const String head = Prefix(kUtf8Bom, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<!-- a <b x=\"1\"/> in a comment -->\r\n<?pi data?>\r\n");
        const String body = "<root>\r\n  <!-- before -->\r\n  <b x=\"1\"/>  text &amp; more\r\n  <a x=\"1\" y=\"2\">\r\n    <![CDATA[<a x=\"1\"/>]]>\r\n  </a>\r\n</root>\r\n<!-- after -->\r\n\r\n";
        const Result result = PatchOne(head + body, "a", {Set("x", "9")});
        ASSERT_TRUE(result.ok) << result.error.c_str();
        EXPECT_EQ(result.text,
                  head + "<root>\r\n  <!-- before -->\r\n  <b x=\"1\"/>  text &amp; more\r\n  <a x=\"9\" y=\"2\">\r\n    <![CDATA[<a x=\"1\"/>]]>\r\n  </a>\r\n</root>\r\n<!-- after -->\r\n\r\n");
    }

    TEST(XmlTextPatchTest, NoEditReturnsTheSameBytes) {
        const String text = Prefix(kUtf8Bom, "<a x=\"1\"\r\n y='2'/>\r\n<!-- c -->");
        pugi::xml_document doc;
        String error;
        ASSERT_TRUE(XmlTextPatch::Parse(text, doc, error)) << error.c_str();
        String out;
        ASSERT_TRUE(XmlTextPatch::Apply(text, {}, out, error)) << error.c_str();
        EXPECT_EQ(out, text);
        const Vector<ElementEdit> empty{{doc.child("a"), {}}};
        ASSERT_TRUE(XmlTextPatch::Apply(text, empty, out, error)) << error.c_str();
        EXPECT_EQ(out, text);
    }

    TEST(XmlTextPatchTest, EntitiesInTheTextBeforeTheTagDoNotShiftTheOffset) {
        const String text = "<root t=\"&lt;&amp;&#10;\">\r\n  &amp;&lt;&#x41;\r\n  <a x=\"&quot;\"/>\r\n</root>";
        const Result result = PatchOne(text, "a", {Set("x", "2")});
        ASSERT_TRUE(result.ok) << result.error.c_str();
        EXPECT_EQ(result.text, "<root t=\"&lt;&amp;&#10;\">\r\n  &amp;&lt;&#x41;\r\n  <a x=\"2\"/>\r\n</root>");
    }

    TEST(XmlTextPatchTest, SeveralElementsInOneCall) {
        const String text = "<root>\n  <a x=\"1\"/>\n  <a x=\"22\" y=\"3\"/>\n  <b/>\n</root>";
        const Result result = Patch(text, {{"b", 0, {Set("z", "long value")}}, {"a", 1, {Remove("y"), Set("x", "")}}, {"a", 0, {Set("x", "333333")}}});
        ASSERT_TRUE(result.ok) << result.error.c_str();
        EXPECT_EQ(result.text, "<root>\n  <a x=\"333333\"/>\n  <a x=\"\"/>\n  <b z=\"long value\"/>\n</root>");
    }

    TEST(XmlTextPatchTest, TheSameElementTwiceIsAnError) {
        const String text = "<a x=\"1\"/>";
        const Result result = Patch(text, {{"a", 0, {Set("x", "2")}}, {"a", 0, {Set("y", "3")}}});
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error.find("twice"), String::npos);
    }

    TEST(XmlTextPatchTest, TheSameAttributeTwiceIsAnError) {
        EXPECT_FALSE(PatchOne("<a x=\"1\"/>", "a", {Set("x", "2"), Remove("x")}).ok);
    }

    TEST(XmlTextPatchTest, AnElementOfAnotherDocumentIsRefused) {
        const String text = "<a x=\"1\"/>";
        pugi::xml_document other;
        String error;
        ASSERT_TRUE(XmlTextPatch::Parse("<b y=\"2\"/>", other, error));
        const Vector<ElementEdit> edits{{other.child("b"), {Set("y", "3")}}};
        String out = "unchanged";
        EXPECT_FALSE(XmlTextPatch::Apply(text, edits, out, error));
        EXPECT_NE(error.find("does not match"), String::npos);
        EXPECT_EQ(out, "unchanged");
    }

    TEST(XmlTextPatchTest, ATextEditedAfterTheParseIsRefused) {
        const String text = "<root><a x=\"1\"/></root>";
        pugi::xml_document doc;
        String error;
        ASSERT_TRUE(XmlTextPatch::Parse(text, doc, error));
        const Vector<ElementEdit> edits{{doc.child("root").child("a"), {Set("x", "2")}}};
        String out;
        EXPECT_FALSE(XmlTextPatch::Apply(Format("<!--shifted-->{}", text), edits, out, error));
        EXPECT_FALSE(XmlTextPatch::Apply("<root><a z=\"1\"/></root>", edits, out, error));
        EXPECT_FALSE(XmlTextPatch::Apply("<root><a x=\"1\" y=\"2\"/></root>", edits, out, error));
        EXPECT_TRUE(XmlTextPatch::Apply(text, edits, out, error)) << error.c_str();
    }

    TEST(XmlTextPatchTest, ANodeThatIsNotAnElementIsRefused) {
        const String text = "<a x=\"1\"/>";
        pugi::xml_document doc;
        String error;
        ASSERT_TRUE(XmlTextPatch::Parse(text, doc, error));
        String out;
        EXPECT_FALSE(XmlTextPatch::Apply(text, Vector<ElementEdit>{{pugi::xml_node{}, {Set("x", "2")}}}, out, error));
        EXPECT_FALSE(XmlTextPatch::Apply(text, Vector<ElementEdit>{{doc, {Set("x", "2")}}}, out, error));
    }

    TEST(XmlTextPatchTest, BadNamesAndValuesAreRefused) {
        EXPECT_FALSE(PatchOne("<a/>", "a", {Set("", "1")}).ok);
        EXPECT_FALSE(PatchOne("<a/>", "a", {Set("a b", "1")}).ok);
        EXPECT_FALSE(PatchOne("<a/>", "a", {Set("a=b", "1")}).ok);
        EXPECT_FALSE(PatchOne("<a/>", "a", {Set("1a", "1")}).ok);
        const Result control = PatchOne("<a/>", "a", {Set("x", "a\x01z")});
        EXPECT_FALSE(control.ok);
        EXPECT_NE(control.error.find("control character"), String::npos);
    }

    TEST(XmlTextPatchTest, ParseRefusesUtf16AndNulAndBrokenText) {
        pugi::xml_document doc;
        String error;
        const String utf16("\xFF\xFE<\0a\0/\0>\0", 10);
        EXPECT_FALSE(XmlTextPatch::Parse(utf16, doc, error));
        EXPECT_NE(error.find("UTF-16"), String::npos);
        const String utf16Big("\xFE\xFF\0<\0a\0/\0>", 10);
        EXPECT_FALSE(XmlTextPatch::Parse(utf16Big, doc, error));
        const String nul("<a x=\"1\0\"/>", 11);
        EXPECT_FALSE(XmlTextPatch::Parse(nul, doc, error));
        EXPECT_FALSE(XmlTextPatch::Parse("<a x=\"1\"", doc, error));
        EXPECT_FALSE(XmlTextPatch::Parse("", doc, error));
        EXPECT_TRUE(XmlTextPatch::Parse(Prefix(kUtf8Bom, "<a/>"), doc, error)) << error.c_str();
    }

    TEST(XmlTextPatchTest, ParseKeepsCommentsDeclarationAndProcessingInstructions) {
        pugi::xml_document doc;
        String error;
        ASSERT_TRUE(XmlTextPatch::Parse("<?xml version=\"1.0\"?><!--c--><?pi x?><a/>", doc, error)) << error.c_str();
        EXPECT_EQ(doc.first_child().type(), pugi::node_declaration);
        EXPECT_EQ(doc.first_child().next_sibling().type(), pugi::node_comment);
        EXPECT_EQ(doc.first_child().next_sibling().next_sibling().type(), pugi::node_pi);
    }

    namespace {

        constexpr size_t kGeneratedTags = 600;
        constexpr size_t kMaxAttributes = 5;
        constexpr size_t kMaxEdits = 4;

        const Vector<String>& Gaps() {
            static const Vector<String> gaps{" ", "  ", "\t", "\n    ", "\r\n  ", " \n\t"};
            return gaps;
        }

        const Vector<String>& Values() {
            static const Vector<String> values{"", "1", "-0.5", "a>b", "a=b", "x y", "50%", "a&b", "<t>", "q\"q", "q'q", "l1\nl2", "t\tt", "\r", "ü", "&amp;"};
            return values;
        }

        const Vector<String>& TagEnds() {
            static const Vector<String> ends{">", "/>", " />", "\n/>", "  >"};
            return ends;
        }

        struct GeneratedTag {
            String text;
            Vector<String> names;
            Vector<String> values;
        };

        /// The text written for @p value between quotes: `&` first so that the references stay
        String WriteValue(const String& value, char quote) {
            String out;
            for (const char c : value) {
                if (c == '&') {
                    out.append("&amp;");
                } else if (c == '<') {
                    out.append("&lt;");
                } else if (c == '\n') {
                    out.append("&#10;");
                } else if (c == '\r') {
                    out.append("&#13;");
                } else if (c == '\t') {
                    out.append("&#9;");
                } else if (c == quote) {
                    out.append(quote == '"' ? "&quot;" : "&apos;");
                } else {
                    out.push_back(c);
                }
            }
            return out;
        }

        GeneratedTag GenerateTag(Random& random) {
            GeneratedTag tag;
            tag.text = "<e";
            const size_t count = random.Below(kMaxAttributes + 1);
            for (size_t i = 0; i < count; ++i) {
                const String name = Format("a{}", i);
                const String value = Values()[random.Below(Values().size())];
                const char quote = random.Below(2) == 0 ? '"' : '\'';
                const String before = random.Below(2) == 0 ? String{" "} : String{};
                const String after = random.Below(2) == 0 ? String{" "} : String{};
                tag.text.append(Gaps()[random.Below(Gaps().size())]);
                tag.text.append(name);
                tag.text.append(before);
                tag.text.append("=");
                tag.text.append(after);
                tag.text.push_back(quote);
                tag.text.append(WriteValue(value, quote));
                tag.text.push_back(quote);
                tag.names.push_back(name);
                tag.values.push_back(value);
            }
            tag.text.append(TagEnds()[random.Below(TagEnds().size())]);
            if (tag.text.back() == '>' && tag.text[tag.text.size() - 2] != '/') {
                tag.text.append("body</e>");
            }
            return tag;
        }

    }  // namespace

    TEST(XmlTextPatchTest, GeneratedTagsAndEditsKeepEveryOtherAttribute) {
        Random random(0xB4610U);
        for (size_t round = 0; round < kGeneratedTags; ++round) {
            const GeneratedTag tag = GenerateTag(random);
            Vector<AttributeEdit> edits;
            Vector<String> expectedNames = tag.names;
            Vector<String> expectedValues = tag.values;
            const size_t editCount = random.Below(kMaxEdits + 1);
            for (size_t i = 0; i < editCount; ++i) {
                const String name = Format("a{}", random.Below(kMaxAttributes + 2));
                bool already = false;
                for (const AttributeEdit& edit : edits) {
                    already = already || edit.name == name;
                }
                if (already) {
                    continue;
                }
                size_t index = expectedNames.size();
                for (size_t j = 0; j < expectedNames.size(); ++j) {
                    if (expectedNames[j] == name) {
                        index = j;
                    }
                }
                if (random.Below(3) == 0) {
                    edits.push_back(Remove(name));
                    if (index < expectedNames.size()) {
                        expectedNames.erase(expectedNames.begin() + static_cast<ptrdiff_t>(index));
                        expectedValues.erase(expectedValues.begin() + static_cast<ptrdiff_t>(index));
                    }
                } else {
                    const String value = Values()[random.Below(Values().size())];
                    edits.push_back(Set(name, value));
                    if (index < expectedNames.size()) {
                        expectedValues[index] = value;
                    } else {
                        expectedNames.push_back(name);
                        expectedValues.push_back(value);
                    }
                }
            }

            const String text = Prefix("<root>\n<!-- c -->", tag.text) + "\n</root>\n";
            const Result result = PatchOne(text, "e", edits);
            ASSERT_TRUE(result.ok) << result.error.c_str() << "\n" << text.c_str();

            pugi::xml_document doc;
            String error;
            ASSERT_TRUE(XmlTextPatch::Parse(result.text, doc, error)) << error.c_str() << "\n" << result.text.c_str();
            const pugi::xml_node element = doc.child("root").child("e");
            size_t index = 0;
            for (const pugi::xml_attribute attribute : element.attributes()) {
                ASSERT_LT(index, expectedNames.size()) << result.text.c_str();
                EXPECT_EQ(String{attribute.name()}, expectedNames[index]) << result.text.c_str();
                EXPECT_EQ(String{attribute.value()}, expectedValues[index]) << result.text.c_str();
                ++index;
            }
            EXPECT_EQ(index, expectedNames.size()) << result.text.c_str();

            const size_t prefixLength = String("<root>\n<!-- c -->").size();
            EXPECT_EQ(result.text.substr(0, prefixLength), text.substr(0, prefixLength));
            const size_t suffixLength = String("\n</root>\n").size();
            EXPECT_EQ(result.text.substr(result.text.size() - suffixLength), "\n</root>\n");
        }
    }

}  // namespace BECore::Tests
