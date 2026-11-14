# tools/codemod

Moves the `eastl::` and `std::` type names of C++ sources to the BECore type layer (`src/Modules/BECore/Types/CLAUDE.md`). Standard library only, Python 3.10+; the engine does not use it, a game runs it after `Update engine`. A rename done by hand over thousands of uses would be slow and would miss some; the tool does the text, the compiler finds what the text cannot know.

```
python3 engine/tools/codemod/codemod.py --root src --family optionals|views|strings|containers|maps|utility|all [--dry-run] [--fail-on-change] [--report]
```

- `--root` is the folder walked (`.` in the engine; a game runs it on its `src` and on its `tests`: test code moves like the rest), `--family` one or a comma list. Walks `*.h *.hpp *.inl *.cpp`; skips the vendored zones, `build*`, `.venv`, the type layer, `pch.h`, the reflection traits that name `eastl::` templates on purpose and the twin tests of the aliases, matched by their path from the repository (the nearest `.git` above `--root`) so `--root tests` skips them too (the list is in the script).
- **Code only:** comments and string / raw-string literals are blanked for matching and kept as they were; template arguments are read with a bracket scanner, so `eastl::fixed_vector<eastl::pair<A, B>, 4>` is right.
- **Idempotent:** the rules run until nothing changes, so a second run changes nothing. `--dry-run` writes nothing and prints the counts per rule; `--fail-on-change` writes nothing and exits 1 when anything would change: the "family is done" check. `--report` lists the notes: what the tool found and left (an allocator argument, `eastl::optional<T&>`, a `std::to_string`, a `find` / `contains` / `count` / `at` whose argument builds a temporary string, a class template written without arguments).
- **`//-NoStd: <why>`** (like PVS-Studio's `//-V` marks: no space after `//`, a colon after `NoStd`) keeps its line as it is, in every family: a `std::` type that meets `std::filesystem`, `nlohmann`, a socket or an SDL API says so there, and so does a test whose subject is the `std::` spelling. A marker line that holds only the comment covers the next line too; a marker after code covers that line alone. Only this exact spelling counts: the old `// std: <why>` and a comment that starts with a `std::` name are no markers, so a forgotten old marker shows up as a change.
- **CTAD is not relied on** (an alias is deduced only by newer compilers): a class template name with no `<` before `{`, `(` or a variable (`std::pair{a, b}`, `std::array x{...}`, `std::tuple(...)`) is reported and left; write the type arguments by hand.
- A name outside `namespace BECore` (and no `using namespace BECore` before it) becomes `BECore::String`.

## Families

| Family | Rules |
|--------|-------|
| `optionals` | `eastl::` / `std::optional` to `Optional` (a reference argument: reported); `eastl::nullopt(_t)`, `make_optional`, `in_place(_t)` to the `std::` ones; `#include <EASTL/optional.h>` goes when no `eastl::optional` is left |
| `views` | `eastl::` / `std::string_view` to `StringView`; `std::string_view{X.data(), X.size()}` to `ToStd(X)`, `eastl::string_view(X.data(), X.size())` to `FromStd(X)` (where `X` is already an `eastl::string` the build says so: write `StringView(X)`) |
| `strings` | `eastl::string`, `std::string` to `String`; `eastl::fixed_string<char, N[, b]>` to `FixedString<N[, b]>`; `eastl::to_string` and `eastl::string(std::to_string(x).c_str())` to `ToString`; other `std::to_string` reported |
| `containers` | `Vector` (one argument; an allocator: reported), `eastl::fixed_vector<T, N>` and `<T, N, true>` to `SmallVector`, `<T, N, false>` to `FixedVector`, `std::` / `eastl::array` to `Array`, `Span`, `std::` / `eastl::unique_ptr<T[, Deleter]>` to `UniquePtr` |
| `maps` | `eastl::hash_map`, `eastl::unordered_map`, `std::unordered_map` to `HashMap`, `eastl::` / `std::map` to `Map`, `eastl::` / `std::set` to `Set`, `eastl::hash_set`, `eastl::unordered_set`, `std::unordered_set` to `HashSet`, `eastl::fixed_hash_map` to `FixedHashMap` (extra allocator arguments: reported); lookups with a temporary string reported, dropped by hand at the receivers that are string-keyed |
| `utility` | `pair` to `Pair`, `make_pair` to `MakePair`, `variant` to `Variant`, `std::tuple` to `Tuple`, `shared_ptr` to `SharedPtr`, `make_shared` to `MakeShared`, `make_unique` to `MakeUnique` (`eastl::` and `std::` alike; `std::monostate`, `get`, `get_if`, `visit`, `holds_alternative`, `tuple_size`, `tie` stay). A file that had an `eastl::variant` gets a note at its `eastl::get` / `visit` / `holds_alternative` / `monostate`: they move to the `std::` ones by hand |

An `#include <EASTL/...>` the file no longer needs stays: `strip_pch_includes.py` removes the PCH ones.

## After a run

Build; the compile errors are the boundaries (`std::string` that meets a `std::` API, `FromStd` of an `eastl::string`, a `std::hash` key without an `eastl::hash`). A function or variable named like an alias (`Span`, `Map`) is a compile error too: rename it. A `[map lookup]` note can be a `String::find(String(x))` and no map: only a `HashMap<String, V>` / `Map<String, V>` receiver takes the view. Then `format_sources.py --changed` and `strip_pch_includes.py`. Tests: `.venv/bin/python -m pytest tools/codemod/tests`.
