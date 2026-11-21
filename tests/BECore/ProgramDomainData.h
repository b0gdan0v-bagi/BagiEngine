#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore::ProgramDomainData {

    // The plain data of the toy a game's cell mask is modelled by: 256 bits (16 x 16 cells) in four words, and the types around it. Reflection sees them as opaque, so the header holds
    // no class and its .gen.hpp is empty; `ProgramDomainTypes.h` has the operations and the registration, `ProgramRowTypes.h` a field of the mask.

    CORE_ENUM(DomainDirection, uint8_t, North, East, South, West)

    inline constexpr int32_t kDomainBoardWidth = 16;
    inline constexpr int32_t kDomainBoardCells = kDomainBoardWidth * kDomainBoardWidth;

    struct DomainBits256 {
        Array<uint64_t, 4> words{};

        [[nodiscard]] bool operator==(const DomainBits256&) const = default;

        template <typename Serializer>
        void Serialize(Serializer& s) const {
            s.WriteAttribute("w0", words[0]);
            s.WriteAttribute("w1", words[1]);
            s.WriteAttribute("w2", words[2]);
            s.WriteAttribute("w3", words[3]);
        }

        template <typename Deserializer>
        void Deserialize(Deserializer& d) {
            d.ReadAttribute("w0", words[0]);
            d.ReadAttribute("w1", words[1]);
            d.ReadAttribute("w2", words[2]);
            d.ReadAttribute("w3", words[3]);
        }

        template <typename Visitor>
        bool VisitProperties(Visitor& v) {
            bool changed = false;
            changed |= v.Visit("w0", words[0]);
            changed |= v.Visit("w1", words[1]);
            changed |= v.Visit("w2", words[2]);
            changed |= v.Visit("w3", words[3]);
            return changed;
        }
    };

    /// 16 bytes: a registered type smaller than a register, whose upper register bytes must stay zero
    struct DomainPair {
        int64_t first = 0;
        int64_t second = 0;

        [[nodiscard]] bool operator==(const DomainPair&) const = default;

        template <typename Serializer>
        void Serialize(Serializer& s) const {
            s.WriteAttribute("first", first);
            s.WriteAttribute("second", second);
        }

        template <typename Deserializer>
        void Deserialize(Deserializer& d) {
            d.ReadAttribute("first", first);
            d.ReadAttribute("second", second);
        }

        template <typename Visitor>
        bool VisitProperties(Visitor& v) {
            bool changed = false;
            changed |= v.Visit("first", first);
            changed |= v.Visit("second", second);
            return changed;
        }
    };

    /// A float member: its bytes cannot say whether two values are equal, so `==` of it is not bitwise
    struct DomainWeighted {
        float weight = 0.0f;
        int32_t count = 0;

        template <typename Serializer>
        void Serialize(Serializer& s) const {
            s.WriteAttribute("weight", weight);
            s.WriteAttribute("count", count);
        }

        template <typename Deserializer>
        void Deserialize(Deserializer& d) {
            d.ReadAttribute("weight", weight);
            d.ReadAttribute("count", count);
        }

        template <typename Visitor>
        bool VisitProperties(Visitor& v) {
            bool changed = false;
            changed |= v.Visit("weight", weight);
            changed |= v.Visit("count", count);
            return changed;
        }
    };

    /// Larger than a register
    struct DomainHuge {
        Array<uint64_t, 5> words{};

        template <typename Serializer>
        void Serialize(Serializer& s) const {
            s.WriteAttribute("w0", words[0]);
        }

        template <typename Deserializer>
        void Deserialize(Deserializer& d) {
            d.ReadAttribute("w0", words[0]);
        }

        template <typename Visitor>
        bool VisitProperties(Visitor& v) {
            return v.Visit("w0", words[0]);
        }
    };

    /// A value type no test registers: a parameter of it is refused
    struct DomainUnregistered {
        int32_t value = 0;

        template <typename Serializer>
        void Serialize(Serializer& s) const {
            s.WriteAttribute("value", value);
        }

        template <typename Deserializer>
        void Deserialize(Deserializer& d) {
            d.ReadAttribute("value", value);
        }

        template <typename Visitor>
        bool VisitProperties(Visitor& v) {
            return v.Visit("value", value);
        }
    };

}  // namespace BECore::ProgramDomainData
