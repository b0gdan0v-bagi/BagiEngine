#include "FuzzProbeAccess.h"

#include <Fuzz/FuzzChecks.h>
#include <Generated/FuzzProbe.gen.hpp>

namespace BECore::Fuzz {

    TypedRef MakeProbeRef(ProbeState& state) {
        return TypedRef::Of(state);
    }

    TypedRef MakeProbeRef(ProbeCalls& calls) {
        return TypedRef::Of(calls);
    }

    namespace {

        void AddNode(FuzzDigest& hasher, const FuzzProbeData::ProbeNode& node) {
            hasher.Add(node.id);
            hasher.Add(node.children.size());
            for (const FuzzProbeData::ProbeNode& child : node.children) {
                AddNode(hasher, child);
            }
        }

        void AddLeaf(FuzzDigest& hasher, const FuzzProbeData::ProbeLeaf& leaf) {
            hasher.Add(leaf.id);
            hasher.Add(leaf.weight);
            hasher.AddText(leaf.label);
        }

        /// The sum of the hashes of the entries, so that the order a hash map iterates in does not matter
        template <typename Map, typename Entry>
        uint64_t UnorderedDigest(const Map& map, Entry entryHasher) {
            uint64_t sum = 0;
            for (const auto& entry : map) {
                FuzzDigest hasher;
                entryHasher(hasher, entry);
                sum += hasher.Value();
            }
            return sum;
        }

    }  // namespace

    ProbeShapes MakeFilledShapes() {
        using namespace FuzzProbeData;
        ProbeShapes shapes;
        shapes.maybe = 7;
        ProbeLeaf leaf;
        leaf.id = 3;
        leaf.weight = 0.5f;
        leaf.label = "in an optional";
        shapes.maybeLeaf = leaf;
        shapes.choice = leaf;
        shapes.ordered = {{2, "two"}, {-1, "minus"}};
        shapes.table = {{"a", 1}, {"b", 2}};
        shapes.triple = {1, 2, 3};
        shapes.few = {4, 5};
        shapes.entry = {9, "nine"};
        return shapes;
    }

    ProbeState MakeFilledProbe() {
        using namespace FuzzProbeData;
        ProbeState state;
        state.flag = true;
        state.i8 = -8;
        state.u8 = 200;
        state.i16 = -1600;
        state.u16 = 60000;
        state.i32 = -320000;
        state.u32 = 4000000000u;
        state.i64 = -6400000000000;
        state.u64 = 18000000000000000000ull;
        state.f32 = 1.5f;
        state.f64 = -2.25;
        state.text = "hello";
        state.name = "Hero"_intern;
        state.mode = ProbeMode::On;
        state.leaf.id = 7;
        state.leaf.weight = 0.5f;
        state.leaf.label = "nested";
        for (int32_t id = 0; id < 3; ++id) {
            ProbeLeaf leaf;
            leaf.id = id;
            leaf.weight = static_cast<float>(id) * 0.25f;
            leaf.label = id == 0 ? "first" : "other";
            state.leaves.push_back(leaf);
        }
        state.numbers = {10, -20, 30};
        state.words = {"one", "two"};
        state.tree.id = 1;
        state.tree.children.resize(2);
        state.tree.children[0].id = 2;
        state.tree.children[1].id = 3;
        state.tree.children[1].children.resize(1);
        state.tree.children[1].children[0].id = 4;
        return state;
    }

    uint64_t DigestProbe(const FuzzProbeData::ProbeNode& node) {
        FuzzDigest hasher;
        AddNode(hasher, node);
        return hasher.Value();
    }

    uint64_t DigestProbe(const ProbeShapes& shapes) {
        FuzzDigest hasher;
        hasher.Add(shapes.maybe.has_value());
        hasher.Add(shapes.maybe.value_or(0));
        hasher.Add(shapes.maybeLeaf.has_value());
        if (shapes.maybeLeaf.has_value()) {
            AddLeaf(hasher, *shapes.maybeLeaf);
        }
        hasher.Add(shapes.choice.index());
        if (const auto* number = std::get_if<int32_t>(&shapes.choice)) {
            hasher.Add(*number);
        } else if (const auto* text = std::get_if<String>(&shapes.choice)) {
            hasher.AddText(*text);
        } else if (const auto* leaf = std::get_if<FuzzProbeData::ProbeLeaf>(&shapes.choice)) {
            AddLeaf(hasher, *leaf);
        }
        hasher.Add(shapes.ordered.size());
        for (const auto& entry : shapes.ordered) {
            hasher.Add(entry.first);
            hasher.AddText(entry.second);
        }
        hasher.Add(shapes.table.size());
        hasher.Add(UnorderedDigest(shapes.table, [](FuzzDigest& entryHasher, const auto& entry) {
            entryHasher.AddText(entry.first);
            entryHasher.Add(entry.second);
        }));
        for (const int32_t number : shapes.triple) {
            hasher.Add(number);
        }
        hasher.Add(shapes.few.size());
        for (const int32_t number : shapes.few) {
            hasher.Add(number);
        }
        hasher.Add(shapes.entry.first);
        hasher.AddText(shapes.entry.second);
        return hasher.Value();
    }

    uint64_t DigestProbe(const ProbeCalls& calls) {
        FuzzDigest hasher;
        hasher.Add(calls.calls);
        hasher.AddText(calls.last);
        return hasher.Value();
    }

    uint64_t DigestProbe(const ProbeState& state) {
        FuzzDigest hasher;
        hasher.Add(state.flag);
        hasher.Add(state.i8);
        hasher.Add(state.u8);
        hasher.Add(state.i16);
        hasher.Add(state.u16);
        hasher.Add(state.i32);
        hasher.Add(state.u32);
        hasher.Add(state.i64);
        hasher.Add(state.u64);
        hasher.Add(state.f32);
        hasher.Add(state.f64);
        hasher.AddText(state.text);
        hasher.AddText(state.name.ToStringView());
        hasher.Add(state.mode);
        AddLeaf(hasher, state.leaf);
        hasher.Add(state.leaves.size());
        for (const FuzzProbeData::ProbeLeaf& leaf : state.leaves) {
            AddLeaf(hasher, leaf);
        }
        hasher.Add(state.numbers.size());
        for (const int32_t number : state.numbers) {
            hasher.Add(number);
        }
        hasher.Add(state.words.size());
        for (const String& word : state.words) {
            hasher.AddText(word);
        }
        AddNode(hasher, state.tree);
        return hasher.Value();
    }

}  // namespace BECore::Fuzz
