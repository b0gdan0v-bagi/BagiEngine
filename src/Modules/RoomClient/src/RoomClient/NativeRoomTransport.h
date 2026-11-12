#pragma once

#include <RoomClient/RoomTransport.h>

#if BE_WITH_REMOTE_CONTROL

#include <RemoteControl/WebSocketClient.h>

namespace BECore {

    /**
     * @brief The desktop transport: a WebSocketClient (RemoteControl) behind IRoomTransport.
     *
     * `ws://` only; `wss://` is refused by Open() with an error that starts with `tls_unsupported` (the browser build is the way to
     * `wss://` today). No `Origin` header: a program is not a page. The client's threads call nothing back; PollEvent() copies what
     * they queued, so the events arrive on the owner's thread. One object serves one connection.
     */
    class NativeRoomTransport final : public IRoomTransport {
    public:
        [[nodiscard]] std::expected<void, eastl::string> Open(eastl::string_view url) override;
        bool SendText(eastl::string_view text) override;
        void Close(uint16_t code, eastl::string_view reason) override;
        void Abort() override;
        [[nodiscard]] bool PollEvent(RoomTransportEvent& event) override;

    private:
        WebSocketClient _client;
        /// Where the client's event is taken: a member, so its buffers keep their capacity
        WebSocketClientEvent _polled;
    };

}  // namespace BECore

#endif  // BE_WITH_REMOTE_CONTROL
