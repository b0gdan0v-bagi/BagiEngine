#include "ProgramBuilder.h"

#include <BECore/Values/Programs/Components.h>
#include <BECore/Values/Programs/Lists/ListImage.h>
#include <BECore/Values/Programs/Peephole.h>

namespace BECore {

    namespace {

        /// The target of a jump that `EmitLoopEnd` has not written yet; `Finish` refuses it (no program is that long)
        constexpr uint32_t kUnpatchedJump = std::numeric_limits<uint32_t>::max();

        /**
         * @brief The checks of the list and loop instructions of `Finish`
         *
         * Together they are the proof that a program ends and never jumps out of its code: a jump exists only as a `LoopBegin` that skips a loop, a `LoopEnd` that goes back to the first
         * instruction of its own body and a `BreakIf` that leaves the innermost loop; each `LoopBegin` is paired with exactly one `LoopEnd`, the pairs nest, and a loop walks a list of at most
         * 2^32 elements, so every loop is finite and the nesting is as deep as the code is long. What the registers hold is not tracked here, as for the other instructions: the lowering emits
         * well-typed code.
         */
        class ListChecker {
        public:
            ListChecker(const ArenaVector<Instruction>& code, const ArenaVector<RegisterImage>& constants, uint8_t registerCount) : _code(code), _constants(constants), _registerCount(registerCount) {}

            /// The reason instruction @p i is no valid list or loop instruction, or nothing; @p reads gets the level a `ListSlot` reads
            [[nodiscard]] Optional<String> Check(size_t i, LevelMask& reads) {
                const Instruction& ins = _code[i];
                if (IsLoadElem(ins.op)) {
                    if (const Optional<String> bad = Register(ins.a)) {
                        return bad;
                    }
                    if (ins.op == Opcode::LoadElemDomain && (ins.b == 0 || ins.b > kProgramRegisterBytes)) {
                        return Format("loads a registered type of {} bytes, but a register holds 1 to {}", ins.b, kProgramRegisterBytes);
                    }
                    if (ins.op == Opcode::LoadElemEnum && !IsEnumFieldBytes(ins.b)) {
                        return Format("loads an enum of {} bytes, but an enum field is 1, 2, 4 or 8", ins.b);
                    }
                }
                if (ins.op != Opcode::LoopEnd && ins.op != Opcode::BreakIf) {
                    if (const Optional<String> bad = WritesLoopRegister(ins)) {
                        return bad;
                    }
                }
                switch (ins.op) {
                    case Opcode::ListSlot:
                        if (ins.a >= kMaxContextLevels) {
                            return Format("reads level {}, but a context has {}", ins.a, kMaxContextLevels);
                        }
                        reads |= LevelBit(ins.a);
                        return Descriptor(ins);
                    case Opcode::ListElem:
                        if (const Optional<String> bad = Register(ins.a)) {
                            return bad;
                        }
                        return Descriptor(ins);
                    case Opcode::ListSize:
                        return Register(ins.a);
                    case Opcode::ListAt:
                        if (const Optional<String> bad = Register(ins.a)) {
                            return bad;
                        }
                        return Register(ins.b);
                    case Opcode::LoopBegin:
                        return Begin(i);
                    case Opcode::LoopEnd:
                        return End(i);
                    case Opcode::BreakIf:
                        return Break(ins);
                    default:
                        return std::nullopt;
                }
            }

            /// The reason the code ends with a loop still open, or nothing
            [[nodiscard]] Optional<String> CheckClosed() const {
                if (!_open.empty()) {
                    return Format("the loop begun at instruction {} is never closed", _open.back());
                }
                return std::nullopt;
            }

        private:
            [[nodiscard]] Optional<String> Register(uint8_t reg) const {
                if (reg >= _registerCount) {
                    return Format("reads r{}, which was never acquired", reg);
                }
                return std::nullopt;
            }

