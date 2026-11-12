#include "FuzzProbeAccess.h"

#include <Generated/FuzzProbe.gen.hpp>

namespace BECore::Fuzz {

    namespace {

        constexpr uint64_t kFnvOffset = 0xCBF29CE484222325ull;
        constexpr uint64_t kFnvPrime = 0x100000001B3ull;

        class Hasher {
        public:
            void AddBytes(const void* data, size_t size) {
                const auto* bytes = static_cast<const uint8_t*>(data);
                for (size_t i = 0; i < size; ++i) {
                    _value = (_value ^ bytes[i]) * kFnvPrime;
                }
            }

            template <typename T>
            void Add(const T& value) {
                AddBytes(&value, sizeof(T));
            }

            void AddText(eastl::string_view text) {
                Add(text.size());
                AddBytes(text.data(), text.size());
            }

            uint64_t Value() const {
                return _value;
            }

        private:
            uint64_t _value = kFnvOffset;
        };

        void AddLeaf(Hasher& hasher, const FuzzProbeData::ProbeLeaf& leaf) {
            hasher.Add(leaf.id);
            hasher.Add(leaf.weight);
            hasher.AddText(leaf.label);
        }

    }  // namespace

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
        state.name = PoolString::Intern("Hero");
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
        return state;
    }

    uint64_t DigestProbe(const ProbeState& state) {
        Hasher hasher;
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
        for (const eastl::string& word : state.words) {
            hasher.AddText(word);
        }
        return hasher.Value();
    }

}  // namespace BECore::Fuzz
