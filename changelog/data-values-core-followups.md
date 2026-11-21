# Computed values: VersionKey ignores the level's object, the engine doc names a Mahjong class (review follow-ups of data-values-core)

- Version: 0.7.1
- Entry: data-values-core-followups
- Date: 2026-11-20
- Bump: patch

## Changed
- `Values/CLAUDE.md` states where a `<context>` refusal is reported (under the level's `name` or `type` key, or under `context` itself) and that an empty `<context/>` declares no level and shadows an outer declaration (now pinned by a test); the version example no longer names a game's class.
  Adapt: nothing; a document that wants to read no context may write `<context/>`.
