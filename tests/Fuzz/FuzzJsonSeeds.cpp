#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Reflection/ReaderLimits.h>
#include <Fuzz/FuzzJsonText.h>
#include <Fuzz/FuzzProbeAccess.h>
#include <Fuzz/FuzzSeeds.h>

namespace BECore::Fuzz {

    namespace {

        constexpr const char* kJsonFolder = "json";

        /// Levels of the chain of nodes: deep for a document, under the depth the target accepts (two levels of JSON for one of the tree)
        constexpr int32_t kChainLevels = 60;

        /// Depth of the nesting seed that the target refuses before it parses
        constexpr size_t kTooDeepLevels = 400;

        /// Levels of a tree whose JSON is exactly as deep as the tape accepts (ReaderLimits::kMaxJsonNesting): deeper than the DOM path takes
        constexpr size_t kLevelsAtTheTapeLimit = (ReaderLimits::kMaxJsonNesting - 2) / 2;

        /// What the old DOM dump wrote: the members saved by `Serialize`, with the keys of every object in byte order
        template <typename T>
        String SavedJson(const T& value) {
            return SortedText(SavedText(value));
        }

        /// @p document (an object) with the members of @p changes replacing the members of the same name, and the ones it has not appended; the values are JSON text
        String WithMembers(StringView document, std::initializer_list<Pair<StringView, StringView>> changes) {
            JsonTape tape;
            FUZZ_CHECK(tape.Parse(document).has_value());
            String text;
            JsonWriter writer(text);
            writer.BeginObject();
            for (const JsonMember member : JsonView(tape, 0).Members()) {
                const StringView key = member.key.Text().value_or(StringView());
                const auto change = eastl::find_if(changes.begin(), changes.end(), [key](const auto& entry) { return entry.first == key; });
                writer.Key(key);
                if (change != changes.end()) {
                    writer.Raw(change->second);
                } else {
                    WriteJson(writer, member.value);
                }
            }
            for (const auto& entry : changes) {
                if (!JsonView(tape, 0).Find(entry.first)) {
                    writer.Key(entry.first);
                    writer.Raw(entry.second);
                }
            }
            writer.EndObject();
            return SortedText(text);
        }

        /// The value of every member replaced by one of another type: an object where there was none, a string for a number, a number for a string
        String WithWrongTypes(StringView saved) {
            JsonTape tape;
            FUZZ_CHECK(tape.Parse(saved).has_value());
            String text;
            JsonWriter writer(text);
            writer.BeginObject();
            for (const JsonMember member : JsonView(tape, 0).Members()) {
                writer.Key(member.key.Text().value_or(StringView()));
                if (member.value.IsString()) {
                    writer.Value(5);
                } else if (member.value.IsNumber() || member.value.IsBool()) {
                    writer.Value("text");
                } else if (member.value.IsArray()) {
                    writer.BeginObject();
                    writer.EndObject();
                } else {
                    writer.Raw("[1,2]");
                }
            }
            writer.EndObject();
            return SortedText(text);
        }

        /// A document with a key that nothing asks for, next to a near miss of a real one
        String WithUnknownKeys(StringView saved) {
            return WithMembers(saved, {{"flagg", "true"}, {"unrelated", R"({"a":1})"}});
        }

        String LeafJson(int32_t id, StringView label) {
            return Format(R"({{"id":{},"label":"{}","weight":0.25}})", id, label);
        }

        /// A tree that is a single chain of children
        String ChainJson(int32_t levels) {
            String text;
            for (int32_t level = 1; level < levels; ++level) {
                text += R"({"children":[)";
            }
            text += Format(R"({{"children":[],"id":{}}})", levels);
            for (int32_t level = levels - 1; level > 0; --level) {
                text += Format(R"(],"id":{}}})", level);
            }
            return text;
        }

        /// A document, written with the keys of every object in byte order as the DOM did
        void Add(Vector<FuzzSeedFile>& seeds, const char* name, StringView document) {
            seeds.push_back({kJsonFolder, name, TextBytes(SortedText(document))});
        }

        /// The arguments of the methods of `ProbeCalls`, as a client writes them
        constexpr StringView kScalarsArguments = R"({"flag":true,"tiny":-5,"medium":500,"big":-9000000000,"huge":18446744073709551615,"single":1.5,"wide":2.25,"mode":"On"})";

