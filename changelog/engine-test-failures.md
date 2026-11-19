# Tests: three failures on main (ASan EditorBars, Release control smoke, flaky WebSocket close)

- Version: 0.4.3
- Entry: engine-test-failures
- Date: 2026-11-18
- Bump: patch

## Changed
- `WebSocketClient::Close` keeps its code and reason in the `Closed` event when the server answers the close frame at once; before, the server's answer could replace them.
- The control smoke of a build without the editor leaves `config/WidgetsConfig.xml` out of its schema check, because only the Editor run mode reads that file.
