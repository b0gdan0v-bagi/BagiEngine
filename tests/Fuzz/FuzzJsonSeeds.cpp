#include <BECore/Control/Json.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/Reflection/ReaderLimits.h>
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

        template <typename T>
        Json SavedJson(const T& value) {
            JsonSerializer writer;
            value.Serialize(writer);
            return writer.Take();
        }

        /// The value of every member replaced by one of another type: an object where there was none, a string for a number, a number for a string
        Json WithWrongTypes(const Json& saved) {
            Json wrong = Json::object();
            for (auto member = saved.begin(); member != saved.end(); ++member) {
                if (member->is_string()) {
                    wrong[member.key()] = 5;
                } else if (member->is_number() || member->is_boolean()) {
                    wrong[member.key()] = "text";
                } else if (member->is_array()) {
                    wrong[member.key()] = Json::object();
                } else {
                    wrong[member.key()] = Json::array({1, 2});
                }
            }
            return wrong;
        }

        /// A document with a key that nothing asks for, next to a near miss of a real one
        Json WithUnknownKeys(Json saved) {
            saved["flagg"] = true;
            saved["unrelated"] = Json::object({{"a", 1}});
            return saved;
        }

        Json LeafJson(int32_t id, StringView label) {
            Json leaf = Json::object();
            leaf["id"] = id;
            leaf["weight"] = 0.25;
            leaf["label"] = MakeJsonString(label);
            return leaf;
        }

        /// A tree that is a single chain of children
        Json ChainJson(int32_t levels) {
            Json node = Json::object({{"id", levels}, {"children", Json::array()}});
            for (int32_t level = levels - 1; level > 0; --level) {
                node = Json::object({{"id", level}, {"children", Json::array({node})}});
            }
            return node;
        }

        /// The arguments of the methods of `ProbeCalls`, as a client writes them
        Json ScalarsArguments() {
            Json arguments = Json::object();
            arguments["flag"] = true;
            arguments["tiny"] = -5;
            arguments["medium"] = 500;
            arguments["big"] = -9000000000;
            arguments["huge"] = 18446744073709551615ull;
            arguments["single"] = 1.5;
            arguments["wide"] = 2.25;
            arguments["mode"] = "On";
            return arguments;
        }

        void Add(Vector<FuzzSeedFile>& seeds, const char* name, const Json& document) {
            seeds.push_back({kJsonFolder, name, TextBytes(DumpJson(document))});
        }

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
        const Json filled = SavedJson(MakeFilledProbe());
        Add(seeds, "probe-filled.json", filled);
        Add(seeds, "probe-empty.json", SavedJson(FuzzProbeData::ProbeState()));
        Add(seeds, "probe-wrong-types.json", WithWrongTypes(filled));
        Add(seeds, "probe-unknown-keys.json", WithUnknownKeys(filled));

        const Json shapes = SavedJson(MakeFilledShapes());
        Add(seeds, "shapes-filled.json", shapes);
        Add(seeds, "shapes-empty.json", SavedJson(ProbeShapes()));
        Add(seeds, "shapes-wrong-types.json", WithWrongTypes(shapes));
        Json badVariant = shapes;
        badVariant["choice"] = Json::object({{"index", 9}});
        Add(seeds, "shapes-variant-index.json", badVariant);

        Add(seeds, "chain.json", ChainJson(kChainLevels));

        // Written as text: DumpJson cuts a document deeper than JsonWriter::kMaxDepth, and this one is deeper on purpose
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

        Add(seeds, "call-scalars.json", ScalarsArguments());
        Add(seeds, "call-echo.json", Json::object({{"text", "hello"}, {"mode", "Auto"}}));
        Add(seeds, "call-total.json", Json::object({{"numbers", Json::array({1, 2, 3})}}));
        Add(seeds, "call-leaf.json", Json::object({{"first", LeafJson(1, "one")}, {"second", LeafJson(2, "two")}}));
        Add(seeds, "call-maybe.json", Json::object({{"value", 4}}));
        Add(seeds, "call-leaves.json", Json::object({{"leaves", Json::array({LeafJson(1, "a"), LeafJson(2, "b")})}, {"node", ChainJson(3)}}));
        Add(seeds, "call-empty.json", Json::object());
    }

}  // namespace BECore::Fuzz
