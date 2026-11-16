#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Values/Programs/Opcode.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace BECore {

    /// The most registers a program uses, and the size of one: the biggest value a program holds (`Vec4`, `Quat`) is 16 bytes
    inline constexpr size_t kMaxProgramRegisters = 32;
    inline constexpr size_t kProgramRegisterBytes = 16;

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
     * size of the field in bytes (1, 2, 4 or 8).
     * `Extract` takes component `imm` of the value in `a`; `Pack` reads its components from the consecutive registers `a`, `a + 1`, ... (as many as the type has).
     * `LoadTable` reads the `int32_t` index in `a` and writes to `dst` entry `index` of the run of constants that starts at constant `imm` and has
     * `TableEntryCount` entries (the count is 16 bits, low byte in `b`, high byte in `c`; `SetTableEntryCount` writes it), the index clamped to the ends.
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

    static_assert(kMaxTableEntries <= std::numeric_limits<uint16_t>::max(), "the count of a table is 16 bits");
    static_assert(sizeof(Instruction) == 12 && alignof(Instruction) == 4 && std::is_trivially_copyable_v<Instruction>);
    static_assert(std::is_trivially_copyable_v<RegisterImage> && sizeof(RegisterImage) == kProgramRegisterBytes);
    static_assert(kMaxProgramRegisters <= std::numeric_limits<uint8_t>::max(), "a register is named by a byte");

}  // namespace BECore
