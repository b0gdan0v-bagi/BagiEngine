# The room server (tools/rooms/CLAUDE.md, README.md "Hosting the room server"). Build from the root of the engine, or of a game that carries it:
#   docker build -f tools/rooms/Dockerfile -t bagi-room-server .
# A game reuses this file with its own server target and a preset of its own that includes the engine's (then -f engine/tools/rooms/Dockerfile):
#   --build-arg SERVER_TARGET=<target> --build-arg PRESET=<preset>
# The platform sets PORT; the rest of the settings are the BAGI_ROOMS_* variables (README.md). TLS is the platform's proxy's job.

FROM debian:trixie-slim AS build
ARG PRESET=linux-server-release
ARG SERVER_TARGET=BagiRoomServer

# cmake and ninja-build: the presets of CMakePresets.json (CMake 3.31 or newer); clang: the compiler the presets name;
# python3 and python3-jinja2: the reflection code generator runs at configure time (CI/meta_generator, templates in Jinja2)
RUN apt-get update \
    && apt-get install -y --no-install-recommends cmake ninja-build clang python3 python3-jinja2 \
    && rm -rf /var/lib/apt/lists/*

# The context is the engine root (or a game root); Dockerfile.dockerignore keeps the build folders and the web SDK out of it
COPY . /src
WORKDIR /src
RUN cmake --preset "$PRESET" \
    && cmake --build --preset "$PRESET" --target "$SERVER_TARGET"

# One folder for the runtime stage: the executable and any shared library next to it. The ldd check fails the build when the
# runtime stage would miss a library (only libstdc++ and libc are installed there)
RUN mkdir /out \
    && cp "build/$PRESET/bin/$SERVER_TARGET" /out/room-server \
    && (cp build/"$PRESET"/bin/*.so* /out/ 2>/dev/null || true) \
    && ldd /out/room-server | tee /out/ldd.txt \
    && ! grep -q "not found" /out/ldd.txt \
    && rm /out/ldd.txt

FROM debian:trixie-slim

# libstdc++6: the only library the static-SDL server needs besides libc
RUN apt-get update \
    && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /out/ /app/

# Behind the platform's proxy every client has the proxy's address, so a per-address limit would cut everyone off together
ENV BAGI_ROOMS_MAX_PER_ADDRESS=0 BAGI_ROOMS_BIND=any
EXPOSE 8080
USER nobody
# Exec form and no CMD: the server is PID 1 and gets SIGTERM itself (it drains its connections and exits 0). Flags go after the image name
ENTRYPOINT ["/app/room-server"]
