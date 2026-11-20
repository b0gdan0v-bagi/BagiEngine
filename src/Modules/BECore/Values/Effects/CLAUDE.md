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
