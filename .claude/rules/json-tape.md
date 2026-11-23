---
paths:
  - "src/Modules/BECore/Control/Json*"
  - "tests/BECore/Json*"
  - "tests/Fuzz/FuzzJsonDeserializer.cpp"
---

# Reading JSON without a DOM: `JsonTape`, `JsonView`, `JsonDeserializer::Load`

The module: `src/Modules/BECore/Control/CLAUDE.md`; what the reflected reader accepts: `Reflection/CLAUDE.md`, "JSON shapes of the reflection pair". This is the first slice of the entry `json-deserializer`: the callers of `Json` (control service, handlers, rooms, widgets) move to the tape in the later slices, and the last one removes nlohmann.

## `JsonTape` (`Control/JsonTape.h`)

`Parse(text)` tokenizes one JSON text in one pass, with no recursion, into a `Vector<JsonToken>` the object keeps. A token is 20 bytes: kind (`Null`, `Bool`, `Integer`, `Number`, `String`, `Array`, `Object`), flags (a string has an escape), `offset` / `length`, `next` (the index after the subtree; a scalar: `index + 1`) and `count` (members or items). A string's span is its content between the quotes, escapes not decoded; a scalar's is its text; a container's is its whole `{...}`, so its raw text is a view. An object is its token, then key token and value, key after key (a key's `next` is its value). `Integer` is `-?(0|[1-9][0-9]*)` with no fraction and no exponent and is not judged against 64 bits: only a reader that knows the target says "out of range".

- **The tape borrows the text.** The text must outlive the tape, every view and every read; `Parse` clears and refills the buffers (capacity kept), so one tape serves many messages and a text of a shape it has seen allocates nothing (`JsonTapeTest`, `JsonDeserializerTextTest`, with `ExpectNoAllocations`: counted in `macos-profile` only, the other presets skip).
- It never aborts or throws: a bad text is a `JsonParseError` (a literal message, the byte offset) and the tape is empty.

**Differences from `ParseJson`** (the fuzz target `FuzzJsonDeserializer` compares both and excludes exactly these):
- The nesting is at most `ReaderLimits::kMaxJsonNesting` (256) open containers and the text `kMaxJsonDocumentBytes` (64 MiB; offsets are `uint32_t`); `ParseJson` takes any depth and the transports keep their own smaller limits (`kMaxRequestBytes`, `kMaxRequestDepth`), so no `IsJsonNestedDeeperThan` is needed in front of the tape.
- A UTF-8 byte order mark is refused (the DOM skips it).
- A NUL byte outside a string is an error (nlohmann's lexer takes it for the end of the input, so `{}` + NUL + anything is a document to it).
- A number beyond a double (`1e999`) is a valid `Number` (the DOM refuses the document: "number overflow"); `JsonView::AsDouble()` gives infinity, and a reader of a `float` / `double` refuses a non-finite value as before.
- The first of two equal keys wins at lookup (the DOM keeps the last); no check is made, a check would be quadratic per object.
- `-0` is an `Integer` that a `double` field reads as `-0.0` (the DOM keeps the integer 0).

## `JsonView` (`Control/JsonView.h`)

A copyable cursor (tape pointer and token index): the read-only document that replaces `const Json&` where no reflected type exists. Every read is checked: a wrong kind is `nullopt` or false, never an assertion on client text. `Find(key)` takes the first member of that name from the start or from a caller-held cursor (wrapping once); a key written with an escape matches its decoded name. `Items()` / `Members()` iterate in document order. `Raw()` is the text of any value, a container too (a string with its quotes). `AsInt64` / `AsUint64` give a value only for an `Integer` token that fits; `AsDouble` an `Integer` or `Number` (`from_chars`, `strtod` on a bounded copy for what `from_chars` refuses); `Text()` is a view of a string with no escape, `ReadText(String&)` decodes any string into the target's capacity (`\u0000` kept). A view is valid while its tape and text are.

## `JsonDeserializer` (`Control/JsonDeserializer.h`)

The same `IDeserializer` rules as before (strict values, the messages, findings, `ReaderLimits`), now walking the tape. `Load(text)` parses into the reader's own tape, `Load(tape)` borrows the caller's (many readers may share one); both reset the scopes, the path, the errors **and the findings** (the private `Reset` calls `ClearWarnings`) and the asked names, and keep the settings (diagnostics, purpose filter), so one reader serves many messages. A text that does not parse is one error with the empty path, `Invalid JSON: <message> at offset <n>`, and every read then fails as for a root that is not an object.

- A key is found among the members of the open object from a cursor after the last match, so a document in the order the type reads costs one compare per field and any other order is found too. The unknown-key findings come in the document's order (the DOM gave them alphabetically); a scope reports when it ends, so a nested object's findings come before its parent's.
- A rejected number quotes its token's text as written; an integer token past 64 bits is `Invalid integer value '<text>' (out of range)` for an integer target and a number for a `float` / `double`.
- **Zero allocations in a steady state:** a second `Load` of a message of the same shape into the same target allocates nothing in `Off`, `Warn` and `Error` (clean documents; a finding or an error allocates), provided the target's containers kept their capacity, its strings fit what they held and the field names fit a string's inline buffer. `JsonDeserializerTextTest` pins it; `JsonReadBenchTest` (disabled, run by hand in `macos-profile`) prints allocations and time per read against the DOM path.
- `JsonDeserializer(const Json&)` is the **interim bridge** (it writes the DOM as text into a member `String` and loads that, so the DOM's own number text is what a message quotes); it exists so `ReflectionControl`, `GamePanelModel` and the tests compile until their slices move, and the last slice deletes it.
