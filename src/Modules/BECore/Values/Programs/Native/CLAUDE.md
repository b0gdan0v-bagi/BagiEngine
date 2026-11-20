# Native programs: generated C++ for the interpreter's programs

Read `../CLAUDE.md` first (the opcodes, `Kernels.h`, the pool). A game keeps the interpreter: content loads, compiles and runs with no compiler on the player's machine. A **tool** of the game that loads the same content can write one C++ function per program, the executable compiles that file, and `Value::Evaluate` calls the function instead of `Run` when one is bound to the program. Opt-in per executable: with no generated file linked nothing here runs, and `Run(program, ...)` never calls generated code (it is the reference the tests compare with).

## Files

| File | Holds |
|------|-------|
| `NativePrograms.h/.cpp` | `NativeProgramEntry` (one function and the code it came from), `NativePrograms` (`Install`, `SetEnabled`, `SetWarnOnMiss`, `Counts`, `HasGeneratedCode`, `LogSummary`), `NativeProgramRegistrar` (the static object a generated file defines) |
| `ProgramCodegen.h/.cpp` | `GenerateNativeSource` (the text of one translation unit), `EmitNativeInstruction` (the statement of one instruction) |
| `NativeProgramsFlags.h/.cpp` | `--emit-native-programs=<file>`, `--interpreted`: `EmitNativeProgramsIfAsked`, `ApplyNativeProgramsFlags`, `WriteNativePrograms` |

`ProgramPool` (`../Program.h`) keeps the binding: a slot beside every program, `Native(id)` is one atomic load with acquire and no lock, and `Program` itself stays immutable.

## The key: the code, never the pool id

A pool id is the order of loading in one process and differs between runs, so it cannot name a function in a file written earlier. A function is found by `Program::CodeHash()` and bound only when the entry's result type and register, register count, `reads`, instructions and constants equal the program's (`ProgramCodeView::operator==`, the same test `SameCode` uses, written once). The source text is no part of the key: it is there for the warning. Consequences:

- A formula, a table, a context declaration or an engine change that makes other code is another program: it finds no function. **A stale or missing file is the interpreter, never a stale function.** A slot offset is part of the code, so a field that moved in a reflected class gives another hash.
- A miss is counted (`Counts().missed`: programs in the pool with no function while generated code is on; a scan, no counters) and warned **once, at `Intern`**, naming the formula and how to regenerate, only when a tool asked (`SetWarnOnMiss`, `ApplyNativeProgramsFlags`). The warning is off by default: the engine's own test executable links a set that covers part of its programs. `Install` rebinding programs already in the pool does not warn, since one `Install` per generated set would warn for every program the other sets cover.
- `Install` may run more than once (the engine's set and a game's) and binds the programs already in the pool; the pool's lock is taken before the registry's, never the other way round. The entries must outlive the process.
- `SetEnabled(false)` clears every slot (`--interpreted`, the equality tests and the benchmark) and `true` rebinds.

## Same bits as the interpreter

A generated function calls the **same kernels** the interpreter's `switch` calls (`Kernels.h`, expanded from the same X-macro lists, so a row cannot be missing; a new row without an emitter fails the every-opcode tests), with the instruction written as a literal so the compiler sees the register numbers and offsets. `LoadConst`, `LoadTable`, `LoadTableSlot` and the store of the result are kernel functions too (`ExecuteLoadConst`, ..., `StoreResult`), so both paths have one meaning for them. `NativeProgramEqualityTest` compares **bytes**, except the payload and sign of a NaN: LLVM folds an operation on a constant NaN to some NaN, so which one is not defined.

Equal bits need equal floating-point code, so the generated file **and** the interpreter's sources (`Interpreter.cpp`, `Peephole.cpp`, `ExprChecker.cpp`, listed in `BECore/CMakeLists.txt`) are compiled with `-ffp-contract=off` (`/fp:precise` on plain MSVC; written from the compiler's documentation, not built by the loop). Clang's default contraction made `Dot` and `Lerp` one ulp different between two inlined kernels. Do not drop the flag from one side.

**Drift guards** (`NativeProgramEqualityTest`): the file regenerated from the set must equal the one the build made, compared without the source text of each entry (a program another test compiled first carries that test's formula as its source, which is no part of the code); a stale constant or a moved field is a miss, never a wrong answer.

## The generator

`GenerateNativeSource(programs, {contentSet})` is deterministic: programs ordered by `CodeHash` (a tie by the code), equal programs once, nothing of the process (an address, an id, the time, a path) in the text, so a file under version control changes only when a program does. Per program: the code and the constants as `constexpr` arrays (the entry's views), a `static void Program_<hash>(const Context&, void* result)` (`_<n>` after the hash only for programs of the same hash) with a `RegisterImage regs[N]` and one kernel call per instruction, then a table of entries and a `NativeProgramRegistrar` that installs it while the process starts. `WriteNativePrograms` leaves the file untouched (modification time too) when the text is the same, so a build that regenerates it without a change compiles nothing.

## How a game takes it

1. A tool target of the game links the engine, loads the content and compiles what the game compiles (a tool is a plain `be_add_headless_executable`), calls `EmitNativeProgramsIfAsked(options, programs)` after the content loaded, and ends with the result's `exitCode` when it returns a value. Example: `tests/NativePrograms/NativeProgramsTool.cpp` (the set `BagiEngineTests` links).
2. `be_add_native_programs(<exe> GENERATOR <tool> [ARGS ...] [DEPENDS ...])` (`cmake/BagiEngine.cmake`): a custom command runs `<tool> --emit-native-programs=<build>/Generated/<exe>.native.cpp` and the output is a source of `<exe>`. The command depends on the tool, which links the engine, so a change of a kernel or of the generator regenerates it. Two stages: **the tool that loads the content writes the file, the executable links it**, so the tool itself never links its own output. A `PoolString` of a game is no problem: the tool and the executable load the same content.
3. The game's code calls `ApplyNativeProgramsFlags()` before its content loads (the engine's `Application` calls nothing of this: `--interpreted` is the reference run of a comparison, and the miss warning needs the call) and may log `NativePrograms::LogSummary()` after.

On the web and when cross-compiling the tool cannot run: `be_add_native_programs` adds nothing, prints one status line and the executable keeps the interpreter. The tool and the executable must be built from the same engine: a stale file only costs speed.

## Measured

The ratios against the interpreter and the plain C++, and what the look for a bound function costs on the interpreter's path, are in `Values/CLAUDE.md`, "Benchmark".

## Not yet

- Operations over arrays (`data-values-collections`, not merged): its rows get emitters when it lands; the every-opcode tests fail until they do.
- **A program with a `PoolString` constant never binds**: the constant is the address of the entry, another one in the next process, so the equality fails by design and the program runs in the interpreter. The engine's set holds no program that loads a string constant; constants of strings by their text would be a change of the generator and of the registry.
- Typed locals instead of the `RegisterImage` array (the generated code still loads and stores through the registers): the benchmark (`Values/CLAUDE.md`) shows what is left over the plain C++; this is the next step if it is wanted.
- The proof on a whole game run (generated against `--interpreted`, the report bytes compared) belongs to the game's repository.
