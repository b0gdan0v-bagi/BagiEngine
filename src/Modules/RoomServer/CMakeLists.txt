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
