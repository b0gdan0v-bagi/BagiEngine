# Effects: scripts that write fields and emit commands

A formula (`Programs/CLAUDE.md`) only reads. A **script** is the short list of statements that change state beside it: store into a field, branch, emit a command to the host. The programs stay pure; a script is steps around them (`Values/CLAUDE.md` has the value and the context). Everything here is `BECore`.

## Files

| File | Holds |
|------|-------|
| `WriteTargets.h/.cpp` | the paths of a context a script may write |

## Writable levels

`Context::SetMutable(level, object, version)` stores the object like `Set` and marks the level; `Set` of the level clears the mark. `MutableObject(level)` is the pointer for a store (null for a level that is not marked) and `BumpVersion(level)` counts a change in the level's counter for `VersionKey` and `CachedValue` (nothing without a counter). The context itself stays `const` for readers: the only `const_cast`s are in these two calls, and they are sound because `SetMutable` is the one place that marks a level and it takes a non-const reference.

`LinkField(declaration, path)` is `LinkSlot` without a type to match: it returns the slot and the field's own type (a descriptor of the registry), with the same refusals and messages (`LinkSlot` is written over the same code). A script needs the type to decide what a write may hold.

## Write targets

`WriteTargets` is the host's list of what a script may write, declared once before content loads. A path is a field (`cell.timer`), a sub-object (`cell.stats`) or a level (`cell`); it permits itself and everything under it, cut at a segment boundary, so `cell.t` does not permit `cell.timer`. An empty set permits nothing: a script that stores anywhere is then a load error. The check is at load time, by the path as linked; a run checks again that the level is writable (`Context::IsMutable`) and skips the store otherwise, so a host that forgot `SetMutable` loses a write instead of corrupting memory.
