# nlohmann/json Module

Vendored copy of [nlohmann/json](https://github.com/nlohmann/json) 3.11.3 (MIT), used by the external control API (`BECore/Control/`).

Do not edit the headers. To update, replace both files from the release assets:

- `https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp` to `nlohmann/json.hpp`
- `https://github.com/nlohmann/json/releases/download/v3.11.3/json_fwd.hpp` to `nlohmann/json_fwd.hpp`

SHA-256 of `nlohmann/json.hpp` (v3.11.3): `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6`. The release publishes no checksum file; the value was compared with the one known for this release.

## Exceptions are off

The engine builds with `-fno-exceptions`, and the `nlohmann_json` target defines `JSON_NOEXCEPTION`, so every error that the library would throw is an `abort()` instead. Checked on AppleClang 17: `at()` on a missing key, `get<T>()` on the wrong type, `value()` on a non-object and `operator[]` by a missing key on a const object all terminate the process. Parsing with `allow_exceptions=false` does not (a bad input is a `discarded` value).

Include it through `BECore/Control/Json.h` and use the checked helpers there on any data that comes from outside the process. The rules are in `src/Modules/BECore/Control/CLAUDE.md`.

The full header is large (about 900 KB): include `nlohmann/json_fwd.hpp` (or `BECore/Control/JsonFwd.h`) in headers and the full one only in `.cpp` files. It is not in the PCH.