            [[nodiscard]] Optional<String> Descriptor(const Instruction& ins) const {
                if (ins.imm >= _constants.size()) {
                    return Format("reads the list descriptor in constant {}, but there are {}", ins.imm, _constants.size());
                }
                const ListDescriptor descriptor = ListDescriptor::FromImage(_constants[ins.imm]);
                if (!IsValidListLayout(descriptor.layout)) {
                    return Format("constant {} is no list descriptor: layout {} is not one of the {}", ins.imm, static_cast<uint32_t>(descriptor.layout), EnumUtils<ListLayout>::Count());
                }
                if (descriptor.stride == 0) {
                    return Format("constant {} is no list descriptor: its stride is 0", ins.imm);
                }
                return std::nullopt;
            }

            /// A body may not write the element or the list register of a loop it is in: the loop's own counter and list are what `LoopEnd` goes on from
            [[nodiscard]] Optional<String> WritesLoopRegister(const Instruction& ins) const {
                for (const size_t begin : _open) {
                    const Instruction& loop = _code[begin];
                    if (ins.dst == loop.dst || ins.dst == loop.a) {
                        return Format("writes r{}, the {} register of the loop begun at instruction {}", ins.dst, ins.dst == loop.dst ? "element" : "list", begin);
                    }
                }
                return std::nullopt;
            }

            [[nodiscard]] Optional<String> Begin(size_t i) {
                const Instruction& ins = _code[i];
                if (const Optional<String> bad = Register(ins.a)) {
                    return bad;
                }
                if (ins.dst == ins.a) {
                    return String("the element and the list of a loop are one register");
                }
                const size_t after = ins.imm;
                if (after < i + 2 || after > _code.size()) {
                    return Format("jumps to instruction {}, which is not behind the loop: its LoopEnd must come after it, inside the {} instructions of the code", after, _code.size());
                }
                const Instruction& end = _code[after - 1];
                if (end.op != Opcode::LoopEnd || end.imm != i + 1 || end.dst != ins.dst || end.a != ins.a) {
                    return Format("jumps past instruction {}, which is not the LoopEnd that goes back to it with the same registers", after - 1);
                }
                _open.push_back(i);
                return std::nullopt;
            }

            [[nodiscard]] Optional<String> End(size_t i) {
                const Instruction& ins = _code[i];
                if (_open.empty()) {
                    return String("closes no loop");
                }
                const size_t begin = _open.back();
                const Instruction& loop = _code[begin];
                if (loop.imm != i + 1 || ins.imm != begin + 1 || ins.dst != loop.dst || ins.a != loop.a) {
                    return Format("is not the end of the innermost open loop, begun at instruction {}: loops must nest", begin);
                }
                _open.pop_back();
                return WritesLoopRegister(ins);
            }

            [[nodiscard]] Optional<String> Break(const Instruction& ins) const {
                if (_open.empty()) {
                    return String("leaves no loop: it is outside of one");
                }
                if (ins.b > 1) {
                    return Format("breaks on {}, which is neither 0 nor 1", ins.b);
                }
                if (const Optional<String> bad = Register(ins.a)) {
                    return bad;
                }
                const Instruction& loop = _code[_open.back()];
                if (ins.imm != loop.imm) {
                    return Format("jumps to instruction {}, but the loop it is in ends before {}", ins.imm, loop.imm);
                }
                return std::nullopt;
            }

            const ArenaVector<Instruction>& _code;
            const ArenaVector<RegisterImage>& _constants;
            uint8_t _registerCount;
            ArenaVector<size_t> _open;
        };

    }  // namespace

    std::expected<uint8_t, ProgramError> ProgramBuilder::AcquireRegister() {
        if (_live >= kMaxProgramRegisters) {
            return std::unexpected(ProgramError{Format("the formula needs more than {} registers at once: split it or nest it less", kMaxProgramRegisters), 0});
        }
        const uint8_t reg = _live++;
        _highWater = std::max(_highWater, _live);
        return reg;
    }

    void ProgramBuilder::ReleaseRegister(uint8_t reg) {
        ASSERT(_live > 0 && reg + 1 == _live, "ProgramBuilder: registers are released in the reverse order they were acquired");
        --_live;
    }

    uint32_t ProgramBuilder::AddConstant(const RegisterImage& image) {
        const auto found = std::find(_constants.begin(), _constants.end(), image);
        if (found != _constants.end()) {
            return static_cast<uint32_t>(found - _constants.begin());
        }
        _constants.push_back(image);
        return static_cast<uint32_t>(_constants.size() - 1);
    }

