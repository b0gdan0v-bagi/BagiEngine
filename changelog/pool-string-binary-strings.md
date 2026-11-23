# Binary content version 3: the PoolString texts of a .bin are read straight into a pool block

- Version: 0.16.1
- Entry: pool-string-binary-strings
- Date: 2026-11-23
- Bump: patch

## Added
- `BECore/Reflection/BinaryContent.h`: `LoadContentFile` / `LoadContentBytes` read a `.bin` and adopt its pool section as one block of the `PoolString` pool, with a checksum, the id of the hash function and, by default in builds with asserts, a check of the stored hashes (`BinaryContentOptions::verifyHashes`); a section written with another hash function is rehashed. The fuzz target `FuzzBinaryContent` covers it. Adapt: nothing.

## Changed
- `BinarySerializer` (the cook and the saves) writes binary version 3: entry names in a name table, `PoolString` texts in a pool section laid out as pool entries, duplicates removed. Versions 1 and 2 stay readable, so cooked content and saves of a game load as before and become version 3 at their next cook or save. `ConfigManager` reads a cooked `.bin` through the content loader. Adapt: nothing.
