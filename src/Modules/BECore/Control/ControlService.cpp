#include "ControlService.h"

#include <BECore/Control/ControlCall.h>

namespace BECore {

    namespace {

        /// A method name from a client is echoed in an error message; keep the message short
        constexpr size_t kMaxEchoedMethodLength = 64;

    }  // namespace

    struct ControlService::Request {
        eastl::string method;
        Json id;
        Json params = Json::object();
        std::optional<ControlError> error;  // found while parsing; answered when the request's turn comes
        ControlReplyFn reply;
    };

    ControlService::ControlService() = default;
    ControlService::~ControlService() = default;

    void ControlService::ParseRequest(eastl::string_view text, Request& request) {
        if (text.size() > kMaxRequestBytes) {
            request.error = MakeControlError(ControlErrors::kRequestTooLarge, Format("the request is {} bytes, the limit is {}", text.size(), kMaxRequestBytes));
            return;
        }

        // Before the parse: a deep value is copied below (and again when it is handled), and that recurses on the stack of the posting thread
        if (IsJsonNestedDeeperThan(text, kMaxRequestDepth)) {
            request.error = MakeControlError(ControlErrors::kInvalidRequest, Format("the request nests deeper than {} levels", kMaxRequestDepth));
            return;
        }

        auto parsed = ParseJson(text);
        if (!parsed) {
            request.error = std::move(parsed.error());
            return;
        }
        const Json& message = *parsed;
        if (!message.is_object()) {
            request.error = MakeControlError(ControlErrors::kInvalidRequest, "the request has to be a JSON object");
            return;
        }

        // The id comes first so that every later error can be answered with it
        const Json* id = FindJsonMember(message, "id");
        if (id == nullptr || !(id->is_string() || id->is_number_integer())) {
            request.error = MakeControlError(ControlErrors::kInvalidRequest, "'id' has to be an integer or a string");
            return;
        }
        request.id = *id;

        auto method = GetJsonString(message, "method");
        if (!method || method->empty()) {
            request.error = MakeControlError(ControlErrors::kInvalidRequest, "'method' has to be a non-empty string");
            return;
        }
        request.method = std::move(*method);

        const Json* params = FindJsonMember(message, "params");
        if (params == nullptr || params->is_null()) {
            return;  // no parameters: the empty object of Request
        }
        if (!params->is_object()) {
            request.error = MakeControlError(ControlErrors::kInvalidParams, "'params' has to be an object");
            return;
        }
        request.params = *params;
    }

    void ControlService::Post(eastl::string_view requestText, ControlReplyFn reply) {
        auto request = eastl::make_unique<Request>();
        request->reply = std::move(reply);
        ParseRequest(requestText, *request);

        {
            LockGuard lock(_mutex);
            if (!_stopped) {
                _queue.push_back(std::move(request));
                _posted.notify_one();
                return;
            }
        }

        request->reply(MakeErrorResponse(request->id, MakeControlError(ControlErrors::kShuttingDown, "the application is shutting down")));
    }

    void ControlService::RegisterHandler(eastl::string_view method, Handler handler, bool bypassBarrier) {
        _handlers[eastl::string(method)] = HandlerEntry{std::move(handler), bypassBarrier};
    }

    void ControlService::SetEventSink(EventSink sink) {
        _eventSink = std::move(sink);
    }

    void ControlService::Publish(eastl::string_view event, const Json& data) {
        if (!_eventSink) {
            return;
        }
        Json message = Json::object();
        message["event"] = MakeJsonString(event);
        message["data"] = data;
        _eventSink(DumpJson(message));
    }

    void ControlService::SetFrameEndHook(eastl::string_view name, FrameEndHook hook) {
        for (HookEntry& entry : _frameEndHooks) {
            if (entry.name == name) {
                entry.hook = std::move(hook);
                return;
            }
        }
        _frameEndHooks.push_back(HookEntry{eastl::string(name), std::move(hook)});
    }

    bool ControlService::BypassesBarrier(const Request& request) const {
        const auto entry = _handlers.find(request.method);
        return !request.error && entry != _handlers.end() && entry->second.bypassBarrier;
    }

