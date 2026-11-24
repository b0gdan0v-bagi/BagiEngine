# Windows: ProgramInstructionInfoTest.EveryRowReadsExactlyTheRegistersItsChangeMoves crashes BagiEngineTests (Shard0)

- Version: 0.9.3
- Entry: program-instruction-info-crash-windows
- Date: 2026-11-23
- Bump: patch

## Changed
- `ProgramInstructionInfoTest` no longer reads past the operand slots of a case (4 slots, 8 loaded registers): EASTL's bounds check, on in a Windows Debug build, broke into the debugger and `BagiEngineTests.Shard0` failed in every Windows ctest.
  Adapt: nothing.
