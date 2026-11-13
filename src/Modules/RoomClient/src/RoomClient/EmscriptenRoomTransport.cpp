#include "EmscriptenRoomTransport.h"

#if PLATFORM_WEB

#include <Rooms/RoomLimits.h>
#include <cctype>

namespace BECore {

    namespace {

        /// The longest address handed to the browser (as RemoteControl's URL parser: the page's own limit is much larger)
        constexpr size_t kMaxUrlBytes = 2048;
        /// What the browser may still hold unsent before SendText refuses (the outbox cap of the desktop client)
        constexpr size_t kMaxBufferedBytes = 1024 * 1024;
        /// The longest reason of a close a script may send (RFC 6455: 125 bytes of a control frame less the code)
        constexpr size_t kMaxCloseReasonBytes = 123;
        /// The first and the last code a script may close with besides 1000 (the application ranges of RFC 6455)
        constexpr uint16_t kFirstScriptCloseCode = 3000;
        constexpr uint16_t kLastScriptCloseCode = 4999;
        /// The close code the browser reports when the connection ended without a status (never connected, dropped)
        constexpr uint16_t kNoStatusReceived = 1005;
        constexpr uint16_t kUnsupportedData = 1003;
        constexpr uint16_t kMessageTooBig = 1009;

        [[nodiscard]] bool StartsWithNoCase(eastl::string_view text, eastl::string_view prefix) {
            if (text.size() < prefix.size()) {
                return false;
            }
            for (size_t i = 0; i < prefix.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(text[i])) != prefix[i]) {
                    return false;
                }
            }
            return true;
        }

        /// The browser throws (and the page dies) on a malformed address, so the plain mistakes are refused here: the scheme, spaces and
        /// control characters. Everything else is the browser's to judge
        [[nodiscard]] std::expected<void, eastl::string> CheckUrl(eastl::string_view url) {
            if (!StartsWithNoCase(url, "ws://") && !StartsWithNoCase(url, "wss://")) {
                return std::unexpected(eastl::string("the address must start with ws:// or wss://"));
            }
            if (url.size() > kMaxUrlBytes) {
                return std::unexpected(eastl::string("the address is too long"));
            }
            for (const char c : url) {
                const auto byte = static_cast<unsigned char>(c);
                if (byte <= ' ' || byte == 0x7F) {
                    return std::unexpected(eastl::string("the address has a space or a control character"));
                }
            }
            return {};
        }

        /// A reason of at most kMaxCloseReasonBytes bytes, cut so that no UTF-8 sequence is split (a longer one makes the browser throw)
        [[nodiscard]] eastl::string ClipReason(eastl::string_view reason) {
            size_t length = std::min(reason.size(), kMaxCloseReasonBytes);
            if (length < reason.size()) {
                // A continuation byte (10xxxxxx) at the cut belongs to a sequence that started before it
                while (length > 0 && (static_cast<unsigned char>(reason[length]) & 0xC0) == 0x80) {
                    --length;
                }
            }
            return {reason.data(), length};
        }

        /// The code a script is allowed to close with: 1000 or the application range; anything else goes out as 1000
        [[nodiscard]] unsigned short ScriptCloseCode(uint16_t code) {
            const bool allowed = code == kRoomTransportNormalClosure || (code >= kFirstScriptCloseCode && code <= kLastScriptCloseCode);
            return allowed ? code : kRoomTransportNormalClosure;
        }

    }  // namespace

    EmscriptenRoomTransport::~EmscriptenRoomTransport() {
        ASSERT(_phase == Phase::Idle || Thread::CurrentId() == _owner, "EmscriptenRoomTransport is destroyed off its thread");
        DeleteSocket();
    }

    std::expected<void, eastl::string> EmscriptenRoomTransport::Open(eastl::string_view url) {
        if (_phase != Phase::Idle) {
            return std::unexpected(eastl::string("the transport serves one connection"));
        }
        if (!emscripten_websocket_is_supported()) {
            return std::unexpected(eastl::string("transport_unavailable: this browser has no WebSocket"));
        }
        if (auto checked = CheckUrl(url); !checked) {
            return checked;
        }

        const eastl::string address(url.data(), url.size());
        EmscriptenWebSocketCreateAttributes attributes;
        emscripten_websocket_init_create_attributes(&attributes);
        attributes.url = address.c_str();
        attributes.createOnMainThread = true;

        _owner = Thread::CurrentId();
        const EMSCRIPTEN_WEBSOCKET_T socket = emscripten_websocket_new(&attributes);
        if (socket <= 0) {
            return std::unexpected(Format("the browser refused to create the WebSocket (code {})", socket));
        }
        _socket = socket;
        _phase = Phase::Connecting;
        _sawError = false;

        const bool registered = emscripten_websocket_set_onopen_callback(socket, this, &EmscriptenRoomTransport::OnOpen) == EMSCRIPTEN_RESULT_SUCCESS &&
                                emscripten_websocket_set_onmessage_callback(socket, this, &EmscriptenRoomTransport::OnMessage) == EMSCRIPTEN_RESULT_SUCCESS &&
                                emscripten_websocket_set_onerror_callback(socket, this, &EmscriptenRoomTransport::OnError) == EMSCRIPTEN_RESULT_SUCCESS &&
                                emscripten_websocket_set_onclose_callback(socket, this, &EmscriptenRoomTransport::OnClose) == EMSCRIPTEN_RESULT_SUCCESS;
        if (!registered) {
            // No Closed follows an Open that fails, so nothing of the connection stays
            DeleteSocket();
            _phase = Phase::Idle;
            return std::unexpected(eastl::string("the browser did not accept the WebSocket callbacks"));
        }
        return {};
    }

    bool EmscriptenRoomTransport::SendText(eastl::string_view text) {
        ASSERT(Thread::CurrentId() == _owner, "EmscriptenRoomTransport is used off its thread");
        if (_phase != Phase::Open) {
            return false;
        }
        // The API takes a C string, so a NUL inside would cut the message; a protocol message is JSON and has none
        if (text.find('\0') != eastl::string_view::npos) {
            return false;
        }
        size_t buffered = 0;
        if (emscripten_websocket_get_buffered_amount(_socket, &buffered) != EMSCRIPTEN_RESULT_SUCCESS || buffered + text.size() > kMaxBufferedBytes) {
            return false;
        }
        _sendBuffer.assign(text.data(), text.size());
        return emscripten_websocket_send_utf8_text(_socket, _sendBuffer.c_str()) == EMSCRIPTEN_RESULT_SUCCESS;
    }

    void EmscriptenRoomTransport::Close(uint16_t code, eastl::string_view reason) {
        ASSERT(Thread::CurrentId() == _owner, "EmscriptenRoomTransport is used off its thread");
        switch (_phase) {
            case Phase::Connecting:
                Abort();
                return;
            case Phase::Open: {
                const eastl::string clipped = ClipReason(reason);
                _phase = Phase::Closing;
                // The browser's `close` follows when the server has answered; Closed carries what it reports
                emscripten_websocket_close(_socket, ScriptCloseCode(code), clipped.c_str());
                return;
            }
            case Phase::Idle:
            case Phase::Closing:
            case Phase::Finished:
                return;
        }
    }

    void EmscriptenRoomTransport::Abort() {
        ASSERT(Thread::CurrentId() == _owner, "EmscriptenRoomTransport is used off its thread");
        if (_phase == Phase::Idle || _phase == Phase::Finished) {
            return;
        }
        // A script cannot end a WebSocket without the closing handshake; it stops listening and reports the abnormal end itself
        Finish(kRoomTransportAbnormalClosure, "aborted");
    }

    bool EmscriptenRoomTransport::PollEvent(RoomTransportEvent& event) {
        ASSERT(_phase == Phase::Idle || Thread::CurrentId() == _owner, "EmscriptenRoomTransport is used off its thread");
        if (_head == _count) {
            return false;
        }
        const RoomTransportEvent& taken = _events[_head++];
        // Every field is written, with copies, so the buffers of both events keep their capacity
        event.kind = taken.kind;
        event.text = taken.text;
        event.closeCode = taken.closeCode;
        event.reason = taken.reason;
        if (_head == _count) {
            _head = 0;
            _count = 0;
        }
        return true;
    }

    bool EmscriptenRoomTransport::OnOpen(int /*eventType*/, const EmscriptenWebSocketOpenEvent* /*event*/, void* userData) {
        auto* self = static_cast<EmscriptenRoomTransport*>(userData);
        ASSERT(Thread::CurrentId() == self->_owner, "a WebSocket callback ran off the thread of its transport");
        if (self->_phase == Phase::Connecting) {
            self->_phase = Phase::Open;
            self->PushEvent(RoomTransportEventKind::Opened, {}, kRoomTransportAbnormalClosure, {});
        }
        return true;
    }

    bool EmscriptenRoomTransport::OnMessage(int /*eventType*/, const EmscriptenWebSocketMessageEvent* event, void* userData) {
        auto* self = static_cast<EmscriptenRoomTransport*>(userData);
        ASSERT(Thread::CurrentId() == self->_owner, "a WebSocket callback ran off the thread of its transport");
        if (self->_phase != Phase::Open && self->_phase != Phase::Closing) {
            return true;
        }
        if (!event->isText) {
            // The room protocol is text; the server's own rule for a client is the same (1003)
            LOG_WARNING("Room client: a binary message from the server closed the connection");
            self->Finish(kUnsupportedData, "binary message");
            return true;
        }
        // The browser's text is UTF-8 with a terminating NUL that numBytes counts (libwebsocket.js: lengthBytesUTF8 + 1)
        const size_t length = event->numBytes > 0 ? event->numBytes - 1 : 0;
        if (length > RoomLimits::kMaxServerMessageBytes) {
            LOG_WARNING("Room client: a message over the limit closed the connection");
            self->Finish(kMessageTooBig, "message too big");
            return true;
        }
        // The buffer is freed when this callback returns, so the text is copied here
        self->PushEvent(RoomTransportEventKind::Text, {reinterpret_cast<const char*>(event->data), length}, kRoomTransportAbnormalClosure, {});
        return true;
    }

    bool EmscriptenRoomTransport::OnError(int /*eventType*/, const EmscriptenWebSocketErrorEvent* /*event*/, void* userData) {
        auto* self = static_cast<EmscriptenRoomTransport*>(userData);
        ASSERT(Thread::CurrentId() == self->_owner, "a WebSocket callback ran off the thread of its transport");
        // The browser says nothing about the cause (refused, no route, a rejected handshake, a certificate); `close` follows at once
        self->_sawError = true;
        return true;
    }

    bool EmscriptenRoomTransport::OnClose(int /*eventType*/, const EmscriptenWebSocketCloseEvent* event, void* userData) {
        auto* self = static_cast<EmscriptenRoomTransport*>(userData);
        ASSERT(Thread::CurrentId() == self->_owner, "a WebSocket callback ran off the thread of its transport");
        if (self->_phase == Phase::Finished) {
            return true;
        }
        const bool neverOpened = self->_phase == Phase::Connecting;
        // The browser reports 1006 for a connection that failed or dropped; a close without a status is reported by the library as 1005
        const uint16_t code = event->code == 0 ? kRoomTransportAbnormalClosure : event->code;
        eastl::string reason(event->reason, strnlen(event->reason, sizeof(event->reason)));
        if (reason.empty() && (code == kRoomTransportAbnormalClosure || code == kNoStatusReceived)) {
            reason = neverOpened ? "connection failed" : "connection lost";
        }
        self->Finish(code, reason);
        return true;
    }

    void EmscriptenRoomTransport::Finish(uint16_t closeCode, eastl::string_view reason) {
        if (_phase == Phase::Finished) {
            return;
        }
        _phase = Phase::Finished;
        DeleteSocket();
        PushEvent(RoomTransportEventKind::Closed, {}, closeCode, reason);
    }

    void EmscriptenRoomTransport::PushEvent(RoomTransportEventKind kind, eastl::string_view text, uint16_t closeCode, eastl::string_view reason) {
        if (_count == _events.size()) {
            _events.emplace_back();
        }
        RoomTransportEvent& slot = _events[_count++];
        slot.kind = kind;
        slot.text.assign(text.data(), text.size());
        slot.closeCode = closeCode;
        slot.reason.assign(reason.data(), reason.size());
    }

    void EmscriptenRoomTransport::DeleteSocket() {
        if (_socket <= 0) {
            return;
        }
        // close(1000) ends a connection that is open or still connecting (a no-op on one that has ended), and the server sees the
        // normal closure, not "no status"; delete detaches every callback, so nothing of this socket reaches the object afterwards
        emscripten_websocket_close(_socket, kRoomTransportNormalClosure, nullptr);
        emscripten_websocket_delete(_socket);
        _socket = 0;
    }

}  // namespace BECore

#endif  // PLATFORM_WEB
