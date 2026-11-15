#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore::ReflectionControlData {

    // =========================================================================
    // Reflected test data of the by-name control methods (ReflectionControlTest). The .gen.hpp is generated from this header and
    // included in ReflectionControlTest.cpp only. The names are unique among all test headers: a type is found by its simple name.
    // =========================================================================

    CORE_ENUM(RcMode, uint8_t, Off, Mid, On)

    struct RcLeaf {
        BE_CLASS(RcLeaf)

        BE_REFLECT_FIELD int32_t id = 0;
        BE_REFLECT_FIELD String label;
    };

    /// One field of each kind a client can read or write, and the guarded ones
    struct RcBox {
        BE_CLASS(RcBox)

        BE_REFLECT_FIELD bool flag = false;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 10)) int32_t _count = 3;
        BE_REFLECT_FIELD int64_t big = 0;
        BE_REFLECT_FIELD uint64_t huge = 0;
        BE_REFLECT_FIELD float ratio = 0.5f;
        BE_REFLECT_FIELD double wide = 0.25;
        BE_REFLECT_FIELD String text = "text";
        BE_REFLECT_FIELD PoolString tag;
        BE_REFLECT_FIELD RcMode mode = RcMode::Mid;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(readonly) int32_t _locked = 7;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(hidden) int32_t _secret = 8;
        BE_REFLECT_FIELD RcLeaf leaf;
        BE_REFLECT_FIELD Vector<int32_t> numbers;
    };

    /// An object with a `PoolString` field, and a type that holds its own kind (with and without one)
    struct RcTagged {
        BE_CLASS(RcTagged)

        BE_REFLECT_FIELD int32_t id = 0;
        BE_REFLECT_FIELD PoolString tag;
    };

    struct RcNode {
        BE_CLASS(RcNode)

        BE_REFLECT_FIELD int32_t id = 0;
        BE_REFLECT_FIELD Vector<RcNode> children;
    };

    struct RcNamedNode {
        BE_CLASS(RcNamedNode)

        BE_REFLECT_FIELD Vector<RcNamedNode> children;
        BE_REFLECT_FIELD PoolString name;
    };

    /// One method for each kind of parameter and of result; every call that runs counts itself, so a refused call is the one that leaves `calls` as it was
    struct RcTool {
        BE_CLASS(RcTool)

        BE_REFLECT_FIELD int32_t calls = 0;

        BE_FUNCTION int32_t Add(int32_t first, int32_t second) {
            ++calls;
            return first + second;
        }

        BE_FUNCTION String Echo(const String& text) {
            ++calls;
            return text;
        }

        BE_FUNCTION int64_t Total(const Vector<int32_t>& numbers) {
            ++calls;
            int64_t total = 0;
            for (const int32_t number : numbers) {
                total += number;
            }
            return total;
        }

        BE_FUNCTION RcLeaf Shift(const RcLeaf& leaf) {
            ++calls;
            RcLeaf shifted = leaf;
            ++shifted.id;
            return shifted;
        }

        BE_FUNCTION void Touch() {
            ++calls;
        }

        BE_FUNCTION int32_t Count() const {
            return calls;
        }

        BE_FUNCTION void Name(const PoolString& name) {
            ++calls;
            (void)name;
        }

        BE_FUNCTION void Tag(const RcTagged& tagged) {
            ++calls;
            (void)tagged;
        }

        BE_FUNCTION void Choose(const Variant<int32_t, RcTagged>& pick) {
            ++calls;
            (void)pick;
        }

        BE_FUNCTION void Table(const Map<int32_t, RcTagged>& table) {
            ++calls;
            (void)table;
        }

        BE_FUNCTION void Nest(const RcNode& node) {
            ++calls;
            (void)node;
        }

        BE_FUNCTION void NestNamed(const RcNamedNode& node) {
            ++calls;
            (void)node;
        }

        BE_FUNCTION RcTool* Self() {
            ++calls;
            return this;
        }
    };

}  // namespace BECore::ReflectionControlData
