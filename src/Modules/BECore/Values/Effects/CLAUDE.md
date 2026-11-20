# Effects: scripts that write fields and emit commands

A formula (`Programs/CLAUDE.md`) only reads. A **script** is the short list of statements that change state beside it: store into a field, branch, emit a command to the host. The programs stay pure; a script is steps around them (`Values/CLAUDE.md` has the value and the context). Everything here is `BECore`.

## Files

| File | Holds |
|------|-------|
| `WriteTargets.h/.cpp` | the paths of a context a script may write |
| `ScriptIdentifier.h` | what an identifier is in a script and in a command name |
| `EffectCommands.h/.cpp` | the registry of commands, `EmittedCommand`, `EmitBuffer`, `EmitBufferStorage` |
| `EffectProgram.h/.cpp` | `EffectOperand`, `EffectStep`, the immutable `EffectProgram` |
| `EffectProgramBuilder.h/.cpp` | assembles and checks a script (`Finish`) |
| `EffectRunner.h/.cpp` | `RunEffects`, `EffectResult` |
| `EffectScript.h` | the handle a content field holds: the shared program, `Run`, `Reads`, `Writes` |
| `EffectCompiler.h/.cpp` | `CompileEffects`, `EffectOptions`: text to `EffectScript` |
| `EffectScriptAccessor.h/.cpp` | `Ser::DataAccessor<EffectScript>`: the content form of the field |
| `ScriptAst.h`, `ScriptParser.h/.cpp` | the syntax tree of a script, its limits and `ParseScript` |

## Writable levels

`Context::SetMutable(level, object, version)` stores the object like `Set` and marks the level; `Set` of the level clears the mark. `MutableObject(level)` is the pointer for a store (null for a level that is not marked) and `BumpVersion(level)` counts a change in the level's counter for `VersionKey` and `CachedValue` (nothing without a counter). The context itself stays `const` for readers: the only `const_cast`s are in these two calls, and they are sound because `SetMutable` is the one place that marks a level and it takes a non-const reference.

`LinkField(declaration, path)` is `LinkSlot` without a type to match: it returns the slot and the field's own type (a descriptor of the registry), with the same refusals and messages (`LinkSlot` is written over the same code). A script needs the type to decide what a write may hold.

## Write targets

`WriteTargets` is the host's list of what a script may write, declared once before content loads. A path is a field (`cell.timer`), a sub-object (`cell.stats`) or a level (`cell`); it permits itself and everything under it, cut at a segment boundary, so `cell.t` does not permit `cell.timer`. An empty set permits nothing: a script that stores anywhere is then a load error. The check is at load time, by the path as linked; a run checks again that the level is writable (`Context::IsMutable`) and skips the store otherwise, so a host that forgot `SetMutable` loses a write instead of corrupting memory.

## Commands

A script does not call C++: the work a game must do is a **command** the host handles, and computation is a formula. `EffectCommands::Register<int32_t, Direction>("Action")` records a name and 0 to `kMaxCommandArguments` (3) parameters, each a program type, an enum or a type the game registered with `ProgramDomain` (found with `ValueTypeOf`; one nobody registered is `parameter N has a type that is not registered`). It is the model of `ProgramDomain`: process-wide, append-only, a lock with a name; **the order of registration is the id**, so the executable and every tool of a game register the same commands in the same order, before its content loads. A name is an identifier and has one signature (no overloads: a command is a message, it has no result to tell overloads apart by); a refusal changes nothing. `Command(id)` returns a copy because the registry may grow while the caller reads.

## The emit buffer

A run pushes `EmittedCommand{id, count, arguments}` into the host's `EmitBuffer`, which points at storage the host owns (`EmitBufferStorage<N>` is one with its own array, for tests and small hosts) and **never grows**: a full buffer drops the newest command and counts it (`Dropped()`), so what is in the buffer keeps its order and a host that reads after every run sees both what it got and how many it lost. `Push` allocates nothing and cannot throw. The host reads `Commands()`, takes each argument with `Argument<T>(i)` as the type the command registered (an enum comes back by its index, a registered type by its bytes; it shares the load of `ProgramDomain`'s operations), and calls `Clear()` (the count and `Dropped()` both) before the next run. How a `CommandId` maps to the game's own enum is the game's.

