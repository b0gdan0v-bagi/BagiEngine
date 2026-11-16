# Programs: computed values compiled into typed code

A formula of a content file (`expr="caster.attack * 2"`, later) becomes a `Program`: straight-line typed instructions over registers, run by a `switch` interpreter. This folder holds the instruction set, the program and its pool, the builder and the interpreter; the text front end, the type checker and the compiler are below (`Values/CLAUDE.md` is the value and the context).

## Files

| File | Holds |
|------|-------|
| `Opcode.h` | the X-macro lists (`BE_PROGRAM_TYPES`, `BE_PROGRAM_ROWS`, `BE_PROGRAM_CONVERSIONS`), `ProgramType`, `Opcode`, `ProgramScalar` |
| `Instruction.h` | `Instruction` (12 bytes: opcode, `dst`, `a`, `b`, `c`, a 32-bit immediate), `RegisterImage` |
| `Kernels.h` | one struct per operation with its rule (`Apply`) and the types that have it (`kSupports`), `Execute`, `Convert` |
| `Program.h/.cpp` | `Program` (immutable), `ProgramPool`, `ProgramError` |
| `ProgramBuilder.h/.cpp` | registers, constants, `Emit`, `Finish`; what the compiler and the tests assemble programs with |
| `Interpreter.h/.cpp` | `Run`: the only `switch` |
| `ExprAst.h/.cpp` | the untyped tree (`ExprNode` arena), `ExprLimits`, `PrintExpression` |
| `ExprLexer.h/.cpp`, `ExprParser.h/.cpp` | text to tokens, tokens to the tree |
| `ExprChecker.h/.cpp`, `ExprFunctions.h` | the tree to a typed tree (`TypedExpr`); the table of functions |
| `ProgramCompiler.h/.cpp` | `CompileExpression`, `CompileValue<T>`, `FormatProgramError` |

## The frame and the pool

