#include "Peephole.h"

#include <BECore/Values/Programs/Kernels.h>

namespace BECore {

    namespace {

        using RegisterMask = uint32_t;
        static_assert(kMaxProgramRegisters <= std::numeric_limits<RegisterMask>::digits, "a RegisterMask holds a bit per register");

        constexpr RegisterMask RegisterBit(uint32_t reg) {
            return reg < kMaxProgramRegisters ? RegisterMask{1} << reg : RegisterMask{0};
        }

        /// The registers a row reads, by the shape of its operation
        template <typename Op, typename T>
        constexpr RegisterMask RowReads(const Instruction& ins) {
            if constexpr (Op::kShape == Kernel::OpShape::Slot) {
                return 0;
            } else if constexpr (Op::kShape == Kernel::OpShape::Unary || Op::kShape == Kernel::OpShape::Extract) {
                return RegisterBit(ins.a);
            } else if constexpr (Op::kShape == Kernel::OpShape::Binary || Op::kShape == Kernel::OpShape::Compare) {
                return RegisterBit(ins.a) | RegisterBit(ins.b);
            } else if constexpr (Op::kShape == Kernel::OpShape::Pack) {
                RegisterMask mask = 0;
                for (uint32_t i = 0; i < ComponentTraits<T>::kCount; ++i) {
                    mask |= RegisterBit(ins.a + i);
                }
                return mask;
            } else {
                return RegisterBit(ins.a) | RegisterBit(ins.b) | RegisterBit(ins.c);
            }
        }

        /// Every register the instruction reads: what must stay intact for it. A new opcode that this switch lacks is a compile error
        RegisterMask Reads(const Instruction& ins) {
            switch (ins.op) {
                case Opcode::Invalid:
                case Opcode::LoadConst:
                    return 0;
                case Opcode::Copy:
                case Opcode::LoadTable:
                    return RegisterBit(ins.a);
#define BE_PEEPHOLE_READS_ROW(Op, Type, Cpp)                                                                                                                                                           \
    case Opcode::Op##Type:                                                                                                                                                                             \
        return RowReads<Kernel::Op, Cpp>(ins);
                    BE_PROGRAM_ROWS(BE_PEEPHOLE_READS_ROW)
#undef BE_PEEPHOLE_READS_ROW
#define BE_PEEPHOLE_READS_FUSED(Op, Type, Cpp)                                                                                                                                                         \
    case Opcode::Op##Type:                                                                                                                                                                             \
        return 0;
                    BE_PROGRAM_FUSED_ROWS(BE_PEEPHOLE_READS_FUSED)
#undef BE_PEEPHOLE_READS_FUSED
#define BE_PEEPHOLE_READS_MIXED(Op, LName, LCpp, RName, RCpp)                                                                                                                                          \
    case Opcode::Op##LName##RName:                                                                                                                                                                     \
        return RegisterBit(ins.a) | RegisterBit(ins.b);
                    BE_PROGRAM_MIXED_ROWS(BE_PEEPHOLE_READS_MIXED)
#undef BE_PEEPHOLE_READS_MIXED
#define BE_PEEPHOLE_READS_CONVERSION(Src, SrcCpp, Dst, DstCpp)                                                                                                                                         \
    case Opcode::Src##To##Dst:                                                                                                                                                                         \
        return RegisterBit(ins.a);
                    BE_PROGRAM_CONVERSIONS(BE_PEEPHOLE_READS_CONVERSION)
#undef BE_PEEPHOLE_READS_CONVERSION
            }
            return ~RegisterMask{0};
        }

        /// Whether nothing after @p from reads what @p reg holds now: the next access is a write, or there is none and the register is not the result
        bool IsDeadAfter(const Vector<Instruction>& code, size_t from, uint8_t reg, uint8_t resultRegister) {
            for (size_t i = from + 1; i < code.size(); ++i) {
                if ((Reads(code[i]) & RegisterBit(reg)) != 0) {
                    return false;
                }
                if (code[i].dst == reg) {
                    return true;
                }
            }
            return reg != resultRegister;
        }

        /// Whether every register of @p dropped other than @p kept is dead after @p from: those are the writes a fusion stops making
        bool AreDeadAfter(const Vector<Instruction>& code, size_t from, Span<const uint8_t> dropped, uint8_t kept, uint8_t resultRegister) {
            return std::all_of(dropped.begin(), dropped.end(), [&](uint8_t reg) { return reg == kept || IsDeadAfter(code, from, reg, resultRegister); });
        }

        /// The opcodes of `slot * k + b` over one number type
        struct MulAddRow {
            Opcode load;
            Opcode mul;
            Opcode add;
            Opcode fused;
        };

#define BE_PEEPHOLE_MULADD_ROW(Op, Type, Cpp) MulAddRow{Opcode::LoadSlot##Type, Opcode::Mul##Type, Opcode::Add##Type, Opcode::MulAddSlot##Type},
        constexpr Array<MulAddRow, 4> kMulAddRows{{BE_PROGRAM_TYPES_NUMBERS(BE_PEEPHOLE_MULADD_ROW, MulAddSlot)}};
#undef BE_PEEPHOLE_MULADD_ROW

        const MulAddRow* MulAddRowOf(Opcode load) {
            const auto found = std::find_if(kMulAddRows.begin(), kMulAddRows.end(), [load](const MulAddRow& row) { return row.load == load; });
            return found == kMulAddRows.end() ? nullptr : &*found;
        }

        constexpr size_t kMulAddLength = 5;

        /// `LoadSlot r; LoadConst k; Mul; LoadConst b; Add` at @p at, written as one `MulAddSlot`; the five instructions are replaced by it
        bool FuseMulAdd(Vector<Instruction>& code, size_t at, uint8_t resultRegister) {
            if (at + kMulAddLength > code.size()) {
                return false;
            }
            const Instruction& load = code[at];
            const MulAddRow* row = MulAddRowOf(load.op);
            if (row == nullptr) {
                return false;
            }
            const Instruction& constK = code[at + 1];
            const Instruction& mul = code[at + 2];
            const Instruction& constB = code[at + 3];
            const Instruction& add = code[at + 4];
            if (constK.op != Opcode::LoadConst || mul.op != row->mul || constB.op != Opcode::LoadConst || add.op != row->add) {
                return false;
            }
            // The multiplication reads the field and the constant, the addition the product and the second constant, each not overwritten in between
            if (mul.a != load.dst || mul.b != constK.dst || constK.dst == load.dst || add.a != mul.dst || add.b != constB.dst || constB.dst == mul.dst) {
                return false;
            }
            if (constK.imm > kMaxFusedConstantIndex || constB.imm > kMaxFusedConstantIndex) {
                return false;
            }
            const Array<uint8_t, 4> dropped{load.dst, constK.dst, mul.dst, constB.dst};
            if (!AreDeadAfter(code, at + kMulAddLength - 1, Span<const uint8_t>(dropped.data(), dropped.size()), add.dst, resultRegister)) {
                return false;
            }
            const Instruction fused{row->fused, add.dst, load.a, static_cast<uint8_t>(constK.imm), static_cast<uint8_t>(constB.imm), load.imm};
            code[at] = fused;
            code.erase(code.begin() + static_cast<ptrdiff_t>(at) + 1, code.begin() + static_cast<ptrdiff_t>(at + kMulAddLength));
            return true;
        }

    }  // namespace

    void Fuse(Vector<Instruction>& code, uint8_t resultRegister) {
        for (size_t at = 0; at < code.size(); ++at) {
            static_cast<void>(FuseMulAdd(code, at, resultRegister));
        }
    }

}  // namespace BECore
