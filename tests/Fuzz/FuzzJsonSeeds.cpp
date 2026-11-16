#include <BECore/Control/Json.h>
#include <BECore/Control/JsonSerializer.h>
#include <Fuzz/FuzzProbeAccess.h>
#include <Fuzz/FuzzSeeds.h>

namespace BECore::Fuzz {

    namespace {

        constexpr const char* kJsonFolder = "json";

        /// Levels of the chain of nodes: deep for a document, under the depth the target accepts (two levels of JSON for one of the tree)
        constexpr int32_t kChainLevels = 60;

        /// Depth of the nesting seed that the target refuses before it parses
        constexpr size_t kTooDeepLevels = 400;

        FuzzBytes TextBytes(const String& text) {
            const auto* begin = reinterpret_cast<const uint8_t*>(text.data());
            return FuzzBytes(begin, begin + text.size());
        }

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

        Json tooDeep = Json::array();
        for (size_t level = 0; level < kTooDeepLevels; ++level) {
            tooDeep = Json::array({tooDeep});
        }
        Add(seeds, "too-deep.json", tooDeep);

        Add(seeds, "call-scalars.json", ScalarsArguments());
        Add(seeds, "call-echo.json", Json::object({{"text", "hello"}, {"mode", "Auto"}}));
        Add(seeds, "call-total.json", Json::object({{"numbers", Json::array({1, 2, 3})}}));
        Add(seeds, "call-leaf.json", Json::object({{"first", LeafJson(1, "one")}, {"second", LeafJson(2, "two")}}));
        Add(seeds, "call-maybe.json", Json::object({{"value", 4}}));
        Add(seeds, "call-leaves.json", Json::object({{"leaves", Json::array({LeafJson(1, "a"), LeafJson(2, "b")})}, {"node", ChainJson(3)}}));
        Add(seeds, "call-empty.json", Json::object());
    }

}  // namespace BECore::Fuzz
