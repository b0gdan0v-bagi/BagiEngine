#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/Values/Programs/Lists/ListImage.h>
#include <BECore/Values/Programs/Program.h>
#include <cstdint>
#include <cstring>
#include <expected>
#include <type_traits>

namespace BECore {

    /**
     * @brief Assembles a `Program`: the registers, the constants and the instructions
     *
     * The lowering of a formula and the tests of the interpreter both use it. Registers are a stack: `AcquireRegister` gives the next one and
     * `ReleaseRegister` takes back the last one given, which is what a post-order walk of a tree needs (the operands of a node are the two
     * top registers, the result replaces the first). `Finish` checks the code and works out `Reads` from the `LoadSlot` instructions.
     */
    class ProgramBuilder {
    public:
        /// The next free register; an error when a program needs more than `kMaxProgramRegisters` at once
        [[nodiscard]] std::expected<uint8_t, ProgramError> AcquireRegister();

        /// Gives back @p reg, which must be the register acquired last and not yet released
        void ReleaseRegister(uint8_t reg);

        /// The index of the constant in the program's table; a value that is there already is not added again
        [[nodiscard]] uint32_t AddConstant(const RegisterImage& image);

        /// The index of the first of @p images, which are appended as they are: a table must be consecutive, so the run is not looked up among the constants and its entries are not shared
        [[nodiscard]] uint32_t AddConstantRun(Span<const RegisterImage> images);

        /// The constant of a value of a program type, zero-padded to a register; an enum is its index
        template <ProgramValue T>
        [[nodiscard]] uint32_t AddConstant(T value) {
            RegisterImage image{};
            if constexpr (IsReflectedEnum<T>) {
                const ProgramEnum index{static_cast<uint32_t>(value)};
                std::memcpy(image.bytes.data(), &index, sizeof(index));
            } else {
                std::memcpy(image.bytes.data(), &value, sizeof(T));
            }
            return AddConstant(image);
        }

        void Emit(const Instruction& instruction);

        void Emit(Opcode op, uint8_t dst, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint32_t imm = 0) {
            Emit(Instruction{op, dst, a, b, c, imm});
        }

        template <ProgramValue T>
        void EmitLoadConst(uint8_t dst, T value) {
            Emit(Opcode::LoadConst, dst, 0, 0, 0, AddConstant(value));
        }

        /// @p enumBytes is the size of the field (`TypeDesc::enumBytes`) and is for a `ProgramType::Enum` only: the load needs the width of the underlying type
        void EmitLoadSlot(uint8_t dst, ProgramType type, SlotRef slot, uint8_t enumBytes = 0) {
            ASSERT((type == ProgramType::Enum) == IsEnumFieldBytes(enumBytes), "EmitLoadSlot: an enum load names the size of its field, no other load does");
            Emit(LoadSlotOpcode(type), dst, slot.level, enumBytes, 0, slot.offset);
        }

        /// @p dst takes entry `index` of the @p count constants from @p first, the `int32_t` index being in register @p indexReg; `Finish` refuses an empty table, a run that is not in the pool and a
        /// table over `kMaxTableEntries`
        void EmitLoadTable(uint8_t dst, uint8_t indexReg, uint32_t first, uint32_t count) {
            Instruction ins{Opcode::LoadTable, dst, indexReg, 0, 0, first};
            SetTableEntryCount(ins, count);
            Emit(ins);
        }

        /// @p dst is the list in the field @p list names (`MakeListDescriptor`), in the object of context level @p level. The descriptor is a constant of the program; `Finish` refuses one that is no
        /// descriptor. The list is read where it is and must not change while the program runs
        void EmitListSlot(uint8_t dst, LevelIndex level, const ListDescriptor& list) {
            Emit(Opcode::ListSlot, dst, level, 0, 0, AddConstant(list.ToImage()));
        }

        /// @p dst is the list in the field @p list names, in the element in register @p element; a missing element has an empty list
        void EmitListElem(uint8_t dst, uint8_t element, const ListDescriptor& list) {
            Emit(Opcode::ListElem, dst, element, 0, 0, AddConstant(list.ToImage()));
        }

        /// @p dst takes the count of the list in register @p list, an `int32_t`
        void EmitListSize(uint8_t dst, uint8_t list) {
            Emit(Opcode::ListSize, dst, list);
        }

        /// @p dst takes element `index` of the list in register @p list, the `int32_t` index being in register @p indexReg; an index outside the list gives the missing element
        void EmitListAt(uint8_t dst, uint8_t list, uint8_t indexReg) {
            Emit(Opcode::ListAt, dst, list, indexReg);
        }

        /// @p dst takes the field at @p offset of the element in register @p element, of the type @p type; a missing element gives the default value. @p enumBytes as for `EmitLoadSlot`
        void EmitLoadElem(uint8_t dst, ProgramType type, uint8_t element, uint32_t offset, uint8_t enumBytes = 0) {
            ASSERT((type == ProgramType::Enum) == IsEnumFieldBytes(enumBytes), "EmitLoadElem: an enum load names the size of its field, no other load does");
            Emit(LoadElemOpcode(type), dst, element, enumBytes, 0, offset);
        }

        /// Opens a loop: @p element runs over the elements of the list in register @p list, and the instructions emitted until `EmitLoopEnd` are its body. Returns the index of the instruction,
        /// which `EmitLoopEnd` takes. A loop over an empty list skips the body, so the registers of the body hold nothing then
        [[nodiscard]] size_t EmitLoopBegin(uint8_t element, uint8_t list);

        /// Closes the loop opened at @p begin and points the jump of its `LoopBegin` and every `BreakIf` of its body (not of an inner loop, which `EmitLoopEnd` closed) past it
        void EmitLoopEnd(size_t begin);

        /// Leaves the innermost open loop when the `bool` in register @p reg equals @p value: `any` breaks on true and `all` on false. Its target is written by `EmitLoopEnd`
        void EmitBreakIf(uint8_t reg, bool value);

        /// Runs the peephole pass (`Peephole.h`) over the code so far; @p resultRegister is the register the program will give back, which no fusion may stop writing. Call it after the
        /// last `Emit` and before `Finish`
        void Fuse(uint8_t resultRegister);

        /// The program, with the result in @p resultRegister; an error names the instruction that is not valid (an operand beyond the registers, a constant that is not there)
        [[nodiscard]] std::expected<Program, ProgramError> Finish(ProgramType resultType, uint8_t resultRegister, PoolString source = {}) const;

        [[nodiscard]] size_t InstructionCount() const {
            return _code.size();
        }

    private:
        Vector<Instruction> _code;
        Vector<RegisterImage> _constants;
        uint8_t _live = 0;
        uint8_t _highWater = 0;
    };

}  // namespace BECore
