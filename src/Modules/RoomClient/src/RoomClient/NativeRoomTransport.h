#pragma once

#include <RoomClient/RoomTransport.h>

#if BE_WITH_REMOTE_CONTROL

#include <RemoteControl/WebSocketClient.h>

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
        [[nodiscard]] std::expected<void, eastl::string> Open(StringView url) override;
        bool SendText(StringView text) override;
        void Close(uint16_t code, StringView reason) override;
        void Abort() override;
        [[nodiscard]] bool PollEvent(RoomTransportEvent& event) override;

    private:
        WebSocketClient _client;
        /// Where the client's event is taken: a member, so its buffers keep their capacity
        WebSocketClientEvent _polled;
    };

}  // namespace BECore

#endif  // BE_WITH_REMOTE_CONTROL
