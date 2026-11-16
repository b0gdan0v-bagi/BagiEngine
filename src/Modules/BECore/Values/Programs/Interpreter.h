#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Opcode.h>
#include <BECore/Values/Programs/Program.h>

namespace BECore {

    /**
     * @brief Runs @p program over the objects of @p context and writes its result, `ProgramTypeSize(program.ResultType())` bytes, to @p result
     *
     * The registers are 16-byte aligned objects on the stack of this call, the dispatch is one `switch`: nothing is allocated, no container or
     * `String` is made and no virtual function is called. The program must come from `ProgramBuilder::Finish`; every level it reads must have an
     * object in the context (`ASSERT`, as `Value::Get`).
     */
    void Run(const Program& program, const Context& context, void* result);

    /// `Run` for a result of the C++ type @p T, which must be the program's result type; for an enum the result is the enumerator of the index (the program does not know which enum it is)
    template <ProgramValue T>
    [[nodiscard]] T Run(const Program& program, const Context& context) {
        ASSERT(program.ResultType() == kProgramTypeOf<T>, "Run: the program does not compute this type");
        if constexpr (IsReflectedEnum<T>) {
            ProgramEnum result;
            Run(program, context, &result);
            return static_cast<T>(result.index);
        } else {
            T result{};
            Run(program, context, &result);
            return result;
        }
    }

}  // namespace BECore
