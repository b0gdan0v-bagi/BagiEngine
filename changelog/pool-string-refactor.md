# PoolString: entries are copied and hashed one by one at runtime, code constants and field names too; fixed table, no batch, two bugs

- Version: 0.3.0
- Entry: pool-string-refactor
- Date: 2026-11-17
- Bump: minor

## Added
- `PoolString::GetStats()` (`PoolStringStats`): strings and bytes by source (runtime, static, adopted), blocks, table capacity, load and longest probe, hits, misses, write locks, batch calls, dead entries of adopted blocks, heap-fallback blocks. A hit adds no shared write (per-thread counter slots, 64 B per thread that ever interned, never freed).
- `PoolString::InternBatch(texts, out)` (hashes every text; for outside data) and `InternBatch(texts, hashes, out)` (trusts the hashes; checked in Debug): one shared pass, one unique lock, one table growth, one allocation. `FileSystem::EnumerateFiles`, `SaveGameService::List` and `ConfigManager` use it.
- `PoolString::RegisterStatic`: inserts ready `const` entries by pointer, no hashing and no copy; the first entry of a text wins and the value is re-pointed to it.
- `PoolString::Reserve(strings, bytes)`: a hint for the table capacity and one block; a hint that cannot be right (overflow, above `kMaxReservedEntries` or the arena) is ignored.
- `PoolString::AllocateBlock(bytes)` + `AdoptBlock(block, out, options)` (`PoolString/PoolBlock.h`): a caller lays entries out in a pool-owned block and the pool validates the whole block, then links it; the token is spent on success; `PoolAdoptError` is a `CORE_ENUM`.
- The pool's memory comes from `IPoolBlockSource` (`PoolString/PoolBlockSource.h`): a reserved, bump-only virtual-memory arena on native platforms (`kDefaultReservedBytes`), the heap on wasm and as a fallback.
- `ReaderLimits::kMaxPoolStringBytes`: XML, binary and JSON readers refuse a `PoolString` value longer than it with an error before interning; the field keeps its value.
- Shared constants: `MountPoints` (`FileSystem/MountPoints.h`), `ConfigNames` (`Config/ConfigNames.h`), `TexturePaths::memory` (`Renderer/ITexture.h`), `SaveMount::point` (`Save/SaveGame.h`).
- `CI/scripts/check_pool_strings.py` with `CI/pool_strings_allowlist.txt`, run by the ctest `BagiPoolStrings`: a literal in `PoolString::Intern`/`Find` (also through `StringView{"x"}`, `"x"sv`, a ternary of literals), the same `_intern` text in two non-test files, more than two loose `_intern` in one file, a loose `_intern` in a header; `--allowlist` rows need a reason, a stale row is a finding.

## Changed
- The pool table is open addressing over `{hash, entry}` slots and grows; an entry header is 12 bytes (hash, size) with no chain pointer. `PoolString` stays one pointer (`static_assert`) and equality stays a pointer compare.
- A string longer than a block gets a block of its own; before, it was written past the end of a 128 KB page (a heap overflow reachable from a save or content file).
- `"text"_intern` constants are ready entries in the program image, registered on first use without hashing or copying; a constant no longer puts anything into the pool's blocks.
- `StaticPoolString` compared with a `PoolString` through `>`, `<=`, `>=` recursed forever; it now compares the texts.
- The process-wide pool is never destroyed, so a static destructor may still read a `PoolString` at exit.

## Breaking
- `PoolString::Intern("literal")` and `PoolString::Find("literal")` do not compile (deleted overloads for a `const char[N]`). Adapt: write `"literal"_intern` for a code constant, `PoolString::Intern(StringView(buffer))` for a runtime `char` buffer, `PoolString::Find(StringView{"literal"})` to ask whether a text was interned; a ternary of two literals becomes `c ? "a"_intern.ToPoolString() : "b"_intern.ToPoolString()`.
- `DeserializeError::path` and `errorMessage` are `String`, not `PoolString`; error texts no longer go into the pool. Adapt: drop `.ToStringView()` on them (a `String` converts to `StringView`) and write `.c_str()` for `.CStr()`.
- A `PoolString` field longer than `ReaderLimits::kMaxPoolStringBytes` (4096 bytes) is now a load error and keeps its default. Adapt: keep names, ids and keys short; a longer text belongs in a `String` field, or the game raises the constant.
- `PoolStringChain` is removed. Adapt: for a key of a name and a number use a `struct { PoolString name; int index; }` with a hash over both fields.
- The constants rules hold for a game's sources once it runs the check. Adapt: add `CI/scripts/check_pool_strings.py --root . --allowlist <the game's allowlist>` next to the engine's checks (`.claude/rules/build-project.md`), move the loose `_intern` of a file into a file-local `struct Ids { static constexpr auto name = "name"_intern; }`, and a text used in two files into one structure in a header.
