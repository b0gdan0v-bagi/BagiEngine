#include "WebSocketServer.h"

#include <BECore/Control/ControlService.h>

namespace BECore {

    namespace {

        WebSocketListenerSettings MakeControlSettings() {
            WebSocketListenerSettings settings;
            settings.bind = SocketBind::Loopback;
            settings.maxConnections = WebSocketServer::kMaxConnections;
            settings.maxMessageBytes = ControlService::kMaxRequestBytes;
            settings.maxOutboxBytes = WebSocketServer::kMaxOutboxBytes;
            settings.handshakeTimeout = WebSocketServer::kHandshakeTimeout;
            settings.logName = "Control API";
            settings.busyMessage = "the control API has too many connections";
            settings.threadName = "Control";
            return settings;
        }

    }  // namespace

    WebSocketServer::WebSocketServer(ControlService& service) : _service(service), _listener(MakeControlSettings(), MakeHandler()) {
        _clients.reserve(kMaxConnections);
    }

    WebSocketServer::~WebSocketServer() {
        Stop();
    }

    std::expected<uint16_t, ControlError> WebSocketServer::Start(uint16_t port) {
        if (IsRunning()) {
            return std::unexpected(MakeControlError(RemoteControlErrors::kAlreadyRunning, "the control server is already running"));
        }

        const auto started = _listener.Start(port);
        if (!started) {
            return started;
        }

        _service.SetEventSink([this](const String& text) { _listener.BroadcastText(text); });
        LOG_INFO("Control API listening on ws://127.0.0.1:{}", *started);
        return started;
    }

    void WebSocketServer::Stop() {
        if (!IsRunning()) {
            return;
        }

        _service.SetEventSink({});
        _listener.Stop();
        LOG_INFO("Control API stopped");
    }

    WebSocketHandler WebSocketServer::MakeHandler() {
        WebSocketHandler handler;
        handler.onOpen = [this](const WebSocketHandler::ConnectionPtr& connection) {
            _clientCount.fetch_add(1, std::memory_order_relaxed);
            IntrusivePtr<ControlClient> client = _service.OpenConnectionClient();
            LockGuard lock(_clientsMutex);
            _clients.push_back(ConnectionClient{connection->GetId(), std::move(client)});
        };
        handler.onClosed = [this](const WebSocketHandler::ConnectionPtr& connection) {
            _service.CloseConnectionClient(TakeClient(connection->GetId()));
            _clientCount.fetch_sub(1, std::memory_order_relaxed);
        };
        handler.onMessage = [this](const WebSocketHandler::ConnectionPtr& connection, WebSocketMessageView message) {
            WebSocketHandler::ConnectionPtr target = connection;
            _service.Post(message.GetText(), [target](const String& response) { target->QueueText(response); }, FindClient(connection->GetId()));
            return true;
        };
        return handler;
    }

    IntrusivePtr<ControlClient> WebSocketServer::FindClient(WebSocketConnectionId connection) {
        LockGuard lock(_clientsMutex);
        for (const ConnectionClient& entry : _clients) {
            if (entry.connection == connection) {
                return entry.client;
            }
        }
        return {};
    }

    IntrusivePtr<ControlClient> WebSocketServer::TakeClient(WebSocketConnectionId connection) {
        LockGuard lock(_clientsMutex);
        for (auto entry = _clients.begin(); entry != _clients.end(); ++entry) {
            if (entry->connection == connection) {
                IntrusivePtr<ControlClient> client = std::move(entry->client);
                _clients.erase(entry);
                return client;
            }
        }
        return {};
    }

}  // namespace BECore
