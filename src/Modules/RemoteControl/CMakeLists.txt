# RemoteControl Module CMake Configuration
# WebSocket transport of the external control API: a server on localhost that feeds ControlService::Post().
# No static initializers, so executables link it as an ordinary static library (no WHOLE_ARCHIVE).

be_add_module(RemoteControl LINK
    BECoreModule
    EventsModule
)

if(WIN32)
    target_link_libraries(RemoteControlModule PUBLIC ws2_32)
endif()

if(APPLE)
    # The TLS backend of macOS (Tls/NetworkFrameworkTlsStream.cpp): Network.framework with the trust evaluation of Security
    target_link_libraries(RemoteControlModule PUBLIC "-framework Network" "-framework Security" "-framework CoreFoundation")
endif()
