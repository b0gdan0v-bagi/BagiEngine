#include "ReflectionAliasesTest.h"

#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/PropertyVisitorDispatch.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Common/NullPropertyVisitor.h>
#include <Common/Printers.h>
#include <Common/SharedPtr.h>
#include <EASTL/optional.h>
#include <Generated/ReflectionAliasesTest.gen.hpp>

namespace BECore::Tests {

    using AliasTestData::AliasState;
    using AliasTestData::EastlState;

    // The types a field can have, by alias, that serialize: an alias is reflected as the eastl:: / std:: type it stands for
    static_assert(Ser::HasDataAccessor<String>);
    static_assert(Ser::HasDataAccessor<FixedString<8>>);
    static_assert(Ser::HasDataAccessor<FixedString<8, false>>);
    static_assert(Ser::HasDataAccessor<Vector<int32_t>>);
    static_assert(Ser::HasDataAccessor<SmallVector<int32_t, 4>>);
    static_assert(Ser::HasDataAccessor<FixedVector<int32_t, 4>>);
    static_assert(Ser::HasDataAccessor<Array<int32_t, 3>>);
    static_assert(Ser::HasDataAccessor<HashMap<String, int32_t>>);
    static_assert(Ser::HasDataAccessor<HashMap<int32_t, String>>);
    static_assert(Ser::HasDataAccessor<Map<String, int32_t>>);
    static_assert(Ser::HasDataAccessor<FixedHashMap<String, int32_t, 8>>);
    static_assert(Ser::HasDataAccessor<Optional<int32_t>>);
    static_assert(Ser::HasDataAccessor<Pair<int32_t, String>>);
    static_assert(Ser::HasDataAccessor<Variant<int32_t, float>>);

    // The aliases that are not reflected: a field of one of them does not compile. Adding support is a decision to make
    // on purpose (the serialized form of a sorted vector, of a view and of a pointer to one object has to be chosen), and this
    // list is where to remove the line.
    static_assert(!Ser::HasDataAccessor<FlatMap<String, int32_t>>);
    static_assert(!Ser::HasDataAccessor<FlatSet<int32_t>>);
    static_assert(!Ser::HasDataAccessor<Span<int32_t>>);
    static_assert(!Ser::HasDataAccessor<UniquePtr<int32_t>>);
    static_assert(!Ser::HasDataAccessor<SharedPtr<int32_t>>);
    static_assert(!Ser::HasDataAccessor<HashSet<int32_t>>);
    static_assert(!Ser::HasDataAccessor<Set<int32_t>>);
    static_assert(!Ser::HasDataAccessor<Tuple<int32_t, float>>);

    // The std:: and eastl:: spellings that are not the aliases' (`Array` and `Pair` are eastl's, `Optional` is std's) have no
    // accessor of their own: a field spells the alias.
    static_assert(!Ser::HasDataAccessor<std::array<int32_t, 3>>);      //-NoStd: the spelling without an accessor is the case
    static_assert(!Ser::HasDataAccessor<std::pair<int32_t, String>>);  //-NoStd: the spelling without an accessor is the case
    static_assert(!Ser::HasDataAccessor<eastl::optional<int32_t>>);    //-NoStd: the spelling without an accessor is the case

    TEST(ReflectionAliasesTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts at the top of this file are the check";
    }

    namespace {

        using ByteBuffer = eastl::vector<uint8_t>;

        template <typename State>
        void Fill(State& state) {
            state.text = "caf\xC3\xA9 <&> \"quoted\"";
            state.fixedText = "fixed";
            state.list = {3, 1, 2};
            state.words = {"one", "two", ""};
            state.small = {1, 2, 3, 4, 5, 6};
            state.fixed = {7, 8};
            state.triple = {{9, 8, 7}};
            for (const char* key : {"pear", "apple", "fig", "banana"}) {
                state.hashed[key] = static_cast<int32_t>(state.hashed.size());
                state.ordered[key] = static_cast<int32_t>(state.ordered.size());
                state.fixedHashed[key] = static_cast<int32_t>(state.fixedHashed.size());
            }
            state.present = 42;
            state.absent.reset();
            state.entry = {11, "eleven"};
            if constexpr (requires { state.choice; }) {
                state.choice = String("picked");
            }
        }

