#pragma once

#include <BECore/Reflection/TypeTraits.h>

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
            BE_REFLECT_FIELD eastl::string label;
        };

    }  // namespace FuzzProbeData

    namespace PropVisit {

        /// The property visitor has no general support for a vector of structs or of values by value (the inspector shows none): the fuzz targets edit none
        inline bool Visit(IPropertyVisitor& /*v*/, eastl::vector<FuzzProbeData::ProbeLeaf>& /*leaves*/, eastl::string_view /*key*/) {
            return false;
        }

        inline bool Visit(IPropertyVisitor& /*v*/, eastl::vector<int32_t>& /*numbers*/, eastl::string_view /*key*/) {
            return false;
        }

        inline bool Visit(IPropertyVisitor& /*v*/, eastl::vector<eastl::string>& /*words*/, eastl::string_view /*key*/) {
            return false;
        }

    }  // namespace PropVisit

    namespace FuzzProbeData {

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
            BE_REFLECT_FIELD eastl::string text;
            BE_REFLECT_FIELD PoolString name;
            BE_REFLECT_FIELD ProbeMode mode = ProbeMode::Auto;
            BE_REFLECT_FIELD ProbeLeaf leaf;
            BE_REFLECT_FIELD eastl::vector<ProbeLeaf> leaves;
            BE_REFLECT_FIELD eastl::vector<int32_t> numbers;
            BE_REFLECT_FIELD eastl::vector<eastl::string> words;
        };

    }  // namespace FuzzProbeData

}  // namespace BECore
