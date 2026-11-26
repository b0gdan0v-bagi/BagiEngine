# Reflection and config: silent data errors are gone

- Version: 0.28.0
- Entry: reflection-config-correctness
- Date: 2026-11-26
- Bump: minor

## Added
- `FindXmlChild` / `FindXmlAttribute` in `Config/XmlNode.h`: view lookups shared by `XmlNode` and `XmlDeserializer`. Adapt: nothing.

## Changed
- The serializers write every reflected field; `ISerializer::SetSkipDefaults` / `IsSkipDefaults` stay one merge as deprecated no-ops. Adapt: drop the calls (a call warns as deprecated); a file a game wrote before reads the same.
- A `const char*` field or value is written as text by every serializer (it was written as a bool). Adapt: nothing.
- `XmlDocument::SaveToFile` writes a temp file and renames it over the target (a failed save leaves the old file whole); a symbolic link is followed, one that leads nowhere is refused. Adapt: nothing; on Windows a save fails while another handle holds the file open without delete sharing.
- A color text with a bad, out-of-range or missing channel is no color (it read as 0), and a font preload range past U+10FFFF is clamped or dropped with an error. Adapt: fix the content the log names.

## Breaking
- A named XML array (a factory list, a map's `entry`, a hand-written array such as `sprite`) reads only the elements of its name and reports the others; an `item` array still takes any element name. Adapt: run the game with `--strict-config` after `Update engine` and rename the elements it reports.
- `PassKey` is sealed: a class deriving from it, or a `PassKey<T>` specialization, no longer compiles; only `CORE_ENUM` enums use the enum formatter. Adapt: take a `PassKey<T>` parameter instead of deriving; format a plain enum through `EnumUtils`.
- A `"..."_format` literal is checked at compile time and `FormatLiteral` has no constructor from a `StringView`. Adapt: fix the literal the compiler names; build a runtime format string with `Format(...)`.
