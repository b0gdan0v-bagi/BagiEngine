---
paths:
  - "tools/control/bagi_bridge.py"
  - "tools/control/tests/fake_page.py"
  - "tools/control/tests/test_bridge.py"
  - "src/Application/ControlLink*"
  - "tools/web/web_game.py"
  - "tests/BECore/ControlLinkTest.cpp"
  - "src/Modules/RoomClient/src/RoomClient/EmscriptenRoomTransport.*"
---

# The control link: a game in a browser page and the bridge on this machine

A page cannot listen on a port, so the direction is reversed: the page dials `tools/control/bagi_bridge.py` on the same machine, and the bridge offers the control endpoint of a desktop game (`src/Modules/RemoteControl/CLAUDE.md`), so `BagiControlClient`, `GameSession.connect` and the MCP server need nothing of their own. The bridge parses no JSON but the hello: requests and answers pass byte for byte. A change of the protocol changes the bridge and the page's end of the link together; nothing else reads it.

```bash
python3 tools/control/bagi_bridge.py [--control-port 8765] [--page-port 8766] [--token T] [--origin URL ...]
```

It prints the query to open the page with, and exits with 2 on a bad argument or a taken port (the control port is checked at the start too). From Python, `Bridge` runs in a thread of its own; port 0 is allowed there.

## The link protocol

One WebSocket, text messages only. The page opens `ws://localhost:8766` by default; the bridge ignores the path.

| Message | From | Meaning |
|---------|------|---------|
| `{"bagiControlLink":1,"token":"<token>"}` | page, first, nothing before it | the hello; the token is 1 to 128 of `A-Z a-z 0-9 _ -`, the object has no other member |
| `<channel> <request text>` | bridge | a request, one space, then the text as the client sent it |
| `<channel>` | bridge | that control connection ended. A page may ignore it: it lets a per-client identity be added without a protocol change |
| `<channel> <response text>` | page | the answer to a request of that channel; dropped silently when the channel has gone |
| `0 <push text>` | page | a push for every control client |

`<channel>` is a decimal from 1, one per control connection, never reused in a run of the bridge.

**The bridge closes the page** with:

- 1008 and the reason `bad_token`, `bad_hello` (no hello in 5 s, not that object, another version, a binary message) or `busy` (a page is linked already: two tabs must not fight over one bridge). The token is judged before `busy`, so a stranger learns nothing about the link.
- 1002 for a later message that is not `<channel> <text>`; 1009 for one over the size limit; 1011 `control_port` when it cannot listen on its control port; 1001 when it stops.

The page retries after every close but 1008: after 1 s, doubling to 10 s, and from 1 s again after a connection that opened. On 1008 it stops and logs the reason once, as a Warning.

**Limits:** a request is at most `ControlService::kMaxRequestBytes` (the bridge closes that client with 1009). The page receives up to that plus the channel prefix and sends up to `WebSocketServer::kMaxOutboxBytes`; a send its transport refuses aborts the link, which then retries. Before the hello the bridge takes no more than `MAX_HELLO_BYTES` (1009).

## The page's end

`ControlLink` (`src/Application/`) is polled once a frame, before `ControlService::Drain()`, and runs on any `IRoomTransport`, so `ControlLinkTest` drives it on the desktop over the rooms' fake one.

- An answer goes out only on the connection its request came on. A later reply is dropped, one after `Stop()` or the link's destruction too (`ControlService::Shutdown()` answers late): a restarted bridge counts its channels from 1 again, so an old answer would reach a stranger.
- The link holds the service's event sink only while it is connected.
- A message without a channel aborts the connection like a refused send (a script cannot close with 1002), and what the transport still holds of it does not run.
- One Info line per outage, and the one Warning of a 1008: a QA run reads warnings as findings. The token is never logged.
- `ControlLink::kTransportLimits` are the `RoomTransportLimits` the browser transport of a link takes; its defaults are those of the rooms, and the desktop transport takes none.

## The control side

- **The port listens only while a page is linked.** When the page goes, the listener closes and every client is closed with 1001, so a client sees what it sees of a desktop game that is not running or has quit. It opens again for the next page.
- It answers as the desktop server does (`RemoteControl/CLAUDE.md`, "Security" and "Protocol"): the same handshake refusals, connection limit and close codes. One addition: text that is not UTF-8 closes that client with 1007, because a browser ends a connection that brings it such text, and one client would take the link away from all.
- A client that does not read is dropped when its queue passes `kMaxOutboxBytes` and holds nobody up. A page that is behind holds the sending clients back instead: nothing of theirs is dropped and memory stays bounded.

## Security

The link gives whoever holds the other end the whole control API of the page's game.

- Both listeners bind 127.0.0.1 only.
- A page is linked only when all of these hold: `Host` names the loopback (DNS rebinding); `Origin` is there and is an `http(s)` page of the loopback or one named with `--origin` (scheme, host and port exact), else 403; the hello has the token, compared with `hmac.compare_digest`. `Origin` keeps foreign sites out, not programs, and any local development server is a loopback origin: the token is what makes it the page the person opened.
- The token is random unless `--token` gives it. The bridge prints it once, in the page query, and logs it nowhere.
- The control side has no token, as the desktop server: any local process may connect.
