# Rooms Module CMake Configuration
# Rooms of an online game by code: the protocol, the room logic and the hub that dispatches messages to rooms. Pure code over
# the ISeatedGame contract of Games: no sockets (the transport is a later slice of the entry network-rooms).
# No static initializers, so executables link it as an ordinary static library (no WHOLE_ARCHIVE).

be_add_module(Rooms LINK
    BECoreModule
    EventsModule
    GamesModule
)
