# Computed values: native tools run the interpreter: no C++ generated from the content's programs

- Version: 0.6.11
- Entry: data-values-codegen
- Date: 2026-11-20
- Bump: patch

## Added
- Native programs (`BECore/Values/Programs/Native/`): a game's tool that loads its content can write one C++ function per compiled program (`--emit-native-programs=<file>`, `EmitNativeProgramsIfAsked`), and `Value::Evaluate` calls that function instead of the interpreter when its code equals the program's (`Program::CodeHash`). A stale or missing file falls back to the interpreter, never to a wrong answer; results are bit-equal to the interpreter's.
  Adapt: optional. To take it, add a tool target that loads the content and calls `EmitNativeProgramsIfAsked`, then `be_add_native_programs(<exe> GENERATOR <tool>)` in the game's CMake, and call `ApplyNativeProgramsFlags()` before the content loads (`Values/Programs/Native/CLAUDE.md`, "How a game takes it"). Without it nothing changes.
- `be_add_native_programs` in `cmake/BagiEngine.cmake`; `--interpreted` runs the interpreter alone (the reference of a comparison).
  Adapt: nothing.

## Changed
- The interpreter's control instructions are kernels (`Values/Programs/Kernels.h`) shared with the generated code; the interpreter sources are compiled with `-ffp-contract=off` (`/fp:precise` on MSVC).
  Adapt: nothing; a game's formula results stay the same.
