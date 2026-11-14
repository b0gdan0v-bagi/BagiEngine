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

        /// Every text message is a request; its response is queued by the main thread when the request has run, and the connection
        /// stays alive until then
        WebSocketHandler MakeControlHandler(ControlService& service, std::atomic<uint32_t>& clientCount) {
            WebSocketHandler handler;
            handler.onOpen = [&clientCount](const WebSocketHandler::ConnectionPtr&) { clientCount.fetch_add(1, std::memory_order_relaxed); };
            handler.onClosed = [&clientCount](const WebSocketHandler::ConnectionPtr&) { clientCount.fetch_sub(1, std::memory_order_relaxed); };
            handler.onMessage = [&service](const WebSocketHandler::ConnectionPtr& connection, WebSocketMessage message) {
                WebSocketHandler::ConnectionPtr target = connection;
                service.Post(message.GetText(), [target](String response) { target->QueueText(response); });
                return true;
            };
            return handler;
        }

    }  // namespace

    WebSocketServer::WebSocketServer(ControlService& service) : _service(service), _listener(MakeControlSettings(), MakeControlHandler(service, _clientCount)) {}

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

        _service.SetEventSink([this](const String& text) { _listener.ForEachOpenConnection([&text](WebSocketConnection& connection) { connection.QueueText(text); }); });
        LOG_INFO("Control API listening on ws://127.0.0.1:{}"_format(*started));
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

}  // namespace BECore
