#pragma once

namespace BECore {

    /// The status a transport reports when a connection ends without a close frame (refused, dropped, aborted): RFC 6455, 1006
    inline constexpr uint16_t kRoomTransportAbnormalClosure = 1006;

    /// The code of a close that the client asks for on purpose: RFC 6455, 1000
    inline constexpr uint16_t kRoomTransportNormalClosure = 1000;

    /// The close the server answers a client that broke its limits with (`rate_limited`, `too_many_failed_joins`): RFC 6455, 1008
    inline constexpr uint16_t kRoomTransportPolicyViolation = 1008;

    CORE_ENUM(RoomTransportEventKind, uint8_t, Opened, Text, Closed)

    /// One thing that happened on a connection
    struct RoomTransportEvent {
        RoomTransportEventKind kind = RoomTransportEventKind::Closed;
        /// Text: the message
        eastl::string text;
        /// Closed: the code of the close, kRoomTransportAbnormalClosure when the connection ended without one
        uint16_t closeCode = kRoomTransportAbnormalClosure;
        /// Closed: why, for the log and for a message to the player
        eastl::string reason;
    };

    /**
     * @brief One connection to a room server, polled.
     *
     * Everything is delivered on the calling thread by PollEvent(): no callback ever runs on a network thread, so the owner needs no
     * lock. A transport serves one connection: after Closed it is finished and the owner makes a new one. Exactly one Closed follows
     * every Open() that returned a value (a refused connect included, with kRoomTransportAbnormalClosure and a reason), and it is the
     * last event; every text that arrived before it is delivered before it. The destructor ends the connection and is safe in every
     * state; it does not wait for the network, except that the native transport joins its reader thread, which may still be inside
     * getaddrinfo (the system call cannot be cancelled), so it can take as long as one name lookup.
     *
     * The implementations: NativeRoomTransport (sockets, desktop) and EmscriptenRoomTransport (the browser's WebSocket).
     */
    class IRoomTransport {
    public:
        virtual ~IRoomTransport() = default;

        /// Starts connecting to @p url (`ws://host[:port][/path]`, or `wss://` where the platform speaks it). An error is a message for
        /// the log and means no Closed will follow
        [[nodiscard]] virtual std::expected<void, eastl::string> Open(StringView url) = 0;

        /// Queues a text message; false when the connection is not open, is closing, or its queue is full
        virtual bool SendText(StringView text) = 0;

        /// Starts the closing handshake with @p code and @p reason; Closed follows. During the connect it is the same as Abort()
        virtual void Close(uint16_t code, StringView reason) = 0;

        /// Ends the connection now, without a close frame; Closed (kRoomTransportAbnormalClosure) still follows
        virtual void Abort() = 0;

        /// Takes the next event and overwrites every field of @p event (so its buffers keep their capacity); false when there is none.
        /// Does not allocate when there is none
        [[nodiscard]] virtual bool PollEvent(RoomTransportEvent& event) = 0;
    };

    /// Makes a transport for one connection; RoomClient asks for a new one for every connection
    using RoomTransportFactory = std::function<eastl::unique_ptr<IRoomTransport>()>;

    /// The transport of this platform, or nullptr where there is none (a build without sockets that is not the browser)
    [[nodiscard]] eastl::unique_ptr<IRoomTransport> MakeDefaultRoomTransport();

}  // namespace BECore
