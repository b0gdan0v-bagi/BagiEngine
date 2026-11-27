# The C++ rules

You will read the rules a C++ file of the engine follows, each with the reason the engine has it and a few lines of code that obey it, run the tests that compile that code, and then see where your Python fits next to the C++. You write no C++ here.

You need the build from [Run the demo](run-the-demo.md) and nothing else. These are the **engine's** rules, and they are stricter than C++ asks for: the repository's `CLAUDE.md` holds the exact list ("Code style", "DO" and "DON'T"), and the nested `CLAUDE.md` of each folder holds the exact contract of its rule. This page explains, and a page that one of those files contradicts is wrong.

Every C++ block below is an excerpt of `tests/BECore/GuideCppRulesExample.h`. The test `GuideCppRulesExampleTest` compiles and runs it, so the code does what the text says. Run it once:

<!-- command -->
```bash
cmake --build --preset macos-debug --target BagiEngineTests
(cd build/macos-debug && bin/BagiEngineTests --gtest_filter='GuideCppRulesExample*')
```

<!-- illustration: the end of the run on the writer's machine; the skipped case is explained in the section on zero allocations -->
```text
[  PASSED  ] 7 tests.
[  SKIPPED ] 1 test, listed below:
[  SKIPPED ] GuideCppRulesExampleTest.ALabelRebuildsItsTextOnlyWhenTheScoreMovedAndAnUnchangedScoreAllocatesNothing
```

## The engine's own type names

| In Python | The engine spells | What differs |
|---|---|---|
| `str` | `String`, and `StringView` | a `String` owns its text; a `StringView` is a look at text that somebody else owns, so passing one copies nothing |
| `list` | `Vector<T>` | the element type is written |
| `dict` | `HashMap<K, V>` | `Map<K, V>` is the ordered one |
| a value or `None` | `Optional<T>` | the "nothing" case is `std::nullopt` |
| the only owner of an object | `UniquePtr<T>` | made with `MakeUnique<T>(...)` |

The rule: engine and game code write these names, not `eastl::` or `std::`. The reason is in `src/Modules/BECore/Types/CLAUDE.md`: each name is a `using` of a type of EASTL (a library of containers that the engine uses in place of the standard ones; `Optional` is the `std::` one, which other libraries speak), so the library and the allocator are chosen in one place. The owning types (`String`, `Vector`, the maps) take the engine's allocator, so `String` is not EASTL's own `eastl::string`, and the two never convert except by copy. A line that must keep a `std::` type for a standard or third-party API says why with a comment, `//-NoStd: <why>`. The test `BagiTypesCheck` finds the rest.

A map keyed by `String` has one more trick, from the same file ("Strings as keys"): `find` takes a `StringView` or a literal and makes no `String` to ask. The lookup of the example:

<!-- source: tests/BECore/GuideCppRulesExample.h -->
```cpp
[[nodiscard]] Optional<int32_t> Find(StringView name) const {
    const auto found = _coins.find(name);
    if (found == _coins.end()) {
        return std::nullopt;
    }
    return found->second;
}
...
HashMap<String, int32_t> _coins;
```

From `tests/BECore/GuideCppRulesExample.h`. The Python habit it replaces is `coins.get(name)`, which returns `None` for a missing key; here the caller gets an `Optional` and the compiler makes it say what happens when it is empty.

## `PoolString`: a name that compares as a pointer

Node names, field names and event names are compared all the time. A `PoolString` is one pointer to the one entry the engine keeps for a text, so two of them are equal when the pointers are (`src/Modules/BECore/PoolString/CLAUDE.md`, "What a PoolString is"). There are two ways to get one:

- a name that **the code knows** is a constant: `"text"_intern`, kept as a named member of a structure, so one text has one place and a typo is a compile error;
- a name that **comes from data** (a key read from a file) is `PoolString::Intern(text)`.

<!-- source: tests/BECore/GuideCppRulesExample.h -->
```cpp
struct Ids {
    static constexpr auto level = "level"_intern;
    static constexpr auto coins = "coins"_intern;
};
...
[[nodiscard]] inline bool IsLevelField(StringView nameFromData) {
    return PoolString::Intern(nameFromData) == Ids::level;
}
```

