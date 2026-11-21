# Lists: loops over reflected lists in a program

A program can read a list of a context object (an `Array`, `Vector`, `SmallVector` or `FixedVector` field), loop over it and read a field of each element. The text of a formula reaches it (`## The language`); a program is also assembled by hand with `ProgramBuilder::EmitListSlot`, `EmitLoopBegin` and the other `Emit*` helpers, and `ProgramListTest` shows each shape (count, `any`, `all`, `sum`, `min`, `max`, a loop in a loop, `size`, an element by index).

## The language

```
count(hand.sets, s, s.pung && s.dragon) == 3      all(hand.tiles, t, t.suit == hand.tiles[0].suit)
sum(hand.yaku, y, select(y.on, y.han, 0))         sum(hand.sets, s, count(s.tiles, t, t.terminal))
```

- **Syntax.** `fold(list, v, body)`: the list is a path of a level that ends in a list field (`hand.sets`), or of an element (`s.tiles`); `v` is a bare name, the element, that lives in the body only. `size(list)` is an `int`. `list[i]` is the element `i` (an `int`; a literal takes it, another number type is told to write `int(x)`), `.field` after it reads a field (`hand.sets[1].tiles[2].rank`); a list of scalars gives the value (`hand.counts[1]`). The level is always in the path: there is no default one.
- **Functions** (`kListFunctions` in `ExprFunctions.h`; the arity decides, so the two-argument `min` / `max` stay): `count(l, v, bool)` and `size(l)` give `int`; `any` and `all` take a `bool` body; `sum`, `min`, `max` take a number body (`int`, `int64`, `float`, `double`) and give its type; a literal body takes the expected type, else `int`. A NaN element is skipped by `min` / `max`.
- **Names.** A loop variable is never a level, a function, a type name (`int`, `true`) or an outer variable, so a name is one thing and the error says which; sibling loops may reuse a name. A path whose first segment is a variable reads through the element's type (`s.tiles`, `t.suit`; embedded objects and components as for a level); `v` alone is the value of a list of scalars and an error for an object element. A list or an element used as a value (`count(..) + hand.tiles`, `hand.tiles[0] == 1`) is an error that says how to read it.
- **Missing element.** `hand.sets[9].suit` over three sets is the default (`0`, `false`, the first enumerator), never an error: see "Total by construction".
- **Where it is checked.** `LinkList`, `LinkElementField` and `DescribeListElement` (`Values/Context.h`) link the paths against the reflected types with the messages of `LinkSlot` (`did you mean`, the fields of the type); a list of pointers, maps, optionals or lists is `bad_element`, a path that is no list `not_a_list`. The checker (`ExprChecker`) keeps the loop scopes, types the body and gives each fold a loop number; a layout error of `MakeListDescriptor` is an error at the column of the node. The text limits of `ExprLimits` apply as everywhere; a nest too deep for 32 registers (a fold keeps its accumulator, list, element and body) is the lowering's register error at the node.
- **Lowering.** A fold writes the identity into the accumulator (`dst`), `ListSlot` / `ListElem`, `LoopBegin`, the body, the accumulation (`count`: `Select(body, 1, 0)` and `Add`; `sum`: `Add`; `min` / `max`: the rows; `any`: `Or` and `BreakIf` on true; `all`: `And` and `BreakIf` on false), `LoopEnd`. The element of a variable is the loop's own register, so `LoadElem` reads it with no copy. A list or an element is never constant, so `FoldConstants` leaves it alone, and `Fuse` does nothing for a loop program.
- **Cost** (`Values/CLAUDE.md`, "Benchmark"): the interpreter takes about 3.4 ns per element of a loop (about 40 times a vectorised plain loop), generated code about 0.7 ns (5 to 9 times).
- **Tests.** `ProgramListCheckerTest` (the checker), `ProgramListCompileTest` (the code against C++ loops), `ValueListLoadTest` (content, errors, the cache), `ValueListMahjongTest` (patterns of a Mahjong hand against C++ predicates, interpreter and generated code; the game's `ScoreSeat` is not here), the native set (`Native/CLAUDE.md`) and `ValueBenchTest` (`list_*`).

## Instructions and registers

A list register holds a `ListImage` (`data`, `count`, `stride`), an element register an `ElementImage` (`ptr`, `index`); both fit a 16-byte register and neither is a value (no `ProgramType`: a program never returns or computes with one).

| Instruction | Does |
|-------------|------|
| `ListSlot dst, a=level, imm=descriptor` | `dst` = the list in a field of the context object of level `a` (the level joins `Reads()`) |
| `ListElem dst, a=element, imm=descriptor` | the list that is a field of an element; a missing element has an empty list |
| `ListSize dst, a=list` | `int32_t`, the count cut to `INT32_MAX` |
| `ListAt dst, a=list, b=int32 register` | the element at the index; an index outside `[0, count)` is the missing element |
| `LoadElem<T> dst, a=element, b=enum bytes, imm=offset` | the field at `imm` of the element; a list of scalars is read at offset 0 |
| `LoopBegin dst=element, a=list, imm=after the loop` | element 0; an empty list jumps to `imm` |
| `LoopEnd dst=element, a=list, imm=first of the body` | the next element and a jump back while there is one |
| `BreakIf a=bool register, b=0 or 1, imm=after the loop` | leaves the innermost loop when the register equals `b` |

The kernels are in `ListKernels.h` (`LoadElem` is in `Kernels.h`, with the typed rows). The three that jump return the decision and the caller only branches, so the interpreter and the generated code (`Native/CLAUDE.md`) cannot differ.

## The descriptor

An instruction cannot hold a function pointer: a program is compared, hashed and generated by its code. A list is therefore read by its **layout**, from a `ListDescriptor` kept as a constant of the program (`offset` of the field in its object, `stride`, `count`, `layout`). `MakeListDescriptor(offset, TypeDesc)` builds it from the reflected type (`TypeDesc::elementSize` is the stride; an error says what is wrong). `Inline` is `Array<T, N>` (`TypeDesc::resizable == false`): the elements start at the field. `Pointers` is every other container: the first two pointers of the object are the begin and the end, `count = (end - begin) / stride`. That is an assumption about EASTL, and `ProgramListTest` compares the raw read with `begin()` and `size()` for every container, element size and stage of life (empty, inline, overflowed, grown and shrunk), so another layout fails a test, not a program.

A field can be an `Array`, `Vector`, `SmallVector` or `FixedVector` of a scalar, an enum, a math type, a `PoolString` or a reflected struct. `Span` has no accessor and so cannot be a reflected field; an element that is a pointer, a list or a map has no field a `LoadElem` reads, so the language of stage 2 refuses a loop over one.

## Total by construction

- **A missing element** (`ptr == nullptr`: the index was outside the list, or a loop never began) reads the default value of every type (`T{}`: 0, `false`, the enumerator 0, the empty `PoolString`) and its lists are empty. `tiles[9].suit` on a hand of three tiles is the default, never a read of a wrong address; clamping like `at(...)` could not do this for an empty list.
- A list is read **where it is, never copied**, and must not change while a program runs. A program runs inside one `Evaluate`; the owner of the object bumps its version counter as for any field, and `CachedValue` sees a list only through its level's counter. Nothing is allocated.
- **Over an empty list** `count` is 0, `sum` 0, `any` false, `all` true. A fold starts from the identity the program writer loads into the accumulator: `min` of an empty list is the largest value of the type (infinity for floats) and `max` the smallest, so a loop needs no "first element" case; a rule that wants a guard writes `size(a) > 0`.

## What `Finish` proves

A jump exists only in the three forms above, and `ProgramBuilder::Finish` refuses (naming the instruction) any other: each `LoopBegin` is paired with exactly one `LoopEnd` placed right before its target, loops nest and never cross, a `BreakIf` is inside a loop and jumps to the end of the innermost one, a body writes neither the element nor the list register of a loop it is in, and every descriptor constant is in the pool with a layout in the enum and a stride over 0. Every loop walks a list of at most 2^32 elements and all jumps go forward or back to the start of an enclosing loop, so a program **ends**, hostile or not. It does not track what a register holds (as for every instruction): a program that reads a register as a list that is none is the writer's error, and the lowering emits well-typed code.

`Fuse` leaves a program with a list instruction alone: its liveness is linear, which a loop breaks (fusing the straight runs inside a body is a later step).

## Not yet

- Lists in `<tree>` node classes (`Values/Nodes/`), a `Span` field (it has no accessor), a list of lists, `a[i]` of a map, fusing the straight runs inside a loop body.
