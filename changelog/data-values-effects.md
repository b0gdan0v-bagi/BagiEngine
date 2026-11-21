# Computed values: programs only read; no statements that write fields or emit commands

- Version: 0.7.3
- Entry: data-values-effects
- Date: 2026-11-20
- Bump: patch

## Added
- `BECore/Values/Effects/`: effect scripts, short statement lists beside the pure formulas: `=`, `+=`, `-=`, `++`, `--`, `toggle`, `if`/`else` and `emit` of a registered command into a fixed-capacity `EmitBuffer` the host provides (an overflow is deterministic and counted). A script writes only the paths the host declares (`WriteTargets`); a write to anything else is a load error. Running a script allocates nothing. `Effects/CLAUDE.md` describes the language and the host side.
  Adapt: nothing; to use it, register commands, declare write targets and give a reflected type an `EffectScript` field.
- `EffectScript`: a content field holding a compiled script, read and written by every format.
  Adapt: nothing.
- `Context::SetMutable(level, object, version)`: a level a script may write; `IsMutable`, `MutableObject` and `BumpVersion` go with it. `Context::Set` keeps a level read-only as before.
  Adapt: nothing.
