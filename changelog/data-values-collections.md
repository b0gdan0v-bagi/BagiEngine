# Computed values: no operations over arrays (count, any, all, sum, min, max over a reflected array)

- Version: 0.8.1
- Entry: data-values-collections
- Date: 2026-11-20
- Bump: patch

## Added
- Formulas over reflected lists (`Values/Programs/Lists/`): `count`, `any`, `all`, `sum`, `min`, `max` as `fold(list, v, body)`, `size(list)` and `list[i]` with fields after it, over `Array`, `Vector`, `SmallVector` and `FixedVector` fields of a context object or of an element, nested loops included. An index outside the list reads the missing element (default values); `any` and `all` stop early. Generated code covers them. `Lists/CLAUDE.md` describes the language.
  Adapt: nothing; a game's content may use them in `expr`.
- `LinkList` and `LinkElementField` (`Values/Context.h`) link a list path and a field of an element with the messages of `LinkSlot`.
  Adapt: nothing.

## Changed
- `[` and `]` are tokens of a formula (an index), no longer a lexer error; a script that writes `x = [1];` now fails when its expression is compiled, not in the lexer.
  Adapt: nothing.
- A game's registered operation may share the name of a list function (`any`, `count`, `size`, ...) only with another argument count: `ProgramDomain::AddOperation` refuses the list function's own count, and a call is told apart by its count.
  Adapt: rename a registered operation whose name and parameter count equal a list function's.
