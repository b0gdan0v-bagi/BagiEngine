#include "ControlLink.h"

#include <BECore/Control/JsonWriter.h>
#include <EASTL/algorithm.h>

namespace BECore {

    namespace {

        constexpr int kLinkVersion = 1;       // the hello's `bagiControlLink`, as bagi_bridge.py expects it
        constexpr uint64_t kPushChannel = 0;  // every control client; the bridge counts the channels of requests from 1
        constexpr std::chrono::milliseconds kFirstRetry{1000};
        constexpr std::chrono::milliseconds kMaxRetry{10000};

    }  // namespace

    class ControlLink::Sender final : public RefCounted {
    public:
        explicit Sender(ControlService& service) : _service(service) {}

        /// A new connection; nothing goes out on it before Open()
        void Attach(UniquePtr<IRoomTransport> transport) {
            _transport = std::move(transport);
            ++_connection;
        }

        /// Sends @p hello and from then on the answers and the pushes; false when the transport refuses it
        [[nodiscard]] bool Open(StringView hello) {
            if (!_transport->SendText(hello)) {
                return false;
            }
            _open = true;
            _service.SetEventSink([sender = IntrusivePtr<Sender>(this)](const String& text) { sender->Send(sender->_connection, kPushChannel, text); });
            return true;
        }

        /// Sends `<channel> <text>` if @p connection is still the open one; a refusal aborts it
        void Send(uint64_t connection, uint64_t channel, StringView text) {
            if (!_open || connection != _connection) {
                return;
            }
            _message.clear();
            AppendFormat(_message, "{} ", channel);
            _message.append(text.data(), text.size());
            if (!_transport->SendText(_message)) {
                LOG_INFO("Control link: the transport refused a message of {} bytes, reconnecting", _message.size());
                Abort();
            }
        }

        /// Ends the connection now; its Closed event follows
        void Abort() {
            Silence();
            _transport->Abort();
        }

        /// Lets go of the transport, which ends the connection
        void Detach() {
            Silence();
            _transport.reset();
        }

        [[nodiscard]] bool PollEvent(RoomTransportEvent& event) {
            return _transport && _transport->PollEvent(event);
        }

        [[nodiscard]] bool HasTransport() const {
            return _transport != nullptr;
        }

        [[nodiscard]] bool IsOpen() const {
            return _open;
        }

        [[nodiscard]] uint64_t GetConnection() const {
            return _connection;
        }

    private:
        void Silence() {
            if (_open) {
                _open = false;
                _service.SetEventSink({});
            }
        }

        ControlService& _service;
        UniquePtr<IRoomTransport> _transport;
        String _message;  // the message being sent; a member so that it keeps its capacity
        uint64_t _connection = 0;
        bool _open = false;  // the hello went out and nothing ended the connection since
    };

    ControlLink::ControlLink(ControlService& service, ControlLinkSettings settings, RoomTransportFactory transportFactory)
        : _service(service), _settings(std::move(settings)), _transportFactory(std::move(transportFactory)), _sender(New<Sender>(service)), _retryDelay(kFirstRetry) {}

    ControlLink::~ControlLink() {
        Stop();
    }

    void ControlLink::Poll(ControlLinkTime now) {
        if (_stopped) {
            return;
        }
        if (!_sender->HasTransport() && (!_retryAt || now >= *_retryAt)) {
            Connect(now);
        }
        // A Closed lets go of the transport, which ends the loop
        while (_sender->PollEvent(_event)) {
            switch (_event.kind) {
                case RoomTransportEventKind::Opened:
                    HandleOpened();
                    break;
                case RoomTransportEventKind::Text:
                    HandleText(_event.text);
                    break;
                case RoomTransportEventKind::Closed:
                    HandleClosed(now);
                    break;
            }
        }
    }

    void ControlLink::Stop() {
        _stopped = true;
        _sender->Detach();
    }

    void ControlLink::Connect(ControlLinkTime now) {
        UniquePtr<IRoomTransport> transport = _transportFactory();
        if (!transport) {
            ReportOutage("there is no transport");
            ScheduleRetry(now);
            return;
        }
        if (const auto opened = transport->Open(_settings.address); !opened) {
            ReportOutage(opened.error());
            ScheduleRetry(now);
            return;
        }
        _sender->Attach(std::move(transport));
    }

    void ControlLink::HandleOpened() {
        _retryDelay = kFirstRetry;
        _outageReported = false;

        String hello;
        JsonWriter writer(hello);
        writer.BeginObject();
        writer.Member("bagiControlLink", kLinkVersion);
        writer.Member("token", _settings.token);
        writer.EndObject();
        if (!_sender->Open(hello)) {
            _sender->Abort();
            return;
        }
        LOG_INFO("Control link: connected to {}", _settings.address);
    }

    void ControlLink::HandleText(StringView message) {
        if (!_sender->IsOpen()) {
            return;  // the connection is being aborted: no answer could go back
        }
        const size_t space = message.find(' ');
        const Optional<uint64_t> channel = StringUtils::ParseNumber<uint64_t>(message.substr(0, space));
        if (!channel || *channel == kPushChannel) {
            LOG_INFO("Control link: a message without a channel, reconnecting");
            _sender->Abort();
            return;
        }
        if (space == StringView::npos) {
            return;  // that control connection ended: the link keeps nothing per channel
        }
        _service.Post(message.substr(space + 1),
                      [sender = _sender, connection = _sender->GetConnection(), channel = *channel](const String& response) { sender->Send(connection, channel, response); });
    }

    void ControlLink::HandleClosed(ControlLinkTime now) {
        _sender->Detach();
        if (_event.closeCode == kRoomTransportPolicyViolation) {
            // The token or the second tab: asking again gets the same answer
            _stopped = true;
            LOG_WARNING("Control link: the bridge refused the page ({}), not retrying", _event.reason);
            return;
        }
        ReportOutage(Format("{} {}", _event.closeCode, _event.reason));
        ScheduleRetry(now);
    }

    void ControlLink::ScheduleRetry(ControlLinkTime now) {
        _retryAt = now + _retryDelay;
        _retryDelay = eastl::min(_retryDelay * 2, kMaxRetry);
    }

    void ControlLink::ReportOutage(StringView why) {
        // One line per outage: the retries go on for as long as no bridge runs. Not a warning, which a QA run reads as a finding
        if (!_outageReported) {
            _outageReported = true;
            LOG_INFO("Control link: no connection to {} ({}), retrying", _settings.address, why);
        }
    }

}  // namespace BECore
