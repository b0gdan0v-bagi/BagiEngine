#include "RemoteSession.h"

#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlProperty.h>
#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneControl.h>
#include <EASTL/algorithm.h>

#if BE_WITH_REMOTE_CONTROL
#include <RemoteControl/WebSocketServer.h>
#include <RoomClient/NativeRoomTransport.h>
#endif

namespace BECore {

    namespace {

        constexpr StringView kStatus = "status";
        constexpr StringView kSchema = "schema";
        constexpr StringView kBatch = "batch";
        constexpr StringView kSceneTree = "scene_tree";
        constexpr StringView kSceneGet = "scene_get";
        constexpr StringView kSceneSet = "scene_set";
        constexpr StringView kSceneSetNode = "scene_set_node";

        [[nodiscard]] bool IsDue(const Optional<RemoteSessionTime>& dueAt, RemoteSessionTime now) {
            return !dueAt || now >= *dueAt;
        }

    }  // namespace

    UniquePtr<IRoomTransport> MakeRemoteTransport() {
#if BE_WITH_REMOTE_CONTROL
        // A schema is longer than a message of the rooms; the server sends nothing longer than its own queue holds
        return MakeUnique<NativeRoomTransport>(RoomTransportLimits{.maxMessageBytes = WebSocketServer::kMaxOutboxBytes}, "Remote editor");
#else
        return nullptr;
#endif
    }

    RemoteSession::RemoteSession(RoomTransportFactory transportFactory) : _transportFactory(std::move(transportFactory)) {}

    std::expected<void, String> RemoteSession::Attach(StringView address) {
        String target(address);  // @p address may view what Detach() clears
        auto transport = Dial(target);
        if (!transport) {
            return std::unexpected(std::move(transport.error()));
        }
        Detach();
        _address = std::move(target);
        _transport = std::move(*transport);
        _state = RemoteSessionState::Connecting;
        return {};
    }

    void RemoteSession::Detach() {
        DropConnection();
        _state = RemoteSessionState::Detached;
        _address.clear();
        _lossReason.clear();
        _runMode.clear();
        _retryAt.reset();
        _retryDelay = RemoteSessionTiming::kFirstRetry;
        _outageReported = false;
        _schema.Clear();
        _tree.Clear();
        ClearSelection();
        _writes.Clear();
    }

    void RemoteSession::Poll(RemoteSessionTime now) {
        if (!_transport && _retryAt && now >= *_retryAt) {
            _retryAt.reset();
            if (auto transport = Dial(_address)) {
                _transport = std::move(*transport);
            } else {
                Lose(transport.error(), now);
            }
        }
        // A loss lets go of the transport, which ends the loop
        while (_transport && _transport->PollEvent(_event)) {
            switch (_event.kind) {
                case RoomTransportEventKind::Opened:
                    HandleOpened();
                    break;
                case RoomTransportEventKind::Text:
                    HandleText(_event.text);
                    break;
                case RoomTransportEventKind::Closed:
                    Lose(Format("{} {}", _event.closeCode, _event.reason), now);
                    break;
            }
        }
        SendDue(now);
        if (!IsWaiting()) {
            _waitingSince.reset();
        } else if (!_waitingSince) {
            _waitingSince = now;
        }
        _stalled = _waitingSince && now - *_waitingSince >= RemoteSessionTiming::kStallAfter;
    }

    void RemoteSession::Select(StringView path) {
        if (_tree.Find(path) == nullptr) {
            ClearSelection();
            return;
        }
        if (_selection && StringView(*_selection) == path) {
            return;
        }
        String selected(path);  // @p path may view a component that goes with the old selection
        ClearSelection();
        _selection = std::move(selected);
    }

    void RemoteSession::ClearSelection() {
        _selection.reset();
        _components.clear();
        _componentsDueAt.reset();
        ++_selectionVersion;
    }

    std::expected<UniquePtr<IRoomTransport>, String> RemoteSession::Dial(StringView address) {
        UniquePtr<IRoomTransport> transport = _transportFactory();
        if (!transport) {
            return std::unexpected(String("this build has no sockets, so the editor cannot attach"));
        }
        if (auto opened = transport->Open(address); !opened) {
            return std::unexpected(std::move(opened.error()));
        }
        return transport;
    }

    void RemoteSession::DropConnection() {
        _transport.reset();
        _open = false;
        _hasSchema = false;
        _hasTree = false;
        _statusRequest = 0;
        _schemaRequest = 0;
        _treeRequest = 0;
        _componentRequest = 0;
        _sentWrites.clear();
        _schemaDueAt.reset();
        _treeDueAt.reset();
        _componentsDueAt.reset();
        _waitingSince.reset();
        _stalled = false;
    }

