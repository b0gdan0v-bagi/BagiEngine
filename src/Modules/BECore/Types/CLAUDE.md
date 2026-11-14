# Types (BECore)

The engine's names for the types it uses (`Types.h`, in the PCH): `String`, `StringView`, `FixedString<N>`, `Vector`, `SmallVector<T, N>`, `FixedVector<T, N>`, `Array`, `Span`, `HashMap`, `Map`, `FixedHashMap`, `FlatMap`, `FlatSet`, `Optional`, `UniquePtr`. Each is a `using` of the EASTL type (`Optional` of `std::optional`), so the implementation, or later an allocator, is chosen in one place.

**Rule:** engine and game code spell these names, not `eastl::` or `std::`; those appear directly only in this folder and in the reflection traits (`Reflection/DataAccessor.h`, `SerializationTraits.h`, `PropertyVisitorDispatch.h`), which name templates on purpose. A line that must keep a `std::` type for a standard or third-party API says why with `// std: <why>`.

- **`SmallVector` / `FixedVector`:** `SmallVector<T, N>` keeps N elements inside the object and then moves to the heap; `FixedVector<T, N>` never allocates and must not pass N. Pick by that, not by the size.
- **Strings as keys:** `HashMap<String, V>`, `Map<String, V>`, `FixedHashMap<String, V, N>` and `FlatMap<String, V>` use `StringHash` / `StringEqual` / `StringLess`, which are transparent over `StringView`: `find`, `contains`, `count`, `at`, `erase` take a view or a literal and make no `String`; do not write `.find(String{x})`. The hash is the one `eastl::hash<eastl::string>` gives (a test pins the iteration order), so replacing a map keeps its order. A different key type keeps its own hash; a custom hash or comparison goes in the extra template argument.
- **`FlatMap` / `FlatSet`** (a sorted vector) are for new code where the map is small, rarely changed and iterated often; the default container for a lookup stays `HashMap`.
- **Text:** `ToString(number)` (an integer's digits, a floating-point value as `std::to_string`; no overload for `bool` and characters) and `Format` / `AppendFormat` (`Format/Format.h`) make strings; do not build one through `std::to_string`.
- **The `std::` boundary:** `ToStd(StringView)` / `FromStd(std::string_view)` copy nothing; `ToStdString` / `FromStdString` make a copy and are named so. Use them at the call that meets `std::filesystem`, `nlohmann` or the like.

Tests: `tests/BECore/TypesTest.cpp`.