## The steps

A script is a flat `Vector<EffectStep>` of four kinds: `Store` (an operand into a whole field), `JumpIfNot` (a `Bool` operand), `Jump` and `Emit` (a command and up to 3 operands). A jump goes **forward only**, to a later step or to the end, so a run ends and needs no step limit; `if` / `else` are a `JumpIfNot` and a `Jump`, there are no loops, locals or calls. An `EffectOperand` is the three forms of a compiled formula (`EffectOperand::Of(CompiledExpression)`): a constant image, one slot, or a program id of the `ProgramPool`, so every expression of a script is the typed, fused and generated code of a formula, and the statements around it are the only new part. `bytes` of a slot operand and of a store is the field's width where the type does not say it: an enum field's underlying width, a registered type's size.

`EffectProgramBuilder::Finish` refuses what a run could not do: a jump that is not forward, not patched or past the end, a level beyond `kMaxContextLevels`, a width that is no width of the type, an operand whose type is not the step's (a condition is `Bool`), a program id outside the pool, an unknown command, a count or argument type that is not the command's signature, more than `kMaxEffectSteps` steps. It returns `IntrusivePtr<const EffectProgram>` (the type is `RefCounted`, so it cannot be returned by value); an empty script is valid. `Reads()` and `Writes()` are the levels the script reads (operands and the reads of its programs) and stores to.

## Running

`RunEffects(script, context, buffer)` evaluates an operand into one `RegisterImage` on the stack, zeroed first: a constant is copied, a slot is copied from the level's object (an enum zero-extends its field width to the index, as `LoadSlotEnum`; a registered type is zero-padded), a program runs through its bound generated function when `ProgramPool::Native` has one and through the interpreter otherwise, so the two cannot differ. A store writes the image back at the field's width (an enum's index truncated to it) **only when the bytes differ**, and then bumps the level's counter once, so `CachedValue` keeps its results when a script rewrites the same value. The operand is evaluated before the write, so a store that reads the field it writes is well defined and the next step sees the new value. A condition is true when byte 0 of its image is not 0.

## What a run guarantees

It ends, allocates nothing and writes only through `Context::MutableObject`: a store to a level that is not writable is skipped, counted in `EffectResult::refused` and an `ASSERT`. `stores` counts the stores that reached a writable level, `changed` those that changed a byte, `emitted` and `dropped` the commands that fitted and those a full buffer refused. The buffer is not cleared by the run. A level a script reads must have an object (`ASSERT`, as `Run`); a slot read from a missing object gives zero rather than a crash in a release build.

## The text of a script

```
script    := statement*
statement := target '=' e ';' | target '+=' e ';' | target '-=' e ';' | target '++' ';' | target '--' ';'
           | 'toggle' target ';' | 'if' '(' e ')' block ['else' (block | if)] | 'emit' Name '(' [e {',' e}] ')' ';'
block     := '{' statement* '}'
```

`target` is a dotted path, `e` a formula (`Programs/CLAUDE.md`, "The text of a formula"). `ExprLexer` has a statement mode (`LexOptions::statements`) for the extra tokens; a formula still refuses them with its old messages, and `a--b` is a minus and a negation there. `ParseScript` checks the shape only and **does not read an expression**: it takes the tokens up to the `;`, the closing `)` or the `,` that ends it (parentheses counted, so a comma inside a call does not split) and keeps their byte range, which the compiler compiles as a formula and whose columns it shifts by the range's start. So an error inside an expression is the formula's own message, and an unbalanced parenthesis is named by the compiler.

`if`, `else`, `emit` and `toggle` are keywords at the start of a statement only (`a = toggle.on` reads a level named `toggle`; a statement cannot start with one as a target). A statement's `column` is the operator of an assignment and the keyword of the others, the place its errors point at. A missing `;` is reported at the end of the previous token, so the caret is where it belongs. An `else if` is the one statement of an else block, built in a loop: a chain as long as the statement limit does not nest the parser, and its block depth is that of the first `if`.

Limits, each a parse error that names it: `ScriptLimits::kMaxTextBytes`, `kMaxStatements` (every block together) and `kMaxBlockDepth`. An empty script is valid and runs nothing.

## Compiling

`CompileEffects(text, declaration, EffectOptions{targets, tables, fuse})` parses, then lowers each statement to steps with `EffectProgramBuilder`. **Every expression goes through `CompileExpression`** with the type of its place, so a script has no type rules of its own: a field's descriptor for the right side of a store (an integer literal takes the field's type, a bare enumerator works, no conversion), `bool` for a condition, a command parameter's descriptor for an argument. `+=`, `-=`, `++`, `--` and `toggle` are compiled as the assignment of the text `target + (e)`, `target - (e)`, `target + 1`, `target - 1`, `!target`, so they share the wrap and IEEE rules of formulas and `toggle` of a non-`bool` is the checker's `!` error. The user's right side is parsed alone first: wrapped in `target + (` and `)` an unbalanced `1) + (2` would compile. A condition that folds to a constant is kept as a `JumpIfNot` of a constant: no algebra, the script runs what it says. An `else if` chain is compiled in a loop (`else` block that is one `if`), so its length does not nest the compiler.

