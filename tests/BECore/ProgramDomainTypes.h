#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Domain/ProgramDomain.h>
#include <Common/Require.h>
#include <bit>
#include <cstdint>

namespace BECore::ProgramDomainData {

    // A toy of the shape a game's cell mask has: 256 bits (16 x 16 cells) in four words, with the operations a formula over a board needs. The reflected `DomainBoard` is a
    // context object that holds one. The .gen.hpp of this header is included once per executable (ProgramDomainTest.cpp); a type is found by its simple name, so the names are
    // unique among all test headers.

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

    /// A context object with a mask, a smaller registered value and an integer, each at an offset a `LoadSlot` reads
    struct DomainBoard {
        BE_CLASS(DomainBoard)

        BE_REFLECT_FIELD int32_t turn = 0;
        BE_REFLECT_FIELD DomainBits256 walls;
        BE_REFLECT_FIELD DomainPair score;
    };

    [[nodiscard]] inline DomainBits256 ShiftLeft(const DomainBits256& mask, int32_t bits) {
        DomainBits256 result;
        for (int32_t i = 0; i < 4; ++i) {
            const int32_t source = i - bits / 64;
            if (source < 0) {
                continue;
            }
            const auto s = static_cast<size_t>(source);
            const int32_t inner = bits % 64;
            uint64_t word = mask.words[s] << inner;
            if (inner != 0 && source > 0) {
                word |= mask.words[s - 1] >> (64 - inner);
            }
            result.words[static_cast<size_t>(i)] = word;
        }
        return result;
    }

    [[nodiscard]] inline DomainBits256 ShiftRight(const DomainBits256& mask, int32_t bits) {
        DomainBits256 result;
        for (int32_t i = 0; i < 4; ++i) {
            const int32_t source = i + bits / 64;
            if (source > 3) {
                continue;
            }
            const auto s = static_cast<size_t>(source);
            const int32_t inner = bits % 64;
            uint64_t word = mask.words[s] >> inner;
            if (inner != 0 && source < 3) {
                word |= mask.words[s + 1] << (64 - inner);
            }
            result.words[static_cast<size_t>(i)] = word;
        }
        return result;
    }

    // The operations a formula calls. Each is a plain function over program types, the registered `DomainBits256` and the enum.

    [[nodiscard]] inline DomainBits256 BitsAnd(const DomainBits256& a, const DomainBits256& b) {
        DomainBits256 result;
        for (size_t i = 0; i < 4; ++i) {
            result.words[i] = a.words[i] & b.words[i];
        }
        return result;
    }

    [[nodiscard]] inline DomainBits256 BitsOr(const DomainBits256& a, const DomainBits256& b) {
        DomainBits256 result;
        for (size_t i = 0; i < 4; ++i) {
            result.words[i] = a.words[i] | b.words[i];
        }
        return result;
    }

    /// The bits that are set in at least two of three masks
    [[nodiscard]] inline DomainBits256 BitsMajority(const DomainBits256& a, const DomainBits256& b, const DomainBits256& c) {
        return BitsOr(BitsOr(BitsAnd(a, b), BitsAnd(a, c)), BitsAnd(b, c));
    }

    /// One step in a direction on the 16 x 16 board: east and west move one cell, north and south one row; cells that leave the board are gone
    [[nodiscard]] inline DomainBits256 BitsShift(const DomainBits256& mask, DomainDirection direction) {
        switch (direction) {
            case DomainDirection::North:
                return ShiftRight(mask, kDomainBoardWidth);
            case DomainDirection::South:
                return ShiftLeft(mask, kDomainBoardWidth);
            case DomainDirection::East:
                return ShiftLeft(mask, 1);
            case DomainDirection::West:
                return ShiftRight(mask, 1);
        }
        return {};
    }

    [[nodiscard]] inline int32_t BitsPopcount(const DomainBits256& mask) {
        int32_t count = 0;
        for (const uint64_t word : mask.words) {
            count += static_cast<int32_t>(std::popcount(word));
        }
        return count;
    }

    /// The mask of one cell; an index outside the board is an empty mask
    [[nodiscard]] inline DomainBits256 BitsBit(int32_t index) {
        DomainBits256 result;
        if (index >= 0 && index < kDomainBoardCells) {
            result.words[static_cast<size_t>(index / 64)] = uint64_t{1} << (index % 64);
        }
        return result;
    }

