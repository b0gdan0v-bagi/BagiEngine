# Control JSON: JsonStringView reads a string without a copy

- Version: 0.12.2
- Entry: json-string-view
- Date: 2026-11-23
- Bump: patch

## Added
- `JsonStringView(const Json& value)` and `JsonStringView(const Json& object, StringView key)` (`BECore/Control/Json.h`): the text of a string value or member as an `Optional<StringView>` without a copy, nullopt when it is not a string (or the member is absent); it points into the `Json` and holds embedded NULs. Adapt: replace `get_ref<const std::string&>()` / `get<std::string>()` reads and their `//-NoStd` marks with `JsonStringView`, and `GetJsonString` where no copy is needed.

## Changed
- `MakeJsonString(StringView)` is documented and tested to keep an embedded NUL. Adapt: nothing.
