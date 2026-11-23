# XML load without name copies, Warn scopes in an arena, node files parsed once per expansion

- Version: 0.11.3
- Entry: xml-load-scratch
- Date: 2026-11-23
- Bump: patch

## Added
- `IDeserializer::ReadAttributeView(name, outView)`: the text of an attribute as a view, valid until the next read; the default goes through a String the reader reuses, `XmlDeserializer` returns the DOM's text with no copy.
- `XmlDocument::ReadFileInto(path, arena, maxBytes)`: a file's bytes as a view into a `LinearArena`, with the checks of `ReadFileBytes`.

## Changed
- An XML scene or config load allocates about 40 times less (a scene of 600 groups: 32.5k allocations and 44.6 MB before, 739 and 2.9 MB now): the scopes of Warn mode keep their name lists in an arena of the deserializer, field and factory type names are read as views, arrays walk a cursor instead of building an element list, the config file is read into scratch and `XmlSerializer` copies no names.
- `ExpandNodeReferences` asks its loader once per distinct referenced file in a call, a failure too; every reference to a file that did not load still gets its own error. Adapt: a game's loader that counted calls per reference counts distinct files now.
