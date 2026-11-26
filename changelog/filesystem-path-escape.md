# FileSystem: a virtual path cannot leave its layer

- Version: 0.29.9
- Entry: filesystem-path-escape
- Date: 2026-11-26
- Bump: patch

## Added
- `IsConfinedVirtualPath(virtualPath)`: whether a relative path cannot leave the folder it is joined to (no empty or `..` names; on Windows no `:` and no names of dots and spaces).

## Changed
- `FileSystem::ResolvePath`, `PathInTopLayer`, `FileWatcher` and the document references of web UI and RmlUi follow one rule: a relative virtual path must pass `IsConfinedVirtualPath`; a refused one is looked up nowhere and logged as a warning. Adapt: a game that named content with `..` or `//` (`config/../x`) names it directly.
- `PathIsUnder` / `Path::IsUnder` fold `.` and `..` and merge separators before comparing, so `/a/b/../../etc` is not under `/a`. A path of ordinary depth still allocates nothing.
