#pragma once

#include <Application/ControlLinkQuery.h>
#include <BECore/Control/ControlService.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <RoomClient/RoomTransport.h>
#include <chrono>
#include <cstddef>

namespace BECore {

    /// The time ControlLink::Poll() runs its retries against
    using ControlLinkTime = std::chrono::steady_clock::time_point;

    /**
     * @brief The page's end of the control link: dials the bridge, posts what it sends to a ControlService and sends the answers and pushes back.
     *
     * The protocol, the limits and the retry rules: .claude/rules/control-web-link.md. Main thread only; call Poll() once per frame, before
     * ControlService::Drain(). The link owns the service's event sink while it is connected, and the service has to outlive the link. A reply the
     * service gives for an earlier connection, or after Stop() or the destruction of the link (ControlService::Shutdown() answers late), is dropped.
     */
    class ControlLink {
    public:
        /// The longest `<channel> ` in front of a message: the digits of a uint64 and the space
        static constexpr size_t kMaxChannelPrefixBytes = 21;

        /// What the transport of a link has to take (a request behind its channel) and to hold unsent (as much as the outbox of the desktop server)
        static constexpr RoomTransportLimits kTransportLimits{.maxMessageBytes = ControlService::kMaxRequestBytes + kMaxChannelPrefixBytes, .maxSendBufferBytes = 16 * 1024 * 1024};

        /// @p transportFactory makes the transport of each connection
        ControlLink(ControlService& service, ControlLinkSettings settings, RoomTransportFactory transportFactory);
        ~ControlLink();

        ControlLink(const ControlLink&) = delete;
        ControlLink& operator=(const ControlLink&) = delete;

        /// Connects when a connection is due at @p now and takes everything the transport has. Does not allocate when there is nothing to take
        void Poll(ControlLinkTime now);

        /// Ends the connection for good
        void Stop();

    private:
        /// The sending end, shared with the replies and the event sink, which may outlive the link
        class Sender;

        void Connect(ControlLinkTime now);
        void HandleOpened();
        void HandleText(StringView message);
        void HandleClosed(ControlLinkTime now);
        void ScheduleRetry(ControlLinkTime now);
        void ReportOutage(StringView why);

        ControlService& _service;
        ControlLinkSettings _settings;
        RoomTransportFactory _transportFactory;
        IntrusivePtr<Sender> _sender;
        RoomTransportEvent _event;  // a member so that its buffers keep their capacity
        Optional<ControlLinkTime> _retryAt;
        std::chrono::milliseconds _retryDelay;
        bool _outageReported = false;
        bool _stopped = false;
    };

}  // namespace BECore