From `tests/BECore/GuideCppRulesExample.h`. Two things to remember. **The pool never frees**: an entry lives to the end of the process, so only names from data go in, never text for display, a message or an error. And **`Intern("text")` with a literal does not compile**, because the engine deleted that form to send you to the constant:

<!-- illustration: a line that breaks the rule, so it exists in no file; the ban is in PoolString/CLAUDE.md, "Constants in code" -->
```cpp
PoolString::Intern("level");  // does not compile: write "level"_intern in a structure of names
```

The test `BagiPoolStrings` holds the rest (the same text in two files, a loose `_intern` in a header).

## `IntrusivePtr` and `New<T>`

In the engine an object that several holders share carries its own counter (it derives from `RefCounted`), and `IntrusivePtr<T>` is the pointer that adds and removes a count. `New<T>(...)` makes the object and returns the first owner (`src/Modules/BECore/RefCounted/CLAUDE.md`):

<!-- source: tests/BECore/GuideCppRulesExample.h -->
```cpp
class Treasure : public RefCounted {
public:
    explicit Treasure(bool& destroyed) : _destroyed(destroyed) {}

    ~Treasure() override {
        _destroyed = true;
    }
...
[[nodiscard]] inline IntrusivePtr<Treasure> DigUp(bool& destroyed) {
    return New<Treasure>(destroyed);
}
...
class Pouch {
public:
    explicit Pouch(IntrusivePtr<Treasure> treasure) : _treasure(std::move(treasure)) {}

private:
    IntrusivePtr<Treasure> _treasure;
};
```

From `tests/BECore/GuideCppRulesExample.h`. The test shows the count at 2 with a pouch and a pointer, and the object dying only when the last one lets go. So: no `new` and `delete` in code that uses the engine (`CLAUDE.md`: "`IntrusivePtr<T>` (no raw `new` / `delete`)"), and no `std::shared_ptr`: the engine has none, it shares through `IntrusivePtr` (`Types/CLAUDE.md`, "Sets, pairs, pointers"). **Who owns it:** an `IntrusivePtr` owns; a function that only looks takes a reference, and owns nothing. `RefCounted` counts for one thread; `RefCountedAtomic` is for an object whose last holder may be any thread (`RefCounted/CLAUDE.md`).

## No exceptions: `std::expected` and `ASSERT`

Exceptions are disabled in this build (`CLAUDE.md`, "Build and test": "Exceptions are disabled"; the flag is in `cmake/BagiEngine.cmake`). A `try` does not compile: the writer's compiler says `cannot use 'try' with exceptions disabled`. So the two things an exception does in Python are split:

**A recoverable error is a value.** A bad text from a person or a file is not a surprise, so the function returns `std::expected<T, E>`: the value, or the error. The caller reads it where the Python code would have `try` and `except`:

<!-- source: tests/BECore/GuideCppRulesExample.h -->
```cpp
CORE_ENUM(ParseError, uint8_t, Empty, NotANumber, OutOfRange)
...
[[nodiscard]] inline std::expected<int32_t, ParseError> ParseLevel(StringView text) {
    if (text.empty()) {
        return std::unexpected(ParseError::Empty);
    }
    const Optional<int32_t> number = StringUtils::ParseNumber<int32_t>(text);
    if (!number) {
        return std::unexpected(ParseError::NotANumber);
    }
    if (*number < kFirstLevel || *number > kLastLevel) {
        return std::unexpected(ParseError::OutOfRange);
    }
    return *number;
}
...
[[nodiscard]] inline int32_t LevelOrFirst(StringView text) {
    const auto level = ParseLevel(text);
    if (!level) {
        LOG_WARNING("Cannot read level '{}': {}", text, level.error());
        return kFirstLevel;
    }
    return *level;
}
```

From `tests/BECore/GuideCppRulesExample.h`. The `Load` of [A save](save-game.md) does the same: it logs a refusal once and leaves the live state untouched.

**A bug is an `ASSERT`.** A caller that breaks a contract is a mistake in the code, not an input, and it stops a debug build with the line:

<!-- source: tests/BECore/GuideCppRulesExample.h -->
```cpp
constexpr int32_t kPercentScale = 100;
...
[[nodiscard]] inline int32_t PercentOf(int32_t part, int32_t whole) {
    ASSERT(whole > 0, "PercentOf needs a positive whole");
    return part * kPercentScale / whole;
}
```