    void ControlService::Drain() {
        {
            LockGuard lock(_mutex);
            while (!_queue.empty()) {
                _inbox.push_back(std::move(_queue.front()));
                _queue.pop_front();
            }
        }

        for (auto it = _inbox.begin(); it != _inbox.end();) {
            if (_pending && !BypassesBarrier(**it)) {
                ++it;  // held back; the requests that bypass the barrier may still follow
                continue;
            }
            eastl::unique_ptr<Request> request = std::move(*it);
            it = _inbox.erase(it);
            Process(*request);
        }
    }

    void ControlService::Process(Request& request) {
        BE_PROFILE_SCOPE("ControlService::Process");
        if (request.error) {
            request.reply(MakeErrorResponse(request.id, *request.error));
            return;
        }

        const auto entry = _handlers.find(request.method);
        if (entry == _handlers.end()) {
            const eastl::string name = request.method.substr(0, kMaxEchoedMethodLength);
            request.reply(MakeErrorResponse(request.id, MakeControlError(ControlErrors::kUnknownMethod, Format("unknown method '{}'", name))));
            return;
        }

        // A copy: the handler may register handlers, which can move the entry
        const Handler handler = entry->second.handler;
        auto call = eastl::make_unique<ControlCall>(PassKey<ControlService>{}, std::move(request.method), std::move(request.id), std::move(request.params), std::move(request.reply));
        handler(*call);
        Settle(std::move(call));
    }

    void ControlService::Settle(eastl::unique_ptr<ControlCall> call) {
        if (call->IsAnswered()) {
            return;
        }

        if (call->_deferFrames == 0) {
            LOG_ERROR(Format("[ControlService] the handler of '{}' gave no answer", call->GetMethod()));
            call->Fail(ControlErrors::kInternalError, "the handler gave no answer");
            return;
        }

        if (_pending) {
            // Only a method that bypasses the barrier can get here while another call is pending
            LOG_ERROR(Format("[ControlService] '{}' cannot defer its answer while '{}' is pending", call->GetMethod(), _pending->GetMethod()));
            call->Fail(ControlErrors::kInternalError, "a call that bypasses the barrier cannot defer its answer");
            return;
        }

        _pendingFramesLeft = call->_deferFrames;
        _pendingSeconds = call->_deferSeconds;
        if (_manualClock && _pause != nullptr) {
            _pause->RequestSteps(_pendingFramesLeft, _pendingSeconds);  // no frame runs by itself: the pause steps the frames the call waits for
        }
        call->_deferFrames = 0;
        _pending = std::move(call);
    }

    void ControlService::WaitForRequests(std::chrono::milliseconds timeout) {
        UniqueLock lock(_mutex);
        _posted.wait_for(lock, timeout, [this] { return !_queue.empty(); });
    }

    void ControlService::EndFrame() {
        BE_PROFILE_SCOPE("ControlService::EndFrame");
        // By index: a hook may add hooks. A copy: a hook may replace itself
        for (size_t i = 0; i < _frameEndHooks.size(); ++i) {
            const FrameEndHook hook = _frameEndHooks[i].hook;
            hook();
        }

        if (!_pending) {
            return;
        }
        if (--_pendingFramesLeft > 0) {
            return;
        }

        eastl::unique_ptr<ControlCall> call = std::move(_pending);
        const ControlCall::DoneFn done = std::move(call->_done);
        call->_done = nullptr;
        if (done) {
            done(*call);
        }
        Settle(std::move(call));  // answered by done, deferred again, or an internal_error
    }

    void ControlService::Shutdown() {
        {
            LockGuard lock(_mutex);
            _stopped = true;
            while (!_queue.empty()) {
                _inbox.push_back(std::move(_queue.front()));
                _queue.pop_front();
            }
        }

        const ControlError error = MakeControlError(ControlErrors::kShuttingDown, "the application is shutting down");

        if (_pending) {
            eastl::unique_ptr<ControlCall> call = std::move(_pending);
            call->Fail(error);
        }
        while (!_inbox.empty()) {
            eastl::unique_ptr<Request> request = std::move(_inbox.front());
            _inbox.pop_front();
            request->reply(MakeErrorResponse(request->id, error));
        }
    }

}  // namespace BECore
