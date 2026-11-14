#include "ReflectionAliasesTest.h"

#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/PropertyVisitorDispatch.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Common/Printers.h>
#include <EASTL/optional.h>
#include <Generated/ReflectionAliasesTest.gen.hpp>

namespace BECore::Tests {

    using AliasTestData::AliasState;
    using AliasTestData::EastlState;

    // The types a field can have, by alias, that serialize: an alias is reflected as the eastl:: / std:: type it stands for was
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

    // The aliases that are not reflected today: a field of one of them does not compile. Adding support is a decision to make
    // on purpose (the serialized form of a sorted vector, of a view and of a pointer to one object has to be chosen), and this
    // list is where to remove the line.
    static_assert(!Ser::HasDataAccessor<FlatMap<String, int32_t>>);
    static_assert(!Ser::HasDataAccessor<FlatSet<int32_t>>);
    static_assert(!Ser::HasDataAccessor<Span<int32_t>>);
    static_assert(!Ser::HasDataAccessor<UniquePtr<int32_t>>);

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
        }

        void ExpectFilled(const AliasState& state) {
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
        EXPECT_EQ(aliasedXml, SaveXml(eastl));
        EXPECT_TRUE(SaveBinary(aliased) == SaveBinary(eastl));

        // A file written before the aliases loads into the alias fields
        EastlState before;
        Fill(before);
        AliasState loaded;
        ASSERT_NO_FATAL_FAILURE(LoadXml(SaveXml(before), loaded));
        ExpectFilled(loaded);
    }

    TEST(ReflectionAliasesTest, SkippedDefaultsOmitEmptyAliasFields) {
        AliasState empty;
        XmlSerializer serializer;
        serializer.SetSkipDefaults(true);
        if (serializer.BeginObject("State")) {
            empty.Serialize(serializer);
            serializer.EndObject();
        }
        EXPECT_NE(serializer.SaveToString().find("<State />"), eastl::string::npos);
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

        /// A visitor that replaces every String it is shown
        class TextEditor : public IPropertyVisitor {
        public:
            bool Visit(eastl::string_view, bool&) override {
                return false;
            }
            bool Visit(eastl::string_view, int8_t&) override {
                return false;
            }
            bool Visit(eastl::string_view, uint8_t&) override {
                return false;
            }
            bool Visit(eastl::string_view, int16_t&) override {
                return false;
            }
            bool Visit(eastl::string_view, uint16_t&) override {
                return false;
            }
            bool Visit(eastl::string_view, int32_t&) override {
                return false;
            }
            bool Visit(eastl::string_view, uint32_t&) override {
                return false;
            }
            bool Visit(eastl::string_view, int64_t&) override {
                return false;
            }
            bool Visit(eastl::string_view, uint64_t&) override {
                return false;
            }
            bool Visit(eastl::string_view, float&) override {
                return false;
            }
            bool Visit(eastl::string_view, double&) override {
                return false;
            }
            bool Visit(eastl::string_view, PoolString&) override {
                return false;
            }
            bool Visit(eastl::string_view, eastl::string& value) override {
                seen = value;
                value = replacement;
                return true;
            }

            eastl::string replacement = "0123456789ABCDEF-too-long";
            eastl::string seen;
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

        // A visit of an alias field of a reflected class compiles and edits the text
        AliasState state;
        state.fixedText = "kept";
        EXPECT_TRUE(state.AcceptPropertyVisitor(editor));
    }

}  // namespace BECore::Tests