From `tests/BECore/GuideCppRulesExample.h`. `ASSERT`, `EXPECT` and `FATALERROR` **compile out in a Release build** (`CLAUDE.md`: "no side effects inside"; `ASSERT` and `FATALERROR` become `((void)0)` in `src/Modules/BECore/Assert/AssertMacros.h`), so nothing the program needs may happen inside one. Python's `assert` is removed by `python -O` in the same way:

<!-- illustration: the trap, so it exists in no file; the call is gone from a Release build and the list is never advanced -->
```cpp
ASSERT(queue.PopFront());  // wrong: in a Release build the pop never happens
```

## `CORE_ENUM`: an enum that knows its names

`enum class` is not used. `CORE_ENUM(Name, type, values...)` makes an enum whose values have names the engine can read: a value formats as its name in a log line (`Format/CLAUDE.md`), a field of such an enum is read and written by name (`src/Modules/BECore/Reflection/CLAUDE.md`, the bullet "`GetProperty` / `SetProperty`"), and `EnumUtils<E>` converts a name back (`src/Modules/BECore/Utils/EnumUtils.h`). The enum of the example is the first line of the previous section. A person reading the log sees `NotANumber`, not `1`; this is the text the test pins:

<!-- illustration: the text of the warning that LevelOrFirst("seven") logs -->
```text
Cannot read level 'seven': NotANumber
```

## `Format` and the log macros

`Format("{} has {} coins", name, coins)` is the engine's `f"{name} has {coins} coins"`: the string is a literal checked when the code compiles, so a `{}` with no argument does not build (an extra argument is ignored: `src/Modules/BECore/Logger/CLAUDE.md`). `AppendFormat` writes into a `String` you keep:

<!-- source: tests/BECore/GuideCppRulesExample.h -->
```cpp
[[nodiscard]] inline String DescribeCoins(StringView name, int32_t coins) {
    return Format("{} has {} coins", name, coins);
}
...
inline void AppendCoins(String& text, StringView name, int32_t coins) {
    AppendFormat(text, "{} has {} coins", name, coins);
}
```

From `tests/BECore/GuideCppRulesExample.h`. A log call takes the format and the arguments itself: `LOG_WARNING("Cannot read level '{}': {}", text, error)`. **Never `LOG_X(Format(...))`**: it still compiles, but it builds a heap `String` for each line, while the logger formats into a buffer of its own (`src/Modules/BECore/Logger/CLAUDE.md`); the test `BagiLogCalls` fails on it. A type of your own becomes printable by a `Formatter<T>` (`src/Modules/BECore/Format/CLAUDE.md`, "Making a type formattable"); a `CORE_ENUM` already is.

## Zero allocations in a steady frame

The engine's principle, in the words of the repository `README.md`: a phone and the browser pay for every allocation in frame time and battery, so a frame in which nothing changed makes nothing new. This is the engine's stated reason, not a number measured here. "Steady" means that nothing the screen shows has changed since the last frame. Two habits follow: a text that is drawn every frame is **a member, rebuilt only when the number it shows has moved**, and a list is a member that keeps its room, not a new one per frame.

<!-- source: tests/BECore/GuideCppRulesExample.h -->
```cpp
class ScoreLabel {
public:
    [[nodiscard]] const String& Text(int32_t score) {
        if (_shown != score) {
            _text = Format("Your best score so far: {}", score);
            _shown = score;
        }
        return _text;
    }

private:
    String _text;
    Optional<int32_t> _shown;
};
```

From `tests/BECore/GuideCppRulesExample.h`. The text is longer than a `String` keeps inside itself, so every build of it takes memory from the heap: that is what the guard saves. The case of the test asks for the text of one score a hundred times and checks that every answer is the same member and that none of them allocated, and that a moved score does allocate, so a counter that counts nothing cannot pass it. **You can see an allocation only in the profile preset**, which counts them (`macos-profile`: `cmake/CLAUDE.md`; what the tracker counts is `src/Modules/BECore/Profiler/Alloc/CLAUDE.md`). In every other preset the case still runs its other checks and is **skipped, with its reason**, as in the output at the top, so a build that cannot count does not report "allocates nothing". To see it count (the writer saw all 8 cases pass there):

