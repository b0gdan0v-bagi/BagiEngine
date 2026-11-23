# Rooms Module CMake Configuration
# Rooms of an online game by code: the protocol, the room logic and the hub that dispatches messages to rooms. Pure code over
# the ISeatedGame contract of Games: no sockets (the transport is a later slice of the entry network-rooms).
# The wire structs of RoomWire.cpp register their TypeInfo at start-up, but the writer in RoomProtocol.cpp uses them, so that object file is always linked:
# executables link it as an ordinary static library (no WHOLE_ARCHIVE).

be_add_module(Rooms LINK
    BECoreModule
    EventsModule
    GamesModule
)