    void RemoteSession::Lose(StringView reason, RemoteSessionTime now) {
        DropConnection();
        _state = RemoteSessionState::Lost;
        _retryAt = now + _retryDelay;
        _retryDelay = eastl::min(_retryDelay * 2, RemoteSessionTiming::kMaxRetry);
        // One line per outage: the retries go on for as long as nothing listens. Not a warning, which a QA run reads as a finding
        if (!_outageReported) {
            _outageReported = true;
            _lossReason = String(reason);
            LOG_INFO("Remote editor: no connection to {} ({}), retrying", _address, _lossReason);
        }
    }

    void RemoteSession::GiveUpWithout(StringView method) {
        DropConnection();
        _state = RemoteSessionState::Lost;
        _retryAt.reset();
        _lossReason = Format("the program has no method '{}' ({})", method, ControlErrors::kUnknownMethod);
        LOG_INFO("Remote editor: cannot edit what listens at {}: {}", _address, _lossReason);
    }

    void RemoteSession::HandleOpened() {
        _open = true;
        // The edits made for the last connection are not for what answers now
        _writes.Clear();
        _statusRequest = SendRequest(kStatus, [](JsonWriter&) {});
    }

    void RemoteSession::HandleText(StringView text) {
        if (!_envelope.Parse(text)) {
            return;
        }
        const JsonView response(_envelope, 0);
        const uint64_t id = FindParam(response, "id").value_or(JsonView{}).AsUint64().value_or(0);
        if (id == 0) {
            return;  // a push, or nothing that answers a request
        }
        const Answer answer = ReadAnswer(response);
        if (id == _statusRequest) {
            _statusRequest = 0;
            _runMode = GetParamString(answer.result, "runMode").value_or(String{});
        } else if (id == _schemaRequest) {
            _schemaRequest = 0;
            HandleSchema(answer);
        } else if (id == _treeRequest) {
            _treeRequest = 0;
            HandleTree(id, answer);
        } else if (id == _componentRequest) {
            _componentRequest = 0;
            HandleComponents(id, answer);
        } else if (!HandleWrite(id, answer)) {
            return;
        }
        _waitingSince.reset();
    }

    RemoteSession::Answer RemoteSession::ReadAnswer(JsonView response) {
        return {GetParamBool(response, "ok").value_or(false), FindParam(response, "result").value_or(JsonView{}), FindParam(response, "error").value_or(JsonView{})};
    }

    bool RemoteSession::Answer::Failed(StringView code) const {
        return !ok && FindParam(error, "code").value_or(JsonView{}).TextEquals(code);
    }

    void RemoteSession::HandleSchema(const Answer& answer) {
        if (answer.ok) {
            _schema.Load(answer.result.Raw());
            _hasSchema = true;
            AttachWhenRead();
        } else if (answer.Failed(ControlErrors::kUnknownMethod)) {
            GiveUpWithout(kSchema);
        }
    }

    void RemoteSession::HandleTree(uint64_t requestId, const Answer& answer) {
        if (answer.ok) {
            _tree.Load(answer.result.Raw());
        } else if (answer.Failed(SceneControlErrors::kNoScene)) {
            _tree.Clear();
        } else {
            if (answer.Failed(ControlErrors::kUnknownMethod)) {
                GiveUpWithout(kSceneTree);
            }
            return;
        }
        _hasTree = true;
        _writes.ReleaseLayers(requestId);
        if (_selection && _tree.Find(*_selection) == nullptr) {
            ClearSelection();
        }
        AttachWhenRead();
    }

    void RemoteSession::HandleComponents(uint64_t requestId, const Answer& answer) {
        if (!answer.ok) {
            if (answer.Failed(ControlErrors::kUnknownMethod)) {
                GiveUpWithout(kBatch);
            }
            return;
        }
        if (_askedSelectionVersion != _selectionVersion) {
            return;
        }
        _components.resize(_askedIndices.size());
        size_t next = 0;
        for (const JsonView source : FindParam(answer.result, "results").value_or(JsonView{}).Items()) {
            if (next == _components.size()) {
                break;
            }
            const Answer call = ReadAnswer(source);
            if (call.Failed(ControlErrors::kUnknownMethod)) {
                GiveUpWithout(kSceneGet);
                return;
            }
            RemoteComponent& component = _components[next];
            if (call.ok) {
                component.Load(call.result.Raw(), _askedIndices[next]);
            } else {
                component.Clear();
            }
            if (component.IsLoaded()) {
                _writes.ReleaseComponent(component.GetNode(), component.GetType(), component.GetIndex(), requestId);
            }
            ++next;
        }
        for (; next < _components.size(); ++next) {
            _components[next].Clear();
        }
    }