    [[nodiscard]] inline DomainBits256 BitsCell(int32_t x, int32_t y) {
        if (x < 0 || x >= kDomainBoardWidth) {
            return {};
        }
        return BitsBit(y * kDomainBoardWidth + x);
    }

    [[nodiscard]] inline bool BitsAny(const DomainBits256& mask) {
        return BitsPopcount(mask) != 0;
    }

    /// Reads the board of level 0
    [[nodiscard]] inline DomainBits256 BoardWalls(const Context& context) {
        return static_cast<const DomainBoard*>(context.Object(0))->walls;
    }

    /// The walls of one row of the board of level 0
    [[nodiscard]] inline DomainBits256 BoardWallsOfRow(const Context& context, int32_t row) {
        DomainBits256 rowMask;
        for (int32_t x = 0; x < kDomainBoardWidth; ++x) {
            rowMask = BitsOr(rowMask, BitsCell(x, row));
        }
        return BitsAnd(BoardWalls(context), rowMask);
    }

    [[nodiscard]] inline DomainPair PairOf(int64_t first, int64_t second) {
        return DomainPair{first, second};
    }

    /// Four parameters: more than an instruction has registers for
    [[nodiscard]] inline int32_t FourOf(int32_t a, int32_t b, int32_t c, int32_t d) {
        return a + b + c + d;
    }

    [[nodiscard]] inline int32_t WeightCount(const DomainWeighted& weighted) {
        return weighted.count;
    }

    /// A parameter type nobody registered
    [[nodiscard]] inline int32_t UnregisteredParameter(const DomainUnregistered& value) {
        return value.value;
    }

    [[nodiscard]] inline DomainUnregistered UnregisteredResult(int32_t value) {
        return DomainUnregistered{value};
    }

    /// The ids of the toys, registered once per process: the registry never forgets, and every test of the process sees the same ones
    struct DomainToys {
        DomainTypeId mask = kNoDomainType;
        DomainTypeId pair = kNoDomainType;
        DomainTypeId weighted = kNoDomainType;
        DomainOperationId bitsAnd = 0;
        DomainOperationId bitsOr = 0;
        DomainOperationId majority = 0;
        DomainOperationId shift = 0;
        DomainOperationId popcount = 0;
        DomainOperationId bit = 0;
        DomainOperationId cell = 0;
        DomainOperationId any = 0;
        DomainOperationId walls = 0;
        DomainOperationId wallsOfRow = 0;
        DomainOperationId pairOf = 0;
        DomainOperationId weightCount = 0;
    };

    [[nodiscard]] inline const DomainToys& RegisterDomainToys() {
        static const DomainToys toys = [] {
            using Tests::Require;
            DomainToys made;
            made.mask = Require(ProgramDomain::RegisterType<DomainBits256>("mask"));
            made.pair = Require(ProgramDomain::RegisterType<DomainPair>("pair"));
            made.weighted = Require(ProgramDomain::RegisterType<DomainWeighted>("weighted"));
            made.bitsAnd = Require(ProgramDomain::AddOperation<&BitsAnd>("and_mask"));
            made.bitsOr = Require(ProgramDomain::AddOperation<&BitsOr>("or_mask"));
            made.majority = Require(ProgramDomain::AddOperation<&BitsMajority>("majority"));
            made.shift = Require(ProgramDomain::AddOperation<&BitsShift>("shift"));
            made.popcount = Require(ProgramDomain::AddOperation<&BitsPopcount>("popcount"));
            made.bit = Require(ProgramDomain::AddOperation<&BitsBit>("bit"));
            made.cell = Require(ProgramDomain::AddOperation<&BitsCell>("cell"));
            made.any = Require(ProgramDomain::AddOperation<&BitsAny>("any"));
            made.walls = Require(ProgramDomain::AddOperation<&BoardWalls>("walls"));
            made.wallsOfRow = Require(ProgramDomain::AddOperation<&BoardWallsOfRow>("walls_of_row"));
            made.pairOf = Require(ProgramDomain::AddOperation<&PairOf>("pair_of"));
            made.weightCount = Require(ProgramDomain::AddOperation<&WeightCount>("weight_count"));
            return made;
        }();
        return toys;
    }

}  // namespace BECore::ProgramDomainData
