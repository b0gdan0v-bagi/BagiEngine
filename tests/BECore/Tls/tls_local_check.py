#!/usr/bin/env python3
"""Runs TlsLocalEndpointTest of BagiEngineTests against real TLS servers started here (standard library only).

    python tests/BECore/Tls/tls_local_check.py --exe build/macos-debug/bin/BagiEngineTests

Starts, each on a free port of 127.0.0.1:
  - a TLS WebSocket echo server with the valid leaf data/localhost.pem (a certificate for DNS:localhost, issued by the test CA),
  - the same with the expired leaf data/expired.pem,
  - a plain TCP server that accepts and never answers (a `ws://` server seen through `wss://`),
then runs the gtest cases with the ports and the CA file (DER) in the environment and stops the servers. The cases skip themselves
when the environment is absent, so a plain ctest shows them skipped; this script is what really runs them.
Exit code: the test run's, 77 (skipped) when `ssl` or the executable is missing.

The fixtures in data/ were made with OpenSSL (a CA without its key kept, two leaves; `openssl ca` backdates the expired one):
  openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out test-ca.pem -days 36500 -subj "/CN=BagiEngine Test CA" \
      -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign"
  openssl x509 -in test-ca.pem -outform der -out test-ca.der
  # ext.cnf: basicConstraints=critical,CA:FALSE / keyUsage=critical,digitalSignature,keyEncipherment /
  #          extendedKeyUsage=serverAuth / subjectAltName=DNS:localhost
  openssl req -newkey rsa:2048 -nodes -keyout localhost.key -out localhost.csr -subj "/CN=localhost"
  openssl ca -batch -config ca.cnf -cert test-ca.pem -keyfile ca.key -in localhost.csr -out localhost.pem -extfile ext.cnf \
      -notext -startdate 20190101000000Z -enddate 21190101000000Z
  (the same for expired.*, with -startdate 20190101000000Z -enddate 20190201000000Z; ca.cnf is a minimal `openssl ca`
   configuration with a database in ./db)
The leaf is valid for 100 years so the test does not age. Its notBefore is early 2019 on purpose: Apple refuses a TLS server
certificate issued after mid-2019 whose validity is longer than 398 days (errSecCertificateValidityPeriodTooLong), also under an
extra trust anchor, and judges "issued" by notBefore. The private keys are test fixtures and protect nothing.
"""

import argparse
import base64
import hashlib
import os
import socket
import struct
import subprocess
import sys
import threading
from pathlib import Path

EXIT_SKIPPED = 77
DATA_DIR = Path(__file__).resolve().parent / "data"
WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
HEAD_LIMIT = 16 * 1024
PAYLOAD_LIMIT = 4 * 1024 * 1024
ACCEPT_POLL_SECONDS = 0.2
CONNECTION_TIMEOUT_SECONDS = 30.0
TEST_RUN_TIMEOUT_SECONDS = 90.0

OP_TEXT = 0x1
OP_CLOSE = 0x8
OP_PING = 0x9
OP_PONG = 0xA


def recv_exact(conn, count: int) -> bytes:
    data = bytearray()
    while len(data) < count:
        chunk = conn.recv(count - len(data))
        if not chunk:
            raise ConnectionError("closed")
        data += chunk
    return bytes(data)


def read_head(conn) -> str:
    data = bytearray()
    while b"\r\n\r\n" not in data:
        chunk = conn.recv(4096)
        if not chunk or len(data) > HEAD_LIMIT:
            raise ConnectionError("no request head")
        data += chunk
    return data.decode("latin-1")


def encode_frame(opcode: int, payload: bytes) -> bytes:
    """A server frame: FIN set, never masked."""
    head = bytes([0x80 | opcode])
    length = len(payload)
    if length < 126:
        head += bytes([length])
    elif length < 65536:
        head += bytes([126]) + struct.pack(">H", length)
    else:
        head += bytes([127]) + struct.pack(">Q", length)
    return head + payload


