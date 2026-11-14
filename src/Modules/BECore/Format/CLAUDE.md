# Format (BECore)

The engine's names for formatting text. Code spells these, not `fmt::`: the library behind them is chosen here, once. `Format.h` is in the PCH; `Formatter.h` (the part a type teaches) is included by `Utils/EnumUtils.h`, which comes before it.

| Name | What it is |
|------|------------|
| `Format(fmt, args...)` / `"{}"_format(args...)` | A `String`. The format string of `Format` is a literal checked at compile time. |
| `AppendFormat(String&, ...)`, `FormatTo(it, ...)` | Into a string or an output iterator. |
| `FormatToN(char*, capacity, ...)` | Into a fixed buffer, allocates nothing, cuts at the capacity, **does not write a terminating zero**; the `FormatResult` has the full `size` and `truncated`. |
| `Print`, `PrintLine`, `Print(FILE*, ...)` | To stdout or a C stream. |
| `FormatString<Args...>` | The type of a checked format string, for a function that forwards to `Format`. |
| `RuntimeFormat(StringView)` | A format string that is not a literal (a string table entry, a user text); parsed when called, so a bad string is a run-time error. Not for a literal. |
| `FormatArg("name", value)` | The argument of `{name}`. It holds a reference to the value: use it inside the expression of the call, never store it. The name is a `const char*` that must outlive the call. |
| `Formattable<T>`, `HasFormatter<T>` | Concepts: fmt can format `T`; a `Formatter<T>` exists. |
| `FormatContext`, `FormatParseContext` | What a `Formatter` reads and writes. |
| `Formatter<T>`, `TextFormatter` | How a type is formatted (below). |

## Making a type formattable

Specialize `Formatter<T>` in `namespace BECore`, before the first format call that names `T` (it is a concept check, and a type that was found unformattable stays so in that translation unit; an incomplete type cannot be detected later):

```cpp
template <>
struct BECore::Formatter<Point> : TextFormatter {
    FormatContext::iterator Format(const Point& p, FormatContext& ctx) const {
        return FormatText(BECore::Format("({},{})", p.x, p.y), ctx);
    }
};
```

- `Parse(FormatParseContext&)` reads the spec (`{:>8}`) and must be `constexpr`; `Format(const T&, FormatContext&) const` writes the value. A specialization names no `fmt::`.
- A type written as text derives from `TextFormatter`: it parses the usual string spec (`{:>8}`, `{:<6}`, `{:.3}`) and `FormatText(StringView, ctx)` writes the text with that spec. Only a type with its own spec writes `Parse` itself.
- Every `CORE_ENUM` type is formattable through a `Formatter` of `Utils/EnumUtils.h` (the name of the value, a string spec works); do not specialize one for it.
- One bridge, `fmt::formatter<T, char>` constrained by `HasFormatter<T>`, hands every such type to fmt. Do not add a second `fmt::formatter` for a type that has a `Formatter`: the two are ambiguous.
- `String` and `StringView` are formattable through `fmt::formatter` specializations in `Format.h`: a layer file may name `fmt::`; nothing else may (the vendored `src/Modules/fmt/` is never edited).

The codemod family `fmt` (`tools/codemod/CLAUDE.md`) moves plain `fmt::` calls of other code to these names; a game runs `codemod.py --family fmt` on its `src` and `tests` after `Update engine`, and a `fmt::` it reports is marked `//-NoStd: <why>` or given a layer name.

Tests: `tests/BECore/FormatTest.cpp`, `tests/BECore/EnumUtilsTest.cpp`.
