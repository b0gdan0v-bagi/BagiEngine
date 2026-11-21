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
            } else if constexpr (Op::kShape == Kernel::OpShape::Unary || Op::kShape == Kernel::OpShape::Element || Op::kShape == Kernel::OpShape::Extract) {
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

        /// The registers a fused instruction reads: the weight of `LerpSlots`; the others read fields only
        RegisterMask FusedReads(const Instruction& ins) {
            return IsLerpSlots(ins.op) ? RegisterBit(ins.c) : RegisterMask{0};
        }

        /// Every register the instruction reads: what must stay intact for it. A new opcode that this switch lacks is a compile error
        RegisterMask Reads(const Instruction& ins) {
            switch (ins.op) {
                case Opcode::Invalid:
                case Opcode::LoadConst:
                    return 0;
                case Opcode::LoadTableSlot:
                    return 0;
                case Opcode::CallDomain: {
                    // As many registers as the operation has parameters; an operation that is not registered leaves the pass blind, as an opcode it does not know
                    if (ins.imm >= ProgramDomain::OperationCount()) {
                        return ~RegisterMask{0};
                    }
                    const uint32_t count = ProgramDomain::Operation(ins.imm).parameterCount;
                    return (count > 0 ? RegisterBit(ins.a) : 0) | (count > 1 ? RegisterBit(ins.b) : 0) | (count > 2 ? RegisterBit(ins.c) : 0);
                }
                case Opcode::Copy:
                case Opcode::LoadTable:
                    return RegisterBit(ins.a);
                case Opcode::ListSlot:
                    return 0;
                case Opcode::ListElem:
                case Opcode::ListSize:
                case Opcode::BreakIf:
                    return RegisterBit(ins.a);
                case Opcode::ListAt:
                    return RegisterBit(ins.a) | RegisterBit(ins.b);
                case Opcode::LoopBegin:
                    return RegisterBit(ins.a);
                case Opcode::LoopEnd:
                    return RegisterBit(ins.a) | RegisterBit(ins.dst);
#define BE_PEEPHOLE_READS_ROW(Op, Type, Cpp)                                                                                                                                                           \
    case Opcode::Op##Type:                                                                                                                                                                             \
        return RowReads<Kernel::Op, Cpp>(ins);
                    BE_PROGRAM_ROWS(BE_PEEPHOLE_READS_ROW)
#undef BE_PEEPHOLE_READS_ROW
#define BE_PEEPHOLE_READS_FUSED(Op, Type, Cpp)                                                                                                                                                         \
    case Opcode::Op##Type:                                                                                                                                                                             \
        return FusedReads(ins);
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

        /// The opcodes of `slot * k + b` over one number type, or over `Vec3` with a `float` factor
        struct MulAddRow {
            Opcode load;
            Opcode mul;
            Opcode add;
            Opcode fused;
        };

#define BE_PEEPHOLE_MULADD_ROW(Op, Type, Cpp) MulAddRow{Opcode::LoadSlot##Type, Opcode::Mul##Type, Opcode::Add##Type, Opcode::MulAddSlot##Type},
        constexpr Array<MulAddRow, 5> kMulAddRows{
            {BE_PROGRAM_TYPES_NUMBERS(BE_PEEPHOLE_MULADD_ROW, MulAddSlot) MulAddRow{Opcode::LoadSlotVec3, Opcode::MulVec3Float, Opcode::AddVec3, Opcode::MulAddSlotVec3}}};
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

        /// The opcodes of `lerp(slot, slot, t)` over one float type
        struct LerpRow {
            Opcode load;
            Opcode lerp;
            Opcode fused;
        };

#define BE_PEEPHOLE_LERP_ROW(Op, Type, Cpp) LerpRow{Opcode::LoadSlot##Type, Opcode::Lerp##Type, Opcode::LerpSlots##Type},
        constexpr Array<LerpRow, 2> kLerpRows{{BE_PROGRAM_TYPES_FLOATS(BE_PEEPHOLE_LERP_ROW, LerpSlots)}};
#undef BE_PEEPHOLE_LERP_ROW

        const LerpRow* LerpRowOf(Opcode load) {
            const auto found = std::find_if(kLerpRows.begin(), kLerpRows.end(), [load](const LerpRow& row) { return row.load == load; });
            return found == kLerpRows.end() ? nullptr : &*found;
        }

        /// `LoadSlot from; LoadSlot to; <the code of t>; Lerp from, to, t` from @p at: the two loads go and the `Lerp` becomes the fused instruction, which reads the fields where the
        /// `Lerp` stood. The code of `t` stays; it must neither read nor write the two registers (a read would see a register the fusion no longer fills, a write would change what the
        /// `Lerp` read), and `t` must not be one of them.
        bool FuseLerp(Vector<Instruction>& code, size_t at, uint8_t resultRegister) {
            constexpr size_t kLoads = 2;
            if (at + kLoads >= code.size()) {
                return false;
            }
            const Instruction from = code[at];
            const Instruction to = code[at + 1];
            const LerpRow* row = LerpRowOf(from.op);
            if (row == nullptr || to.op != from.op || to.dst == from.dst) {
                return false;
            }
            const RegisterMask watched = RegisterBit(from.dst) | RegisterBit(to.dst);
            for (size_t i = at + kLoads; i < code.size(); ++i) {
                const Instruction& ins = code[i];
                const bool isLerp = ins.op == row->lerp && ins.a == from.dst && ins.b == to.dst;
                if (!isLerp) {
                    if (((Reads(ins) | RegisterBit(ins.dst)) & watched) != 0) {
                        return false;
                    }
                    continue;
                }
                if ((RegisterBit(ins.c) & watched) != 0 || from.imm > kMaxFusedHalf || to.imm > kMaxFusedHalf) {
                    return false;
                }
                const Array<uint8_t, 2> dropped{from.dst, to.dst};
                if (!AreDeadAfter(code, i, Span<const uint8_t>(dropped.data(), dropped.size()), ins.dst, resultRegister)) {
                    return false;
                }
                code[i] = Instruction{row->fused, ins.dst, from.a, to.a, ins.c, PackHalves(from.imm, to.imm)};
                code.erase(code.begin() + static_cast<ptrdiff_t>(at), code.begin() + static_cast<ptrdiff_t>(at + kLoads));
                return true;
            }
            return false;
        }

        /// `LoadSlot index; LoadTable` at @p at: a table indexed by a field, written as one `LoadTableSlot`
        bool FuseTable(Vector<Instruction>& code, size_t at, uint8_t resultRegister) {
            if (at + 1 >= code.size() || code[at].op != Opcode::LoadSlotInt32 || code[at + 1].op != Opcode::LoadTable) {
                return false;
            }
            const Instruction& load = code[at];
            const Instruction& table = code[at + 1];
            if (table.a != load.dst || load.imm > kMaxFusedHalf || table.imm > kMaxFusedHalf) {
                return false;
            }
            const Array<uint8_t, 1> dropped{load.dst};
            if (!AreDeadAfter(code, at + 1, Span<const uint8_t>(dropped.data(), dropped.size()), table.dst, resultRegister)) {
                return false;
            }
            const Instruction fused{Opcode::LoadTableSlot, table.dst, load.a, table.b, table.c, PackHalves(load.imm, table.imm)};
            code[at] = fused;
            code.erase(code.begin() + static_cast<ptrdiff_t>(at) + 1);
            return true;
        }

    }  // namespace

    void Fuse(Vector<Instruction>& code, uint8_t resultRegister) {
        // The liveness below reads the code as straight-line: a register the body writes is read again by the next round, which it does not see
        if (std::any_of(code.begin(), code.end(), [](const Instruction& ins) { return IsListInstruction(ins.op); })) {
            return;
        }
        size_t at = 0;
        while (at < code.size()) {
            // A `Lerp` leaves the code of its weight where it was, so what starts at @p at now is looked at again; a fused instruction starts no sequence
            if (FuseLerp(code, at, resultRegister)) {
                continue;
            }
            static_cast<void>(FuseMulAdd(code, at, resultRegister) || FuseTable(code, at, resultRegister));
            ++at;
        }
    }

}  // namespace BECore