<!-- command -->
```bash
cmake --preset macos-profile
cmake --build --preset macos-profile --target BagiEngineTests
(cd build/macos-profile && bin/BagiEngineTests --gtest_filter='GuideCppRulesExample*')
```

A callback that must never allocate is a `FixedFunction<Bytes>`, which has no heap path, where the usual `MoveOnlyFunction` takes one allocation for a callable that does not fit its buffer (`src/Modules/BECore/Utils/CLAUDE.md`, "The callable types"). `std::function` is not used.

## How the rules are held

You do not have to remember them; the repository checks them. These are tests, and the five below carry the label `extended`, so they run in `ctest --preset macos-debug-all` and not in the everyday `ctest --preset macos-debug` (`CLAUDE.md`, "Build and test"; `tests/CMakeLists.txt`):

- `BagiTypesCheck`: a raw `std::` or `eastl::` type that the engine's names replace (`CI/scripts/check_types.py`);
- `BagiPoolStrings`: the rules of the section on `PoolString` (`CI/scripts/check_pool_strings.py`);
- `BagiIncludeStyle`: a quoted include is for a source's own header only, every other include is `<...>` from an include root;
- `BagiHeadersStandalone`: a header compiles alone;
- `BagiLogCalls`: a `LOG_X(Format(...))`.

A red one names the file, the line and the rule, with the `CLAUDE.md` section that explains it:

<!-- illustration: the shape of a finding of BagiPoolStrings; the file name is made up -->
```text
src/Modules/Example/Thing.cpp:42: [literal] PoolString::Intern of a literal text: name it as a constant, "text"_intern (PoolString/CLAUDE.md, "Constants in code")
```

What you do is use the engine's name or the constant, as the finding says. You do not add the line to a baseline: `BagiTypesCheck` has one (`CI/types_baseline.txt`) for code not yet migrated, and it only shrinks, so a new hit fails (`tests/CMakeLists.txt`).

## Where your Python goes

| Your Python habit | What the engine has |
|---|---|
| `d = {}`, `d.get(k)` | `HashMap`, a `Find` that returns `Optional` |
| `None`, or a value | `Optional<T>` |
| `try` / `except`, `raise` | `std::expected<T, E>`; `ASSERT` for a bug |
| `f"{x}"` | `Format("{}", x)` |
| a `str` used as a key or a name | `PoolString` for names; `String` for text |
| a class with fields set in `__init__` | a class marked `BE_CLASS` with `BE_REFLECT_FIELD` fields |

And the places where your Python does the work, with no C++ in them:

- **The control API.** A running game answers calls, and `GameSession` turns them into plain Python: [The control API from Python](control-api.md).
- **A field needs no Python glue.** A field marked `BE_REFLECT_FIELD` is reachable by its name from the file, the inspector and the control API: [Config and reflection](config-and-reflection.md).
- **Scenarios and tests.** A scenario is a recorded session with a reference picture, and `ctest` runs it: [Test a game](test-a-game.md).
- **The tools under `tools/`.** `tools/CLAUDE.md` lists them; the ones a Python developer meets are:
  - `tools/control`: the clients that drive a game through the control API, the scenario player and the MCP server (`tools/control/CLAUDE.md`);
  - `tools/perf`: the benchmark runner of the skills `perf` and `perf-investigate` (`tools/perf/CLAUDE.md`);
  - `tools/qa`: the library behind the skill `/qa`, which plays a game by a charter and checks every step (`tools/qa/CLAUDE.md`);
  - `tools/shots`: before and after pictures of a game's scenes and a text diff of them (`tools/shots/CLAUDE.md`);
  - `tools/migrate`: rewrites a game's content files to the format the engine reads now (`tools/migrate/CLAUDE.md`);
  - `tools/codemod`: moves `eastl::` and `std::` names to the engine's names, and `LOG_X(Format(...))` to the formatted form (`tools/codemod/CLAUDE.md`).

Each of these six says it is Python 3.10 or later, and all but `tools/control` also say "standard library only"; `tools/CLAUDE.md` states no rule for every tool, so check the file of the one you run.

## What you did

You read the engine's C++ rules with their reasons, saw each obeyed in code that a test runs, and found where your Python goes.

Next: [Where the data lives](where-the-data-lives.md)