    uint32_t ProgramBuilder::AddConstantRun(Span<const RegisterImage> images) {
        const auto first = static_cast<uint32_t>(_constants.size());
        _constants.insert(_constants.end(), images.begin(), images.end());
        return first;
    }

    void ProgramBuilder::Emit(const Instruction& instruction) {
        _code.push_back(instruction);
    }

    size_t ProgramBuilder::EmitLoopBegin(uint8_t element, uint8_t list) {
        Emit(Opcode::LoopBegin, element, list, 0, 0, kUnpatchedJump);
        return _code.size() - 1;
    }

    void ProgramBuilder::EmitLoopEnd(size_t begin) {
        ASSERT(begin < _code.size() && _code[begin].op == Opcode::LoopBegin, "EmitLoopEnd: not the index of a LoopBegin");
        const Instruction loop = _code[begin];
        Emit(Opcode::LoopEnd, loop.dst, loop.a, 0, 0, static_cast<uint32_t>(begin + 1));
        const auto after = static_cast<uint32_t>(_code.size());
        _code[begin].imm = after;
        // The breaks of inner loops were patched when those closed, so the ones still waiting are this loop's
        for (size_t i = begin + 1; i + 1 < _code.size(); ++i) {
            if (_code[i].op == Opcode::BreakIf && _code[i].imm == kUnpatchedJump) {
                _code[i].imm = after;
            }
        }
    }

    void ProgramBuilder::EmitBreakIf(uint8_t reg, bool value) {
        Emit(Opcode::BreakIf, 0, reg, value ? 1 : 0, 0, kUnpatchedJump);
    }

    void ProgramBuilder::Fuse(uint8_t resultRegister) {
        BECore::Fuse(_code, resultRegister);
    }

