#include "NativeRoomTransport.h"

#if BE_WITH_REMOTE_CONTROL

#include <EASTL/algorithm.h>

namespace BECore {

    namespace {

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

    std::expected<void, String> NativeRoomTransport::Open(StringView url) {
        WebSocketClientSettings settings;
        settings.logName = _logName;
        settings.threadName = String(kThreadName);
        settings.threadOrder = kThreadOrder;
        if (_limits) {
            settings.maxMessageBytes = _limits->maxMessageBytes;
            settings.maxOutboxBytes = _limits->maxSendBufferBytes;
            settings.maxInboxBytes = eastl::max(settings.maxInboxBytes, _limits->maxMessageBytes);
        }
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
