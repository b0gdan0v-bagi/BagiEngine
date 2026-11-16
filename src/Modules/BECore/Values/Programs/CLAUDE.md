# Programs: computed values compiled into typed code

A formula of a content file (`expr="caster.attack * 2"`, later) becomes a `Program`: straight-line typed instructions over registers, run by a `switch` interpreter. This folder holds the instruction set, the program and its pool, the builder and the interpreter; the text front end and the compiler come with later work of the entry `data-values-programs` (`Values/CLAUDE.md` is the value and the context).

## Files

| File | Holds |
|------|-------|
| `Opcode.h` | the X-macro lists (`BE_PROGRAM_TYPES`, `BE_PROGRAM_ROWS`, `BE_PROGRAM_CONVERSIONS`), `ProgramType`, `Opcode`, `ProgramScalar` |
| `Instruction.h` | `Instruction` (12 bytes: opcode, `dst`, `a`, `b`, `c`, a 32-bit immediate), `RegisterImage` |
| `Kernels.h` | one struct per operation with its rule (`Apply`) and the types that have it (`kSupports`), `Execute`, `Convert` |
| `Program.h/.cpp` | `Program` (immutable), `ProgramPool`, `ProgramError` |
| `ProgramBuilder.h/.cpp` | registers, constants, `Emit`, `Finish`; what the compiler and the tests assemble programs with |
| `Interpreter.h/.cpp` | `Run`: the only `switch` |

## The frame and the pool

Registers are `alignas(16)` objects of 16 bytes (the biggest value a program holds later is `Vec4` / `Quat`), at most 32, on the stack of `Run` and left uninitialized: an instruction reads only a register an earlier one wrote as that type. The builder hands them out like a stack (`AcquireRegister` / `ReleaseRegister`, the registers of a node's operands are the top ones), so a post-order walk of a tree needs few. A program that would need more is an error from `AcquireRegister`. **`Run` allocates nothing by construction**: it makes only stack objects and calls no container, `String` or virtual function. A program is pure and total: it writes nothing, and `select`, `&&` and `||` have computed both operands (no control flow).

`ProgramPool` is process-wide and append-only, like `PoolString`: it interns by the program's code (result type and register, registers used, `reads`, instructions, constants), never by its source text, so equal formulas written differently are one program, and `Get(id)` takes no lock and returns an address that never moves (chunks that are published before an id in them is given out). `Program::Source()` is the text of the first formula that made the entry. The pool refuses the 262144th distinct program (`Intern` returns `nullopt`) so hostile content cannot grow it without bound. `Reads()` is derived from the `LoadSlot` instructions by `Finish`: it is the mask of the levels the code loads from.

## One list, one meaning

`Opcode.h` lists every typed instruction once (`BE_PROGRAM_ROWS`: operation x type, by groups of types), and the opcode enum, the interpreter's `switch`, `kHasRow` and the matrix of `ProgramInterpreterTest` are all expanded from it. An operation has a type when its C++ operator or function does (a concept in the struct, `Add<bool>` has none); `Kernels.h` ends with a `static_assert` that the rows and the concepts agree for every operation and every type, so a type that gains an operator, or a row that names an operation a type lacks, is a compile error. A new type is a row of `BE_PROGRAM_TYPES`, a value of `ProgramType` and rows in the groups.

## Meaning of the operations

Every row has a test against a plain C++ reference (`ProgramInterpreterTest`). Operations never have undefined behaviour:

- Integer `+ - *` and `-` wrap (two's complement); `abs` of the minimum is the minimum.
- Integer `/` and `%` by zero give 0; `INT_MIN / -1` is `INT_MIN` and `INT_MIN % -1` is 0. A literal zero divisor is a load error (the checker's, not the interpreter's). `%` is for integers only.
- Floats follow IEEE (`1 / 0` is infinity, `sqrt(-1)` is NaN, a comparison with NaN is false except `!=`).
- Comparisons, `&&`, `||`, `!` give `bool`; ordering is for numbers only, logic for `bool` only.
- `min`, `max`: a NaN operand gives the other. `clamp(x, lo, hi)` is `min(max(x, lo), hi)` and `lo` when `lo > hi` (never `std::clamp`'s undefined case).
- `lerp` is `std::lerp` (exact at 0 and 1, `t` not clamped), floats only. `round` goes away from zero on halves.
- Conversions between the four numbers: integer to integer keeps the low bits; integer to float rounds to nearest; float to integer truncates toward zero, saturates at the range and gives 0 for NaN; `double` to `float` rounds to nearest and gives infinity beyond the range.
- `Select` takes the condition in its first operand register, then the two values.

A constant is the 16-byte image of the value, zero-padded, so equal constants are equal bytes and the builder keeps one entry for them.