        template <typename State>
        eastl::string SaveXml(const State& state) {
            XmlSerializer serializer;
            if (serializer.BeginObject("State")) {
                state.Serialize(serializer);
                serializer.EndObject();
            }
            return serializer.SaveToString();
        }

        template <typename State>
        void LoadXml(const eastl::string& xml, State& state) {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(xml)) << xml.c_str();
            ASSERT_TRUE(deserializer.BeginObject("State"));
            state.Deserialize(deserializer);
            deserializer.EndObject();
            EXPECT_FALSE(deserializer.HasErrors());
        }

        template <typename State>
        ByteBuffer SaveBinary(const State& state) {
            BinarySerializer serializer;
            if (serializer.BeginObject("State")) {
                state.Serialize(serializer);
                serializer.EndObject();
            }
            return serializer.GetData();
        }

        template <typename State>
        void LoadBinary(const ByteBuffer& data, State& state) {
            BinaryDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromBuffer(data));
            ASSERT_TRUE(deserializer.BeginObject("State"));
            state.Deserialize(deserializer);
            deserializer.EndObject();
            EXPECT_FALSE(deserializer.HasErrors());
        }

        /// Stale content in every field, so that a load that skips one shows
        void Dirty(AliasState& state) {
            state.text = "stale";
            state.fixedText = "stale";
            state.list = {99};
            state.words = {"stale"};
            state.small = {99};
            state.fixed = {99};
            state.triple = {{1, 1, 1}};
            state.hashed["stale"] = 1;
            state.ordered["stale"] = 1;
            state.fixedHashed["stale"] = 1;
            state.present.reset();
            state.absent = 5;
            state.entry = {0, "stale"};
            state.choice = 99;
        }

        /// @p withChoice: the file had the `choice` the twin of the eastl:: spelling has not
        void ExpectFilled(const AliasState& state, bool withChoice = true) {
            EXPECT_EQ(state.text, "caf\xC3\xA9 <&> \"quoted\"");
            EXPECT_EQ(state.fixedText, "fixed");
            EXPECT_EQ(state.list.size(), 3u);
            EXPECT_EQ(state.list[0], 3);
            ASSERT_EQ(state.words.size(), 3u);
            EXPECT_EQ(state.words[1], "two");
            EXPECT_EQ(state.words[2], "");
            EXPECT_EQ(state.small.size(), 6u);
            EXPECT_EQ(state.small[5], 6);
            EXPECT_EQ(state.fixed.size(), 2u);
            EXPECT_EQ(state.triple[0], 9);
            EXPECT_EQ(state.triple[2], 7);
            EXPECT_EQ(state.hashed.size(), 4u);
            EXPECT_EQ(state.hashed.at("fig"), 2);
            EXPECT_EQ(state.ordered.size(), 4u);
            EXPECT_EQ(state.ordered.at("banana"), 3);
            EXPECT_EQ(state.ordered.begin()->first, "apple");
            EXPECT_EQ(state.fixedHashed.size(), 4u);
            EXPECT_EQ(state.fixedHashed.at("pear"), 0);
            EXPECT_FALSE(state.hashed.contains("stale"));
            EXPECT_TRUE(state.present.has_value());
            EXPECT_EQ(state.present.value_or(0), 42);
            EXPECT_FALSE(state.absent.has_value());
            EXPECT_EQ(state.entry.first, 11);
            EXPECT_EQ(state.entry.second, "eleven");
            if (withChoice) {
                ASSERT_EQ(state.choice.index(), 1u);
                EXPECT_EQ(std::get<1>(state.choice), "picked");
            }
        }

        /// @p xml without the line of its `choice` element, the field the twin of the eastl:: spelling has not
        eastl::string WithoutChoice(eastl::string xml) {
            const size_t at = xml.find("<choice");
            if (at != eastl::string::npos) {
                const size_t lineStart = xml.rfind('\n', at);
                xml.erase(lineStart, xml.find("/>", at) + 2 - lineStart);
            }
            return xml;
        }

    }  // namespace

    TEST(ReflectionAliasesTest, XmlRoundTrip) {
        AliasState saved;
        Fill(saved);
        const eastl::string xml = SaveXml(saved);

        AliasState loaded;
        Dirty(loaded);
        ASSERT_NO_FATAL_FAILURE(LoadXml(xml, loaded));
        ExpectFilled(loaded);
        EXPECT_EQ(SaveXml(loaded), xml);
    }

    TEST(ReflectionAliasesTest, BinaryRoundTrip) {
        AliasState saved;
        Fill(saved);
        const ByteBuffer data = SaveBinary(saved);

        AliasState loaded;
        Dirty(loaded);
        ASSERT_NO_FATAL_FAILURE(LoadBinary(data, loaded));
        ExpectFilled(loaded);
        EXPECT_TRUE(SaveBinary(loaded) == data);
    }

    TEST(ReflectionAliasesTest, SavedTextIsTheSameAsOfTheEastlSpelling) {
        AliasState aliased;
        EastlState eastl;
        Fill(aliased);
        Fill(eastl);

        const eastl::string aliasedXml = SaveXml(aliased);
        // Only the type of the root differs in the text when the names do not
        ASSERT_NE(aliasedXml.find("<choice index=\"1\" value=\"picked\""), eastl::string::npos) << aliasedXml.c_str();
        EXPECT_EQ(WithoutChoice(aliasedXml), SaveXml(eastl));
        // The binary entries have sizes, so the twin reads the alias bytes (skipping `choice`) and writes the same bytes as its own
        EastlState binaryTwin;
        ASSERT_NO_FATAL_FAILURE(LoadBinary(SaveBinary(aliased), binaryTwin));
        EXPECT_TRUE(SaveBinary(binaryTwin) == SaveBinary(eastl));

        // A file written from the eastl:: / std:: spelling loads into the alias fields
        EastlState before;
        Fill(before);
        AliasState loaded;
        ASSERT_NO_FATAL_FAILURE(LoadXml(SaveXml(before), loaded));
        ExpectFilled(loaded, false);
    }

    TEST(ReflectionAliasesTest, ThePairIsSavedAsTheEastlPairIs) {
        const Pair<int32_t, String> aliased{4, "four"};
        const eastl::pair<int32_t, eastl::string> reference{4, "four"};
        const auto save = [](const auto& pair) {
            XmlSerializer serializer;
            if (serializer.BeginObject("State")) {
                Ser::Save(serializer, pair, "pair");
                serializer.EndObject();
            }
            return serializer.SaveToString();
        };
        const eastl::string xml = save(aliased);
        EXPECT_EQ(xml, save(reference));

        Pair<int32_t, String> loaded{0, "stale"};
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml));
        ASSERT_TRUE(deserializer.BeginObject("State"));
        EXPECT_TRUE(Ser::Load(deserializer, loaded, "pair"));
        deserializer.EndObject();
        EXPECT_EQ(loaded, aliased);
    }

    TEST(ReflectionAliasesTest, SkippedDefaultsOmitEmptyAliasFields) {
        AliasState empty;
        XmlSerializer serializer;
        serializer.SetSkipDefaults(true);
        if (serializer.BeginObject("State")) {
            empty.Serialize(serializer);
            serializer.EndObject();
        }
        // A Variant has no default to skip (its index is always written): it is the one field left
        const eastl::string xml = serializer.SaveToString();
        EXPECT_NE(xml.find("<State>\n    <choice index=\"0\" value=\"0\" />\n  </State>"), eastl::string::npos) << xml.c_str();
    }

    TEST(ReflectionAliasesTest, TheKeysOfAHashMapComeOutSortedWhatEverTheHashOrder) {
        AliasState state;
        for (int i = 0; i < 40; ++i) {
            state.hashed[Format("key_{:02}", (i * 17) % 40)] = i;
        }
        const eastl::string xml = SaveXml(state);
        size_t previous = 0;
        for (int i = 0; i < 40; ++i) {
            const size_t at = xml.find(Format("\"key_{:02}\"", i).c_str());
            ASSERT_NE(at, eastl::string::npos) << i;
            EXPECT_GE(at, previous) << i;
            previous = at;
        }
    }

    TEST(ReflectionAliasesTest, ATextTooLongForAFixedStringThatCannotOverflowIsCut) {
        FixedString<8, false> text;
        const eastl::string xml = "<root><State value=\"0123456789\"/></root>";
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml));
        ASSERT_TRUE(deserializer.BeginObject("State"));
        EXPECT_TRUE(Ser::Load(deserializer, text, "value"));
        deserializer.EndObject();
        EXPECT_EQ(text.size(), 7u);
        EXPECT_EQ(text, "0123456");

        FixedString<8> roomy;
        ASSERT_TRUE(deserializer.BeginObject("State"));
        EXPECT_TRUE(Ser::Load(deserializer, roomy, "value"));
        deserializer.EndObject();
        EXPECT_EQ(roomy, "0123456789");
    }

    TEST(ReflectionAliasesTest, AFixedHashMapThatCannotOverflowKeepsWhatFits) {
        FixedHashMap<String, int32_t, 2, 3, false> source;
        const eastl::string xml = "<root><State><map><entry key=\"a\" value=\"1\"/><entry key=\"b\" value=\"2\"/><entry key=\"c\" value=\"3\"/></map></State></root>";
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml));
        ASSERT_TRUE(deserializer.BeginObject("State"));
        EXPECT_TRUE(Ser::Load(deserializer, source, "map"));
        deserializer.EndObject();
        EXPECT_EQ(source.size(), 2u);
        EXPECT_TRUE(source.contains("a"));
    }

    namespace {

        /// What a fixed container that cannot overflow reports when the file has more than it holds: one error, under the field
        void ExpectOneDropReport(const IDeserializer& deserializer, StringView field, StringView message) {
            ASSERT_TRUE(deserializer.HasErrors());
            ASSERT_EQ(deserializer.GetErrors().size(), 1u);
            EXPECT_EQ(deserializer.GetErrors()[0].path, field);
            EXPECT_EQ(deserializer.GetErrors()[0].errorMessage, message);
        }

        template <typename Value>
        ByteBuffer SaveBinaryField(const Value& value) {
            BinarySerializer serializer;
            if (serializer.BeginObject("State")) {
                Ser::Save(serializer, value, "value");
                serializer.EndObject();
            }
            return serializer.GetData();
        }

        /// Loads @p value from XML text `<root><State>...</State></root>` and leaves the errors in @p deserializer
        template <typename Value>
        void LoadXmlField(XmlDeserializer& deserializer, const eastl::string& xml, Value& value) {
            ASSERT_TRUE(deserializer.LoadFromString(xml));
            ASSERT_TRUE(deserializer.BeginObject("State"));
            Ser::Load(deserializer, value, "value");
            deserializer.EndObject();
        }

        template <typename Value>
        void LoadBinaryField(BinaryDeserializer& deserializer, const ByteBuffer& data, Value& value) {
            ASSERT_TRUE(deserializer.LoadFromBuffer(data));
            ASSERT_TRUE(deserializer.BeginObject("State"));
            Ser::Load(deserializer, value, "value");
            deserializer.EndObject();
        }

        constexpr StringView kDroppedText = "Data lost, the fixed container is full: kept 7 of 10 bytes";
        constexpr StringView kDroppedEntries = "Data lost, the fixed container is full: kept 2 of 3 entries";
        constexpr StringView kDroppedItems = "Data lost, the fixed container is full: kept 2 of 4 items";

    }  // namespace

    TEST(ReflectionAliasesTest, ATextTooLongForAFixedStringThatCannotOverflowReportsTheDroppedTextFromXml) {
        FixedString<8, false> text;
        XmlDeserializer deserializer;
        LoadXmlField(deserializer, "<root><State value=\"0123456789\"/></root>", text);
        EXPECT_EQ(text, "0123456");
        ExpectOneDropReport(deserializer, "State.value", kDroppedText);
    }

    TEST(ReflectionAliasesTest, ATextTooLongForAFixedStringThatCannotOverflowReportsTheDroppedTextFromBinary) {
        FixedString<64> roomy = "0123456789";
        FixedString<8, false> text;
        BinaryDeserializer deserializer;
        LoadBinaryField(deserializer, SaveBinaryField(roomy), text);
        EXPECT_EQ(text, "0123456");
        ExpectOneDropReport(deserializer, "State.value", kDroppedText);
    }

    TEST(ReflectionAliasesTest, ATextThatFitsReportsNothing) {
        FixedString<8, false> text;
        FixedString<8, false> exact;
        XmlDeserializer xml;
        LoadXmlField(xml, "<root><State value=\"0123456\"/></root>", text);
        EXPECT_EQ(text, "0123456");
        EXPECT_FALSE(xml.HasErrors());

        BinaryDeserializer binary;
        LoadBinaryField(binary, SaveBinaryField(FixedString<64>("0123456")), exact);
        EXPECT_EQ(exact, "0123456");
        EXPECT_FALSE(binary.HasErrors());

        FixedString<4> roomy;
        XmlDeserializer overflowing;
        LoadXmlField(overflowing, "<root><State value=\"0123456789\"/></root>", roomy);
        EXPECT_EQ(roomy, "0123456789");
        EXPECT_FALSE(overflowing.HasErrors());
    }

    TEST(ReflectionAliasesTest, MoreEntriesThanAFixedHashMapThatCannotOverflowHoldsAreReportedFromXml) {
        FixedHashMap<String, int32_t, 2, 3, false> map;
        XmlDeserializer deserializer;
        LoadXmlField(deserializer, "<root><State><value><entry key=\"a\" value=\"1\"/><entry key=\"b\" value=\"2\"/><entry key=\"c\" value=\"3\"/></value></State></root>", map);
        EXPECT_EQ(map.size(), 2u);
        EXPECT_TRUE(map.contains("a"));
        EXPECT_TRUE(map.contains("b"));
        ExpectOneDropReport(deserializer, "State.value", kDroppedEntries);
    }

    TEST(ReflectionAliasesTest, MoreEntriesThanAFixedHashMapThatCannotOverflowHoldsAreReportedFromBinary) {
        FixedHashMap<String, int32_t, 8> roomy;
        roomy["a"] = 1;
        roomy["b"] = 2;
        roomy["c"] = 3;
        FixedHashMap<String, int32_t, 2, 3, false> map;
        BinaryDeserializer deserializer;
        LoadBinaryField(deserializer, SaveBinaryField(roomy), map);
        EXPECT_EQ(map.size(), 2u);
        EXPECT_TRUE(map.contains("a"));
        EXPECT_TRUE(map.contains("b"));
        ExpectOneDropReport(deserializer, "State.value", kDroppedEntries);
    }

    TEST(ReflectionAliasesTest, EntriesThatFitAFixedHashMapReportNothing) {
        FixedHashMap<String, int32_t, 2, 3, false> map;
        XmlDeserializer xml;
        LoadXmlField(xml, "<root><State><value><entry key=\"a\" value=\"1\"/><entry key=\"b\" value=\"2\"/></value></State></root>", map);
        EXPECT_EQ(map.size(), 2u);
        EXPECT_FALSE(xml.HasErrors());

        FixedHashMap<String, int32_t, 2, 3> overflowing;
        XmlDeserializer roomy;
        LoadXmlField(roomy, "<root><State><value><entry key=\"a\" value=\"1\"/><entry key=\"b\" value=\"2\"/><entry key=\"c\" value=\"3\"/></value></State></root>", overflowing);
        EXPECT_EQ(overflowing.size(), 3u);
        EXPECT_FALSE(roomy.HasErrors());
    }

    TEST(ReflectionAliasesTest, MoreItemsThanAFixedVectorHoldsAreCutAndReportedFromXml) {
        FixedVector<int32_t, 2> list;
        XmlDeserializer deserializer;
        LoadXmlField(deserializer, "<root><State><value><item value=\"1\"/><item value=\"2\"/><item value=\"3\"/><item value=\"4\"/></value></State></root>", list);
        ASSERT_EQ(list.size(), 2u);
        EXPECT_EQ(list[0], 1);
        EXPECT_EQ(list[1], 2);
        ExpectOneDropReport(deserializer, "State.value", kDroppedItems);
    }

    TEST(ReflectionAliasesTest, MoreItemsThanAFixedVectorHoldsAreCutAndReportedFromBinary) {
        FixedVector<int32_t, 8> roomy;
        roomy = {1, 2, 3, 4};
        FixedVector<int32_t, 2> list;
        BinaryDeserializer deserializer;
        LoadBinaryField(deserializer, SaveBinaryField(roomy), list);
        ASSERT_EQ(list.size(), 2u);
        EXPECT_EQ(list[1], 2);
        ExpectOneDropReport(deserializer, "State.value", kDroppedItems);
    }

    TEST(ReflectionAliasesTest, ItemsThatFitAFixedVectorReportNothing) {
        FixedVector<int32_t, 2> list;
        XmlDeserializer deserializer;
        LoadXmlField(deserializer, "<root><State><value><item value=\"1\"/><item value=\"2\"/></value></State></root>", list);
        EXPECT_EQ(list.size(), 2u);
        EXPECT_FALSE(deserializer.HasErrors());
    }

    namespace {

        /// A visitor that replaces every String it is shown
        class TextEditor : public NullPropertyVisitor {
        public:
            using NullPropertyVisitor::Visit;
            bool Visit(StringView, String& value) override {
                seen = value;
                value = replacement;
                return true;
            }

            String replacement = "0123456789ABCDEF-too-long";
            String seen;
        };

    }  // namespace

    TEST(ReflectionAliasesTest, TheInspectorEditsAStringAndAFixedString) {
        TextEditor editor;

        eastl::string text = "before";
        EXPECT_TRUE(PropVisit::Visit(editor, text, "text"));
        EXPECT_EQ(editor.seen, "before");
        EXPECT_EQ(text, "0123456789ABCDEF-too-long");

        FixedString<8, false> cut = "abc";
        EXPECT_TRUE(PropVisit::Visit(editor, cut, "cut"));
        EXPECT_EQ(editor.seen, "abc");
        EXPECT_EQ(cut, "0123456");

        FixedString<64> roomy = "xyz";
        EXPECT_TRUE(PropVisit::Visit(editor, roomy, "roomy"));
        EXPECT_EQ(roomy, "0123456789ABCDEF-too-long");

        AliasState state;
        state.fixedText = "kept";
        EXPECT_TRUE(state.AcceptPropertyVisitor(editor));
    }

    TEST(ReflectionAliasesTest, TheInspectorEditsTheStringsOfAListAndOfAVariant) {
        AliasState state;
        state.words = {"one", "two"};
        state.choice = String("picked");
        TextEditor editor;
        EXPECT_TRUE(PropVisit::Visit(editor, state.words, "words"));
        EXPECT_EQ(editor.seen, "two");
        ASSERT_EQ(state.words.size(), 2u);
        EXPECT_EQ(state.words[0], editor.replacement);
        EXPECT_EQ(state.words[1], editor.replacement);

        EXPECT_TRUE(PropVisit::Visit(editor, state.choice, "choice"));
        EXPECT_EQ(editor.seen, "picked");
        EXPECT_EQ(std::get<1>(state.choice), editor.replacement);
    }

    namespace {

        /// The name of every field of a visit, in order: the first name the visit of a field gives after its BeginField
        class FieldNames final : public TextEditor {
        public:
            using TextEditor::Visit;

            Vector<String> names;

            void BeginField(const FieldAttributes& /*attributes*/) override {
                ++_depth;
                _named = false;
            }
            void EndField() override {
                --_depth;
            }
            bool Visit(StringView name, int32_t& /*value*/) override {
                Note(name);
                return false;
            }
            bool Visit(StringView name, String& /*value*/) override {
                Note(name);
                return false;
            }
            bool BeginCompound(StringView name) override {
                Note(name);
                return true;
            }
            bool BeginList(StringView name, const ListInfo& /*info*/) override {
                Note(name);
                return true;
            }
            bool VisitPresence(StringView name, bool& /*present*/) override {
                Note(name);
                return false;
            }

        private:
            void Note(StringView name) {
                if (_depth == 1 && !_named) {
                    names.push_back(String(name.data(), name.size()));
                    _named = true;
                }
            }

            int32_t _depth = 0;
            bool _named = false;
        };

    }  // namespace

    TEST(ReflectionAliasesTest, TheVisitorReachesEveryFieldThroughTheDispatch) {
        AliasState state;
        FieldNames visitor;
        EXPECT_FALSE(state.AcceptPropertyVisitor(visitor));
        const Vector<String> expected{"text", "fixedText", "list", "words", "small", "fixed", "triple", "hashed", "ordered", "fixedHashed", "present", "absent", "entry", "choice"};
        EXPECT_EQ(visitor.names, expected);
    }

}  // namespace BECore::Tests
