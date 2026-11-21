#pragma once

#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Instruction.h>
#include <BECore/Values/Programs/Kernels.h>
#include <BECore/Values/Programs/Lists/ListImage.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

/**
 * The kernels of the list and loop instructions. Like the rows of `Kernels.h` they are shared by the interpreter and the generated code, so the two cannot differ: the three that decide a
 * jump (`ExecuteLoopBegin`, `ExecuteLoopEnd`, `ExecuteBreakIf`) return it and the caller only branches. Every one is total: an index outside a list, a missing element and an empty list are
 * answers, never a read outside the list.
 */
namespace BECore::Kernel {

    /// The list of descriptor @p descriptor that is a field of the object at @p object. A `Pointers` list reads the begin and the end pointer from the object by `memcpy` (no aliasing trap) and
    /// counts `(end - begin) / stride`, cut to 32 bits; an end before the begin, which no container makes, is an empty list
    [[nodiscard]] inline ListImage ReadList(const uint8_t* object, const ListDescriptor& descriptor) {
        const uint8_t* field = object + descriptor.offset;
        if (descriptor.layout == ListLayout::Inline) {
            return ListImage{field, descriptor.count, descriptor.stride};
        }
        const uint8_t* begin = nullptr;
        const uint8_t* end = nullptr;
        std::memcpy(&begin, field, sizeof(begin));
        std::memcpy(&end, field + sizeof(begin), sizeof(end));
        const auto first = reinterpret_cast<uintptr_t>(begin);
        const auto last = reinterpret_cast<uintptr_t>(end);
        uint64_t count = 0;
        if (last > first && descriptor.stride > 0) {
            count = std::min<uint64_t>((last - first) / descriptor.stride, std::numeric_limits<uint32_t>::max());
        }
        return ListImage{begin, static_cast<uint32_t>(count), descriptor.stride};
    }

    [[nodiscard]] inline ElementImage ElementAt(const ListImage& list, uint32_t index) {
        return ElementImage{list.data + static_cast<size_t>(index) * list.stride, index, 0};
    }

    /// Runs `ListSlot`: `dst` is the list in the field the descriptor constant `imm` names, in the object of context level `a`
    inline void ExecuteListSlot(RegisterImage* regs, const Instruction& ins, Span<const RegisterImage> constants, const Context& context) {
        ASSERT(ins.imm < constants.size());
        const void* object = context.Object(ins.a);
        ASSERT(object != nullptr, "Program: the context has no object at the level the program reads from");
        Store<ListImage>(regs[ins.dst], ReadList(static_cast<const uint8_t*>(object), ListDescriptor::FromImage(constants[ins.imm])));
    }

    /// Runs `ListElem`: `dst` is the list in the field of the element in register `a`; a missing element has an empty list, so a nested loop over it does not run
    inline void ExecuteListElem(RegisterImage* regs, const Instruction& ins, Span<const RegisterImage> constants) {
        ASSERT(ins.imm < constants.size());
        const ListDescriptor descriptor = ListDescriptor::FromImage(constants[ins.imm]);
        const auto element = Load<ElementImage>(regs[ins.a]);
        Store<ListImage>(regs[ins.dst], element.ptr != nullptr ? ReadList(element.ptr, descriptor) : ListImage{nullptr, 0, descriptor.stride});
    }

    /// Runs `ListSize`: the count of the list in register `a` as an `int32_t`, cut to its largest value
    inline void ExecuteListSize(RegisterImage* regs, const Instruction& ins) {
        const auto list = Load<ListImage>(regs[ins.a]);
        Store<int32_t>(regs[ins.dst], static_cast<int32_t>(std::min<uint32_t>(list.count, static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))));
    }

    /// Runs `ListAt`: `dst` is element `index` (the `int32_t` in register `b`) of the list in register `a`; an index outside `[0, count)` gives the missing element
    inline void ExecuteListAt(RegisterImage* regs, const Instruction& ins) {
        const auto list = Load<ListImage>(regs[ins.a]);
        const int32_t index = Load<int32_t>(regs[ins.b]);
        const bool inside = index >= 0 && static_cast<uint32_t>(index) < list.count;
        Store<ElementImage>(regs[ins.dst], inside ? ElementAt(list, static_cast<uint32_t>(index)) : ElementImage{});
    }

    /// Runs `LoopBegin`: `dst` becomes element 0 of the list in register `a`. True (jump to the instruction after the loop) when the list is empty, and then `dst` is the missing element
    [[nodiscard]] inline bool ExecuteLoopBegin(RegisterImage* regs, const Instruction& ins) {
        const auto list = Load<ListImage>(regs[ins.a]);
        if (list.count == 0) {
            Store<ElementImage>(regs[ins.dst], ElementImage{});
            return true;
        }
        Store<ElementImage>(regs[ins.dst], ElementAt(list, 0));
        return false;
    }

    /// Runs `LoopEnd`: `dst` becomes the next element of the list in register `a`. True (jump back to the start of the body) while there is one
    [[nodiscard]] inline bool ExecuteLoopEnd(RegisterImage* regs, const Instruction& ins) {
        const auto list = Load<ListImage>(regs[ins.a]);
        const auto element = Load<ElementImage>(regs[ins.dst]);
        const uint32_t next = element.index + 1;
        if (next >= list.count) {
            return false;
        }
        Store<ElementImage>(regs[ins.dst], ElementAt(list, next));
        return true;
    }

    /// Runs `BreakIf`: true (jump out of the loop) when the `bool` in register `a` equals `b` (0 or 1)
    [[nodiscard]] inline bool ExecuteBreakIf(const RegisterImage* regs, const Instruction& ins) {
        return Load<bool>(regs[ins.a]) == (ins.b != 0);
    }

}  // namespace BECore::Kernel
