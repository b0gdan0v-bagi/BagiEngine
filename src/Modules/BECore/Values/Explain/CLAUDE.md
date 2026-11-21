# Explain: where a computed value comes from

Answers "why is this value what it is" for a `Value<T>` (`../CLAUDE.md`): the tree of how its program ran, for a tooltip, the control API and the inspector. Nothing here runs in a frame: it allocates freely and is called when a person asks.

## The trace

`RunTraced` (`../Programs/Interpreter.h`) is `Run` with a `ProgramTraceSink` told after each instruction (`ProgramTraceSink.h`: pc, the next pc, the instruction, the register file). The loop is one template over an observer that is empty for `Run`, so the plain run pays nothing. A traced run **always uses the interpreter**, never the generated function bound to the program (`../Programs/Native/CLAUDE.md`): the two give the same bits and the generated code has no hook.

`ExplainProgram(program, context, declaration)` (`ProgramTrace.h`) builds a `ProgramTrace` from those steps, **not from the formula text**: it works for a program built by hand, a tree or a registered type, needs no second compile, and shows the program as it runs. The price: a fused instruction is one node (`MulAddSlot`: the field and two constants as children; `LerpSlots`: two fields and the weight; `LoadTableSlot`: the index field) and a subtree of constants is the constant the compiler folded it to.

- A node is a `TraceStepKind` (`Constant`, `ContextKey`, `Table`, `Operation`, `Conversion`, `Call`, `List`), its type, its register image and a `label`: the path of a field (`caster.attack`), the operation, `table row 1 of 3`, the name of a game's function, the path of a list. Its children are the nodes that wrote the registers the instruction read, in operand order (`InstructionInfo.h`: `DescribeInstruction`, `OperandTypeOf`, the one table the peephole pass uses too); they precede it in `nodes`. `Copy` and the loop control instructions make no node.
- A constant has no type of its own: it takes the type of the first instruction that reads it, and an enum or a registered type takes its `described` descriptor (what names the enumerators) from the other operand or from the game's operation. A value is printed by `FormatImage` / `FormatNodeValue`.
- A field is named by the reverse of `LinkField` (`FieldResolver.h`: `ResolveField`, `ResolveElementField`); when no field starts at the offset, or the declaration lacks the level, the label says `level 'caster' +24` or `level 3 +24`, never an assertion. The context must still hold an object at every level the program reads, else the trace is empty and `missingLevel` says which.
- **Loops.** `sum(hand.tiles, t, t.rank)`: a `List` node (its path, the count as value) and, per element, a `ContextKey` node `hand.tiles[3].rank` with the list as its one child; a nested list is `hand.sets[1].tiles`. The tree has at most `kMaxTraceNodes` nodes: later steps run but are not recorded (`truncated`), and the root's value is still the program's (`ProgramTrace::result`).
- A node several parents read (a constant register) is written under each; `FormatTrace` stops at `kMaxFormattedLines` lines.

## Files

| File | Holds |
|------|-------|
| `FieldResolver.h` | `ResolveField`: level + offset to the path and `TypeDesc`; a component of a `Vec3` is `field.y` |
| `ProgramTrace.h` | `ExplainProgram`, `ProgramTrace`, `TraceNode`, `FormatImage`, `FormatTrace` |
