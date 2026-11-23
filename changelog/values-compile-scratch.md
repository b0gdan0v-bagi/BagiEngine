# Formula and effect compile builds 15-30 heap temporaries per formula; interned programs hold heap vectors

- Version: 0.9.5
- Entry: values-compile-scratch
- Date: 2026-11-23
- Bump: patch

## Changed
- Compiling a formula or an effect keeps its temporaries (tokens, syntax tree, checker and lowering tables, builder code) in the thread's scratch arena: about 42 % fewer heap allocations per compile, at content load and on every editor edit of a formula.
- The program pool keeps the code and constants of interned programs in one arena of its own instead of two heap vectors per program (fewer heap blocks; the held bytes grow by the arena's first chunk).
