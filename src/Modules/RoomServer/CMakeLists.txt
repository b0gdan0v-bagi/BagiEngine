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
