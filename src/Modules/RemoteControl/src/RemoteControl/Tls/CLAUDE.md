# Tls: the client TLS stream behind one interface

`WebSocketClient` opens `wss://` through `ITlsStream` (`TlsStream.h`), a client TLS byte stream shaped like `Socket`, with one backend per platform chosen at build time (the models are `BECore/Profiler/IProfilerBackend.h` and the renderer backends). The browser has its own `wss://` (RemoteControl is not built there). The server never terminates TLS itself: the hosting platform does.

## The contract

- **Connection-level:** the stream owns the TCP connection (a system TLS library like Network.framework cannot be layered on a file descriptor). `Connect(TlsConnectParams)` resolves, connects and does the handshake; it honours `cancel` (within `Socket::kWakeInterval`), `connectTimeout` for the TCP connect and `handshakeDeadline` (one `steady_clock` time point: the caller's single deadline for TLS and for what it does next) and names `host:port` in its errors.
- **Threads, as `Socket`'s:** after `Connect()` one thread in `Receive()`, one in `SendAll()` and one in `ShutdownBoth()` / `Close()` at once. `ShutdownBoth()` wakes the other two. `Close()` and the destructor are safe in every state.
- **Errors:** `SocketError` (`Closed`, `TimedOut`, `Failed`) and a text, `GetLastError()` (a copy, any thread), for the log and the player: certificate, host name, handshake, reset.
- **The check is never off.** The chain against the system trust store and the host name (SNI) are always verified. The one knob is `TlsClientOptions::additionalTrustAnchorsDer`: extra CA certificates trusted *in addition* (the chain, the validity and the host name are still checked); it exists so a test can trust its own CA. No "insecure" option, no environment variable, no `#ifdef` in a release build.
- `IsTlsSupported()` is false on a platform without a backend (the stub of `TlsStream.cpp`: `Connect()` fails with `tls_unsupported: ...`). `WebSocketClient::Open()` refuses `wss://` at once only when the factory is the default and `!IsTlsSupported()`.

`ClientStream` is what `WebSocketClient` holds: a move-only value that is a plain `Socket` (`ws`) or an `ITlsStream` (`wss`, made by `TlsClientOptions::factory` or `MakeTlsStream()`), so the client's reader and writer keep their shape. A TLS failure is the reader's `Closed` 1006 with `GetLastError()` as the reason; `handshakeTimeout` is one deadline for the TLS handshake and the opening request (the stream gets it as `handshakeDeadline`).

## Adding a backend

A new file `<Name>TlsStream.cpp` in this folder, the whole file inside `#ifdef PLATFORM_<X>`, that defines `IsTlsSupported()` and `MakeTlsStream()`; the stub of `TlsStream.cpp` gets the matching `#if !defined(...)` so exactly one definition exists. `be_add_module` globs every `src/*.cpp`; `CMakeLists.txt` only links the system libraries.

## macOS: Network.framework (`NetworkFrameworkTlsStream.cpp`)

Why not Secure Transport: deprecated since macOS 10.15 and TLS 1.2 at most; Network.framework does 1.3 and checks the chain against the system trust store and the host name by default. It is a C API with blocks, so the file stays a `.cpp` (Apple clang has `-fblocks` on). It cannot be layered on a file descriptor, hence the connection-level interface. `CMakeLists.txt` links `Network`, `Security`, `CoreFoundation`.

- **State and lifetime:** everything the blocks touch lives in one `StreamState` that every block holds by `shared_ptr`, under one mutex and one condition variable. The stream never waits for the system to finish a cancel: `Close()` / `ShutdownBoth()` cancel the connection, `Close()` drops our reference, a late callback finds the state alive. Safe in every state, `Connect()` included (it sees `closing` within `kWakeInterval`).
- **Default check:** nothing of the verification is touched, so the system does it (chain, validity, name as the endpoint host). With additional anchors a verify block evaluates the trust itself (`SecTrustEvaluateWithError`, the SSL policy for the host, the anchors added with `SecTrustSetAnchorCertificatesOnly(false)`) and its failure code is kept because it says more than the connection's error. A verify block that returned true without evaluating would be a hole in the check.
- **Failures** are mapped to words by `DescribeNetworkFailure()` (`NetworkFrameworkErrors.h`: pure, tested); the codes go only to the log. The system's own check reports a wrong host name as a generic chain error, so that text says both. A refused connection or an unresolvable name arrives as state `waiting` with an error, which the stream treats as a failure.
- **Receive:** one `nw_connection_receive` stays pending across a `Receive()` that timed out, its data waits in the inbox; end of stream is `is_complete` without content (or a final context). `SendAll()` returns when the send completion ran (data is copied, so a wake-up by `ShutdownBoth()` may leave early).
- `connectTimeout` is the TCP connection timeout option (whole seconds, at least 1); `handshakeDeadline` is the caller's clock for ready.

## Tests against a real handshake

`tests/BECore/Tls/tls_local_check.py` (Python standard library; ctest entry `BagiTlsLocalEndpoint`, skipped with 77 when `ssl` or the executable is missing) starts three local servers on free ports (a TLS WebSocket echo with the valid leaf, one with an expired leaf, a plain one that never answers) and runs `TlsLocalEndpointTest` of `BagiEngineTests` with their ports and the test CA (DER) in the environment: trusted open and echo, no anchor, wrong host name (`127.0.0.1`), expired, handshake deadline against a plain server. The cases skip themselves without that environment or without a backend, so a plain `ctest` shows them skipped and only `BagiTlsLocalEndpoint` runs them. Fixtures are in `data/` (how they were made: the head of the script); the leaf's notBefore is 2019 because Apple refuses a longer-than-398-days TLS leaf issued later, also under a test anchor.

## The fake backend

`tests/BECore/Tls/FakeTlsStream.h` is a scripted backend over a real loopback `Socket`, so everything that uses an `ITlsStream` runs on every platform without a certificate. The client sends `FAKE-HELLO <serverName> <anchor count>\n`; the test server answers `FAKE-OK\n` or `FAKE-REJECT <reason>\n`; then every byte is XOR-ed with `kFakeTlsMask` both ways. `FakeTlsServerSide` is the other end for a hand-written server on an accepted `Socket`. `WebSocketClientSettings::tls.factory` takes `FakeTlsFactory()`.
