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

## The explanation of a value

`ExplainValue(value, context, declaration)` (`ValueExplain.h`) gives a `ValueExplanation`: kind, `source` (the literal of a `Const`, the path of a `Slot`, the formula text of a `Program`), `hasValue`, `valueText` and the `trace` (none for a `Const`, one `ContextKey` node for a `Slot`, `ExplainProgram` for a `Program`). It never asserts on a context that lacks an object at a level the value reads: `hasValue` is false and `why` names the level. A `Program` whose result type is not the type of the value, or of a type no program computes (`Rect`, an unregistered game type), has no value and says why. `FormatValue<T>` is the text of a `T`: `FormatImage` for a program type, an enum or a registered type, `<Name N bytes: hex>` for the rest. An enum result is named after `T`, because a program does not know which enum it computes.

`ValueExplainers` is the erased form: `ExplainValue` of every `Value<T>` registered, found by the `opaqueHash` of `DescribeType<Value<T>>()`, for code that holds only the address of a field. `RegisterBuiltinValueExplainers()` registers the program types and `Rect`; a game registers its enums and its own types (`Register<T>()`), as it registers them with `ProgramDomain`.

## The tooltip

`FormatTooltip(template, bindings, words)` (`ValueTooltip.h`) fills `Deals {damage} damage` from `ValueExplanation`s: `{damage}` is the value and, for a `Program` whose parts differ from the value, its parts (`12 (10 + 2 per level)`); `{damage:value}` is the value only; `{{` and `}}` are braces. An unknown `{name}` stays as written and a value the context lacks is `?`: a template's mistake is never an empty hole. The parts are the formula of the trace: a constant subtree is its value, `k * field` in either order (also the fused `MulAddSlot`) is the `per` pattern, other operations are infix with the parentheses their precedence needs, a function is `name(args)`; one longer than a tooltip needs shows the value only.

The words are data (`TooltipWords`): the `per` pattern (`{0}` the number, `{1}` the field's label, replaced by hand because the text is a translator's) and a label function. `MakeTooltipWords(UIStrings)` (`TooltipStrings.h`, the one file of this folder that includes `UI/`) reads `values.per` and `values.label.<path>` through `Find`, so a language is strings only and a missing label shows the path. A template that is itself a string is resolved first (`UIStrings::Resolve("{#tip.fireball}")`).

## The control method

`explain_value {root, path, context?}` (`ValueExplainControl.h`, registered by `RegisterControlBuiltins`) finds the `Value<T>` field at `root.object + CompilePath(path).offset`, its `ValueExplainers` entry by the field's `opaqueHash`, and answers `{kind, source, has_value, value?, why?, trace?, truncated?}`; `trace` is the tree as nested `{source, label, value, type, children}` (`source` the `TraceStepKind`), cut at `kMaxFormattedLines` nodes like `FormatTrace`. Codes: the by-name ones (`unknown_root`, `no_object`, `invalid_params`, `invalid_path`, `not_found`), `not_a_value` (the message lists the registered types), `not_fixed` (an element or a pointee in the path) and `unknown_context`. Everything is client data: no assert, no `PoolString`.

The context is a named provider of `ValueContextRegistry` (`ValueContextRegistry.h`; main thread, names `String`, never interned) that a game fills, because a scene has no context of its own: the request's `context`, else the one named `default`. With none the answer is kind and source only (`has_value` false, `why` says no context); a provider that returns nothing now, or a context that lacks a level the value reads, gives `has_value` false too.

## The inspector

`Value<T>::VisitProperties` calls `IPropertyVisitor::VisitValueField(name, ValueFieldInfo)` first, for every kind, and then visits the literal of a `Const` as before. The hook runs inside the field's compound, so a closed node costs nothing. `ValueFieldInfo` carries the kind, the source (the path of a `Slot`, the formula of a `Program`, empty for a `Const`: its literal is the child `value`) and `explain`, a `FunctionRef` that fills a `ValueExplanation` for a context: it allocates, so a visitor calls it only for a field the person has opened. `ImGuiPropertyVisitor` draws a dim `Slot: caster.attack` and, when `ValueContextRegistry` has the provider `kInspectorContextName` (`inspector`), a node `Explain` with the value and the trace as a tree, the provider and the explanation called only while that node is open. A game registers the provider as it registers `default`; without it only the kind and source show.

## Files

| File | Holds |
|------|-------|
| `FieldResolver.h` | `ResolveField`: level + offset to the path and `TypeDesc`; a component of a `Vec3` is `field.y` |
| `ProgramTrace.h` | `ExplainProgram`, `ProgramTrace`, `TraceNode`, `FormatImage`, `FormatTrace` |
| `ValueExplain.h` | `ExplainValue`, `ValueExplanation`, `FormatValue`, `ValueExplainers` |
| `ValueExplainControl.h`, `ValueContextRegistry.h` | the method `explain_value`, the named contexts a game registers |
| `ValueTooltip.h`, `TooltipStrings.h` | `FormatTooltip`, `TooltipWords`; the words from `UIStrings` |
