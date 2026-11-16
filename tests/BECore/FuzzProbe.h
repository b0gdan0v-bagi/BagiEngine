#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    // =========================================================================
    // The types the fuzz targets (tests/Fuzz) read untrusted XML and saves into: one field of every kind a content file or a save has.
    // The .gen.hpp is generated from this header and included in tests/Fuzz/FuzzProbeAccess.cpp only.
    // =========================================================================

    namespace FuzzProbeData {

        CORE_ENUM(ProbeMode, uint8_t, Off, On, Auto)

        /// An element of a list of objects, and a nested object
        struct ProbeLeaf {
            BE_CLASS(ProbeLeaf)

            BE_REFLECT_FIELD int32_t id = 0;
            BE_REFLECT_FIELD float weight = 0.0f;
            BE_REFLECT_FIELD String label;
        };

        /// A type that holds its own kind, as a UI tree does: reading it recurses once per level of the document
        struct ProbeNode {
            BE_CLASS(ProbeNode)

            BE_REFLECT_FIELD int32_t id = 0;
            BE_REFLECT_FIELD Vector<ProbeNode> children;
        };

        /// Every scalar type, both string types, an enum, a nested object, a list of objects and lists of scalars
        struct ProbeState {
            BE_CLASS(ProbeState)

            BE_REFLECT_FIELD bool flag = false;
            BE_REFLECT_FIELD int8_t i8 = 0;
            BE_REFLECT_FIELD uint8_t u8 = 0;
            BE_REFLECT_FIELD int16_t i16 = 0;
            BE_REFLECT_FIELD uint16_t u16 = 0;
            BE_REFLECT_FIELD int32_t i32 = 0;
            BE_REFLECT_FIELD uint32_t u32 = 0;
            BE_REFLECT_FIELD int64_t i64 = 0;
            BE_REFLECT_FIELD uint64_t u64 = 0;
            BE_REFLECT_FIELD float f32 = 0.0f;
            BE_REFLECT_FIELD double f64 = 0.0;
            BE_REFLECT_FIELD String text;
            BE_REFLECT_FIELD PoolString name;
            BE_REFLECT_FIELD ProbeMode mode = ProbeMode::Auto;
            BE_REFLECT_FIELD ProbeLeaf leaf;
            BE_REFLECT_FIELD Vector<ProbeLeaf> leaves;
            BE_REFLECT_FIELD Vector<int32_t> numbers;
            BE_REFLECT_FIELD Vector<String> words;
            BE_REFLECT_FIELD ProbeNode tree;
        };

        /// The shapes a JSON document has and an XML one does not (or reads differently): optionals, a variant, maps, a fixed array and a fixed list
        struct ProbeShapes {
            BE_CLASS(ProbeShapes)

            BE_REFLECT_FIELD Optional<int32_t> maybe;
            BE_REFLECT_FIELD Optional<ProbeLeaf> maybeLeaf;
            BE_REFLECT_FIELD Variant<std::monostate, int32_t, String, ProbeLeaf> choice;
            BE_REFLECT_FIELD Map<int32_t, String> ordered;
            BE_REFLECT_FIELD HashMap<String, int32_t> table;
            BE_REFLECT_FIELD Array<int32_t, 3> triple{};
            BE_REFLECT_FIELD FixedVector<int32_t, 4> few;
            BE_REFLECT_FIELD Pair<int32_t, String> entry;
        };

        /// The object the arguments of `CallMethod` are fuzzed against: a method for each kind of parameter and of result, and one that cannot be called by name. Every
        /// call that runs counts itself, so a refused call is the one that leaves the state as it was.
        struct ProbeCalls {
            BE_CLASS(ProbeCalls)

            BE_REFLECT_FIELD int32_t calls = 0;
            BE_REFLECT_FIELD String last;

            BE_FUNCTION double Scalars(bool flag, int8_t tiny, uint16_t medium, int64_t big, uint64_t huge, float single, double wide, ProbeMode mode) {
                ++calls;
                last = mode == ProbeMode::Off ? "off" : "on";
                return static_cast<double>(single) + wide + static_cast<double>(big) + static_cast<double>(huge) + static_cast<double>(medium) + tiny + (flag ? 1 : 0);
            }

            BE_FUNCTION String Echo(const String& text, ProbeMode mode) {
                ++calls;
                last = text;
                return mode == ProbeMode::Auto ? text : String("fixed");
            }

            BE_FUNCTION int64_t Total(const Vector<int32_t>& numbers) {
                ++calls;
                int64_t total = 0;
                for (const int32_t number : numbers) {
                    total += number;
                }
                return total;
            }

            BE_FUNCTION ProbeLeaf Leaf(const ProbeLeaf& first, ProbeLeaf second) {
                ++calls;
                last = second.label;
                return first;
            }

            BE_FUNCTION Optional<int32_t> Maybe(Optional<int32_t> value) {
                ++calls;
                return value;
            }

            BE_FUNCTION Vector<ProbeLeaf> Leaves(const Vector<ProbeLeaf>& leaves, Optional<ProbeNode> node) {
                ++calls;
                return node.has_value() ? Vector<ProbeLeaf>() : leaves;
            }

            BE_FUNCTION void Touch() {
                ++calls;
            }

            BE_FUNCTION ProbeCalls* Self() {
                ++calls;
                return this;
            }
        };

    }  // namespace FuzzProbeData

}  // namespace BECore
