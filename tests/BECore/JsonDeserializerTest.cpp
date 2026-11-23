// JsonDeserializer (Control/JsonDeserializer.h): the round trip with JsonSerializer, strict values, paths of errors, unknown keys, limits.
// The generated code of the test types is included in TypeRegistryTest.cpp, FuzzProbeAccess.cpp and FactoryInitializeTest.cpp; here the types
// are used through their headers.

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/FactoryInitializeTest.h>
#include <BECore/FuzzProbe.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/TypeRegistryTest.h>
#include <Common/Printers.h>
#include <Common/TestText.h>
#include <Fuzz/FuzzProbeAccess.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace RegistryTestData;
    using FuzzProbeData::ProbeNode;
    using FuzzProbeData::ProbeState;

    namespace {

        Json Parse(StringView text) {
            auto parsed = ParseJson(text);
            EXPECT_TRUE(parsed.has_value()) << String(text).c_str();
            return parsed ? std::move(*parsed) : Json();
        }

        template <typename T>
        String ObjectText(const T& object) {
            JsonSerializer serializer;
            object.Serialize(serializer);
            return DumpJson(serializer.Take());
        }

        /// "path: message" of every error, one per line
        String ErrorsText(const IDeserializer& reader) {
            String text;
            for (const DeserializeError& error : reader.GetErrors()) {
                text += error.path;
                text += ": ";
                text += error.errorMessage;
                text += '\n';
            }
            return text;
        }

        String WarningsText(const IDeserializer& reader) {
            String text;
            for (const DeserializeError& warning : reader.GetWarnings()) {
                text += warning.path;
                text += ": ";
                text += warning.errorMessage;
                text += '\n';
            }
            return text;
        }

        /// Loads `{"k": <valueJson>}` into @p value as the field `k`; true when the load answered true
        template <typename T>
        bool LoadK(StringView valueJson, T& value, String& errors) {
            const Json doc = Parse(Format("{{\"k\":{}}}", valueJson));
            JsonDeserializer reader(doc);
            const bool loaded = Ser::Load(reader, value, "k");
            errors = ErrorsText(reader);
            return loaded;
        }

        /// A refused value: false, one error `k: <message>`, and the target as it was
        template <typename T>
        void ExpectRefused(StringView valueJson, T initial, StringView message) {
            T value = initial;
            String errors;
            EXPECT_FALSE(LoadK(valueJson, value, errors)) << String(valueJson).c_str();
            EXPECT_EQ(errors, Format("k: {}\n", message)) << String(valueJson).c_str();
            EXPECT_EQ(value, initial) << String(valueJson).c_str();
        }

        template <typename T>
        void ExpectLoaded(StringView valueJson, const T& expected) {
            T value{};
            String errors;
            EXPECT_TRUE(LoadK(valueJson, value, errors)) << String(valueJson).c_str() << ": " << errors.c_str();
            EXPECT_TRUE(errors.empty()) << errors.c_str();
            EXPECT_EQ(value, expected) << String(valueJson).c_str();
        }

        /// A tree of ProbeNode nested @p depth levels, built without recursion
        Json MakeDeepTree(size_t depth) {
            Json node = Json::object();
            node["id"] = 1;
            node["children"] = Json::array();
            for (size_t level = 0; level < depth; ++level) {
                Json children = Json::array();
                children.push_back(std::move(node));
                Json outer = Json::object();
                outer["id"] = 1;
                outer["children"] = std::move(children);
                node = std::move(outer);
            }
            return node;
        }

        RegAllKinds MakeFilledAllKinds() {
            RegAllKinds all;
            all.flag = true;
            all.tiny = -8;
            all.word = -1600;
            all.count = 3;
            all.big = std::numeric_limits<int64_t>::min();
            all.utiny = 200;
            all.uword = 60000;
            all.ucount = 4000000000u;
            all.ubig = std::numeric_limits<uint64_t>::max();
            all.ratio = 0.1f;
            all.precise = 0.1;
            all.text = "t \"q\"";
            all.fixedText = "abc";
            all.name = "n"_intern;
            all.mode = RegMode::Auto;
            all.inner.value = 5;
            all.inner.label = "in";
            all.items = {1, 2};
            all.smallItems = {3};
            all.fixedItems = {4, 5};
            all.pairOfInts = {6, 7};
            all.table["a"] = 1;
            all.table["b"] = 2;
            all.fixedTable["x"] = 9;
            all.ordered[1] = "o";
            all.maybe = 7;
            all.entry = {8, "e"};
            all.choice = String("c");
            all.node = New<RegNode>();
            all.node->id = 3;
            RegInner first;
            first.value = 1;
            first.label = "p";
            RegInner second;
            second.value = 2;
            second.label = "q";
            all.inners = {first, second};
            return all;
        }

    }  // namespace

    // -------------------------------------------------------------------------
    // Round trips
    // -------------------------------------------------------------------------

    TEST(JsonDeserializerTest, EveryKindOfAClassSurvivesTheRoundTrip) {
        const RegAllKinds all = MakeFilledAllKinds();
        const Json document = Parse(ObjectText(all));

        RegAllKinds loaded;
        JsonDeserializer reader(document);
        loaded.Deserialize(reader);

        EXPECT_TRUE(reader.GetErrors().empty()) << ErrorsText(reader).c_str();
        EXPECT_EQ(ObjectText(loaded), ObjectText(all));
        EXPECT_EQ(loaded.big, std::numeric_limits<int64_t>::min());
        EXPECT_EQ(loaded.ubig, std::numeric_limits<uint64_t>::max());
        EXPECT_EQ(loaded.ratio, 0.1f);
        EXPECT_EQ(loaded.precise, 0.1);
        ASSERT_TRUE(loaded.node);
        EXPECT_EQ(loaded.node->id, 3);
    }

    TEST(JsonDeserializerTest, TheProbeStateSurvivesTheRoundTrip) {
        const ProbeState state = Fuzz::MakeFilledProbe();
        const Json document = Parse(ObjectText(state));

        ProbeState loaded;
        JsonDeserializer reader(document);
        loaded.Deserialize(reader);

        EXPECT_TRUE(reader.GetErrors().empty()) << ErrorsText(reader).c_str();
        EXPECT_EQ(Fuzz::DigestProbe(loaded), Fuzz::DigestProbe(state));
    }

    TEST(JsonDeserializerTest, AFactoryListSurvivesTheRoundTrip) {
        NoHookProbeList list;
        IntrusivePtr<NoHookProbeImpl> item = New<NoHookProbeImpl>();
        item->value = 11;
        list.items.push_back(item);
        const Json document = Parse(ObjectText(list));

        NoHookProbeList loaded;
        JsonDeserializer reader(document);
        loaded.Deserialize(reader);

        EXPECT_TRUE(reader.GetErrors().empty()) << ErrorsText(reader).c_str();
        ASSERT_EQ(loaded.items.size(), 1u);
        EXPECT_EQ(loaded.items[0]->GetValue(), 11);
    }

    TEST(JsonDeserializerTest, ASceneNodeWithComponentsLoadsAsFromXml) {
        const Json document = Parse(R"({"name":"Box","components":[{"type":"TransformComponent","x":1.5},{"type":"ClickableComponent","actionName":"go"}]})");
        XmlDeserializer xml;
        ASSERT_TRUE(xml.LoadFromString(R"(<node name="Box"><components><component type="TransformComponent" x="1.5"/><component type="ClickableComponent" actionName="go"/></components></node>)"));

        const IntrusivePtr<SceneNode> fromJson = TypeRegistry::GetInstance().CreateAs<SceneNode>("SceneNode");
        const IntrusivePtr<SceneNode> fromXml = TypeRegistry::GetInstance().CreateAs<SceneNode>("SceneNode");
        ASSERT_TRUE(fromJson);
        ASSERT_TRUE(fromXml);
        JsonDeserializer reader(document);
        reader.SetDiagnostics(DiagnosticsMode::Warn);
        fromJson->Deserialize(reader);
        reader.FinishLoad();
        fromXml->Deserialize(xml);

        EXPECT_TRUE(reader.GetErrors().empty()) << ErrorsText(reader).c_str();
        EXPECT_TRUE(reader.GetWarnings().empty()) << WarningsText(reader).c_str();
        ASSERT_EQ(fromJson->GetComponents().size(), 2u);
        EXPECT_EQ(fromJson->GetComponents()[0]->GetTypeMeta().typeName, "TransformComponent");
        EXPECT_EQ(fromJson->GetComponents()[1]->GetTypeMeta().typeName, "ClickableComponent");
        EXPECT_EQ(ObjectText(*fromJson), ObjectText(*fromXml));
    }

    TEST(JsonDeserializerTest, AListElementIsReadAsAScalarOrAsAnObjectWithValue) {
        ProbeState state;
        const Json document = Parse(R"({"numbers":[1,{"value":2},3],"words":["a",{"value":"b"}]})");
        JsonDeserializer reader(document);

        state.Deserialize(reader);

        EXPECT_TRUE(reader.GetErrors().empty()) << ErrorsText(reader).c_str();
        EXPECT_EQ(state.numbers, (Vector<int32_t>{1, 2, 3}));
        EXPECT_EQ(state.words, (Vector<String>{"a", "b"}));
    }

    TEST(JsonDeserializerTest, AMissingMemberIsNoError) {
        ProbeState state;
        const Json document = Parse("{}");
        JsonDeserializer reader(document);

        state.Deserialize(reader);

        EXPECT_FALSE(reader.HasErrors());
        EXPECT_EQ(Fuzz::DigestProbe(state), Fuzz::DigestProbe(ProbeState{}));
        EXPECT_FALSE(reader.HasKey("flag"));
    }

    // -------------------------------------------------------------------------
    // Strict values
    // -------------------------------------------------------------------------

    TEST(JsonDeserializerTest, StrictValuesAreAlwaysOn) {
        const Json document = Parse("{}");
        JsonDeserializer reader(document);

        EXPECT_TRUE(reader.IsStrictValues());
        reader.SetStrictValues(false);
        EXPECT_TRUE(reader.IsStrictValues());
    }

    TEST(JsonDeserializerTest, ValuesOfTheRightKindAndRangeAreRead) {
        ExpectLoaded<bool>("true", true);
        ExpectLoaded<int8_t>("-128", std::numeric_limits<int8_t>::min());
        ExpectLoaded<uint8_t>("255", std::numeric_limits<uint8_t>::max());
        ExpectLoaded<int16_t>("-1600", -1600);
        ExpectLoaded<uint16_t>("65535", std::numeric_limits<uint16_t>::max());
        ExpectLoaded<int32_t>("-2147483648", std::numeric_limits<int32_t>::min());
        ExpectLoaded<uint32_t>("4294967295", std::numeric_limits<uint32_t>::max());
        ExpectLoaded<int64_t>("-9223372036854775808", std::numeric_limits<int64_t>::min());
        ExpectLoaded<int64_t>("9223372036854775807", std::numeric_limits<int64_t>::max());
        ExpectLoaded<uint64_t>("18446744073709551615", std::numeric_limits<uint64_t>::max());
        ExpectLoaded<uint64_t>("9223372036854775808", uint64_t{1} << 63);
        ExpectLoaded<float>("0.1", 0.1f);
        ExpectLoaded<float>("3", 3.0f);
        ExpectLoaded<double>("-2.5e3", -2500.0);
        ExpectLoaded<double>("7", 7.0);
        ExpectLoaded<String>("\"text\"", String("text"));
        ExpectLoaded<RegMode>("\"On\"", RegMode::On);
    }

    TEST(JsonDeserializerTest, ABoolIsOnlyAJsonBool) {
        ExpectRefused<bool>("1", true, "Invalid boolean value '1'");
        ExpectRefused<bool>("0", true, "Invalid boolean value '0'");
        ExpectRefused<bool>("\"true\"", false, "Invalid boolean value 'true'");
        ExpectRefused<bool>("null", true, "Invalid boolean value 'null'");
        ExpectRefused<bool>("[]", true, "Invalid boolean value '<array>'");
    }

    TEST(JsonDeserializerTest, AnIntegerIsOnlyAJsonIntegerInRange) {
        ExpectRefused<int32_t>("3.0", 5, "Invalid integer value '3.0'");
        ExpectRefused<int32_t>("1.5", 5, "Invalid integer value '1.5'");
        ExpectRefused<int32_t>("\"3\"", 5, "Invalid integer value '3'");
        ExpectRefused<int32_t>("true", 5, "Invalid integer value 'true'");
        ExpectRefused<int32_t>("null", 5, "Invalid integer value 'null'");
        ExpectRefused<int32_t>("{}", 5, "Invalid integer value '<object>'");
        ExpectRefused<int32_t>("2147483648", 5, "Invalid integer value '2147483648' (out of range)");
        ExpectRefused<int32_t>("-2147483649", 5, "Invalid integer value '-2147483649' (out of range)");
        ExpectRefused<int8_t>("200", 5, "Invalid integer value '200' (out of range)");
        ExpectRefused<uint8_t>("256", 5, "Invalid integer value '256' (out of range)");
        ExpectRefused<uint8_t>("-1", 5, "Invalid integer value '-1'");
        ExpectRefused<uint64_t>("-1", 5, "Invalid integer value '-1'");
        ExpectRefused<int64_t>("9223372036854775808", 5, "Invalid integer value '9223372036854775808' (out of range)");
        ExpectRefused<uint64_t>("18446744073709551616", 5, "Invalid integer value '1.8446744073709552e+19'");
    }

    TEST(JsonDeserializerTest, AFloatIsAFiniteJsonNumber) {
        ExpectRefused<float>("\"1.5\"", 2.0f, "Invalid number value '1.5'");
        ExpectRefused<float>("true", 2.0f, "Invalid number value 'true'");
        ExpectRefused<float>("null", 2.0f, "Invalid number value 'null'");
        ExpectRefused<float>("1e39", 2.0f, "Invalid number value '1e+39' (out of range)");
        ExpectRefused<double>("\"x\"", 2.0, "Invalid number value 'x'");
        ExpectRefused<double>("[1]", 2.0, "Invalid number value '<array>'");
    }

    TEST(JsonDeserializerTest, ANonFiniteNumberOfABuiltDocumentIsRefused) {
        Json document = Json::object();
        document["k"] = std::numeric_limits<double>::infinity();
        JsonDeserializer reader(document);
        double value = 2.0;

        EXPECT_FALSE(reader.ReadAttribute("k", value));

        EXPECT_EQ(ErrorsText(reader), "k: Invalid number value 'null' (out of range)\n");
        EXPECT_EQ(value, 2.0);
    }

    TEST(JsonDeserializerTest, AStringIsOnlyAJsonString) {
        ExpectRefused<String>("5", String("old"), "Invalid string value '5'");
        ExpectRefused<String>("null", String("old"), "Invalid string value 'null'");
        ExpectRefused<String>("{}", String("old"), "Invalid string value '<object>'");
        ExpectRefused<String>("false", String("old"), "Invalid string value 'false'");
        ExpectRefused<RegMode>("1", RegMode::Auto, "Invalid string value '1'");
        ExpectRefused<RegMode>("\"Bogus\"", RegMode::Auto, "Invalid enum value: 'Bogus'");
    }

    TEST(JsonDeserializerTest, APoolStringIsReadAsAString) {
        PoolString value;
        String errors;

        EXPECT_TRUE(LoadK("\"Hero\"", value, errors));
        EXPECT_EQ(value.ToStringView(), "Hero");
        EXPECT_FALSE(LoadK("12", value, errors));
        EXPECT_EQ(errors, "k: Invalid string value '12'\n");
        EXPECT_EQ(value.ToStringView(), "Hero");
    }

    TEST(JsonDeserializerTest, ARefusedValueNamesTheValueCutAndWithoutControlCharacters) {
        const String longText(500, 'x');
        int32_t value = 0;
        String errors;

        EXPECT_FALSE(LoadK(Format("\"{}\"", longText), value, errors));
        EXPECT_TRUE(Contains(errors, Format("Invalid integer value '{}...'", String(32, 'x')))) << errors.c_str();
        EXPECT_LT(errors.size(), 100u);

        EXPECT_FALSE(LoadK("\"a\\nb\\tc\\u0001\"", value, errors));
        EXPECT_EQ(errors, "k: Invalid integer value 'a?b?c?'\n");
    }

    TEST(JsonDeserializerTest, NullWhereAValueIsExpectedIsRefused) {
        ProbeState state;
        const Json document = Parse(R"({"flag":null,"i32":null,"text":null,"leaf":null,"leaves":null,"mode":null})");
        JsonDeserializer reader(document);

        state.Deserialize(reader);

        const String errors = ErrorsText(reader);
        EXPECT_TRUE(Contains(errors, "flag: Invalid boolean value 'null'")) << errors.c_str();
        EXPECT_TRUE(Contains(errors, "i32: Invalid integer value 'null'")) << errors.c_str();
        EXPECT_TRUE(Contains(errors, "text: Invalid string value 'null'")) << errors.c_str();
        EXPECT_TRUE(Contains(errors, "leaf: Invalid object value 'null'")) << errors.c_str();
        EXPECT_TRUE(Contains(errors, "leaves: Invalid array value 'null'")) << errors.c_str();
        EXPECT_TRUE(Contains(errors, "mode: Invalid string value 'null'")) << errors.c_str();
        EXPECT_EQ(Fuzz::DigestProbe(state), Fuzz::DigestProbe(ProbeState{}));
    }

    TEST(JsonDeserializerTest, AnObjectOrArrayOfTheWrongKindIsRefusedUnderItsName) {
        Optional<int32_t> maybe;
        Variant<int32_t, String> choice;
        String errors;

        EXPECT_FALSE(LoadK("[1]", choice, errors));
        EXPECT_EQ(errors, "k: Invalid object value '<array>'\n");
        EXPECT_FALSE(LoadK("null", maybe, errors));
        EXPECT_EQ(errors, "k: Invalid integer value 'null'\n");
        EXPECT_FALSE(maybe.has_value());

        Vector<int32_t> items{1};
        EXPECT_FALSE(LoadK("{}", items, errors));
        EXPECT_EQ(errors, "k: Invalid array value '<object>'\n");
        EXPECT_EQ(items, (Vector<int32_t>{1}));
    }

    // -------------------------------------------------------------------------
    // Paths of errors
    // -------------------------------------------------------------------------

    TEST(JsonDeserializerTest, AnErrorNamesTheFieldPathWithElementPositions) {
        ProbeState state;
        const Json document = Parse(R"({"leaves":[{"id":1},{"id":"x"},{"label":5}],"numbers":[1,"z"],"leaf":{"weight":true},"tree":{"children":[{"id":false}]}})");
        JsonDeserializer reader(document);

        state.Deserialize(reader);

        EXPECT_EQ(ErrorsText(reader), "leaf.weight: Invalid number value 'true'\n"
                                      "leaves[1].id: Invalid integer value 'x'\n"
                                      "leaves[2].label: Invalid string value '5'\n"
                                      "numbers[1].value: Invalid integer value 'z'\n"
                                      "tree.children[0].id: Invalid integer value 'false'\n");
        EXPECT_EQ(state.leaves.size(), 3u);
        EXPECT_EQ(state.leaves[0].id, 1);
        EXPECT_EQ(state.numbers, (Vector<int32_t>{1, 0}));
    }

    // -------------------------------------------------------------------------
    // Containers
    // -------------------------------------------------------------------------

    TEST(JsonDeserializerTest, AVariantWithABadOrMissingIndexKeepsItsValue) {
        Variant<int32_t, String> choice = String("old");
        String errors;

        EXPECT_FALSE(LoadK(R"({"index":5,"value":1})", choice, errors));
        EXPECT_EQ(errors, "k.index: Invalid variant index 5\n");
        EXPECT_FALSE(LoadK(R"({"index":-1,"value":1})", choice, errors));
        EXPECT_EQ(errors, "k.index: Invalid variant index -1\n");
        EXPECT_FALSE(LoadK(R"({"value":1})", choice, errors));
        EXPECT_EQ(errors, "k.index: Variant index is missing\n");
        EXPECT_FALSE(LoadK(R"({"index":"1","value":"s"})", choice, errors));
        EXPECT_EQ(errors, "k.index: Invalid integer value '1'\n");
        EXPECT_EQ(std::get<String>(choice), "old");

        EXPECT_TRUE(LoadK(R"({"index":0,"value":4})", choice, errors));
        EXPECT_EQ(std::get<int32_t>(choice), 4);
        EXPECT_TRUE(LoadK(R"({"index":1,"value":"s"})", choice, errors));
        EXPECT_EQ(std::get<String>(choice), "s");
        EXPECT_TRUE(errors.empty());
    }

    TEST(JsonDeserializerTest, AMapReadsItsEntriesAndAKeyOfAnotherKindIsAnError) {
        HashMap<String, int32_t> table;
        Map<int32_t, String> ordered;
        String errors;

        EXPECT_TRUE(LoadK(R"([{"key":"a","value":1},{"key":"a","value":2},{"key":"b","value":3}])", table, errors));
        EXPECT_TRUE(errors.empty()) << errors.c_str();
        EXPECT_EQ(table.size(), 2u);
        EXPECT_EQ(table["a"], 1);
        EXPECT_EQ(table["b"], 3);

        EXPECT_TRUE(LoadK(R"([{"key":2,"value":"x"},{"key":"one","value":"y"}])", ordered, errors));
        EXPECT_EQ(errors, "k[1].key: Invalid integer value 'one'\n");
        EXPECT_EQ(ordered[2], "x");
        EXPECT_EQ(ordered[0], "y");
    }

    TEST(JsonDeserializerTest, AFixedContainerReportsWhatItCouldNotKeep) {
        FixedVector<int32_t, 2> items;
        String errors;

        EXPECT_TRUE(LoadK("[1,2,3]", items, errors));
        EXPECT_EQ(items.size(), 2u);
        EXPECT_EQ(errors, "k: Data lost, the fixed container is full: kept 2 of 3 items\n");
    }

    TEST(JsonDeserializerTest, AnUnknownOrDisabledFactoryElementIsReportedOrDropped) {
        NoHookProbeList list;
        const Json document = Parse(R"({"items":[{"type":"Nope"},{"type":"NoHookProbeImpl","enabled":false,"value":1},{"type":"NoHookProbeImpl","value":2}]})");
        JsonDeserializer reader(document);
        reader.SetDiagnostics(DiagnosticsMode::Warn);

        list.Deserialize(reader);
        reader.FinishLoad();

        EXPECT_EQ(ErrorsText(reader), "items[0].type: Unknown factory type: 'Nope'\n");
        EXPECT_TRUE(reader.GetWarnings().empty()) << WarningsText(reader).c_str();
        ASSERT_EQ(list.items.size(), 1u);
        EXPECT_EQ(list.items[0]->GetValue(), 2);
    }

    // -------------------------------------------------------------------------
    // Unknown keys
    // -------------------------------------------------------------------------

    TEST(JsonDeserializerTest, AnUnknownKeyIsAFindingWithAHintForANearName) {
        ProbeState state;
        const Json document = Parse(R"({"flg":true,"i32":3,"bogus":1,"leaf":{"idd":1,"id":2},"leaves":[{"id":1,"extra":2}],"numbers":[1]})");
        JsonDeserializer reader(document);
        reader.SetDiagnostics(DiagnosticsMode::Warn);

        state.Deserialize(reader);
        reader.FinishLoad();

        EXPECT_FALSE(reader.HasErrors()) << ErrorsText(reader).c_str();
        const String warnings = WarningsText(reader);
        EXPECT_TRUE(Contains(warnings, "flg: Unknown key 'flg', did you mean 'flag'?\n")) << warnings.c_str();
        EXPECT_TRUE(Contains(warnings, "bogus: Unknown key 'bogus'\n")) << warnings.c_str();
        EXPECT_TRUE(Contains(warnings, "leaf.idd: Unknown key 'idd', did you mean 'id'?\n")) << warnings.c_str();
        EXPECT_TRUE(Contains(warnings, "leaves[0].extra: Unknown key 'extra'\n")) << warnings.c_str();
        EXPECT_EQ(reader.GetWarnings().size(), 4u) << warnings.c_str();
        EXPECT_EQ(state.i32, 3);
    }

    TEST(JsonDeserializerTest, UnknownKeysAreErrorsUnderErrorAndNothingUnderOff) {
        const Json document = Parse(R"({"flg":true})");

        ProbeState underError;
        JsonDeserializer strict(document);
        strict.SetDiagnostics(DiagnosticsMode::Error);
        underError.Deserialize(strict);
        strict.FinishLoad();
        EXPECT_EQ(ErrorsText(strict), "flg: Unknown key 'flg', did you mean 'flag'?\n");

        ProbeState underOff;
        JsonDeserializer quiet(document);
        underOff.Deserialize(quiet);
        quiet.FinishLoad();
        EXPECT_FALSE(quiet.HasErrors());
        EXPECT_TRUE(quiet.GetWarnings().empty());
    }

    TEST(JsonDeserializerTest, AKeyTheCallerReadsByHandOrSkipsIsNotUnknown) {
        const Json document = Parse(R"({"mine":1,"other":2})");
        JsonDeserializer reader(document);
        reader.SetDiagnostics(DiagnosticsMode::Warn);

        reader.NoteKnownKey("mine");
        reader.FinishLoad();
        EXPECT_EQ(WarningsText(reader), "other: Unknown key 'other'\n");

        const Json other = Parse(R"({"a":{"x":1}})");
        JsonDeserializer skipping(other);
        skipping.SetDiagnostics(DiagnosticsMode::Warn);
        ASSERT_TRUE(skipping.BeginObject("a"));
        skipping.SkipScope();
        skipping.EndObject();
        skipping.FinishLoad();
        EXPECT_TRUE(skipping.GetWarnings().empty()) << WarningsText(skipping).c_str();
    }

    TEST(JsonDeserializerTest, ALongUnknownKeyIsCutAndCleanedInTheFinding) {
        const Json document = Parse(Format("{{\"{}\\n\":1}}", String(300, 'k')));
        JsonDeserializer reader(document);
        reader.SetDiagnostics(DiagnosticsMode::Warn);

        reader.FinishLoad();

        ASSERT_EQ(reader.GetWarnings().size(), 1u);
        const StringView message = reader.GetWarnings()[0].errorMessage;
        EXPECT_LT(message.size(), 100u);
        EXPECT_EQ(message.find('\n'), StringView::npos);
    }

    // -------------------------------------------------------------------------
    // Limits and hostile documents
    // -------------------------------------------------------------------------

    TEST(JsonDeserializerTest, AnArrayOverTheLimitIsRefused) {
        Json items = Json::array();
        for (size_t i = 0; i <= ReaderLimits::kMaxArrayElements; ++i) {
            items.push_back(1);
        }
        Json document = Json::object();
        document["numbers"] = std::move(items);
        ProbeState state;
        state.numbers = {9};
        JsonDeserializer reader(document);

        state.Deserialize(reader);

        EXPECT_EQ(ErrorsText(reader), "numbers: Array has more elements than the limit\n");
        EXPECT_EQ(state.numbers, (Vector<int32_t>{9}));
    }

    TEST(JsonDeserializerTest, NestingDeeperThanTheLimitIsRefusedAndShallowIsRead) {
        const Json deep = MakeDeepTree(300);
        ProbeNode tooDeep;
        JsonDeserializer refusing(deep);
        tooDeep.Deserialize(refusing);
        EXPECT_TRUE(Contains(ErrorsText(refusing), "Nesting is deeper than the limit")) << ErrorsText(refusing).c_str();

        const Json shallow = MakeDeepTree(50);
        ProbeNode fine;
        JsonDeserializer reading(shallow);
        fine.Deserialize(reading);
        EXPECT_FALSE(reading.HasErrors()) << ErrorsText(reading).c_str();
        const ProbeNode* node = &fine;
        size_t depth = 0;
        while (!node->children.empty()) {
            node = &node->children[0];
            ++depth;
        }
        EXPECT_EQ(depth, 50u);
    }

    TEST(JsonDeserializerTest, TheListOfErrorsIsCapped) {
        Json items = Json::array();
        for (size_t i = 0; i < ReaderLimits::kMaxRecordedErrors * 3; ++i) {
            items.push_back("x");
        }
        Json document = Json::object();
        document["numbers"] = std::move(items);
        ProbeState state;
        JsonDeserializer reader(document);

        state.Deserialize(reader);

        ASSERT_EQ(reader.GetErrors().size(), ReaderLimits::kMaxRecordedErrors);
        EXPECT_TRUE(Contains(ErrorsText(reader), "Too many errors"));
    }

    TEST(JsonDeserializerTest, ADocumentThatIsNotAnObjectMakesEveryReadAnError) {
        for (const StringView text : {"[1]", "5", "null", "\"s\"", "true"}) {
            const Json document = Parse(text);
            JsonDeserializer reader(document);
            int32_t number = 4;
            ProbeState state;

            EXPECT_FALSE(reader.ReadAttribute("i32", number)) << String(text).c_str();
            EXPECT_FALSE(reader.BeginObject("leaf"));
            size_t count = 0;
            EXPECT_FALSE(reader.BeginArray("leaves", "item", count));
            EXPECT_FALSE(reader.BeginArrayElement());
            state.Deserialize(reader);
            reader.FinishLoad();

            EXPECT_EQ(number, 4);
            EXPECT_TRUE(reader.HasErrors()) << String(text).c_str();
            EXPECT_TRUE(Contains(ErrorsText(reader), "i32: Document root is not an object")) << ErrorsText(reader).c_str();
        }
    }

    TEST(JsonDeserializerTest, CallsOutOfOrderAreIgnored) {
        const Json document = Parse(R"({"list":[1]})");
        JsonDeserializer reader(document);

        reader.EndObject();
        reader.EndArray();
        reader.EndArrayElement();
        EXPECT_FALSE(reader.BeginArrayElement());
        size_t count = 0;
        ASSERT_TRUE(reader.BeginArray("list", "item", count));
        EXPECT_EQ(count, 1u);
        reader.EndObject();
        reader.EndArrayElement();
        ASSERT_TRUE(reader.BeginArrayElement());
        EXPECT_FALSE(reader.BeginArrayElement());
        int32_t value = 0;
        EXPECT_TRUE(reader.ReadAttribute("value", value));
        EXPECT_EQ(value, 1);
        EXPECT_FALSE(reader.ReadAttribute("other", value));
        reader.EndArrayElement();
        EXPECT_FALSE(reader.BeginArrayElement());
        reader.EndArray();
        EXPECT_FALSE(reader.HasErrors()) << ErrorsText(reader).c_str();
    }

    TEST(JsonDeserializerTest, AControlCharacterOfTheDocumentIsReplacedInAnErrorMessage) {
        const Json document = Parse(R"({"mode":"Aus\u007f\u001b:[]}to"})");
        JsonDeserializer reader(document);
        DiagnosticsMode mode = DiagnosticsMode::Warn;
        EXPECT_FALSE(Ser::Load(reader, mode, "mode"));
        ASSERT_EQ(reader.GetErrors().size(), 1u);
        EXPECT_EQ(reader.GetErrors()[0].errorMessage, "Invalid enum value: 'Aus??:[]}to'");
    }

    TEST(JsonDeserializerTest, APoolStringOverTheCapIsRefusedAndAStringIsNot) {
        const String overCap(ReaderLimits::kMaxPoolStringBytes + 1, 'j');
        const Json document = Parse(Format("{{\"k\":\"{}\"}}", overCap));
        JsonDeserializer reader(document);

        const size_t before = PoolString::GetStats().stringCount;
        PoolString pooled;
        EXPECT_FALSE(reader.ReadAttribute("k", pooled));
        EXPECT_EQ(PoolString::GetStats().stringCount, before);
        EXPECT_TRUE(pooled.ToStringView().empty());
        ASSERT_EQ(reader.GetErrors().size(), 1u);
        EXPECT_NE(reader.GetErrors()[0].errorMessage.find("kMaxPoolStringBytes"), String::npos);

        String plain;
        EXPECT_TRUE(reader.ReadAttribute("k", plain));
        EXPECT_EQ(plain, overCap);
    }

    TEST(JsonDeserializerTest, ALookupNeverInterns) {
        constexpr StringView kNeverInterned = "json_deserializer_never_interned_lookup";
        const Json document = Parse(R"({"a":1})");
        JsonDeserializer reader(document);
        int32_t value = 0;

        EXPECT_FALSE(reader.ReadAttribute(kNeverInterned, value));
        EXPECT_FALSE(reader.HasKey(kNeverInterned));
        EXPECT_FALSE(reader.BeginObject(kNeverInterned));

        EXPECT_FALSE(PoolString::Find(kNeverInterned).has_value());
    }

}  // namespace BECore::Tests
