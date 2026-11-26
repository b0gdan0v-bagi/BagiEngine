#pragma once

#include <RoomClient/RoomTransport.h>

#if BE_WITH_REMOTE_CONTROL

#include <BECore/Types/Types.h>
#include <RemoteControl/WebSocketClient.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /**
     * @brief The desktop transport: a WebSocketClient (RemoteControl) behind IRoomTransport.
     *
     * `ws://`, and `wss://` through the system TLS (RemoteControl `Tls/`: the certificate and the host name are always checked, a
     * failure arrives as a Closed event with the reason). On a platform without a TLS backend Open() refuses `wss://` with an error that
     * starts with `tls_unsupported`. No `Origin` header: a program is not a page. The client's threads call nothing back; PollEvent() copies what
     * they queued, so the events arrive on the owner's thread. One object serves one connection.
     */
    class NativeRoomTransport final : public IRoomTransport {
    public:
        NativeRoomTransport() = default;

        /// For a protocol that is not the rooms': @p limits take the place of the client's own, and the inbox holds at least one message of the longest kind.
        /// @p logName begins the client's log lines
        NativeRoomTransport(const RoomTransportLimits& limits, StringView logName) : _limits(limits), _logName(logName) {}

        [[nodiscard]] std::expected<void, String> Open(StringView url) override;
        bool SendText(StringView text) override;
        void Close(uint16_t code, StringView reason) override;
        void Abort() override;
        [[nodiscard]] bool PollEvent(RoomTransportEvent& event) override;

    private:
        Optional<RoomTransportLimits> _limits;
        String _logName = "Room client";
        WebSocketClient _client;
        /// Where the client's event is taken: a member, so its buffers keep their capacity
        WebSocketClientEvent _polled;
    };

}  // namespace BECore

#endif  // BE_WITH_REMOTE_CONTROL
