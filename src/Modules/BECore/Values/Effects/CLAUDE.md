# Effects: scripts that write fields and emit commands

A formula (`Programs/CLAUDE.md`) only reads. A **script** is the short list of statements that change state beside it: store into a field, branch, emit a command to the host. The programs stay pure; a script is steps around them (`Values/CLAUDE.md` has the value and the context). Everything here is `BECore`.

## Files

| File | Holds |
|------|-------|
| `WriteTargets.h/.cpp` | the paths of a context a script may write |
| `ScriptIdentifier.h` | what an identifier is in a script and in a command name |
| `EffectCommands.h/.cpp` | the registry of commands, `EmittedCommand`, `EmitBuffer`, `EmitBufferStorage` |

## Writable levels

`Context::SetMutable(level, object, version)` stores the object like `Set` and marks the level; `Set` of the level clears the mark. `MutableObject(level)` is the pointer for a store (null for a level that is not marked) and `BumpVersion(level)` counts a change in the level's counter for `VersionKey` and `CachedValue` (nothing without a counter). The context itself stays `const` for readers: the only `const_cast`s are in these two calls, and they are sound because `SetMutable` is the one place that marks a level and it takes a non-const reference.

`LinkField(declaration, path)` is `LinkSlot` without a type to match: it returns the slot and the field's own type (a descriptor of the registry), with the same refusals and messages (`LinkSlot` is written over the same code). A script needs the type to decide what a write may hold.

## Write targets

`WriteTargets` is the host's list of what a script may write, declared once before content loads. A path is a field (`cell.timer`), a sub-object (`cell.stats`) or a level (`cell`); it permits itself and everything under it, cut at a segment boundary, so `cell.t` does not permit `cell.timer`. An empty set permits nothing: a script that stores anywhere is then a load error. The check is at load time, by the path as linked; a run checks again that the level is writable (`Context::IsMutable`) and skips the store otherwise, so a host that forgot `SetMutable` loses a write instead of corrupting memory.

## Commands

A script does not call C++: the work a game must do is a **command** the host handles, and computation is a formula. `EffectCommands::Register<int32_t, Direction>("Action")` records a name and 0 to `kMaxCommandArguments` (3) parameters, each a program type, an enum or a type the game registered with `ProgramDomain` (found with `ValueTypeOf`; one nobody registered is `parameter N has a type that is not registered`). It is the model of `ProgramDomain`: process-wide, append-only, a lock with a name; **the order of registration is the id**, so the executable and every tool of a game register the same commands in the same order, before its content loads. A name is an identifier and has one signature (no overloads: a command is a message, it has no result to tell overloads apart by); a refusal changes nothing. `Command(id)` returns a copy because the registry may grow while the caller reads.

## The emit buffer

A run pushes `EmittedCommand{id, count, arguments}` into the host's `EmitBuffer`, which points at storage the host owns (`EmitBufferStorage<N>` is one with its own array, for tests and small hosts) and **never grows**: a full buffer drops the newest command and counts it (`Dropped()`), so what is in the buffer keeps its order and a host that reads after every run sees both what it got and how many it lost. `Push` allocates nothing and cannot throw. The host reads `Commands()`, takes each argument with `Argument<T>(i)` as the type the command registered (an enum comes back by its index, a registered type by its bytes; it shares the load of `ProgramDomain`'s operations), and calls `Clear()` (the count and `Dropped()` both) before the next run. How a `CommandId` maps to the game's own enum is the game's.
