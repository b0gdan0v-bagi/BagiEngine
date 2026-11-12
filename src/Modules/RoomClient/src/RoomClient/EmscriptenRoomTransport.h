#pragma once

#include <RoomClient/RoomTransport.h>

#if PLATFORM_WEB

#include <emscripten/websocket.h>
#include <thread>

namespace BECore {

    /**
     * @brief The browser transport: the page's WebSocket (`emscripten_websocket_*`) behind IRoomTransport.
     *
     * **Main thread only.** The callbacks of the Emscripten WebSocket API run in the browser's event loop on the page's main thread
     * (in a threaded page too: the registration is proxied there), and they push into a member queue without a lock. So Open(), the
     * other calls and the destruction must happen on that thread, the one the game's frame runs on; this is asserted in the debug
     * build. The browser gives the script nothing of the closing handshake and no WebSocket pings (it answers them by itself).
     *
     * The URL is handed to the browser as it is (`ws://` or `wss://`; the page's `Origin` is sent by the browser, and the server's
     * list of origins has to name it; an `https://` page can open `wss://` only). A refused or failed connect is a Closed with
     * kRoomTransportAbnormalClosure. A script may close with 1000 or 3000..4999 only, so a close for another reason of ours (a binary
     * message: 1003, one over the limit: 1009) is reported with that code and ends the connection with 1000.
     */
    class EmscriptenRoomTransport final : public IRoomTransport {
    public:
        EmscriptenRoomTransport() = default;
        ~EmscriptenRoomTransport() override;
        EmscriptenRoomTransport(const EmscriptenRoomTransport&) = delete;
        EmscriptenRoomTransport& operator=(const EmscriptenRoomTransport&) = delete;

        [[nodiscard]] std::expected<void, eastl::string> Open(eastl::string_view url) override;
        bool SendText(eastl::string_view text) override;
        void Close(uint16_t code, eastl::string_view reason) override;
        void Abort() override;
        [[nodiscard]] bool PollEvent(RoomTransportEvent& event) override;

    private:
        enum class Phase : uint8_t {
            Idle,        // Open() was not called
            Connecting,  // the socket exists, `open` did not come
            Open,        // texts go both ways
            Closing,     // Close() was called: waiting for the browser's `close`
            Finished     // Closed is queued, nothing more comes
        };

        static bool OnOpen(int eventType, const EmscriptenWebSocketOpenEvent* event, void* userData);
        static bool OnMessage(int eventType, const EmscriptenWebSocketMessageEvent* event, void* userData);
        static bool OnError(int eventType, const EmscriptenWebSocketErrorEvent* event, void* userData);
        static bool OnClose(int eventType, const EmscriptenWebSocketCloseEvent* event, void* userData);

        /// Queues the one Closed and ends the connection on our side: the browser's callbacks are detached, the socket deleted
        void Finish(uint16_t closeCode, eastl::string_view reason);
        void PushEvent(RoomTransportEventKind kind, eastl::string_view text, uint16_t closeCode, eastl::string_view reason);
        void DeleteSocket();

        EMSCRIPTEN_WEBSOCKET_T _socket = 0;
        Phase _phase = Phase::Idle;
        /// An `error` came: the `close` that follows reports a failed connect, and the reason says so
        bool _sawError = false;
        std::thread::id _owner;
        /// The events not taken yet are _events[_head, _count); the slots stay allocated, so a steady stream reuses their buffers
        eastl::vector<RoomTransportEvent> _events;
        size_t _head = 0;
        size_t _count = 0;
        /// The NUL-terminated copy of the text being sent (the API takes a C string); a member so it keeps its capacity
        eastl::string _sendBuffer;
    };

}  // namespace BECore

#endif  // PLATFORM_WEB