    std::expected<Program, ProgramError> ProgramBuilder::Finish(ProgramType resultType, uint8_t resultRegister, PoolString source) const {
        const auto refuse = [](size_t index, const Instruction& ins, StringView what) {
            return std::unexpected(ProgramError{Format("instruction {} ({}): {}", index, EnumUtils<Opcode>::ToString(ins.op), what), 0});
        };
        if (_code.empty()) {
            return std::unexpected(ProgramError{"the program has no instruction", 0});
        }
        if (resultRegister >= _highWater) {
            return std::unexpected(ProgramError{Format("the result register r{} was never acquired", resultRegister), 0});
        }
        LevelMask reads = 0;
        ListChecker lists(_code, _constants, _highWater);
        for (size_t i = 0; i < _code.size(); ++i) {
            const Instruction& ins = _code[i];
            if (ins.op == Opcode::Invalid) {
                return refuse(i, ins, "not an operation");
            }
            if (ins.dst >= _highWater) {
                return refuse(i, ins, Format("writes r{}, which was never acquired", ins.dst));
            }
            const bool loadSlot = IsLoadSlot(ins.op);
            const bool callDomain = ins.op == Opcode::CallDomain;
            const bool tableLoad = ins.op == Opcode::LoadTable;
            const bool slotOnly = IsMulAddSlot(ins.op) || ins.op == Opcode::LoadTableSlot;
            const bool lerpSlots = IsLerpSlots(ins.op);
            const bool loadElem = IsLoadElem(ins.op);
            // The fused and table instructions keep counts, constant indexes and levels in a, b and c: only a field the opcode reads as a register is one
            // (a load of a slot has a level in a and a size in b, a load of an element's field the element in a and a size in b; a call has as many registers as the operation has
            // parameters, checked below)
            const bool aIsRegister = !slotOnly && !lerpSlots && !loadSlot && !callDomain;
            const bool bIsRegister = !slotOnly && !lerpSlots && !tableLoad && !loadSlot && !callDomain && !loadElem;
            const bool cIsRegister = !slotOnly && !tableLoad && !loadSlot && !callDomain && !loadElem;
            if ((aIsRegister && ins.a >= kMaxProgramRegisters) || (bIsRegister && ins.b >= kMaxProgramRegisters) || (cIsRegister && ins.c >= kMaxProgramRegisters)) {
                return refuse(i, ins, "an operand names a register beyond the last");
            }
            if (callDomain) {
                if (ins.imm >= ProgramDomain::OperationCount()) {
                    return refuse(i, ins, Format("calls operation {}, but {} are registered", ins.imm, ProgramDomain::OperationCount()));
                }
                const DomainOperation& operation = ProgramDomain::Operation(ins.imm);
                const Array<uint8_t, kMaxDomainArguments> arguments{ins.a, ins.b, ins.c};
                for (size_t argument = 0; argument < operation.parameterCount; ++argument) {
                    if (arguments[argument] >= _highWater) {
                        return refuse(i, ins, Format("'{}' reads r{}, which was never acquired", operation.name.ToStringView(), arguments[argument]));
                    }
                }
                reads |= operation.reads;
            }
            if (ins.op == Opcode::LoadConst && ins.imm >= _constants.size()) {
                return refuse(i, ins, Format("reads constant {}, but there are {}", ins.imm, _constants.size()));
            }
            if (ins.op == Opcode::LoadTable || ins.op == Opcode::LoadTableSlot) {
                const uint32_t count = TableEntryCount(ins);
                const uint32_t first = ins.op == Opcode::LoadTable ? ins.imm : HighHalf(ins.imm);
                if (count == 0) {
                    return refuse(i, ins, "reads a table with no entry");
                }
                if (count > kMaxTableEntries) {
                    return refuse(i, ins, Format("reads a table of {} entries, but a table has at most {}", count, kMaxTableEntries));
                }
                if (static_cast<size_t>(first) + count > _constants.size()) {
                    return refuse(i, ins, Format("reads constants {} to {}, but there are {}", first, static_cast<size_t>(first) + count - 1, _constants.size()));
                }
            }
            if (const Optional<ProgramType> componentType = ComponentOpcodeType(ins.op)) {
                const uint32_t count = ComponentsOf(*componentType).count;
                if (IsPack(ins.op) && ins.a + count > _highWater) {
                    return refuse(i, ins, Format("packs {} operands from r{}, but only {} registers were acquired", count, ins.a, _highWater));
                }
                if (!IsPack(ins.op) && ins.imm >= count) {
                    return refuse(i, ins, Format("takes component {} of a value that has {}", ins.imm, count));
                }
            }
            if (IsMulAddSlot(ins.op) && (ins.b >= _constants.size() || ins.c >= _constants.size())) {
                return refuse(i, ins, Format("reads constants {} and {}, but there are {}", ins.b, ins.c, _constants.size()));
            }
            if (lerpSlots || IsMulAddSlot(ins.op) || ins.op == Opcode::LoadTableSlot) {
                if (ins.a >= kMaxContextLevels || (lerpSlots && ins.b >= kMaxContextLevels)) {
                    return refuse(i, ins, Format("reads a level beyond the {} of a context", kMaxContextLevels));
                }
                reads |= LevelBit(ins.a);
                if (lerpSlots) {
                    reads |= LevelBit(ins.b);
                }
            }
            if (const Optional<String> bad = lists.Check(i, reads)) {
                return refuse(i, ins, *bad);
            }
            if (IsLoadSlot(ins.op)) {
                if (ins.a >= kMaxContextLevels) {
                    return refuse(i, ins, Format("reads level {}, but a context has {}", ins.a, kMaxContextLevels));
                }
                if (ins.op == Opcode::LoadSlotDomain && (ins.b == 0 || ins.b > kProgramRegisterBytes)) {
                    return refuse(i, ins, Format("loads a registered type of {} bytes, but a register holds 1 to {}", ins.b, kProgramRegisterBytes));
                }
                if (ins.op == Opcode::LoadSlotEnum && !IsEnumFieldBytes(ins.b)) {
                    return refuse(i, ins, Format("loads an enum of {} bytes, but an enum field is 1, 2, 4 or 8", ins.b));
                }
                reads |= LevelBit(ins.a);
            }
        }
        if (const Optional<String> bad = lists.CheckClosed()) {
            return std::unexpected(ProgramError{*bad, 0});
        }
        Program program;
        program._resultType = resultType;
        program._resultRegister = resultRegister;
        program._registerCount = _highWater;
        program._reads = reads;
        program._code.assign(_code.begin(), _code.end());
        program._constants.assign(_constants.begin(), _constants.end());
        program._source = source;
        return program;
    }

}  // namespace BECore