A target is a **whole field**: `LinkField` finds it and gives the type, a path that ends in a component of a field (`cell.pos.x`) is refused first with the message that says to assign the whole field, then the host's `WriteTargets` must permit the path, then `CheckExpectedType` must accept the field's type (a program type, an enum or a registered type: not `Rect`, a sub-object, a list or `uint32_t`). Only an enum field and a registered type need their width (`FieldBytesOf`), the other types have it.

**The pool.** Every expression is parsed and checked in a first pass over the whole text and compiled only in the second, so a refused text adds no program to `ProgramPool`; the same text compiled again adds none (programs are interned by code) and gives an equal handle (`EffectProgram::Equivalent`: text, steps and operands, not the address). A failure that only lowering finds (running out of registers in a very deep formula) comes after the first pass and may leave the programs of the statements before it, which cost nothing twice.

## Load errors

An error is `{message, column}` in the **whole script's text**, and the message begins with the statement: `assignment to 'cell.timer'`, `'+=' on 'x'`, `'toggle' of 'x'`, `'if' condition`, `'emit Action' argument 2`. A mistake inside the user's expression is at its own byte (the columns of the sub-formula shifted by where it starts, `FormulaText`); a mistake the checker finds in what the compiler wrote around it (`flag += 1`: the `+` of a `bool`) is at the statement's operator, the keyword for `if`, `emit` and `toggle`; a target error is at the target's first byte and an unknown command or a wrong argument count at the command's name. Messages are those of `LinkField` and the checker (`did you mean`, the list of fields); the ones this file adds are: no write targets, a path the host does not allow (it lists what it allows), a component, a field type a script cannot write, an unknown command (it lists the registered ones) and an argument count that is not the command's.

## In content

A reflected struct can have a field `EffectScript`; its form is the object `<onTick script="cell.timer -= 1; ..."/>`, the same in XML, binary and JSON (the accessor is included at the bottom of `EffectScript.h`, as `Value.h` does, so a generated file that names the field sees it). It is compiled **while the document loads**, against the declaration of the load's `ValueLinkScope` (the document's `<context>`, which therefore comes before the script fields) and the host's `ValueLinkScope::UseWriteTargets`, found in the innermost scope that has some (`Targets()`), per thread as the declaration is. The tables of `UseTables` reach the script's formulas the same way.

Load errors are reported under the `script` key as `column N: <message>` ending with the document name (`ValueLinkScope::AtDocumentWithin`), and the field stays as it was; a failed load adds no program to the pool. They are: no `script`, a `from`, `expr` or `value` beside it, no declared context, **no write targets in scope** and any compile error. No targets is an error even for a script that stores nothing: a document with scripts is not ready until the host said what they may write, and an empty `WriteTargets` is that decision. `script=""` is a script that runs nothing. `Save` writes the source text as loaded, so a file round-trips to an equal handle; the editor shows no properties of a script.

## Cost

`RunEffects` is the interpreter over a few steps, each expression a program through `Run` or its generated function, so a trigger costs about ten times the same C++ (`ValueBench trigger_*`, `Values/CLAUDE.md`, "Benchmark"). The reason is the same as for a formula: the cost is in the entry of each program and in the copies through one register image, not in the steps. Run a script per event or per cell that changed, never per cell per frame.
