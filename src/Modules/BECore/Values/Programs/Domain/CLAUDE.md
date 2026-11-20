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

## In a program

A registered type is the program type `Domain` (`ProgramDomainValue`: 32 bytes): `LoadSlotDomain` copies the registered size (`b`, 1 to 32) and zeroes the rest, `Eq`, `Ne` and `Select` compare and move the whole register. A game's operation is **one** opcode, `CallDomain`: `imm` is the operation id, `a`, `b`, `c` the argument registers (as many as the operation has parameters; the others are unused), `dst` the result. The interpreter calls the registered function pointer from one `case`; the operation's id fixes its types, as the type of an engine row is in its opcode. `Finish` refuses an id nobody registered and an argument register the program never acquired.

**A call cannot say which levels it reads.** An operation that takes `const Context&` declares them at registration (`DomainOperationOptions::reads`, a `LevelMask`; empty by default) and `Finish` adds them to `Program::Reads()`; without it `CachedValue` would keep a result after the board changed. The peephole pass reads a `CallDomain` as reading the registers of its parameters, so it fuses around a call and never drops a register the call reads.

## In the checker

`ExprType` carries the registry id (`domain`) beside the enum's descriptor; two registered types are equal when their ids are. `ProgramTypeFor` asks `FindTypeByDescriptor` after the engine's rows, so a field of a registered type is a `Slot` of type `Domain` and a formula may have that type; the messages that list the types of a formula list the registered names too, and a type is named by its registered name.

A name that is no engine function is looked up with `FindOperations` and checked by `CheckDomainCall`: the **argument count** picks the overloads; each argument is checked with its parameter's type as the hint **when the overloads at that count agree on it** (a literal beside it takes it, a bare name is an enumerator of its enum: `shift(m, East)`, `pair_of(1, 2)` over two `int64`), with no hint otherwise; the **argument types** then pick the overload (equal types, no conversion). A call that matches none is an error at the call naming the name, the argument types found and every signature of the name; so is a wrong count. The node is an `Operation` of `CallDomain`, its `immediate` the operation id and its type the signature's result (a registered type, an engine type or an enum).

`==`, `!=` and `select` of a registered type go through the rows of `Domain` and are refused with the type's name when its equality is `None`; arithmetic, ordering, components and conversions are errors that say what the type has (`==`, `!=`, `select` for a bitwise one, and the operations that take it). A type name is no function and no value; an operation name is no value (`'popcount' is a function`).

## Compiling and loading

The lowering loads a field of a registered type with `LoadSlot` of the type's size and a call as one `CallDomain` whose operands sit in consecutive registers like any other operation's. **Folding** runs a call over constants through the interpreter, so `cell(3, 4)` is a `Constant` image and a formula of constants is a `Const` of the type; an operation must therefore be total (an argument out of range gives a documented result, never undefined behaviour). **A call that takes `const Context&` is never folded**, constant arguments or not: folding runs over an empty context. `CompiledExpression::domain` is the registry id of a result of type `Domain`.

`CompileValue<T>` and `Value<T>` take a registered `T` with the three outcomes of an engine type (`Const` by a copy of `sizeof(T)` bytes of the image, `Slot`, `Program`); an unregistered `T` is the error that lists the types. `Value<T>::Evaluate` runs the program through `RunDomain<T>` (`Interpreter.h`): the result is a whole register, so `T` takes its own bytes from it, and the generated function is used when one is bound. A program does not know which registered type it computes; `T` must be the type of the field it was compiled for. A formula's error reaches the document as `column N: <message> (document 'X')`; the message of an unknown name lists every function and operation, so it is cut to fit the reader's 256 bytes and the registered operations at the end of a long list may not show.
