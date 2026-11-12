# RoomClient Module CMake Configuration
# The client of the rooms (Rooms/CLAUDE.md): the protocol client a game polls once per frame, and the transports under it. It is built in
# every configuration, the web build included (the browser transport is the one the web build uses). The sockets of the desktop transport
# come from RemoteControl, which exists only with ENGINE_WITH_REMOTE_CONTROL.
# No static initializers, so executables link it as an ordinary static library (no WHOLE_ARCHIVE).

be_add_module(RoomClient LINK
    BECoreModule
    EventsModule
    RoomsModule
)

if(ENGINE_WITH_REMOTE_CONTROL)
    target_link_libraries(RoomClientModule PUBLIC RemoteControlModule)
endif()