def read_frame(conn):
    first, second = recv_exact(conn, 2)
    opcode = first & 0x0F
    length = second & 0x7F
    if length == 126:
        length = struct.unpack(">H", recv_exact(conn, 2))[0]
    elif length == 127:
        length = struct.unpack(">Q", recv_exact(conn, 8))[0]
    if length > PAYLOAD_LIMIT or not second & 0x80:
        raise ConnectionError("unsupported frame")
    mask = recv_exact(conn, 4)
    masked = recv_exact(conn, length)
    return opcode, bytes(b ^ mask[i % 4] for i, b in enumerate(masked))


def serve_websocket(conn) -> None:
    """The minimal server side of RFC 6455: the handshake, then text echoed back, pings answered, a close answered."""
    head = read_head(conn)
    key = ""
    for line in head.split("\r\n")[1:]:
        name, _, value = line.partition(":")
        if name.strip().lower() == "sec-websocket-key":
            key = value.strip()
    if not key:
        raise ConnectionError("no key")
    accept = base64.b64encode(hashlib.sha1((key + WEBSOCKET_GUID).encode()).digest()).decode()
    conn.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                  f"Sec-WebSocket-Accept: {accept}\r\n\r\n").encode())
    while True:
        opcode, payload = read_frame(conn)
        if opcode == OP_TEXT:
            conn.sendall(encode_frame(OP_TEXT, payload))
        elif opcode == OP_PING:
            conn.sendall(encode_frame(OP_PONG, payload))
        elif opcode == OP_CLOSE:
            conn.sendall(encode_frame(OP_CLOSE, payload[:2]))
            return


class Server:
    """A listener on a free port whose connections are served by `handler(connection)` on a thread each."""

    def __init__(self, handler, context=None):
        self._handler = handler
        self._context = context
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.bind(("127.0.0.1", 0))
        self._listener.listen(16)
        self._listener.settimeout(ACCEPT_POLL_SECONDS)
        self.port = self._listener.getsockname()[1]
        self._stop = threading.Event()
        self._connections = []
        self._thread = threading.Thread(target=self._accept_loop, daemon=True)
        self._thread.start()

    def _accept_loop(self) -> None:
        while not self._stop.is_set():
            try:
                conn, _ = self._listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            conn.settimeout(CONNECTION_TIMEOUT_SECONDS)
            self._connections.append(conn)
            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()

    def _serve(self, conn) -> None:
        try:
            if self._context is not None:
                conn = self._context.wrap_socket(conn, server_side=True)
            self._handler(conn)
        except (OSError, ConnectionError):
            pass  # a client that refuses the certificate or leaves is the normal end of several cases
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def stop(self) -> None:
        self._stop.set()
        self._listener.close()
        for conn in self._connections:
            try:
                conn.close()
            except OSError:
                pass
        self._thread.join()


def hold_open(conn) -> None:
    """Reads and says nothing until the client leaves."""
    while conn.recv(4096):
        pass


def make_context(name: str):
    import ssl

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(DATA_DIR / f"{name}.pem", DATA_DIR / f"{name}.key")
    return context


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", required=True, help="the built BagiEngineTests")
    args = parser.parse_args()

    try:
        import ssl  # noqa: F401
    except ImportError:
        print("SKIP: this Python has no ssl module")
        return EXIT_SKIPPED
    exe = Path(args.exe)
    if not exe.exists():
        print(f"SKIP: {exe} does not exist")
        return EXIT_SKIPPED

    valid = Server(serve_websocket, make_context("localhost"))
    expired = Server(serve_websocket, make_context("expired"))
    plain = Server(hold_open)
    env = dict(os.environ)
    env["BAGI_TLS_TEST_PORT"] = str(valid.port)
    env["BAGI_TLS_TEST_EXPIRED_PORT"] = str(expired.port)
    env["BAGI_TLS_TEST_PLAIN_PORT"] = str(plain.port)
    env["BAGI_TLS_TEST_CA"] = str(DATA_DIR / "test-ca.der")
    try:
        result = subprocess.run([str(exe), "--gtest_filter=TlsLocalEndpointTest.*"], env=env, timeout=TEST_RUN_TIMEOUT_SECONDS)
        return result.returncode
    except subprocess.TimeoutExpired:
        print("FAIL: the test run did not end")
        return 1
    finally:
        for server in (valid, expired, plain):
            server.stop()


if __name__ == "__main__":
    sys.exit(main())
