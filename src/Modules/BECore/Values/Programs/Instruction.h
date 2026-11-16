#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Values/Programs/Opcode.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace BECore {

    /// The most registers a program uses, and the size of one: the biggest value a program holds later (`Vec4`, `Quat`, `Rect`) is 16 bytes
    inline constexpr size_t kMaxProgramRegisters = 32;
    inline constexpr size_t kProgramRegisterBytes = 16;

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
     * `imm` is the index of a constant for `LoadConst` and the offset of the field for `LoadSlot`, whose `a` is the level.
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

    static_assert(sizeof(Instruction) == 12 && alignof(Instruction) == 4 && std::is_trivially_copyable_v<Instruction>);
    static_assert(std::is_trivially_copyable_v<RegisterImage> && sizeof(RegisterImage) == kProgramRegisterBytes);
    static_assert(kMaxProgramRegisters <= std::numeric_limits<uint8_t>::max(), "a register is named by a byte");

}  // namespace BECore
