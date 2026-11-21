#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Domain/ProgramDomain.h>
#include <BECore/Values/Programs/Opcode.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramTraceSink.h>
#include <cstring>

namespace BECore {

    /**
     * @brief Runs @p program over the objects of @p context and writes its result, `ProgramTypeSize(program.ResultType())` bytes, to @p result
     *
     * The registers are 32-byte aligned objects on the stack of this call, the dispatch is one `switch`: nothing is allocated, no container or
     * `String` is made and no virtual function is called. The program must come from `ProgramBuilder::Finish`, which proves that every jump stays inside the code and that a program with loops ends;
     * every level it reads must have an object in the context (`ASSERT`, as `Value::Get`).
     */
    void Run(const Program& program, const Context& context, void* result);

    /**
     * @brief `Run` that tells @p sink about every instruction it executes, in execution order (`ProgramTraceSink.h`); the result is bit for bit that of `Run`
     *
     * Always the interpreter, even for a program a generated function is bound to (`Native/`): the two give the same bits and the generated code has no hook. `Run` is the same loop
     * with no observer, so it pays nothing for this. Allocates only what the sink does.
     */
    void RunTraced(const Program& program, const Context& context, void* result, ProgramTraceSink& sink);

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

    /**
     * @brief `Run<T>` of the program of @p id: the generated function when one is bound to it (`Native/NativePrograms.h`), else the interpreter
     *
     * What `Value::Evaluate` calls. The two give the same bits (the tests of the generated set prove it), so the caller cannot tell which ran.
     */
    template <ProgramValue T>
    [[nodiscard]] T Run(ProgramId id, const Context& context) {
        const NativeProgramFn native = ProgramPool::Native(id);
        if (native == nullptr) {
            return Run<T>(ProgramPool::Get(id), context);
        }
        ASSERT(ProgramPool::Get(id).ResultType() == kProgramTypeOf<T>, "Run: the program does not compute this type");
        if constexpr (IsReflectedEnum<T>) {
            ProgramEnum result;
            native(context, &result);
            return static_cast<T>(result.index);
        } else {
            T result{};
            native(context, &result);
            return result;
        }
    }

    /**
     * @brief `Run` for a result of a type a game registered (`ProgramDomain`): the program's result is a register of type `Domain` and the first `sizeof(T)` bytes of it are the value
     *
     * The generated function when one is bound, else the interpreter, as `Run<T>(id, ...)`. A program does not know which registered type it computes (the checker did), so
     * @p T must be the type of the field the program was compiled for; the `ASSERT` checks that it is a registered type.
     */
    template <DomainValueClass T>
    [[nodiscard]] T RunDomain(ProgramId id, const Context& context) {
        const Program& program = ProgramPool::Get(id);
        ASSERT(program.ResultType() == ProgramType::Domain, "RunDomain: the program does not compute a registered type");
        ASSERT(ProgramDomain::FindTypeByDescriptor(DescribeType<T>()).has_value(), "RunDomain: T is not a registered type");
        // The result is a whole register whatever the registered type's size, so it lands in a register of its own and `T` takes the bytes it has
        RegisterImage result{};
        const NativeProgramFn native = ProgramPool::Native(id);
        if (native == nullptr) {
            Run(program, context, result.bytes.data());
        } else {
            native(context, result.bytes.data());
        }
        T value{};
        std::memcpy(&value, result.bytes.data(), sizeof(T));
        return value;
    }

}  // namespace BECore
