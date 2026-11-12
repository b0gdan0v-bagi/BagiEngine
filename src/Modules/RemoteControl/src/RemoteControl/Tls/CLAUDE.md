# Tls: the client TLS stream behind one interface

`WebSocketClient` opens `wss://` through `ITlsStream` (`TlsStream.h`), a client TLS byte stream shaped like `Socket`, with one backend per platform chosen at build time (the models are `BECore/Profiler/IProfilerBackend.h` and the renderer backends). The browser has its own `wss://` (RemoteControl is not built there). The server never terminates TLS itself: the hosting platform does.

## The contract

- **Connection-level:** the stream owns the TCP connection (a system TLS library like Network.framework cannot be layered on a file descriptor). `Connect(TlsConnectParams)` resolves, connects and does the handshake; it honours `cancel` (within `Socket::kWakeInterval`), `connectTimeout` for the TCP connect and `handshakeDeadline` (one `steady_clock` time point: the caller's single deadline for TLS and for what it does next) and names `host:port` in its errors.
- **Threads, as `Socket`'s:** after `Connect()` one thread in `Receive()`, one in `SendAll()` and one in `ShutdownBoth()` / `Close()` at once. `ShutdownBoth()` wakes the other two. `Close()` and the destructor are safe in every state.
- **Errors:** `SocketError` (`Closed`, `TimedOut`, `Failed`) and a text, `GetLastError()` (a copy, any thread), for the log and the player: certificate, host name, handshake, reset.
- **The check is never off.** The chain against the system trust store and the host name (SNI) are always verified. The one knob is `TlsClientOptions::additionalTrustAnchorsDer`: extra CA certificates trusted *in addition* (the chain, the validity and the host name are still checked); it exists so a test can trust its own CA. No "insecure" option, no environment variable, no `#ifdef` in a release build.
- `IsTlsSupported()` is false on a platform without a backend (the stub of `TlsStream.cpp`: `Connect()` fails with `tls_unsupported: ...`). `WebSocketClient::Open()` refuses `wss://` at once only when the factory is the default and `!IsTlsSupported()`.

## Adding a backend

A new file `<Name>TlsStream.cpp` in this folder, the whole file inside `#ifdef PLATFORM_<X>`, that defines `IsTlsSupported()` and `MakeTlsStream()`; the stub of `TlsStream.cpp` gets the matching `#if !defined(...)` so exactly one definition exists. `be_add_module` globs every `src/*.cpp`; `CMakeLists.txt` only links the system libraries.

## The fake backend

`tests/BECore/Tls/FakeTlsStream.h` is a scripted backend over a real loopback `Socket`, so everything that uses an `ITlsStream` runs on every platform without a certificate. The client sends `FAKE-HELLO <serverName> <anchor count>\n`; the test server answers `FAKE-OK\n` or `FAKE-REJECT <reason>\n`; then every byte is XOR-ed with `kFakeTlsMask` both ways. `FakeTlsServerSide` is the other end for a hand-written server on an accepted `Socket`. `WebSocketClientSettings::tls.factory` takes `FakeTlsFactory()`.
