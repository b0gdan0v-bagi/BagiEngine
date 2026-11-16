#include "ControlService.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlLog.h>
#include <BECore/Control/ControlRoots.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        /// A method name from a client is echoed in an error message; keep the message short
        constexpr size_t kMaxEchoedMethodLength = 64;

    }  // namespace

    struct ControlService::Request {
        String method;
        Json id;
        Json params = Json::object();
        Optional<ControlError> error;  // found while parsing; answered when the request's turn comes
        ControlReplyFn reply;
    };

    ControlService::ControlService() : _roots(MakeUnique<ControlRoots>()), _log(MakeUnique<ControlLog>()) {}
    ControlService::~ControlService() = default;

    void ControlService::ParseRequest(StringView text, Request& request) {
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
            return;
        }
        if (!params->is_object()) {
            request.error = MakeControlError(ControlErrors::kInvalidParams, "'params' has to be an object");
            return;
        }
        request.params = *params;
    }

    void ControlService::Post(StringView requestText, ControlReplyFn reply) {
        auto request = MakeUnique<Request>();
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

    void ControlService::RegisterHandler(StringView method, Handler handler, bool bypassBarrier) {
        if (!_handlers.contains(method)) {
            ++_methodsVersion;
        }
        _handlers[String(method)] = HandlerEntry{std::move(handler), bypassBarrier, ++_registrations};
    }

    void ControlService::DescribeMethod(StringView method, ControlMethodInfo info) {
        _descriptions[String(method)] = std::move(info);
        ++_methodsVersion;
    }

    Vector<ControlMethodListing> ControlService::GetMethods() const {
        Vector<ControlMethodListing> methods;
        methods.reserve(_handlers.size());
        for (const auto& handler : _handlers) {
            const auto description = _descriptions.find(handler.first);
            methods.push_back(ControlMethodListing{handler.first, description != _descriptions.end() ? &description->second : nullptr});
        }
        eastl::sort(methods.begin(), methods.end(), [](const ControlMethodListing& a, const ControlMethodListing& b) { return a.name < b.name; });
        return methods;
    }

    void ControlService::SetEventSink(EventSink sink) {
        _eventSink = std::move(sink);
        _eventSinkGeneration = ++_registrations;
    }

    ControlService::PublishObserverId ControlService::AddPublishObserver(PublishObserver observer) {
        auto entry = MakeUnique<ObserverEntry>();
        entry->id = ++_lastPublishObserverId;
        entry->observer = std::move(observer);
        _publishObservers.push_back(std::move(entry));
        return _publishObservers.back()->id;
    }

    void ControlService::PrunePublishObservers() {
        eastl::erase_if(_publishObservers, [](const UniquePtr<ObserverEntry>& entry) { return entry->id == kNoPublishObserver; });
    }

    void ControlService::RemovePublishObserver(PublishObserverId id) {
        if (id == kNoPublishObserver) {
            return;
        }
        for (UniquePtr<ObserverEntry>& entry : _publishObservers) {
            if (entry->id == id) {
                entry->id = kNoPublishObserver;
                break;
            }
        }
        if (!_notifyingObservers) {
            PrunePublishObservers();
        }
    }

    void ControlService::NotifyPublishObservers(StringView event, const Json& data) {
        _notifyingObservers = true;
        const size_t count = _publishObservers.size();  // an observer added by an observer waits for the next Publish
        for (size_t i = 0; i < count; ++i) {
            ObserverEntry& entry = *_publishObservers[i];
            if (entry.id != kNoPublishObserver) {
                entry.observer(event, data);
            }
        }
        _notifyingObservers = false;
        PrunePublishObservers();
    }

    void ControlService::Publish(StringView event, const Json& data) {
        if (!_publishObservers.empty() && !_notifyingObservers) {
            NotifyPublishObservers(event, data);
        }
        if (!_eventSink) {
            return;
        }
        Json message = Json::object();
        message["event"] = MakeJsonString(event);
        message["data"] = data;

        // The running sink is moved out like a running handler: it may set another sink, and its capture must live until it returns
        const uint64_t generation = _eventSinkGeneration;
        EventSink sink = std::move(_eventSink);
        sink(DumpJson(message));
        if (_eventSinkGeneration == generation) {
            _eventSink = std::move(sink);
        }
    }

    void ControlService::SetFrameEndHook(StringView name, FrameEndHook hook) {
        for (HookEntry& entry : _frameEndHooks) {
            if (entry.name == name) {
                entry.hook = std::move(hook);
                entry.generation = ++_registrations;
                return;
            }
        }
        _frameEndHooks.push_back(HookEntry{String(name), std::move(hook), ++_registrations});
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
            UniquePtr<Request> request = std::move(*it);
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
            const String name = request.method.substr(0, kMaxEchoedMethodLength);
            request.reply(MakeErrorResponse(request.id, MakeControlError(ControlErrors::kUnknownMethod, Format("unknown method '{}'", name))));
            return;
        }

        auto call = MakeUnique<ControlCall>(PassKey<ControlService>{}, std::move(request.method), std::move(request.id), std::move(request.params), std::move(request.reply));
        InvokeHandler(entry->second, *call);
        Settle(std::move(call));
    }

    void ControlService::InvokeHandler(HandlerEntry& entry, ControlCall& call) {
        // The running handler is moved out of the table: it may register handlers, which replaces entries (its own too, and its capture dies with the replaced handler)
        const uint64_t generation = entry.generation;
        Handler handler = std::move(entry.handler);
        handler(call);

        const auto current = _handlers.find(call.GetMethod());
        if (current != _handlers.end() && current->second.generation == generation) {
            current->second.handler = std::move(handler);
        }
    }

    Optional<ControlAnswer> ControlService::RunNested(ControlCall& outer, StringView method, Json params, NestedContinuation&& onAnswer) {
        EXPECT(!outer.IsAnswered() && outer._deferFrames == 0, "ControlService::RunNested: the outer call is already answered or deferred");

        const auto entry = _handlers.find(method);
        if (entry == _handlers.end()) {
            const String name(method.substr(0, kMaxEchoedMethodLength));
            return std::unexpected(MakeControlError(ControlErrors::kUnknownMethod, Format("unknown method '{}'", name)));
        }
        if (!entry->second.handler) {
            // Its handler is the one running (a method that runs itself): the table holds nothing to call
            return std::unexpected(MakeControlError(ControlErrors::kInternalError, Format("'{}' cannot run inside itself", entry->first)));
        }

        auto nested = MakeUnique<ControlCall>(PassKey<ControlService>{}, String(method), std::move(params));
        InvokeHandler(entry->second, *nested);
        return AwaitNested(outer, nested, onAnswer);
    }

    Optional<ControlAnswer> ControlService::AwaitNested(ControlCall& outer, UniquePtr<ControlCall>& nested, NestedContinuation& onAnswer) {
        if (nested->_captured) {
            return std::move(*nested->_captured);
        }
        if (nested->_deferFrames == 0) {
            LOG_ERROR(Format("[ControlService] the handler of '{}' gave no answer", nested->GetMethod()));
            return std::unexpected(MakeControlError(ControlErrors::kInternalError, "the handler gave no answer"));
        }

        // The outer call takes over the frames. Its done runs the nested one and then asks what comes next: an answer goes to onAnswer, a new deferral is taken over again
        const uint32_t frames = nested->_deferFrames;
        const double seconds = nested->_deferSeconds;
        nested->_deferFrames = 0;
        ControlCall::DoneFn nestedDone = std::move(nested->_done);
        outer.DeferFrames(frames, seconds, [this, nested = std::move(nested), nestedDone = std::move(nestedDone), onAnswer = std::move(onAnswer)](ControlCall& outerCall) mutable {
            nestedDone(*nested);
            if (auto answer = AwaitNested(outerCall, nested, onAnswer)) {
                onAnswer(outerCall, std::move(*answer));
            }
        });
        return std::nullopt;
    }

    void ControlService::Settle(UniquePtr<ControlCall> call) {
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
        // By index: a hook may add hooks. The running hook is moved out of its entry, which a hook that replaces itself (or adds hooks, which can move the entries) must not destroy
        for (size_t i = 0; i < _frameEndHooks.size(); ++i) {
            const uint64_t generation = _frameEndHooks[i].generation;
            FrameEndHook hook = std::move(_frameEndHooks[i].hook);
            if (hook) {
                hook();
            }
            if (_frameEndHooks[i].generation == generation) {
                _frameEndHooks[i].hook = std::move(hook);
            }
        }

        if (!_pending) {
            return;
        }
        if (--_pendingFramesLeft > 0) {
            return;
        }

        UniquePtr<ControlCall> call = std::move(_pending);
        const ControlCall::DoneFn done = std::move(call->_done);
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
            UniquePtr<ControlCall> call = std::move(_pending);
            call->Fail(error);
        }
        while (!_inbox.empty()) {
            UniquePtr<Request> request = std::move(_inbox.front());
            _inbox.pop_front();
            request->reply(MakeErrorResponse(request->id, error));
        }
    }

}  // namespace BECore