        /// A document written by hand: the tape reads text the DOM would write differently (escapes, key order, repeated keys, numbers past the types)
        void AddText(Vector<FuzzSeedFile>& seeds, const char* name, StringView text) {
            seeds.push_back({kJsonFolder, name, TextBytes(text)});
        }

        /// A tree of ProbeNode @p levels levels deep, written as text: two levels of JSON for each, and an empty list at the bottom
        String NestedTreeText(size_t levels) {
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

    }  // namespace

    void AddJsonSeeds(Vector<FuzzSeedFile>& seeds) {
        const String filled = SavedJson(MakeFilledProbe());
        Add(seeds, "probe-filled.json", filled);
        Add(seeds, "probe-empty.json", SavedJson(FuzzProbeData::ProbeState()));
        Add(seeds, "probe-wrong-types.json", WithWrongTypes(filled));
        Add(seeds, "probe-unknown-keys.json", WithUnknownKeys(filled));

        const String shapes = SavedJson(MakeFilledShapes());
        Add(seeds, "shapes-filled.json", shapes);
        Add(seeds, "shapes-empty.json", SavedJson(ProbeShapes()));
        Add(seeds, "shapes-wrong-types.json", WithWrongTypes(shapes));
        Add(seeds, "shapes-variant-index.json", WithMembers(shapes, {{"choice", R"({"index":9})"}}));

        Add(seeds, "chain.json", ChainJson(kChainLevels));

        // Written as text: a writer cuts a document deeper than JsonWriter::kMaxDepth, and this one is deeper on purpose
        String tooDeep(kTooDeepLevels + 1, '[');
        tooDeep.append(kTooDeepLevels + 1, ']');
        seeds.push_back({kJsonFolder, "too-deep.json", TextBytes(tooDeep)});

        AddText(seeds, "text-escapes.json", R"({"text":"a\"b\\c\/\b\f\n\r\t\u00e9\u0000z","name":"x\u0041y","fl\u0061g":true,"i\u0033\u0032":7})");
        AddText(seeds, "text-surrogate-pair.json", R"({"text":"\ud83d\ude00 \u20ac","words":["\ud83d\ude00"]})");
        AddText(seeds, "text-lone-surrogate.json", R"({"text":"\ud83d","words":["\ude00"]})");
        AddText(seeds, "text-big-numbers.json", R"({"i64":9223372036854775808,"u64":18446744073709551616,"i32":123456789012345678901234567890,"f32":-1e-999,"f64":1.5e300,"u8":-0})");
        AddText(seeds, "text-overflowing-number.json", R"({"f64":1e999,"leaf":{"weight":-1e400}})");
        AddText(seeds, "text-empty-containers.json", R"({"leaf":{},"leaves":[],"numbers":[],"words":[],"tree":{"children":[]}})");
        AddText(
            seeds, "text-key-order.json",
            R"({"words":["b","a"],"tree":{"id":2,"children":[]},"text":"x","numbers":[3,2,1],"name":"n","mode":"Off","leaves":[{"label":"l","id":5}],"leaf":{"weight":0.5,"id":1},"i32":-1,"flag":true})");
        AddText(seeds, "text-repeated-key.json", R"({"i32":1,"i32":2,"leaf":{"id":1},"leaf":{"id":2},"flg":true})");
        AddText(seeds, "text-byte-order-mark.json", "\xEF\xBB\xBF{\"flag\":true}");
        AddText(seeds, "text-nul-after-document.json", String("{\"flag\":true}\0garbage", 21));
        seeds.push_back({kJsonFolder, "nest-at-the-tape-limit.json", TextBytes(NestedTreeText(kLevelsAtTheTapeLimit))});

        Add(seeds, "call-scalars.json", kScalarsArguments);
        Add(seeds, "call-echo.json", R"({"text":"hello","mode":"Auto"})");
        Add(seeds, "call-total.json", R"({"numbers":[1,2,3]})");
        Add(seeds, "call-leaf.json", Format(R"({{"first":{},"second":{}}})", LeafJson(1, "one"), LeafJson(2, "two")));
        Add(seeds, "call-maybe.json", R"({"value":4})");
        Add(seeds, "call-leaves.json", Format(R"({{"leaves":[{},{}],"node":{}}})", LeafJson(1, "a"), LeafJson(2, "b"), ChainJson(3)));
        Add(seeds, "call-empty.json", "{}");
    }

}  // namespace BECore::Fuzz
