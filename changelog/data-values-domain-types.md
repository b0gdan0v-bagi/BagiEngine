# Computed values: a game cannot add its own value types and operations (a 256-bit cell mask, its opcodes)

- Version: 0.7.2
- Entry: data-values-domain-types
- Date: 2026-11-20
- Bump: patch

## Added
- `BECore/Values/Programs/Domain/ProgramDomain.h`: a game registers its own value types (`RegisterType<T>("mask")`: trivially copyable, reflected, up to 32 bytes) and operations over them (`AddOperation<&Function>("popcount")`, 0 to 3 arguments, optionally `const Context&` first) at start-up; formulas then compute these types and call these functions through one `CallDomain` instruction, with no virtual call and no allocation. The checker knows them: an unknown operation or a wrong argument type is a load error naming the place. `Domain/CLAUDE.md` has the example.
  Adapt: nothing; to use it, register in one function that the executable and the native-programs tool both call, before `ApplyNativeProgramsFlags()` and before any content loads.
- `NativePrograms::BindPending()`: generated functions of programs that call a game's operations carry the registry's fingerprint and are bound only when it matches; `ApplyNativeProgramsFlags()` calls it.
  Adapt: nothing; a game that registers operations does so before `ApplyNativeProgramsFlags()`, and its native tool registers the same set in the same order.

## Changed
- Program registers are 32 bytes (`kProgramRegisterBytes`) so a registered type of up to 32 bytes fits; the engine's formula results are unchanged.
  Adapt: nothing.
