#include "WebSocketClient.h"

#include <RemoteControl/WebSocketHandshake.h>

namespace BECore {

    namespace {

        constexpr eastl::string_view kHandshakeHeadEnd = "\r\n\r\n";
        constexpr size_t kReadChunkSize = 16 * 1024;
        constexpr size_t kHandshakeChunkSize = 1024;
        constexpr size_t kFrameOverheadBytes = 14;  // header and masking key of a frame, for the byte cap of the send queue
        constexpr size_t kCloseCodeSize = 2;
        constexpr size_t kMaskSize = 4;
        constexpr uint32_t kByteMask = 0xFF;
        constexpr uint32_t kBitsPerByte = 8;

        [[nodiscard]] size_t EventBytes(const WebSocketClientEvent& event) {
            return event.text.size() + event.reason.size();
        }

        /// The payload of a close frame: the code, then the reason cut to fit a control frame
        [[nodiscard]] eastl::vector<uint8_t> MakeClosePayload(uint16_t code, eastl::string_view reason) {
            eastl::vector<uint8_t> payload;
            payload.push_back(static_cast<uint8_t>(code >> kBitsPerByte));
            payload.push_back(static_cast<uint8_t>(code & kByteMask));
            const size_t reasonSize = std::min(reason.size(), kMaxControlPayload - kCloseCodeSize);
            payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(reason.data()), reinterpret_cast<const uint8_t*>(reason.data()) + reasonSize);
            return payload;
        }

    }  // namespace

    WebSocketClient::~WebSocketClient() {
        Abort();
        if (_reader.joinable()) {
            _reader.join();
        }
    }

    std::expected<void, eastl::string> WebSocketClient::Open(eastl::string_view url, WebSocketClientSettings settings) {
        auto parsed = ParseWebSocketUrl(url);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        if (parsed->secure && !settings.tls.factory && !IsTlsSupported()) {
            return std::unexpected(Format("{}: wss:// needs TLS, and this platform has no TLS backend (use ws://, or the browser build for wss://)", kTlsUnsupported));
        }

        if (_reader.joinable()) {
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (_state != WebSocketClientState::Ended) {
                    return std::unexpected(eastl::string("the previous connection has not ended yet"));
                }
            }
            _reader.join();  // the reader only has to leave its function; the writer was joined before the state became Ended
        }
        _stream.Close();

        _settings = std::move(settings);
        _url = std::move(*parsed);
        _cancel = false;
        _bytesReceived = 0;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _state = WebSocketClientState::Connecting;
            _aborted = false;
            _readerDone = false;
            _writerDone = false;
            _outbox.clear();
            _outboxBytes = 0;
            _inbox.clear();
            _inboxBytes = 0;
            _endSet = false;
            _endCode = WebSocketCloseCode::kAbnormalClosure;
            _endReason.clear();
        }
        _reader = std::thread([this] { ReaderMain(); });
        return {};
    }

    bool WebSocketClient::SendText(eastl::string_view text) {
        return QueueFrame(WebSocketOpcode::kText, eastl::vector<uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), reinterpret_cast<const uint8_t*>(text.data()) + text.size()),
                          /*isClose=*/false);
    }

    void WebSocketClient::Close(uint16_t code, eastl::string_view reason) {
        bool connecting = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            connecting = _state == WebSocketClientState::Connecting;
        }
        if (connecting) {
            AbortConnection(code, reason);
        } else if (QueueFrame(WebSocketOpcode::kClose, MakeClosePayload(code, reason), /*isClose=*/true)) {
            SetEnd(code, reason);
        }
    }

    void WebSocketClient::Abort() {
        AbortConnection(WebSocketCloseCode::kAbnormalClosure, "the connection was aborted");
    }

    bool WebSocketClient::PollEvent(WebSocketClientEvent& event) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_inbox.empty()) {
                return false;
            }
            event = std::move(_inbox.front());
            _inbox.pop_front();
            _inboxBytes -= EventBytes(event);
        }
        _inboxSpace.notify_one();
        return true;
    }

    bool WebSocketClient::IsOpen() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _state == WebSocketClientState::Open && !_aborted;
    }

    bool WebSocketClient::QueueFrame(uint8_t opcode, eastl::vector<uint8_t> payload, bool isClose) {
        enum class Outcome : uint8_t { Refused, Queued, Overflow };
        Outcome outcome = Outcome::Refused;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_state != WebSocketClientState::Open || _aborted) {
                return false;
            }
            const size_t bytes = payload.size() + kFrameOverheadBytes;
            if (_outboxBytes + bytes > _settings.maxOutboxBytes) {
                // A frame that could never fit is the caller's mistake and is only refused; a frame that would fit an empty queue, but not this one, means the server does not read
                outcome = bytes > _settings.maxOutboxBytes || _outbox.empty() ? Outcome::Refused : Outcome::Overflow;
            } else {
                _outboxBytes += bytes;
                if (isClose) {
                    _state = WebSocketClientState::Closing;  // nothing may follow the close frame
                }
                _outbox.push_back(OutgoingFrame{opcode, std::move(payload), isClose});
                outcome = Outcome::Queued;
            }
        }
        if (outcome == Outcome::Overflow) {
            LOG_WARNING("{}: the server does not read its messages, the connection is dropped"_format(_settings.logName));
            AbortConnection(WebSocketCloseCode::kAbnormalClosure, "the server does not read the messages sent to it");
        } else if (outcome == Outcome::Queued) {
            _wake.notify_one();
        }
        return outcome == Outcome::Queued;
    }

    void WebSocketClient::AbortConnection(uint16_t code, eastl::string_view reason) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            SetEndLocked(code, reason);
            _aborted = true;
            _outbox.clear();
            _outboxBytes = 0;
            if (_stream.IsValid()) {
                _stream.ShutdownBoth();  // wakes the reader and the writer where they wait on the stream
            }
        }
        _cancel = true;  // wakes a connect in progress
        _wake.notify_all();
        _inboxSpace.notify_all();
    }

    eastl::string WebSocketClient::WithStreamError(eastl::string_view what) const {
        const eastl::string detail = _stream.GetLastError();
        return detail.empty() ? eastl::string(what.data(), what.size()) : Format("{}: {}", what, detail);
    }

    void WebSocketClient::SetEndLocked(uint16_t code, eastl::string_view reason) {
        if (!_endSet) {
            _endSet = true;
            _endCode = code;
            _endReason.assign(reason.data(), reason.size());
        }
    }

    void WebSocketClient::SetEnd(uint16_t code, eastl::string_view reason) {
        std::lock_guard<std::mutex> lock(_mutex);
        SetEndLocked(code, reason);
    }

    void WebSocketClient::PushEvent(WebSocketClientEvent event) {
        std::lock_guard<std::mutex> lock(_mutex);
        _inboxBytes += EventBytes(event);
        _inbox.push_back(std::move(event));
    }

    void WebSocketClient::ReaderMain() {
        BE_PROFILE_THREAD_ORDER(Format("{} reader", _settings.threadName).c_str(), _settings.threadOrder);

        eastl::vector<uint8_t> pendingBytes;
        if (Connect() && Handshake(pendingBytes)) {
            ReadMessages(std::move(pendingBytes));
        }

        // The reading is over. A connection nobody is closing is over altogether; one that is closing gets closeTimeout to send its close frame
        bool closing = false;
        bool timedOut = false;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _readerDone = true;
            closing = _state == WebSocketClientState::Closing && !_aborted && _writer.joinable();
            if (closing) {
                _wake.notify_all();
                timedOut = !_wake.wait_for(lock, _settings.closeTimeout, [this] { return _writerDone || _aborted; });
            }
        }
        if (!closing || timedOut) {
            AbortConnection(WebSocketCloseCode::kAbnormalClosure, "the connection was lost");
        }
        if (_writer.joinable()) {
            _writer.join();
        }

        WebSocketClientEvent closed;
        closed.kind = WebSocketClientEventKind::Closed;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            closed.closeCode = _endSet ? _endCode : WebSocketCloseCode::kAbnormalClosure;
            closed.reason = _endSet ? _endReason : eastl::string("the connection ended");
            _inboxBytes += EventBytes(closed);
            _inbox.push_back(closed);
            _state = WebSocketClientState::Ended;
        }
        LOG_INFO("{}: the connection to {}:{} ended: {} {}"_format(_settings.logName, _url.host, _url.port, closed.closeCode, closed.reason));
    }

    bool WebSocketClient::Connect() {
        // A TLS stream connects and does its handshake inside Connect(), so with wss:// the one deadline of the opening starts before the
        // name lookup and the TCP connect, and they count against handshakeTimeout; with ws:// it starts when the socket is connected
        const auto started = std::chrono::steady_clock::now();
        _handshakeDeadline = started + _settings.handshakeTimeout;
        auto connected = ClientStream::Connect(_url, _settings.tls, _settings.connectTimeout, _handshakeDeadline, &_cancel);
        if (!connected) {
            SetEnd(WebSocketCloseCode::kAbnormalClosure, connected.error());
            return false;
        }
        if (!_url.secure) {
            _handshakeDeadline = std::chrono::steady_clock::now() + _settings.handshakeTimeout;
        }
        std::lock_guard<std::mutex> lock(_mutex);
        _stream = std::move(*connected);
        if (_aborted) {
            _stream.ShutdownBoth();  // Abort() came while connecting: the handshake then ends at once
            return false;
        }
        return true;
    }

    bool WebSocketClient::Handshake(eastl::vector<uint8_t>& pendingBytes) {
        const eastl::string key = GenerateClientKey(_random);
        const eastl::string request = BuildClientHandshakeRequest(_url, key, _settings.origin);
        if (!_stream.SendAll(AsBytes(request))) {
            SetEnd(WebSocketCloseCode::kAbnormalClosure, WithStreamError("sending the opening request failed"));
            return false;
        }

        // One deadline for the whole answer, as on the server's side: a timeout per Receive() would let a server that sends a byte now and then hold us.
        // With TLS it is the deadline of the TLS handshake as well
        const auto deadline = _handshakeDeadline;
        eastl::vector<uint8_t> received;
        std::array<uint8_t, kHandshakeChunkSize> chunk;
        size_t headSize = 0;
        for (;;) {
            // Zero would mean "wait for ever" to the socket, so a deadline that is (nearly) reached ends the handshake
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left <= std::chrono::milliseconds::zero()) {
                SetEnd(WebSocketCloseCode::kAbnormalClosure, Format("the server did not answer the opening request within {} ms", _settings.handshakeTimeout.count()));
                return false;
            }
            _stream.SetReceiveTimeout(left);
            const auto count = _stream.Receive(eastl::span<uint8_t>(chunk.data(), chunk.size()));
            if (!count) {
                switch (count.error()) {
                    case SocketError::TimedOut:
                        SetEnd(WebSocketCloseCode::kAbnormalClosure, Format("the server did not answer the opening request within {} ms", _settings.handshakeTimeout.count()));
                        break;
                    case SocketError::Closed:
                        SetEnd(WebSocketCloseCode::kAbnormalClosure, "the connection was closed during the opening handshake");
                        break;
                    case SocketError::Failed:
                        SetEnd(WebSocketCloseCode::kAbnormalClosure, WithStreamError("the connection failed during the opening handshake"));
                        break;
                }
                return false;
            }
            _bytesReceived.fetch_add(*count, std::memory_order_relaxed);
            // Pointers, not std::array iterators: EASTL's insert does not take MSVC's class iterator
            received.insert(received.end(), chunk.data(), chunk.data() + *count);

            const eastl::string_view text(reinterpret_cast<const char*>(received.data()), received.size());
            const size_t end = text.find(kHandshakeHeadEnd);
            if (end != eastl::string_view::npos) {
                headSize = end + kHandshakeHeadEnd.size();
                break;
            }
            if (received.size() > kMaxHandshakeBytes) {
                SetEnd(WebSocketCloseCode::kAbnormalClosure, "the server's answer to the opening request is too long");
                return false;
            }
        }

        const auto accepted = CheckServerHandshakeResponse(eastl::string_view(reinterpret_cast<const char*>(received.data()), headSize), key);
        if (!accepted) {
            SetEnd(WebSocketCloseCode::kAbnormalClosure, accepted.error());
            return false;
        }
        _stream.SetReceiveTimeout(std::chrono::milliseconds::zero());
        pendingBytes.assign(received.begin() + static_cast<ptrdiff_t>(headSize), received.end());

        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_aborted) {
                return false;
            }
            _state = WebSocketClientState::Open;
        }
        LOG_INFO("{}: connected to {}:{}"_format(_settings.logName, _url.host, _url.port));
        _writer = std::thread([this] { WriterMain(); });
        WebSocketClientEvent opened;
        opened.kind = WebSocketClientEventKind::Opened;
        PushEvent(std::move(opened));
        return true;
    }

    void WebSocketClient::ReadMessages(eastl::vector<uint8_t> pendingBytes) {
        WebSocketFrameParser parser(_settings.maxMessageBytes, WebSocketPeer::Server);
        parser.Feed(eastl::span<const uint8_t>(pendingBytes.data(), pendingBytes.size()));

        std::array<uint8_t, kReadChunkSize> chunk;
        for (;;) {
            for (;;) {
                auto popped = parser.Pop();
                if (!popped) {
                    LOG_WARNING("{}: the server broke the protocol: {}"_format(_settings.logName, popped.error().reason));
                    SetEnd(popped.error().closeCode, popped.error().reason);
                    QueueFrame(WebSocketOpcode::kClose, MakeClosePayload(popped.error().closeCode, popped.error().reason), /*isClose=*/true);
                    return;
                }
                if (!popped->has_value()) {
                    break;
                }
                if (!HandleMessage(std::move(**popped))) {
                    return;
                }
            }

            const auto received = _stream.Receive(eastl::span<uint8_t>(chunk.data(), chunk.size()));
            if (!received) {
                // An Abort() has set its reason before it shut the socket down, and the first reason stays
                SetEnd(WebSocketCloseCode::kAbnormalClosure,
                       received.error() == SocketError::Closed ? eastl::string("the server closed the connection without a close frame") : WithStreamError("the connection failed"));
                return;
            }
            _bytesReceived.fetch_add(*received, std::memory_order_relaxed);
            parser.Feed(eastl::span<const uint8_t>(chunk.data(), *received));
        }
    }

    bool WebSocketClient::HandleMessage(WebSocketMessage message) {
        switch (message.kind) {
            case WebSocketMessageKind::Text:
                return PushText(message);
            case WebSocketMessageKind::Binary: {
                constexpr eastl::string_view kReason = "binary messages are not supported";
                SetEnd(WebSocketCloseCode::kUnsupportedData, kReason);
                QueueFrame(WebSocketOpcode::kClose, MakeClosePayload(WebSocketCloseCode::kUnsupportedData, kReason), /*isClose=*/true);
                return false;
            }
            case WebSocketMessageKind::Ping:
                QueueFrame(WebSocketOpcode::kPong, std::move(message.payload), /*isClose=*/false);
                return true;
            case WebSocketMessageKind::Pong:
                return true;
            case WebSocketMessageKind::Close:
                SetEnd(message.closeCode, message.GetText());
                QueueFrame(WebSocketOpcode::kClose, MakeClosePayload(message.closeCode == WebSocketCloseCode::kNoStatusReceived ? WebSocketCloseCode::kNormal : message.closeCode, {}),
                           /*isClose=*/true);
                return false;
        }
        return false;
    }

    bool WebSocketClient::PushText(WebSocketMessage& message) {
        WebSocketClientEvent event;
        event.kind = WebSocketClientEventKind::Text;
        event.text.assign(message.GetText().data(), message.GetText().size());

        std::unique_lock<std::mutex> lock(_mutex);
        // A full inbox stops the reader, so the server's queue fills and TCP slows it; an empty one takes any message, whatever the limits say
        _inboxSpace.wait(lock, [&] { return _aborted || _inbox.empty() || (_inbox.size() < _settings.maxInboxEvents && _inboxBytes + EventBytes(event) <= _settings.maxInboxBytes); });
        if (_aborted) {
            return false;
        }
        _inboxBytes += EventBytes(event);
        _inbox.push_back(std::move(event));
        return true;
    }

    void WebSocketClient::WriterMain() {
        BE_PROFILE_THREAD_ORDER(Format("{} writer", _settings.threadName).c_str(), _settings.threadOrder + 1);

        for (;;) {
            OutgoingFrame frame;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _wake.wait(lock, [this] { return _aborted || !_outbox.empty(); });
                if (_aborted) {
                    break;
                }
                frame = std::move(_outbox.front());
                _outbox.pop_front();
                _outboxBytes -= frame.payload.size() + kFrameOverheadBytes;
            }

            // A fresh key for every frame (RFC 6455, 5.3)
            const uint32_t random = _random();
            WebSocketMask mask{};
            for (size_t i = 0; i < kMaskSize; ++i) {
                mask[i] = static_cast<uint8_t>((random >> (kBitsPerByte * i)) & kByteMask);
            }
            const eastl::vector<uint8_t> bytes = EncodeFrame(frame.opcode, eastl::span<const uint8_t>(frame.payload.data(), frame.payload.size()), mask);
            if (!_stream.SendAll(eastl::span<const uint8_t>(bytes.data(), bytes.size()))) {
                AbortConnection(WebSocketCloseCode::kAbnormalClosure, WithStreamError("sending to the server failed"));
                break;
            }
            if (frame.isClose) {
                // Our side of the closing handshake is sent; the answer (or the reader ending) completes it
                bool answered = false;
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    answered = _wake.wait_for(lock, _settings.closeTimeout, [this] { return _aborted || _readerDone; });
                }
                if (!answered) {
                    AbortConnection(WebSocketCloseCode::kAbnormalClosure, "the server did not answer the close frame");
                }
                _stream.ShutdownBoth();  // the server sees the end of the stream after the close frame
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(_mutex);
            _writerDone = true;
        }
        _wake.notify_all();
    }

}  // namespace BECore