    bool RemoteSession::HandleWrite(uint64_t requestId, const Answer& answer) {
        const auto sent = eastl::find_if(_sentWrites.begin(), _sentWrites.end(), [requestId](const SentWrite& write) { return write.requestId == requestId; });
        if (sent == _sentWrites.end()) {
            return false;
        }
        if (answer.ok) {
            _writes.Accept(sent->writeId, _lastRequestId);
        } else {
            _writes.Refuse(sent->writeId, GetParamString(answer.error, "code").value_or(String{}), GetParamString(answer.error, "message").value_or(String{}));
        }
        _sentWrites.erase(sent);
        return true;
    }

    void RemoteSession::AttachWhenRead() {
        if (_state == RemoteSessionState::Attached || !_hasSchema || !_hasTree) {
            return;
        }
        _state = RemoteSessionState::Attached;
        _lossReason.clear();
        _retryDelay = RemoteSessionTiming::kFirstRetry;
        _outageReported = false;
        LOG_INFO("Remote editor: attached to {} (run mode '{}')", _address, _runMode);
    }

    void RemoteSession::SendDue(RemoteSessionTime now) {
        // Each step asks _open again: a refused send ends the connection
        if (_open && _state == RemoteSessionState::Attached) {
            SendWrites();
        }
        if (_open && !_hasSchema && _schemaRequest == 0 && IsDue(_schemaDueAt, now)) {
            _schemaRequest = SendRequest(kSchema, [](JsonWriter&) {});
            _schemaDueAt = now + RemoteSessionTiming::kTreeInterval;
        }
        if (_open && _treeRequest == 0 && IsDue(_treeDueAt, now)) {
            _treeRequest = SendRequest(kSceneTree, [](JsonWriter& writer) { writer.Member("limit", SceneControlLimits::kMaxTreeNodes); });
            _treeDueAt = now + RemoteSessionTiming::kTreeInterval;
        }
        if (_open && _state == RemoteSessionState::Attached && _selection && _componentRequest == 0 && IsDue(_componentsDueAt, now)) {
            SendComponents();
            _componentsDueAt = now + RemoteSessionTiming::kComponentInterval;
        }
    }

    void RemoteSession::SendWrites() {
        while (const RemoteWrite* write = _writes.NextToSend()) {
            const uint64_t request = SendRequest(write->IsLayer() ? kSceneSetNode : kSceneSet, [write](JsonWriter& writer) {
                if (write->IsLayer()) {
                    writer.Key("layer");
                    WritePropertyValue(writer, write->value);
                    writer.Member("node", StringView(write->node));
                    return;
                }
                writer.Member("component", StringView(write->component));
                writer.Member("index", write->index);
                writer.Member("node", StringView(write->node));
                writer.Member("path", StringView(write->path));
                writer.Key("value");
                WritePropertyValue(writer, write->value);
            });
            if (request == 0) {
                return;
            }
            _sentWrites.push_back({request, write->id});
            _writes.MarkSent(write->id);
        }
    }

    void RemoteSession::SendComponents() {
        const RemoteTreeNode* node = _tree.Find(*_selection);
        if (node == nullptr || node->components.empty()) {
            _components.clear();
            return;
        }
        const size_t count = eastl::min(node->components.size(), static_cast<size_t>(Scene::kMaxComponentsPerNode));
        _askedIndices.clear();
        for (auto type = node->components.begin(); type != node->components.begin() + count; ++type) {
            _askedIndices.push_back(static_cast<size_t>(eastl::count(node->components.begin(), type, *type)));
        }
        _askedSelectionVersion = _selectionVersion;
        _componentRequest = SendRequest(kBatch, [this, node](JsonWriter& writer) {
            writer.Key("calls");
            writer.BeginArray();
            for (size_t i = 0; i < _askedIndices.size(); ++i) {
                writer.BeginObject();
                writer.Member("method", kSceneGet);
                writer.Key("params");
                writer.BeginObject();
                writer.Member("component", StringView(node->components[i]));
                writer.Member("index", _askedIndices[i]);
                writer.Member("node", StringView(*_selection));
                writer.EndObject();
                writer.EndObject();
            }
            writer.EndArray();
            // A component that left the node must not keep the others from being read
            writer.Member("continueOnError", true);
        });
    }

    uint64_t RemoteSession::SendRequest(StringView method, FunctionRef<void(JsonWriter&)> writeParams) {
        const uint64_t id = _lastRequestId + 1;
        _request.clear();
        JsonWriter writer(_request);
        writer.BeginObject();
        writer.Member("id", id);
        writer.Member("method", method);
        writer.Key("params");
        writer.BeginObject();
        writeParams(writer);
        writer.EndObject();
        writer.EndObject();
        if (!_transport->SendText(_request)) {
            // The Closed that follows is the loss
            _open = false;
            _transport->Abort();
            return 0;
        }
        _lastRequestId = id;
        return id;
    }

    bool RemoteSession::IsWaiting() const {
        return _statusRequest != 0 || _schemaRequest != 0 || _treeRequest != 0 || _componentRequest != 0 || !_sentWrites.empty();
    }

}  // namespace BECore
