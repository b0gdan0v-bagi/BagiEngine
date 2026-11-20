#pragma once

#include <BECore/ProgramDomainData.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Domain/ProgramDomain.h>
#include <BECore/Values/Programs/Native/NativePrograms.h>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>

namespace BECore::ProgramDomainData {

    // The operations a formula over the toy mask (`ProgramDomainData.h`) calls, the reflected `DomainBoard` context object that holds one and the registration of all of it. The
    // .gen.hpp of this header is included once per executable (ProgramDomainTest.cpp); a type is found by its simple name, so the names are unique among all test headers.

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

    /// The mask of one cell; an index outside the board is an empty mask, whatever the integer is
    [[nodiscard]] inline DomainBits256 BitsBit(int32_t index) {
        DomainBits256 result;
        if (index >= 0 && index < kDomainBoardCells) {
            result.words[static_cast<size_t>(index / 64)] = uint64_t{1} << (index % 64);
        }
        return result;
    }

    /// Total: a cell outside the board is an empty mask, whatever the integers are
    [[nodiscard]] inline DomainBits256 BitsCell(int32_t x, int32_t y) {
        if (x < 0 || x >= kDomainBoardWidth || y < 0 || y >= kDomainBoardWidth) {
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

    /// The toys are registered by the tests and by the tool that writes the native set, which has no gtest: a refusal stops the process with its reason
    template <typename T>
    [[nodiscard]] T Registered(const std::expected<T, String>& result) {
        if (!result.has_value()) {
            std::fprintf(stderr, "the toy domain registration was refused: %s\n", result.error().c_str());
            std::abort();
        }
        return *result;
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
        /// `ProgramDomain::Fingerprint()` right after the toys, before a test adds an operation of its own: what the tool of the native set had
        uint64_t fingerprint = 0;
    };

    [[nodiscard]] inline const DomainToys& RegisterDomainToys() {
        static const DomainToys toys = [] {
            DomainToys made;
            made.mask = Registered(ProgramDomain::RegisterType<DomainBits256>("mask"));
            made.pair = Registered(ProgramDomain::RegisterType<DomainPair>("pair"));
            made.weighted = Registered(ProgramDomain::RegisterType<DomainWeighted>("weighted"));
            made.bitsAnd = Registered(ProgramDomain::AddOperation<&BitsAnd>("and_mask"));
            made.bitsOr = Registered(ProgramDomain::AddOperation<&BitsOr>("or_mask"));
            made.majority = Registered(ProgramDomain::AddOperation<&BitsMajority>("majority"));
            made.shift = Registered(ProgramDomain::AddOperation<&BitsShift>("shift"));
            made.popcount = Registered(ProgramDomain::AddOperation<&BitsPopcount>("popcount"));
            made.bit = Registered(ProgramDomain::AddOperation<&BitsBit>("bit"));
            made.cell = Registered(ProgramDomain::AddOperation<&BitsCell>("cell"));
            made.any = Registered(ProgramDomain::AddOperation<&BitsAny>("any"));
            made.walls = Registered(ProgramDomain::AddOperation<&BoardWalls>("walls"));
            made.wallsOfRow = Registered(ProgramDomain::AddOperation<&BoardWallsOfRow>("walls_of_row"));
            made.pairOf = Registered(ProgramDomain::AddOperation<&PairOf>("pair_of"));
            made.weightCount = Registered(ProgramDomain::AddOperation<&WeightCount>("weight_count"));
            made.fingerprint = ProgramDomain::Fingerprint();
            // The generated set of BagiEngineTests binds while the registry equals the tool's, which is now and never again once a test adds an operation
            NativePrograms::BindPending();
            return made;
        }();
        return toys;
    }

}  // namespace BECore::ProgramDomainData
