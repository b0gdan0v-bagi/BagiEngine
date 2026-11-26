#pragma once

#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneControl.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/SceneNodeControl.h>
#include <BECore/Scene/SceneNodePath.h>
#include <Common/ControlTestSupport.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <Common/SharedPtr.h>
#include <RoomClient/RoomTransport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// One connection to a FakeControlEndpoint, shared by the transport its client owns and the endpoint
    struct FakeControlConnection {
        String url;
        /// What the client sent, in order; the first `served` have reached the service
        Vector<String> requests;
        size_t served = 0;
        /// What the client polls
        Vector<RoomTransportEvent> inbox;
        size_t nextEvent = 0;
        bool closed = false;
        /// The client let go of its transport
        bool destroyed = false;
        /// SendText() says no
        bool refusesSend = false;

        void PushOpened() {
            inbox.emplace_back().kind = RoomTransportEventKind::Opened;
        }

        void PushText(StringView text) {
            RoomTransportEvent& event = inbox.emplace_back();
            event.kind = RoomTransportEventKind::Text;
            event.text = String(text);
        }

        /// The one Closed of the connection; a second call does nothing
        void PushClosed(uint16_t code, StringView reason) {
            if (closed) {
                return;
            }
            closed = true;
            RoomTransportEvent& event = inbox.emplace_back();
            event.kind = RoomTransportEventKind::Closed;
            event.closeCode = code;
            event.reason = String(reason);
        }

        /// The methods of the requests sent from index @p first on
        [[nodiscard]] Vector<String> Methods(size_t first = 0) const {
            Vector<String> methods;
            for (size_t i = first; i < requests.size(); ++i) {
                const ControlReply request{StringView(requests[i])};
                methods.push_back(StringOf(request.View(), "method"));
            }
            return methods;
        }
    };

    class FakeControlTransport final : public IRoomTransport {
    public:
        FakeControlTransport(SharedPtr<FakeControlConnection> connection, bool refused) : _connection(std::move(connection)), _refused(refused) {}

        ~FakeControlTransport() override {
            _connection->destroyed = true;
        }

        std::expected<void, String> Open(StringView url) override {
            if (!url.starts_with("ws://")) {
                return std::unexpected(String("not a ws:// address"));
            }
            _connection->url = String(url);
            if (_refused) {
                _connection->PushClosed(kRoomTransportAbnormalClosure, "connection refused");
            } else {
                _connection->PushOpened();
            }
            return {};
        }

        bool SendText(StringView text) override {
            if (_connection->closed || _connection->refusesSend) {
                return false;
            }
            _connection->requests.push_back(String(text));
            return true;
        }

        void Close(uint16_t code, StringView reason) override {
            _connection->PushClosed(code, reason);
        }

        void Abort() override {
            _connection->PushClosed(kRoomTransportAbnormalClosure, "aborted");
        }

        bool PollEvent(RoomTransportEvent& event) override {
            if (_connection->nextEvent >= _connection->inbox.size()) {
                return false;
            }
            // A copy, so that the buffers of the caller's event keep their capacity
            event = _connection->inbox[_connection->nextEvent++];
            return true;
        }

    private:
        SharedPtr<FakeControlConnection> _connection;
        bool _refused;
    };

    /**
     * @brief The program an editor attaches to: a real ControlService with the builtin and the scene methods over a scene of its own, reached
     * through transports in memory. Nothing runs until Serve().
     *
     * Needs the core services; sets a RecordingRenderer and takes it back. Outlives the clients that use its factory.
     */
    class FakeControlEndpoint {
    public:
        enum class Methods : uint8_t { Builtins, All };

        explicit FakeControlEndpoint(StringView nodes, Methods methods = Methods::All) {
            CoreManager::GetRendererManager().SetRenderer(New<RecordingRenderer>());
            XmlDeserializer deserializer;
            const String xml = SceneConfigText(nodes);
            EXPECT_TRUE(deserializer.LoadFromString(xml.c_str()));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            EXPECT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _scene = _manager->GetSceneByName("Main"_intern);
            _active = _scene;
            RegisterControlBuiltins(_service);
            if (methods == Methods::All) {
                RegisterSceneControl(_service, [this] { return _active; });
                RegisterSceneNodeControl(_service, [this] { return _active; });
            }
        }

        ~FakeControlEndpoint() {
            _service.Shutdown();
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        FakeControlEndpoint(const FakeControlEndpoint&) = delete;
        FakeControlEndpoint& operator=(const FakeControlEndpoint&) = delete;

        [[nodiscard]] RoomTransportFactory Factory() {
            return [this]() -> UniquePtr<IRoomTransport> {
                auto connection = MakeShared<FakeControlConnection>();
                _connections.push_back(connection);
                return MakeUnique<FakeControlTransport>(connection, _refusing);
            };
        }

        /// Runs what the latest connection has sent and queues the answers for it
        void Serve() {
            if (_connections.empty()) {
                return;
            }
            const SharedPtr<FakeControlConnection> connection = _connections.back();
            while (!connection->closed && connection->served < connection->requests.size()) {
                _service.Post(connection->requests[connection->served++], [connection](const String& response) { connection->PushText(response); });
            }
            _service.Drain();
        }

        /// The program ends the latest connection
        void Drop(uint16_t code = kRoomTransportAbnormalClosure, StringView reason = "connection lost") {
            Require(Latest()).PushClosed(code, reason);
        }

        /// The connections made from now on are refused
        void SetRefusing(bool refusing) {
            _refusing = refusing;
        }

        [[nodiscard]] size_t GetConnectionCount() const {
            return _connections.size();
        }

        /// The latest connection; one has to have been made
        [[nodiscard]] FakeControlConnection& Connection() {
            return Require(Latest());
        }

        [[nodiscard]] Scene& GetScene() {
            return Require(_scene);
        }

        /// No scene is active while @p has is false
        void SetHasScene(bool has) {
            _active = has ? _scene : nullptr;
        }

        [[nodiscard]] SceneNode& Node(StringView path) {
            const auto found = FindSceneNodeByPath(GetScene().GetRootNode(), path);
            return Require(found.has_value() ? *found : nullptr);
        }

    private:
        [[nodiscard]] FakeControlConnection* Latest() {
            return _connections.empty() ? nullptr : _connections.back().get();
        }

        ControlService _service;
        IntrusivePtr<SceneManager> _manager;
        Scene* _scene = nullptr;
        Scene* _active = nullptr;  // what the scene methods see
        Vector<SharedPtr<FakeControlConnection>> _connections;
        bool _refusing = false;
    };

}  // namespace BECore::Tests
