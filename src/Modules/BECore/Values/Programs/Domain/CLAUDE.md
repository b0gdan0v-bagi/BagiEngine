# Domain: a game's own value types and operations in formulas

`ProgramDomain` (`ProgramDomain.h`) is the registry a game fills at start-up so that `expr` can compute a type the engine does not know (a 256-bit cell mask) and call functions over it: `popcount(and(walls, shift(m, East)))`. The engine's own types stay rows of `BE_PROGRAM_TYPES` (`Programs/CLAUDE.md`); everything here is run-time data.

## What a game registers, and when

- `RegisterType<T>("mask")`: `T` is trivially copyable, reflected (`DescribeType<T>()` of a value kind: an opaque self-serializing struct or a `BE_CLASS` object), no engine type and no enum, at most `kProgramRegisterBytes`. The name is what a formula and the messages say; it is an identifier and not an engine type name (`ProgramTypeName`).
- `AddOperation<&Function>("popcount")`: an ordinary function. Parameters (0 to 3, `kMaxDomainArguments`) and the result are engine program types, reflected enums (the register holds the index) or registered types, optionally `const Context&` first: such a function reads the objects of the context, the formula stays pure. The template builds the raw function over registers (`DomainOperationFn`) and records the signature. The name is no engine function (`kExprFunctions`, `at`, `ByLevel`); overloads differ in their parameters, never in the result alone.
- **Before the content loads**, in the executable and the tool alike, in the same order (the order is the id, and ids are in pooled programs and generated code). Nothing checks "before"; a program already in the pool is never affected by a later registration.
- A refused registration is a `std::expected` error with the reason and changes nothing. A parameter that no program computes (`String`, a vector) is a compile error; a type nobody registered is the error `parameter N has a type that is not registered`.

## Contract of the registry

Append-only: an id never moves or goes away and an entry's address is stable, so `Type(id)` and `Operation(id)` take no lock (chunked storage published with a release before the id is out, the model of `ProgramPool`). Registering and the lookups by name take the registry's lock (`FindTypeByDescriptor` and `FindOperations` read without it). `Fingerprint()` hashes the names (by text, not by pointer), sizes, signatures and ids in order: equal in two processes exactly when they registered the same set in the same order.

A value of a registered type is its C++ bytes zero-padded to the register, and a result is stored with the rest zeroed, so equal values are equal images (`==` and `select` read the whole register). `==` is bitwise (`DomainEquality::Bitwise`) only for a type with unique object representations; a float member or padding gives `None` by default and the game registers an operation that compares.
