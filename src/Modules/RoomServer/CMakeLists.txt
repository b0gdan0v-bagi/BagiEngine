# RoomServer Module CMake Configuration
# The Rooms hub on the network: RoomWebSocketServer over the shared WebSocketListener of RemoteControl. Added only with
# ENGINE_WITH_REMOTE_CONTROL (the web build has no sockets). No static initializers, so executables link it as an ordinary static library.

be_add_module(RoomServer LINK
    BECoreModule
    EventsModule
    GamesModule
    RoomsModule
    RemoteControlModule
)

# The headless server of the demo games: a registry fill and RunRoomServer(). server/ is outside src/, so the module glob does not take main.cpp
be_add_headless_executable(BagiRoomServer
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/server/main.cpp
    LINK RoomServerModule RoomsModule RemoteControlModule
)

# ctest entry BagiRoomServerSmoke: tools/rooms/smoke.py starts the executable (it picks a free port itself, --port 0) and plays hands from a client in
# another process, and runs BagiRoomClientProbe (RoomClient) against it. Does not build the executables (a missing server is a skip, exit code
# 77; a missing probe skips its scenarios). The RESOURCE_LOCK keeps it apart from the other
# smoke tests of one ctest run; it is not RUN_SERIAL, which would stop every other test while it runs. TIMEOUT 120 is far above the few seconds a
# run takes; a stuck server is cut off by the script's own limits first
if(ENGINE_BUILD_TESTS AND Python3_FOUND)
    set(ROOM_SMOKE_PROBE_ARGS "")
    if(TARGET BagiRoomClientProbe)
        set(ROOM_SMOKE_PROBE_ARGS --probe $<TARGET_FILE:BagiRoomClientProbe>)
    endif()
    add_test(NAME BagiRoomServerSmoke COMMAND ${Python3_EXECUTABLE} ${BAGI_ENGINE_DIR}/tools/rooms/smoke.py --exe $<TARGET_FILE:BagiRoomServer> ${ROOM_SMOKE_PROBE_ARGS})
    set_tests_properties(BagiRoomServerSmoke PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 120
        RESOURCE_LOCK bagi_room_server_smoke
    )
    # ctest entry BagiRoomServerHostile: tools/rooms/hostile.py attacks the executable with hand-built frames while a good table plays. The same skip
    # rule and lock as the smoke test; a run takes about 15 s, TIMEOUT 180 is the stuck-server bound
    add_test(NAME BagiRoomServerHostile COMMAND ${Python3_EXECUTABLE} ${BAGI_ENGINE_DIR}/tools/rooms/hostile.py --exe $<TARGET_FILE:BagiRoomServer>)
    set_tests_properties(BagiRoomServerHostile PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 180
        RESOURCE_LOCK bagi_room_server_smoke
    )
endif()
