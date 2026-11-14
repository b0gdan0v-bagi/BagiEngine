#include "NativeRoomTransport.h"

#if BE_WITH_REMOTE_CONTROL

namespace BECore {

    namespace {

        constexpr StringView kLogName = "Room client";
        constexpr StringView kThreadName = "RoomClient";
        constexpr uint32_t kThreadOrder = 230;  // the reader takes this and the writer the next number (the server's threads start at 210)

        [[nodiscard]] RoomTransportEventKind ToTransportKind(WebSocketClientEventKind kind) {
            switch (kind) {
                case WebSocketClientEventKind::Opened:
                    return RoomTransportEventKind::Opened;
                case WebSocketClientEventKind::Text:
                    return RoomTransportEventKind::Text;
                case WebSocketClientEventKind::Closed:
                    break;
            }
            return RoomTransportEventKind::Closed;
        }

    }  // namespace

    std::expected<void, eastl::string> NativeRoomTransport::Open(StringView url) {
        WebSocketClientSettings settings;
        settings.logName = eastl::string(kLogName);
        settings.threadName = eastl::string(kThreadName);
        settings.threadOrder = kThreadOrder;
        return _client.Open(url, std::move(settings));
    }

    bool NativeRoomTransport::SendText(StringView text) {
        return _client.SendText(text);
    }

    void NativeRoomTransport::Close(uint16_t code, StringView reason) {
        _client.Close(code, reason);
    }

    void NativeRoomTransport::Abort() {
        _client.Abort();
    }

    bool NativeRoomTransport::PollEvent(RoomTransportEvent& event) {
        if (!_client.PollEvent(_polled)) {
            return false;
        }
        // Every field is written, with copies, so the buffers of both events keep their capacity
        event.kind = ToTransportKind(_polled.kind);
        event.text = _polled.text;
        event.closeCode = _polled.closeCode;
        event.reason = _polled.reason;
        return true;
    }

}  // namespace BECore

#endif  // BE_WITH_REMOTE_CONTROL
