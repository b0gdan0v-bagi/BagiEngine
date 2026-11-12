# BagiEngine

A C++23 game engine on a scene graph of `SceneNode` + `IComponent`, with an EnTT registry for the data of the systems. It runs on SDL3 with an SDL or Vulkan renderer, on desktop and in the browser (Emscripten), and drives its in-game UI with its own canvases (adaptive layout, a validator of touch sizes and text fit). Games live in `games/<Name>/` (the engine's own is Match3) or in a repository of their own that carries the engine as the submodule `engine/` (`NEW_GAME.md`).

## Principles

- **Zero allocations in a steady frame.** A frame in which nothing changed allocates nothing: the scene, the UI canvases, the text and the game's own components make a thing again only when its key moved and keep their lists between frames. A phone and the browser pay for every allocation in frame time and battery, and an allocation per frame stays invisible until a profile shows it. The `macos-profile` preset counts every `operator new` (`ENGINE_PROFILER_ALLOC`): per frame (`frame_stats`), per profiler zone (`zone_allocs`) and in tests (`AllocTracker`); a game holds its scene to zero with a test of its own.
- **External clients through the control API** (`tools/control`): the same actions a player has, over WebSocket, for bots, smoke tests and MCP.

## Build and test

```bash
cmake --preset macos-debug          # configure (also regenerates reflection code); list: cmake --list-presets
cmake --build --preset macos-debug  # BagiMatch3 and the test executables
ctest --preset macos-debug
```

Presets, the editor option and the browser builds: `cmake/CLAUDE.md`. The architecture, the code style and the contracts of every module are in `CLAUDE.md` and the nested `CLAUDE.md` files under `src/`. The launcher for the workspace: `WORKSPACE_README.md`.

## Hosting the room server

`BagiRoomServer` (`src/Modules/RoomServer/CLAUDE.md`) is the server of the online rooms: a headless program that speaks WebSocket and keeps the rooms in memory. The image of `tools/rooms/Dockerfile` builds it for Linux (Clang, static SDL, no window) and runs it as `nobody`. The engine's own server registers only the demo card game, so it starts with `BAGI_ROOMS_TEST_GAMES=1` and is meant for the first check; a game builds its own server target (a `main` that fills a `SeatedGameRegistry`) into the same image.

```bash
docker build -f tools/rooms/Dockerfile -t bagi-room-server .      # from the engine root
docker run --rm -e PORT=9090 -e BAGI_ROOMS_TEST_GAMES=1 -p 127.0.0.1:9090:9090 bagi-room-server
curl -i http://127.0.0.1:9090/healthz                             # 200 ok
```

A game that carries the engine builds from its own root, with its server target and a preset that includes the engine's `linux-server-release`: `docker build -f engine/tools/rooms/Dockerfile --build-arg SERVER_TARGET=<target> --build-arg PRESET=<preset> -t <name> .`. The build reads `external/` (SDL, SDL_image, Vulkan-Headers), so the context must hold the checked-out submodules. The image is built for the architecture of the machine; a host that runs amd64 needs `docker buildx build --platform linux/amd64`.

**Settings.** The order is the default, then the variable, then the command-line flag (the image has no `CMD`: flags go after the image name). An empty variable is unset; a bad value stops the server with exit code 1 naming the variable.

| Variable | Flag | Default | Meaning |
|----------|------|---------|---------|
| `PORT` | `--port` | 8080 | the port to listen on (the platform sets it) |
| `BAGI_ROOMS_BIND` | `--bind` | `any` (the image sets it too) | `any` is every address, `loopback` the machine's own |
| `BAGI_ROOMS_ORIGINS` | `--origin` (repeatable) | none | the web pages that may connect, comma-separated (`https://game.example`); `*` is every page. With none, browsers are refused and programs (no `Origin`) are let in |
| `BAGI_ROOMS_MAX_CONNECTIONS` | `--max-connections` | 512 | connections the hub admits |
| `BAGI_ROOMS_MAX_PER_ADDRESS` | `--max-per-address` | 32 (the image sets 0) | open sockets of one address, 0 is off |
| `BAGI_ROOMS_MAX_ROOMS_PER_ADDRESS` | `--max-rooms-per-address` | 4 | rooms one address may have at once (0..64), 0 is off; needs the address (a per-address socket limit above 0, or trusted proxy hops) |
| `BAGI_ROOMS_LOBBY_TIMEOUT_MS` | `--lobby-timeout-ms` | 900000 | a lobby with no message but `ping` for this long is closed (`left {lobby_timeout}`), 0 is off |
| `BAGI_ROOMS_TRUSTED_PROXY_HOPS` | `--trusted-proxy-hops` | 0 | proxies in front that append to `X-Forwarded-For` (0..4): the client is the entry that many places from the right; 0 reads the socket's address |
| `BAGI_ROOMS_COMPUTER_DELAY_MS` | `--computer-delay-ms` | 800 | the pause before a computer player moves |
| `BAGI_ROOMS_RECONNECT_GRACE_MS` | `--reconnect-grace-ms` | 30000 | how long a dropped human keeps the seat before a computer plays it |
| `BAGI_ROOMS_PING_INTERVAL_MS` | `--ping-interval-ms` | 20000 | liveness pings, 0 is off |
| `BAGI_ROOMS_STATS_INTERVAL_MS` | `--stats-interval-ms` | 30000 | the log line with connections and rooms, at most this often, 0 is off |
| `BAGI_ROOMS_SHUTDOWN_GRACE_MS` | `--shutdown-grace-ms` | 1000 | after SIGTERM, how long to wait for the clients to end (0..60000) |
| `BAGI_ROOMS_TEST_GAMES` | `--test-games` | off | registers the demo games `cards` and `cards-leaky`; for the first live check only |

**Behind a platform's proxy.** The platform terminates TLS: clients use `wss://<domain>`, the process speaks plain `ws://`. Every client then arrives from the proxy's address, so the per-address limit would cut everybody off together; the image sets `BAGI_ROOMS_MAX_PER_ADDRESS=0` and the global limits hold. `X-Forwarded-For` is read only with `BAGI_ROOMS_TRUSTED_PROXY_HOPS`: set it to 1 (a platform with one proxy that appends the real client as the last entry) together with `BAGI_ROOMS_MAX_PER_ADDRESS` above 0 and the per-address rules (sockets, rooms, failed joins) work behind the proxy; without both, only the server-wide limits and the lobby timeout protect the server, and the 15 minute lobby timeout drops a host who waits without touching the screen. A plain `GET /healthz` is answered `200 ok` without a WebSocket handshake. The log goes to stdout, line by line: the listening line, the settings, one line per connection and per refusal, the stats line; it never names a player, a room code or a token.

**Railway.** The service builds from the Dockerfile `tools/rooms/Dockerfile` (for a game: `engine/tools/rooms/Dockerfile` with the build arguments above); health check path `/healthz`; `PORT` is set by Railway; set `BAGI_ROOMS_ORIGINS` to the `https://` origin of the web service that serves the game, or browsers are refused. If the GitHub build does not clone submodules, or you do not want Railway to compile, build here and upload the binary only (below). On a redeploy Railway sends SIGTERM: the server closes every connection with 1001 `server_stopping`, waits up to `BAGI_ROOMS_SHUTDOWN_GRACE_MS` and exits 0. **A restart loses the rooms** (they live in memory); a client that reconnects is told `room_gone`. A longer drain is a larger grace together with Railway's `RAILWAY_DEPLOYMENT_DRAINING_SECONDS`.

**Deploy a prebuilt binary.** `python3 tools/rooms/build_and_deploy.py --deploy --service <name>` builds the server for `linux/amd64` in Docker on this machine (the build stage of `tools/rooms/Dockerfile`; slow in emulation on an arm machine), stages the binary with a runtime-only `Dockerfile` (debian-slim, `libstdc++6`, `USER nobody`, the same settings) and runs `railway up <stage> --path-as-root --service <name> --detach`: the upload is a few MB and Railway compiles nothing. `--dry-run` prints the commands. From a game: `python3 engine/tools/rooms/build_and_deploy.py --server-target <target> --preset <preset> --deploy --service <name>`. It needs Docker and the Railway CLI (logged in, the project linked); it reads and writes no secret.

**Check.** `python3 tools/rooms/docker_check.py` builds the image, runs it and plays against it (the listening and settings lines, `/healthz`, the Origin rules, a room, `docker stop` with a client connected); manual, it needs Docker (`tools/rooms/CLAUDE.md`).
