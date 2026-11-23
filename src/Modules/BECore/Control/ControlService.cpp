#include "ControlService.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlEnvelope.h>
#include <BECore/Control/ControlLog.h>
#include <BECore/Control/ControlRoots.h>
#include <BECore/Memory/Pooled.h>
#include <BECore/Memory/ScratchArena.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        /// A method name from a client is echoed in an error message; keep the message short
        constexpr size_t kMaxEchoedMethodLength = 64;

        /// A reply buffer that grew past this (a screenshot as text) is released after its message, so one large answer does not keep its memory for good
        constexpr size_t kMaxKeptScratchBytes = 1024 * 1024;

        /// The buffer a message is written into: the service's scratch when it is free, else a local one (a send from inside a send). Marks the scratch busy
        /// for the lease's life, so the text handed to a callback is never overwritten while the callback runs
        class ScratchLease {
        public:
            ScratchLease(String& scratch, bool& busy) : _busy(busy), _usesScratch(!busy) {
                if (_usesScratch) {
                    _busy = true;
                    scratch.clear();
                    _text = &scratch;
                } else {
                    _text = &_local;
                }
            }

            ~ScratchLease() {
                if (_usesScratch) {
                    _busy = false;
                    if (_text->capacity() > kMaxKeptScratchBytes) {
                        String().swap(*_text);
                    }
                }
            }

            ScratchLease(const ScratchLease&) = delete;
            ScratchLease& operator=(const ScratchLease&) = delete;

            [[nodiscard]] String& Text() {
                return *_text;
            }

        private:
            bool& _busy;
            bool _usesScratch;
            String _local;
            String* _text = nullptr;
        };

    }  // namespace

    struct ControlService::Request {
        BE_POOLED(ControlService::Request)  // made on a reader thread, freed on the main one

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

        // Before the parse: copying a deep value recurses on the stack of the posting thread, and so does the destructor of the document
        if (IsJsonNestedDeeperThan(text, kMaxRequestDepth)) {
            request.error = MakeControlError(ControlErrors::kInvalidRequest, Format("the request nests deeper than {} levels", kMaxRequestDepth));
            return;
        }

        auto parsed = ParseJson(text);
        if (!parsed) {
            request.error = std::move(parsed.error());
            return;
        }
        Json& message = *parsed;
        if (!message.is_object()) {
            request.error = MakeControlError(ControlErrors::kInvalidRequest, "the request has to be a JSON object");
            return;
        }

        // The id comes first so that every later error can be answered with it
        Json* id = FindJsonMember(message, "id");
        if (id == nullptr || !(id->is_string() || id->is_number_integer())) {
            request.error = MakeControlError(ControlErrors::kInvalidRequest, "'id' has to be an integer or a string");
            return;
        }
        request.id = std::move(*id);

        auto method = GetJsonString(message, "method");
        if (!method || method->empty()) {
            request.error = MakeControlError(ControlErrors::kInvalidRequest, "'method' has to be a non-empty string");
            return;
        }
        request.method = std::move(*method);

        Json* params = FindJsonMember(message, "params");
        if (params == nullptr || params->is_null()) {
            return;
        }
        if (!params->is_object()) {
            request.error = MakeControlError(ControlErrors::kInvalidParams, "'params' has to be an object");
            return;
        }
        request.params = std::move(*params);
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

        // The posting thread, not the main one: the reply buffer of the service is not its to use
        request->reply(MakeErrorResponse(request->id, MakeControlError(ControlErrors::kShuttingDown, "the application is shutting down")));
    }

    void ControlService::SendOk(ControlReplyFn& reply, const Json& id, WriteResultFn writeResult) {
        ScratchLease lease(_replyScratch, _replyScratchBusy);
        const std::expected<void, ControlError> written = TryAppendOkResponse(lease.Text(), id, writeResult);
        if (!written) {
            AppendErrorResponse(lease.Text(), id, written.error());
        }
        reply(lease.Text());
    }

    void ControlService::SendError(ControlReplyFn& reply, const Json& id, const ControlError& error) {
        ScratchLease lease(_replyScratch, _replyScratchBusy);
        AppendErrorResponse(lease.Text(), id, error);
        reply(lease.Text());
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

    ArenaVector<ControlMethodView> ControlService::GetMethodViews() const {
        ArenaVector<ControlMethodView> methods;
        methods.reserve(_handlers.size());
        for (const auto& handler : _handlers) {
            const auto description = _descriptions.find(handler.first);
            methods.push_back(ControlMethodView{handler.first, description != _descriptions.end() ? &description->second : nullptr});
        }
        eastl::sort(methods.begin(), methods.end(), [](const ControlMethodView& a, const ControlMethodView& b) { return a.name < b.name; });
        return methods;
    }

    Vector<ControlMethodListing> ControlService::GetMethods() const {
        ScratchScope scratch;  // the views are copied out before it ends
        const ArenaVector<ControlMethodView> views = GetMethodViews();
        Vector<ControlMethodListing> methods;
        methods.reserve(views.size());
        for (const ControlMethodView& view : views) {
            methods.push_back(ControlMethodListing{String(view.name), view.info});
        }
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

        // The running sink is moved out like a running handler: it may set another sink, and its capture must live until it returns
        const uint64_t generation = _eventSinkGeneration;
        EventSink sink = std::move(_eventSink);
        {
            ScratchLease lease(_publishScratch, _publishScratchBusy);
            ScratchScope scratch;  // the envelope's arena members die with the push
            JsonWriter writer(lease.Text());
            ControlPushEnvelope envelope;
            envelope.data = JsonPayload::Borrowed(data);
            envelope.event = ArenaString(event.data(), event.size());
            envelope.WriteJson(writer);
            sink(lease.Text());
        }
        if (_eventSinkGeneration == generation) {
            _eventSink = std::move(sink);
        }
    }

    void ControlService::Publish(StringView event, FunctionRef<void(JsonWriter&)> writeData) {
        const bool hasObservers = !_publishObservers.empty() && !_notifyingObservers;
        if (!hasObservers && !_eventSink) {
            return;
        }

        String observed;  // the data as text, written once for the observers and entered into the envelope with Raw
        if (hasObservers) {
            JsonWriter writer(observed);
            writeData(writer);
            if (writer.GetFailure() != nullptr) {
                return;  // the data could not be written: a push has no one to answer with the error
            }
            EXPECT(writer.IsComplete(), "ControlService::Publish: the data is not one JSON value");
            auto parsed = ParseJson(observed);
            if (!writer.IsComplete() || !parsed) {
                return;
            }
            NotifyPublishObservers(event, *parsed);
            if (!_eventSink) {
                return;
            }
        }

        const uint64_t generation = _eventSinkGeneration;
        EventSink sink = std::move(_eventSink);
        {
            ScratchLease lease(_publishScratch, _publishScratchBusy);
            ScratchScope scratch;  // the envelope's arena members die with the push
            JsonWriter writer(lease.Text());
            const auto write = [&writeData](JsonWriter& out) -> std::expected<void, ControlError> {
                writeData(out);
                return {};
            };
            ControlPushEnvelope envelope;
            envelope.data = hasObservers ? JsonPayload::Text(observed) : JsonPayload::Streamed(write);
            envelope.event = ArenaString(event.data(), event.size());
            envelope.WriteJson(writer);
            if (writer.IsComplete()) {
                sink(lease.Text());
            } else {
                EXPECT(writer.GetFailure() != nullptr, "ControlService::Publish: the data is not one JSON value");  // a failure is the writer's own error, not a bug
            }
        }
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

        // One pass over what was waiting when it began: a held request goes to the back, so after the pass the held ones are in their old order. A request
        // that Process() makes wait (a deferred call sets _pending) is checked against the barrier like the others behind it
        for (size_t left = _inbox.size(); left > 0; --left) {
            UniquePtr<Request> request = std::move(_inbox.front());
            _inbox.pop_front();
            if (_pending && !BypassesBarrier(*request)) {
                _inbox.push_back(std::move(request));  // held back; the requests that bypass the barrier may still follow
                continue;
            }
            Process(*request);
        }
    }

    void ControlService::Process(Request& request) {
        BE_PROFILE_SCOPE("ControlService::Process");
        if (request.error) {
            SendError(request.reply, request.id, *request.error);
            return;
        }

        const auto entry = _handlers.find(request.method);
        if (entry == _handlers.end()) {
            const String name = request.method.substr(0, kMaxEchoedMethodLength);
            SendError(request.reply, request.id, MakeControlError(ControlErrors::kUnknownMethod, Format("unknown method '{}'", name)));
            return;
        }

        auto call = MakeUnique<ControlCall>(PassKey<ControlService>{}, *this, std::move(request.method), std::move(request.id), std::move(request.params), std::move(request.reply));
        InvokeHandler(entry->second, *call);
        Settle(std::move(call));
    }

    void ControlService::InvokeHandler(HandlerEntry& entry, ControlCall& call) {
        // The running handler is moved out of the table: it may register handlers, which replaces entries (its own too, and its capture dies with the replaced handler)
        const uint64_t generation = entry.generation;
        Handler handler = std::move(entry.handler);
        {
            // The answer struct a handler builds on the scratch arena is written into the reply inside the call and dies here, on this thread
            ScratchScope scratch;
            handler(call);
        }

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
            LOG_ERROR("[ControlService] the handler of '{}' gave no answer", nested->GetMethod());
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

    void ControlService::DeferSteppedFrames(ControlCall& call, uint32_t frames, double seconds, MoveOnlyFunction<void(ControlCall&)> done) {
        const bool stepsPausedGame = _pause != nullptr && _pause->IsPaused() && !_manualClock;
        uint32_t countedFrames = frames;
        if (stepsPausedGame) {
            _pause->RequestSteps(frames, seconds);
            countedFrames += _inFrameEnd ? 0 : 1;
        }
        call.DeferFrames(countedFrames, seconds, std::move(done));
    }

    void ControlService::Settle(UniquePtr<ControlCall> call) {
        if (call->IsAnswered()) {
            return;
        }

        if (call->_deferFrames == 0) {
            LOG_ERROR("[ControlService] the handler of '{}' gave no answer", call->GetMethod());
            call->Fail(ControlErrors::kInternalError, "the handler gave no answer");
            return;
        }

        if (_pending) {
            // Only a method that bypasses the barrier can get here while another call is pending
            LOG_ERROR("[ControlService] '{}' cannot defer its answer while '{}' is pending", call->GetMethod(), _pending->GetMethod());
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
            _inFrameEnd = true;
            {
                ScratchScope scratch;  // as in InvokeHandler: the answer struct of `done` dies with the reply; the continuation of a nested call runs here too
                done(*call);
            }
            _inFrameEnd = false;
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
            SendError(request->reply, request->id, error);
        }
    }

}  // namespace BECore