Registers are `alignas(16)` objects of 16 bytes (the biggest value a program holds later is `Vec4` / `Quat`), at most 32, on the stack of `Run` and left uninitialized: an instruction reads only a register an earlier one wrote as that type. The builder hands them out like a stack (`AcquireRegister` / `ReleaseRegister`, the registers of a node's operands are the top ones), so a post-order walk of a tree needs few. A program that would need more is an error from `AcquireRegister`. **`Run` allocates nothing by construction**: it makes only stack objects and calls no container, `String` or virtual function. A program is pure and total: it writes nothing, and `select`, `&&` and `||` have computed both operands (no control flow).

`ProgramPool` is process-wide and append-only, like `PoolString`: it interns by the program's code (result type and register, registers used, `reads`, instructions, constants), never by its source text, so equal formulas written differently are one program, and `Get(id)` takes no lock and returns an address that never moves (chunks that are published before an id in them is given out). `Program::Source()` is the text of the first formula that made the entry. The pool refuses the 262144th distinct program (`Intern` returns `nullopt`) so hostile content cannot grow it without bound. `Reads()` is derived from the `LoadSlot` instructions by `Finish`: it is the mask of the levels the code loads from.

## One list, one meaning

`Opcode.h` lists every typed instruction once (`BE_PROGRAM_ROWS`: operation x type, by groups of types), and the opcode enum, the interpreter's `switch`, `kHasRow` and the matrix of `ProgramInterpreterTest` are all expanded from it. An operation has a type when its C++ operator or function does (a concept in the struct, `Add<bool>` has none); `Kernels.h` ends with a `static_assert` that the rows and the concepts agree for every operation and every type, so a type that gains an operator, or a row that names an operation a type lacks, is a compile error. `ProgramOperation` names the operations apart from the type and `OpcodeOf(operation, type)` is the row lookup (`Invalid` when the type has none), so the checker keeps no table of its own. A new type is a row of `BE_PROGRAM_TYPES`, a value of `ProgramType` and rows in the groups.

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

## The text of a formula

Loosest first: `||`; `&&`; `== !=`; `< <= > >=`; `+ -`; `* / %`; unary `- !`; postfix call and member. Binary operators group to the left; parentheses leave no node. Numbers are `12`, `1.5`, `.5`, `1e3` with no suffix and no sign (a sign is the unary operator, so a literal never carries one); `true` and `false` are literals. `a.b.c` is one `Name`, `a.b(c)` a `Call` of the name `a.b` (the checker says why that is no function), `.x` after anything that is not a bare name (`f(x).y`, `(a).b`) is a `Member`. `[` is refused by the lexer: a path must name a fixed field. Only ASCII is part of the language; any other byte is an error that names it (an invalid UTF-8 byte by its hex value, never copied into the message).

`PrintExpression` is the canonical text: one space around a binary operator, `, ` between arguments, every binary operand that is itself a binary in parentheses, numbers as written; parsing it gives the same tree. Limits against hostile content, each an error that names it: 4096 bytes of text, 64 levels of nesting (parentheses, call arguments, chained unary operators), 512 nodes. An error is `ProgramError{message, column}`, the column the 1-based byte of the token or node; the node of a binary or unary operator is at its operator.

## The type rules

`CheckExpression(ast, declaration, expected)` gives a `TypedExpr`: post-order nodes, each of a program type and a `Constant` (the 16-byte register image), a `Slot` (`SlotRef` and the path as written) or an `Operation` (the final opcode, a conversion included). The opcode comes from `OpcodeOf(operation, type)`: **an operation exists for a type exactly when `BE_PROGRAM_ROWS` has its row**, and the error of a missing one lists the types that have it, so a type that gains a row needs no change in the checker.

- **No implicit conversion.** Operands of `+ - * / %`, comparisons, `min`, `max`, `clamp`, `lerp` and the two values of `select` have one type; two types are an error at the second operand that names both and the hint `float(x)` / `int(x)`. The conversions are the functions `int`, `int64`, `float`, `double` (`int` is `int32_t`); a conversion to the type the operand has adds no node. `&&`, `||`, `!` and the condition of `select` need `bool`. The result is exactly the type the field has (`SameType` of the descriptors): there is no widening of the whole formula either.
- **Literals** have no type of their own: an integer literal becomes the type of the operand beside it, else the expected type when that is a number, else `int` (`int64` when it does not fit); a float literal is `float` or `double` and an error where an integer is needed. The operands that read the context are checked first, so a literal beside one takes its type whichever side it is on; a subtree of literals beside no such operand takes the expected number type, else `float` when it holds a float literal. A sign before a literal is part of the literal (`-2147483648` is an `int`). An integer or float that does not fit its type is an error. The argument of a conversion that is a literal is read as what it spells (a float literal as `double`, so `double(0.1)` is exact).
- **Paths** are linked with `LinkSlot` for each program type in turn, so its messages (`did you mean`, the fields of the type, the declared levels) are the ones shown; a field of any other type, and a bare level or function name, say what they are. A bare unknown name lists the levels and the functions. `.x` on an expression is an error until a type has components.
- A division or modulo by a **literal** zero is an error at the divisor; every error is `{message, column of the node}`, and an expected type outside the program types (column 0) lists the types. The limit of 32 registers is the compiler's, not the checker's.

## Compiling

`CompileExpression(text, declaration, expected)` is parse, check, fold, lower; the answer is a `CompiledExpression` of one of three kinds, and `CompileValue<T>` makes the `Value<T>` of it (`Const`, `Slot` or `Program`). The type asked for is checked first (`CheckExpectedType`), so a field no formula can fill says so whatever the text. **Folding** runs a node whose operands are all constants through the interpreter on a one-instruction program, so the folded value is what the program would compute; there is no algebra (`a - a`, `x * 1` run), and a divisor that folds to zero is a run-time 0, not an error. A formula that folds to a constant is a `Constant` (the register image), one that is a single field a `Slot` (the path as written), so `expr="caster.attack"` loads and saves like `from`. **Lowering** is a recursive post-order walk: an operation writes into the register of its first operand and its other operands go into the registers above it, which are given back after the instruction, so a left-leaning chain needs two registers and a right-nested one a register per level. The 32-register limit is the lowering's error (column of the node that did not fit). The source text of the program is the text as written; the pool does not intern by it, and the slot offsets are part of the code, so one text under two declarations is two programs.

`FormatProgramError(text, error)` is `<message> (column N)`, the text around the column (a window for a long one, a byte that is not printable ASCII as `?`) and a caret line; an error without a column is the message alone.
