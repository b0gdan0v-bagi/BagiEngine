---
paths:
  - "tools/control/bagi_bridge.py"
  - "tools/control/tests/fake_page.py"
  - "tools/control/tests/test_bridge.py"
  - "src/Application/ControlLink*"
  - "tools/web/web_game.py"
  - "tools/web/tests/web_control_smoke.py"
  - "tools/web/tests/test_web_game.py"
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

```
?controlToken=<token>[&control=<port or ws:// address>][&controlClock=manual]
```

- **Only a web Debug build dials** (`web-debug`, `web-editor`; `BE_CONTROL_LINK`, `cmake/BagiPlatformOptions.cmake`). No option adds it to another build.
- `controlToken` is the switch. Without `control` the page dials the bridge's default page port; `controlClock=manual` is what `--manual-clock` is to a desktop game, from the first frame on. Values are taken as written (no percent-decoding) and the first of a repeated parameter counts. A query that cannot be read is one warning: the page does not dial and keeps the real clock.

`ControlLink` (`src/Application/`; the browser's half is `ControlLinkPage`) is polled once a frame, before `ControlService::Drain()`, the idle pass of the manual clock included, and runs on any `IRoomTransport`, so `ControlLinkTest` drives it on the desktop over the rooms' fake one.

- An answer goes out only on the connection its request came on. A later reply is dropped, one after `Stop()` or the link's destruction too (`ControlService::Shutdown()` answers late): a restarted bridge counts its channels from 1 again, so an old answer would reach a stranger.
- The link holds the service's event sink only while it is connected.
- A message without a channel aborts the connection like a refused send (a script cannot close with 1002), and what the transport still holds of it does not run.
- A failed dial also puts one error of the browser's own into its console, which no script can silence: a page with no bridge shows one per retry. `localhost` reaches the bridge's 127.0.0.1 in Chromium and WebKit.
- One Info line per outage, and the one Warning of a 1008: a QA run reads warnings as findings. The token is never logged.
- `ControlLink::kTransportLimits` are the `RoomTransportLimits` the browser transport of a link takes; its defaults are those of the rooms, and the desktop transport takes none.

**What a linked page does not do as a desktop game does:**

- A hidden tab runs no frame: requests wait until it is shown again.
- `record_start`, `replay` and `profiler_dump` answer `unsupported`: their files would be in the page's memory, where no client can reach them.

## A browser game as an executable

`tools/web/web_game.py` stands where a game executable is expected: `GameSession.launch`, `bagi_play.py`, `/qa`, the monkey and `run_smoke` of `smoke.py` start it with a game's flags.

```bash
python3 tools/web/package.py build/web-debug/bin build/web-control-dist/debug --game BagiStress
BAGI_WEB_DIST=build/web-control-dist/debug python3 tools/control/bagi_play.py tools/web/web_game.py <scenario>
BAGI_WEB_DIST=build/web-control-dist/debug .venv/bin/python tools/web/tests/web_control_smoke.py --exe tools/web/web_game.py --window --startup-timeout 120
```

It serves the dist with `serve.py`, runs the bridge on the control port with a token of its own, opens the page and copies the browser's console to stdout, so the engine's log lines are the "game's" log. It exits 0 when the page says the game ended (after `quit`), 1 when the page aborted, threw, crashed or was closed (a `[Fatal] web_game:` line says which) or the port is taken, 77 without a dist, Playwright or the browser. A page that never dials (a release build) is not noticed: the harness's start-up timeout ends it. The variables (`BAGI_WEB_DIST`, `BAGI_WEB_BROWSER`, `BAGI_WEB_HEADED`) and the flags are in its docstring; the Playwright venv is that of `tools/web/tests/CLAUDE.md`. It starts as a script through its first line, which Windows does not do.

**Against a headless desktop game**, a harness meets:

- `status.window.headless` is false and `set_window` is `not_headless`; `--window` and `--density` size the browser's viewport at the start. `screenshot` works (the canvas as the game drew it).
- `--editor` and every other flag are refused (exit 2). A `web-editor` page is the editor by its build, so `imgui_find` works on its dist, but nothing that passes `--editor` or `--content-overlay` (an editor scenario, the editor charters) runs on a page.
- The browser's renderer is not the software one: a `reference` picture taken on a desktop does not match.

Run in headless Chromium and Playwright's WebKit with `BagiStress` and the `BagiDemo` editor page; Safari itself, a headed window and a hosted `https` page have not run.

## The control side

- **The port listens only while a page is linked.** When the page goes, the listener closes and every client is closed with 1001, so a client sees what it sees of a desktop game that is not running or has quit. It opens again for the next page.
- It answers as the desktop server does (`RemoteControl/CLAUDE.md`, "Security" and "Protocol"): the same handshake refusals, connection limit and close codes. One addition: text that is not UTF-8 closes that client with 1007, because a browser ends a connection that brings it such text, and one client would take the link away from all.
- A client that does not read is dropped when its queue passes `kMaxOutboxBytes` and holds nobody up. A page that is behind holds the sending clients back instead: nothing of theirs is dropped and memory stays bounded.

## Security

The link gives whoever holds the other end the whole control API of the page's game.

- Both listeners bind 127.0.0.1 only.
- The page dials a loopback `ws://` address only (`localhost`, `127.0.0.1`, `[::1]`): anyone can craft a link to a hosted page, and it must not hand the game to their machine.
- A page is linked only when all of these hold: `Host` names the loopback (DNS rebinding); `Origin` is there and is an `http(s)` page of the loopback or one named with `--origin` (scheme, host and port exact), else 403; the hello has the token, compared with `hmac.compare_digest`. `Origin` keeps foreign sites out, not programs, and any local development server is a loopback origin: the token is what makes it the page the person opened.
- The token is random unless `--token` gives it. The bridge prints it once, in the page query, and logs it nowhere.
- The control side has no token, as the desktop server: any local process may connect.
