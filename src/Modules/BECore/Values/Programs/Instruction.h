#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Values/Programs/Opcode.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace BECore {

    /// The sizes of the field of an enum that `LoadSlotEnum` reads (the underlying types of a `CORE_ENUM`)
    [[nodiscard]] constexpr bool IsEnumFieldBytes(uint8_t bytes) {
        return bytes == sizeof(uint8_t) || bytes == sizeof(uint16_t) || bytes == sizeof(uint32_t) || bytes == sizeof(uint64_t);
    }

    /// The most entries a table of constants has (`at(...)`, `ByLevel`): the count of a `LoadTable` is 16 bits and a program holds its own copy of every entry
    inline constexpr uint32_t kMaxTableEntries = 4096;

    /// One register: the bytes of a value of any program type, left as the last write made them (an instruction reads only what it wrote as that type)
    struct alignas(kProgramRegisterBytes) RegisterImage {
        Array<uint8_t, kProgramRegisterBytes> bytes;

        /// Zero-padded images of equal values are equal bytes, which is what the constant pool relies on
        [[nodiscard]] bool operator==(const RegisterImage& other) const {
            return bytes == other.bytes;
        }
    };

    /**
     * @brief The fixed record the interpreter reads
     *
     * Which fields an opcode uses: `dst` is the register it writes; `a`, `b`, `c` the registers it reads (`Select` reads `a` as the condition);
     * `imm` is the index of a constant for `LoadConst` and the offset of the field for `LoadSlot`, whose `a` is the level and, for an enum, `b` the
     * size of the field in bytes (1, 2, 4 or 8); for `LoadSlotDomain` `b` is the size of the registered type (1 to `kProgramRegisterBytes`), and the rest of the register is zero.
     * `CallDomain` calls the operation `imm` of `ProgramDomain` with the registers `a`, `b`, `c` (as many as the operation has parameters) and writes `dst`.
     * `Extract` takes component `imm` of the value in `a`; `Pack` reads its components from the consecutive registers `a`, `a + 1`, ... (as many as the type has).
     * `LoadTable` reads the `int32_t` index in `a` and writes to `dst` entry `index` of the run of constants that starts at constant `imm` and has
     * `TableEntryCount` entries (the count is 16 bits, low byte in `b`, high byte in `c`; `SetTableEntryCount` writes it), the index clamped to the ends.
     *
     * The fused instructions (`BE_PROGRAM_FUSED_ROWS`, written only by `Fuse`) read a field of the context themselves, so a field of theirs that does not fit makes the
     * sequence stay unfused. `MulAddSlot<T>`: the field at offset `imm` of level `a` times the constant `b` plus the constant `c` (both constant indexes are bytes; over `Vec3` `b` is a `float`).
     * `LerpSlots<T>`: `lerp(field, field, t)`, `t` in register `c`, the first field at the low half of `imm` in level `a`, the second at its high half in level `b`.
     * `LoadTableSlot`: `LoadTable` of a field: the `int32_t` index at the low half of `imm` in level `a`, the first constant in its high half, the count in `b` and `c` as `LoadTable`.
     */
    struct Instruction {
        Opcode op = Opcode::Invalid;
        uint8_t dst = 0;
        uint8_t a = 0;
        uint8_t b = 0;
        uint8_t c = 0;
        uint32_t imm = 0;

        [[nodiscard]] bool operator==(const Instruction&) const = default;
    };

    /// The count of entries of a `LoadTable`
    [[nodiscard]] constexpr uint32_t TableEntryCount(const Instruction& ins) {
        return static_cast<uint32_t>(ins.b) | static_cast<uint32_t>(ins.c) << 8;
    }

    /// Writes the count of entries of a `LoadTable` into `b` and `c`; a count beyond 16 bits is cut, `ProgramBuilder::Finish` refuses one beyond `kMaxTableEntries` first
    constexpr void SetTableEntryCount(Instruction& ins, uint32_t count) {
        ins.b = static_cast<uint8_t>(count & 0xFFU);
        ins.c = static_cast<uint8_t>((count >> 8) & 0xFFU);
    }

#define BE_PROGRAM_TYPE_FITS_A_REGISTER(Name, Cpp)                                                                                                                                                     \
    static_assert(std::is_trivially_copyable_v<Cpp> && sizeof(Cpp) <= kProgramRegisterBytes, "a program type is a trivially copyable value of at most one register: " #Name);
    BE_PROGRAM_TYPES(BE_PROGRAM_TYPE_FITS_A_REGISTER)
#undef BE_PROGRAM_TYPE_FITS_A_REGISTER

    /// The largest constant index a field of a fused instruction holds (a byte)
    inline constexpr uint32_t kMaxFusedConstantIndex = std::numeric_limits<uint8_t>::max();

    /// The largest value a half of `imm` holds: two offsets, or an offset and a constant index, share it in a fused instruction
    inline constexpr uint32_t kMaxFusedHalf = std::numeric_limits<uint16_t>::max();

    [[nodiscard]] constexpr uint32_t PackHalves(uint32_t low, uint32_t high) {
        return low | high << 16;
    }

    [[nodiscard]] constexpr uint32_t LowHalf(uint32_t imm) {
        return imm & kMaxFusedHalf;
    }

    [[nodiscard]] constexpr uint32_t HighHalf(uint32_t imm) {
        return imm >> 16;
    }

    static_assert(kMaxTableEntries <= std::numeric_limits<uint16_t>::max(), "the count of a table is 16 bits");
    static_assert(sizeof(Instruction) == 12 && alignof(Instruction) == 4 && std::is_trivially_copyable_v<Instruction>);
    static_assert(std::is_trivially_copyable_v<RegisterImage> && sizeof(RegisterImage) == kProgramRegisterBytes);
    static_assert(kMaxProgramRegisters <= std::numeric_limits<uint8_t>::max(), "a register is named by a byte");

}  // namespace BECore
