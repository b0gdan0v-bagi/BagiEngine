// JsonDeserializer::Load over text and over a tape (Control/JsonDeserializer.h): the document is never a Json here. JsonDeserializerTest.cpp pins the
// rules of the values and the messages through the bridge; this file pins what only the tape reader promises: any order of keys, escapes in keys
// and strings, a reader that serves many documents, the findings in the document's order and that a second load of a message allocates nothing.

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <BECore/FactoryInitializeTest.h>
#include <BECore/FuzzProbe.h>
#include <BECore/Reflection/ReaderLimits.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Common/TestText.h>
#include <Fuzz/FuzzProbeAccess.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using FuzzProbeData::ProbeLeaf;
    using FuzzProbeData::ProbeNode;
    using FuzzProbeData::ProbeShapes;
    using FuzzProbeData::ProbeState;

    namespace {

        /// "path: message" of every error or finding, one per line
        String Lines(const Vector<DeserializeError>& findings) {
            String text;
            for (const DeserializeError& finding : findings) {
                AppendFormat(text, "{}: {}\n", finding.path, finding.errorMessage);
            }
            return text;
        }

        template <typename T>
        String ObjectText(const T& object) {
            JsonSerializer serializer;
            object.Serialize(serializer);
            return DumpJson(serializer.Take());
        }

        /// The members of every object written in the order @p reverse and @p rotate give: the document means the same, its keys come in another order
        void AppendReordered(String& out, const Json& node, bool reverse, size_t rotate) {
            if (node.is_array()) {
                out += '[';
                bool first = true;
                for (const Json& item : node) {
                    out += first ? "" : ",";
                    first = false;
                    AppendReordered(out, item, reverse, rotate);
                }
                out += ']';
            } else if (node.is_object()) {
                Vector<Pair<String, const Json*>> members;
                for (const auto& member : node.items()) {
                    members.push_back({String(member.key().c_str()), &member.value()});
                }
                out += '{';
                bool first = true;
                for (size_t position = 0; position < members.size(); ++position) {
                    const size_t shifted = (position + rotate) % members.size();
                    const auto& member = members[reverse ? members.size() - 1 - shifted : shifted];
                    out += first ? "" : ",";
                    first = false;
                    out += DumpJson(Json(member.first.c_str()));
                    out += ':';
                    AppendReordered(out, *member.second, reverse, rotate);
                }
                out += '}';
            } else {
                out += DumpJson(node);
            }
        }

        String Reordered(StringView text, bool reverse, size_t rotate) {
            auto parsed = ParseJson(text);
            EXPECT_TRUE(parsed.has_value());
            String out;
            if (parsed) {
                AppendReordered(out, *parsed, reverse, rotate);
            }
            return out;
        }

        /// A tree of ProbeNode of @p levels levels below the root; the innermost node has an empty list, so the nesting is 2 * levels + 2
        String MakeTreeText(size_t levels) {
            String text;
            for (size_t level = 0; level < levels; ++level) {
                text += R"({"id":1,"children":[)";
            }
            text += R"({"id":1,"children":[]})";
            for (size_t level = 0; level < levels; ++level) {
                text += "]}";
            }
            return text;
        }

        constexpr size_t kLevelsAtTheNestingLimit = (ReaderLimits::kMaxJsonNesting - 2) / 2;

    }  // namespace

    TEST(JsonDeserializerTextTest, TheOutputOfTheSerializerAsTextSurvivesTheRoundTrip) {
        const ProbeState state = Fuzz::MakeFilledProbe();
        const String text = ObjectText(state);

        ProbeState loaded;
        JsonDeserializer reader;
        ASSERT_TRUE(reader.Load(text));
        loaded.Deserialize(reader);

        EXPECT_FALSE(reader.HasErrors()) << Lines(reader.GetErrors()).c_str();
        EXPECT_EQ(Fuzz::DigestProbe(loaded), Fuzz::DigestProbe(state));
    }

    TEST(JsonDeserializerTextTest, TheOrderOfKeysDoesNotMatterAtAnyDepth) {
        const ProbeShapes shapes = Fuzz::MakeFilledShapes();
        const String written = ObjectText(shapes);

        for (const bool reverse : {false, true}) {
            for (const size_t rotate : {size_t{0}, size_t{1}, size_t{3}}) {
                const String text = Reordered(written, reverse, rotate);
                ProbeShapes loaded;
                JsonDeserializer reader;
                reader.SetDiagnostics(DiagnosticsMode::Warn);
                ASSERT_TRUE(reader.Load(text)) << text.c_str();
                loaded.Deserialize(reader);
                reader.FinishLoad();

                EXPECT_FALSE(reader.HasErrors()) << text.c_str() << "\n" << Lines(reader.GetErrors()).c_str();
                EXPECT_TRUE(reader.GetWarnings().empty()) << text.c_str() << "\n" << Lines(reader.GetWarnings()).c_str();
                EXPECT_EQ(Fuzz::DigestProbe(loaded), Fuzz::DigestProbe(shapes)) << text.c_str();
            }
        }
    }

    TEST(JsonDeserializerTextTest, AFactoryTypeAndAVariantIndexMayFollowTheFields) {
        NoHookProbeList list;
        ProbeShapes shapes;
        JsonDeserializer reader;
        ASSERT_TRUE(reader.Load(R"({"items":[{"value":5,"type":"NoHookProbeImpl"}],"choice":{"value":"s","index":2}})"));

        list.Deserialize(reader);
        shapes.Deserialize(reader);

        EXPECT_FALSE(reader.HasErrors()) << Lines(reader.GetErrors()).c_str();
        ASSERT_EQ(list.items.size(), 1u);
        EXPECT_EQ(list.items[0]->GetValue(), 5);
        EXPECT_EQ(std::get<String>(shapes.choice), "s");
    }

    TEST(JsonDeserializerTextTest, AKeyWrittenWithAnEscapeIsFoundAndIsNotUnknown) {
        ProbeState state;
        JsonDeserializer reader;
        reader.SetDiagnostics(DiagnosticsMode::Warn);
        ASSERT_TRUE(reader.Load(R"({"flag":true,"i32":7})"));

        state.Deserialize(reader);
        reader.FinishLoad();

        EXPECT_TRUE(state.flag);
        EXPECT_EQ(state.i32, 7);
        EXPECT_TRUE(reader.GetWarnings().empty()) << Lines(reader.GetWarnings()).c_str();
    }

    TEST(JsonDeserializerTextTest, AnEscapedStringIsDecodedIntoAStringAndAPoolString) {
        ProbeState state;
        JsonDeserializer reader;
        ASSERT_TRUE(reader.Load(R"({"text":"a\"b\\c\/\né😀z","name":"xAy"})"));

        state.Deserialize(reader);

        EXPECT_FALSE(reader.HasErrors()) << Lines(reader.GetErrors()).c_str();
        EXPECT_EQ(state.text, "a\"b\\c/\n\xC3\xA9\xF0\x9F\x98\x80z");
        EXPECT_EQ(state.name.ToStringView(), "xAy");
    }

    TEST(JsonDeserializerTextTest, APoolStringOverTheCapIsRefusedWithAndWithoutEscapes) {
        const String plain(ReaderLimits::kMaxPoolStringBytes + 1, 'j');
        const String escaped = "\\u006a" + String(ReaderLimits::kMaxPoolStringBytes, 'j');
        const String atTheCap(ReaderLimits::kMaxPoolStringBytes, 'j');

        for (const String* over : {&plain, &escaped}) {
            const String text = Format("{{\"name\":\"{}\",\"text\":\"{}\"}}", *over, *over);
            JsonDeserializer reader;
            ASSERT_TRUE(reader.Load(text));
            const size_t before = PoolString::GetStats().stringCount;

            ProbeState state;
            state.Deserialize(reader);

            EXPECT_EQ(PoolString::GetStats().stringCount, before);
            EXPECT_TRUE(state.name.ToStringView().empty());
            ASSERT_EQ(reader.GetErrors().size(), 1u) << Lines(reader.GetErrors()).c_str();
            EXPECT_EQ(reader.GetErrors()[0].path, "name");
            EXPECT_EQ(state.text.size(), ReaderLimits::kMaxPoolStringBytes + 1) << "a String has no such cap";
        }

        const String atTheCapText = Format("{{\"name\":\"{}\"}}", atTheCap);
        JsonDeserializer reader;
        ASSERT_TRUE(reader.Load(atTheCapText));
        ProbeState state;
        state.Deserialize(reader);
        EXPECT_FALSE(reader.HasErrors()) << Lines(reader.GetErrors()).c_str();
        EXPECT_EQ(state.name.ToStringView(), atTheCap);
    }

    TEST(JsonDeserializerTextTest, ABadTextIsOneErrorAndLaterReadsFailThenAGoodTextLoadsClean) {
        JsonDeserializer reader;
        ProbeState state;

        EXPECT_FALSE(reader.Load(R"({"flag":tru})"));
        ASSERT_EQ(reader.GetErrors().size(), 1u);
        EXPECT_EQ(reader.GetErrors()[0].path, "");
        EXPECT_EQ(reader.GetErrors()[0].errorMessage, "Invalid JSON: Invalid literal at offset 8") << "the offset of the first byte the grammar refused";
        state.Deserialize(reader);
        EXPECT_FALSE(state.flag);
        EXPECT_GT(reader.GetErrors().size(), 1u) << "every read of a document that did not load is an error";

        ASSERT_TRUE(reader.Load(R"({"flag":true})"));
        EXPECT_FALSE(reader.HasErrors()) << "Load clears the errors of the earlier document";
        state.Deserialize(reader);
        EXPECT_TRUE(state.flag);
        EXPECT_FALSE(reader.HasErrors()) << Lines(reader.GetErrors()).c_str();
    }

    TEST(JsonDeserializerTextTest, AReaderKeepsItsModeAndLosesTheFindingsOfTheLastDocument) {
        JsonDeserializer reader;
        reader.SetDiagnostics(DiagnosticsMode::Warn);
        ProbeState state;

        ASSERT_TRUE(reader.Load(R"({"first":1})"));
        state.Deserialize(reader);
        reader.FinishLoad();
        EXPECT_EQ(Lines(reader.GetWarnings()), "first: Unknown key 'first'\n");

        ASSERT_TRUE(reader.Load(R"({"second":1})"));
        state.Deserialize(reader);
        reader.FinishLoad();
        EXPECT_EQ(Lines(reader.GetWarnings()), "second: Unknown key 'second'\n") << "Load clears the findings of the first document and keeps the mode";
    }

    TEST(JsonDeserializerTextTest, ATapeTheCallerParsedServesManyReaders) {
        const String text = ObjectText(Fuzz::MakeFilledProbe());
        JsonTape tape;
        ASSERT_TRUE(tape.Parse(text).has_value());

        uint64_t digests[2] = {};
        for (uint64_t& digest : digests) {
            JsonDeserializer reader;
            ASSERT_TRUE(reader.Load(tape));
            ProbeState state;
            state.Deserialize(reader);
            EXPECT_FALSE(reader.HasErrors()) << Lines(reader.GetErrors()).c_str();
            digest = Fuzz::DigestProbe(state);
        }

        EXPECT_EQ(digests[0], Fuzz::DigestProbe(Fuzz::MakeFilledProbe()));
        EXPECT_EQ(digests[0], digests[1]);
    }

    TEST(JsonDeserializerTextTest, AnErrorNamesThePathOfTheFieldOverText) {
        ProbeState state;
        JsonDeserializer reader;
        ASSERT_TRUE(reader.Load(R"({"leaves":[{"id":1},{},{"label":5}],"numbers":[1,"z"],"leaf":{"weight":true}})"));

        state.Deserialize(reader);

        EXPECT_EQ(Lines(reader.GetErrors()), "leaf.weight: Invalid number value 'true'\n"
                                             "leaves[2].label: Invalid string value '5'\n"
                                             "numbers[1].value: Invalid integer value 'z'\n");
        EXPECT_EQ(state.leaves.size(), 3u);
    }

    TEST(JsonDeserializerTextTest, TheFindingsComeInTheOrderOfTheDocumentWithAHintForANearName) {
        const StringView text = R"({"zeta":1,"flg":true,"alpha":2,"leaf":{"idd":1}})";
        // A scope reports when it ends, so the nested object's finding comes before the root's, which come in the document's order
        const StringView expected = "leaf.idd: Unknown key 'idd', did you mean 'id'?\n"
                                    "zeta: Unknown key 'zeta'\n"
                                    "flg: Unknown key 'flg', did you mean 'flag'?\n"
                                    "alpha: Unknown key 'alpha'\n";

        for (const DiagnosticsMode mode : {DiagnosticsMode::Warn, DiagnosticsMode::Error}) {
            ProbeState state;
            JsonDeserializer reader;
            reader.SetDiagnostics(mode);
            ASSERT_TRUE(reader.Load(text));

            state.Deserialize(reader);
            reader.FinishLoad();

            const Vector<DeserializeError>& findings = mode == DiagnosticsMode::Warn ? reader.GetWarnings() : reader.GetErrors();
            EXPECT_EQ(Lines(findings), String(expected));
        }
    }

    TEST(JsonDeserializerTextTest, ScalarElementsAndObjectElementsAreRead) {
        ProbeState state;
        JsonDeserializer reader;
        ASSERT_TRUE(reader.Load(R"({"numbers":[1,2,3],"words":["a",{"value":"b"}],"leaves":[{"id":4},{"id":5,"label":"x"}]})"));

        state.Deserialize(reader);

        EXPECT_FALSE(reader.HasErrors()) << Lines(reader.GetErrors()).c_str();
        EXPECT_EQ(state.numbers, (Vector<int32_t>{1, 2, 3}));
        EXPECT_EQ(state.words, (Vector<String>{"a", "b"}));
        ASSERT_EQ(state.leaves.size(), 2u);
        EXPECT_EQ(state.leaves[1].label, "x");
    }

    TEST(JsonDeserializerTextTest, OfTwoEqualKeysTheFirstIsRead) {
        ProbeState state;
        JsonDeserializer reader;
        ASSERT_TRUE(reader.Load(R"({"i32":1,"i32":2,"leaf":{"id":3},"leaf":{"id":4}})"));

        state.Deserialize(reader);

        EXPECT_EQ(state.i32, 1);
        EXPECT_EQ(state.leaf.id, 3);
    }

    TEST(JsonDeserializerTextTest, ANestAtTheLimitIsReadAndOneLevelDeeperIsNoDocument) {
        const String atTheLimit = MakeTreeText(kLevelsAtTheNestingLimit);
        ProbeNode root;
        JsonDeserializer reader;
        ASSERT_TRUE(reader.Load(atTheLimit));
        root.Deserialize(reader);
        EXPECT_FALSE(reader.HasErrors()) << Lines(reader.GetErrors()).c_str();
        size_t depth = 0;
        for (const ProbeNode* node = &root; !node->children.empty(); node = &node->children[0]) {
            ++depth;
        }
        EXPECT_EQ(depth, kLevelsAtTheNestingLimit);

        const String tooDeep = MakeTreeText(kLevelsAtTheNestingLimit + 1);
        EXPECT_FALSE(reader.Load(tooDeep));
        ASSERT_EQ(reader.GetErrors().size(), 1u);
        EXPECT_TRUE(Contains(reader.GetErrors()[0].errorMessage, "Invalid JSON: ")) << reader.GetErrors()[0].errorMessage.c_str();
    }

    // -------------------------------------------------------------------------
    // Allocations: a reader that has read a message of a shape reads the next one of that shape without allocating
    // -------------------------------------------------------------------------

    namespace {

        /// Every kind of field a message has, with names and strings short enough for a string's inline buffer
        constexpr StringView kMessage = R"({"flag":true,"i8":-3,"u8":200,"i16":-300,"u16":60000,"i32":-70000,"u32":4000000000,"i64":-5000000000,"u64":18000000000000000000,)"
                                        R"("f32":1.5,"f64":0.25,"text":"hello","name":"pooled_name","mode":"On","leaf":{"id":9,"weight":2.5,"label":"in"},)"
                                        R"("leaves":[{"id":1,"label":"a"},{"id":2,"label":"b"}],"numbers":[1,2,3],"words":["x","yy"],)"
                                        R"("tree":{"id":3,"children":[{"id":4,"children":[]}]}})";

        constexpr StringView kShorterMessage = R"({"text":"hi","leaves":[{"id":7,"label":"c"}],"numbers":[5]})";

        void ExpectSecondLoadAllocatesNothing(StringView first, StringView second, DiagnosticsMode mode) {
            ProbeState state;
            JsonDeserializer reader;
            reader.SetDiagnostics(mode);
            ASSERT_TRUE(reader.Load(first));
            state.Deserialize(reader);
            reader.FinishLoad();
            ASSERT_FALSE(reader.HasErrors()) << Lines(reader.GetErrors()).c_str();
            ASSERT_TRUE(reader.GetWarnings().empty()) << Lines(reader.GetWarnings()).c_str();

            bool loaded = false;
            ExpectNoAllocations([&] {
                loaded = reader.Load(second);
                state.Deserialize(reader);
                reader.FinishLoad();
            });

            EXPECT_TRUE(loaded);
            EXPECT_FALSE(reader.HasErrors());
            EXPECT_TRUE(reader.GetWarnings().empty());
        }

    }  // namespace

    TEST(JsonDeserializerTextTest, TheSecondLoadOfAMessageOfTheSameShapeAllocatesNothingInEveryMode) {
        for (const DiagnosticsMode mode : {DiagnosticsMode::Off, DiagnosticsMode::Warn, DiagnosticsMode::Error}) {
            ExpectSecondLoadAllocatesNothing(kMessage, kMessage, mode);
        }
    }

    TEST(JsonDeserializerTextTest, TheSecondLoadOfAShorterMessageAllocatesNothing) {
        for (const DiagnosticsMode mode : {DiagnosticsMode::Off, DiagnosticsMode::Warn}) {
            ExpectSecondLoadAllocatesNothing(kMessage, kShorterMessage, mode);
        }
    }

    // -------------------------------------------------------------------------
    // Load(JsonView): the root is a value inside a tape the caller keeps
    // -------------------------------------------------------------------------

    namespace {

        constexpr StringView kEnvelope = R"({"id":1,"params":{"id":9,"weight":2.5,"label":"in"},"other":[1,2]})";

        /// The leaf at "params" of @p tape, read by the view of that member; the errors as lines
        String ReadLeafThroughView(JsonDeserializer& reader, const JsonTape& tape, ProbeLeaf& leaf) {
            const Optional<JsonView> params = JsonView(tape, 0).Find("params");
            if (!params || !reader.Load(*params)) {
                return "no params";
            }
            leaf.Deserialize(reader);
            reader.FinishLoad();
            return Lines(reader.GetErrors());
        }

    }  // namespace

    TEST(JsonDeserializerTextTest, ALoadOfANestedViewReadsWhatALoadOfItsTextReads) {
        JsonTape tape;
        ASSERT_TRUE(tape.Parse(kEnvelope));
        JsonDeserializer viewReader;
        ProbeLeaf viaView;
        EXPECT_EQ(ReadLeafThroughView(viewReader, tape, viaView), "");

        JsonDeserializer textReader;
        ProbeLeaf viaText;
        ASSERT_TRUE(textReader.Load(R"({"id":9,"weight":2.5,"label":"in"})"));
        viaText.Deserialize(textReader);

        EXPECT_EQ(viaView.id, 9);
        EXPECT_EQ(viaView.label, "in");
        EXPECT_EQ(viaView.id, viaText.id);
        EXPECT_EQ(viaView.weight, viaText.weight);
        EXPECT_EQ(viaView.label, viaText.label);
    }

    TEST(JsonDeserializerTextTest, ALoadOfAViewDoesNotSeeTheMembersAroundIt) {
        JsonTape tape;
        ASSERT_TRUE(tape.Parse(R"({"id":5,"params":{"label":"x"}})"));
        JsonDeserializer reader;
        ProbeLeaf leaf;
        leaf.id = 77;
        ASSERT_TRUE(reader.Load(*JsonView(tape, 0).Find("params")));
        leaf.Deserialize(reader);

        EXPECT_EQ(leaf.id, 77);
        EXPECT_EQ(leaf.label, "x");
    }

    TEST(JsonDeserializerTextTest, ALoadOfAViewThatIsNotAnObjectOrNotValidFailsEveryRead) {
        JsonTape tape;
        ASSERT_TRUE(tape.Parse(R"({"list":[1,2],"id":4})"));
        JsonDeserializer reader;

        ProbeLeaf leaf;
        EXPECT_TRUE(reader.Load(*JsonView(tape, 0).Find("list")));
        leaf.Deserialize(reader);
        EXPECT_TRUE(reader.HasErrors());
        EXPECT_EQ(leaf.id, 0);

        EXPECT_FALSE(reader.Load(JsonView()));
        EXPECT_FALSE(reader.HasErrors());
        leaf.Deserialize(reader);
        EXPECT_TRUE(reader.HasErrors());
    }

    TEST(JsonDeserializerTextTest, ALoadOfAViewForgetsTheErrorsAndFindingsOfTheLastDocument) {
        JsonTape tape;
        ASSERT_TRUE(tape.Parse(R"({"bad":{"id":"x","extra":1},"good":{"id":3}})"));
        JsonDeserializer reader;
        reader.SetDiagnostics(DiagnosticsMode::Warn);
        ProbeLeaf leaf;
        ASSERT_TRUE(reader.Load(*JsonView(tape, 0).Find("bad")));
        leaf.Deserialize(reader);
        reader.FinishLoad();
        ASSERT_TRUE(reader.HasErrors());
        ASSERT_FALSE(reader.GetWarnings().empty());

        ASSERT_TRUE(reader.Load(*JsonView(tape, 0).Find("good")));
        leaf.Deserialize(reader);
        reader.FinishLoad();
        EXPECT_FALSE(reader.HasErrors());
        EXPECT_TRUE(reader.GetWarnings().empty());
        EXPECT_EQ(leaf.id, 3);
    }

    TEST(JsonDeserializerTextTest, TheSecondLoadOfAViewAllocatesNothing) {
        JsonTape tape;
        ASSERT_TRUE(tape.Parse(kEnvelope));
        const JsonView params = *JsonView(tape, 0).Find("params");
        for (const DiagnosticsMode mode : {DiagnosticsMode::Off, DiagnosticsMode::Warn, DiagnosticsMode::Error}) {
            JsonDeserializer reader;
            reader.SetDiagnostics(mode);
            ProbeLeaf leaf;
            ASSERT_TRUE(reader.Load(params));
            leaf.Deserialize(reader);
            reader.FinishLoad();
            ASSERT_FALSE(reader.HasErrors());

            ExpectNoAllocations([&] {
                reader.Load(params);
                leaf.Deserialize(reader);
                reader.FinishLoad();
            });
            EXPECT_FALSE(reader.HasErrors());
            EXPECT_EQ(leaf.id, 9);
        }
    }

}  // namespace BECore::Tests
