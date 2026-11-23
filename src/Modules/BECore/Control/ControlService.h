#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonFwd.h>
#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Threading/ConditionVariable.h>
#include <BECore/Threading/LockSite.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Types/Fifo.h>
#include <BECore/Utils/FunctionRef.h>
#include <chrono>

namespace BECore {

    class ControlCall;
    class ControlLog;
    class ControlRoots;

    /**
     * @brief Command queue of the external control API: requests come from any thread and run on the main thread.
     *
     * One instance lives in CoreManager. It knows nothing about transports or games: a transport calls Post(), and
     * modules register handlers by method name. The wire shape, the frame stage of Drain() and the manual clock are
     * in Control/CLAUDE.md.
     *
     * A handler that defers its answer (ControlCall::DeferFrames) is a barrier: the requests behind it wait until it
     * is answered, except those of methods registered with bypassBarrier. All state of the manual clock lives here,
     * so that a test can play the application loop without the application.
     *
     * Everything except Post() belongs to the main thread.
     */
    class ControlService {
    public:
        /// Longest request text Post() accepts, in bytes; a longer one is answered with request_too_large
        static constexpr size_t kMaxRequestBytes = 1024 * 1024;

        /// Deepest nesting of arrays and objects in a request; a deeper one is answered with invalid_request. The request is copied while it is handled, which recurses once per level
        static constexpr size_t kMaxRequestDepth = 128;

        /// Version of the request and response format, reported by the "ping" method
        static constexpr int kProtocolVersion = 1;

        using Handler = MoveOnlyFunction<void(ControlCall&)>;
        using EventSink = MoveOnlyFunction<void(const String&)>;
        using FrameEndHook = MoveOnlyFunction<void()>;
        using PublishObserver = MoveOnlyFunction<void(StringView event, const Json& data)>;
        using PublishObserverId = uint64_t;

        /// What AddPublishObserver() never returns; RemovePublishObserver() of it does nothing
        static constexpr PublishObserverId kNoPublishObserver = 0;

        ControlService();
        ~ControlService();

        ControlService(const ControlService&) = delete;
        ControlService& operator=(const ControlService&) = delete;

        /**
         * @brief Queues a request. Thread-safe.
         *
         * @p requestText is parsed here, on the posting thread; a request that is not valid is answered with an error
         * when its turn comes, so the order of the answers stays the order of the requests. @p reply gets the response
         * on the main thread. After Shutdown() the request is answered at once, on the calling thread.
         */
        void Post(StringView requestText, ControlReplyFn reply);

        /// Registers (or replaces) the handler of @p method. A method with @p bypassBarrier runs even while a deferred call is pending
        void RegisterHandler(StringView method, Handler handler, bool bypassBarrier = false);

        /// The named objects the by-name methods may reach (ControlRoots.h); empty until a module registers one. Main thread only
        [[nodiscard]] ControlRoots& Roots() {
            return *_roots;
        }

        [[nodiscard]] const ControlRoots& Roots() const {
            return *_roots;
        }

        /// The warnings and errors of the log, counted and kept for the log methods; starts listening when RegisterControlBuiltins runs. Main thread only
        [[nodiscard]] ControlLog& Log() {
            return *_log;
        }

        [[nodiscard]] const ControlLog& Log() const {
            return *_log;
        }

        /// Says what @p method does and which parameters it reads (for list_methods and the completion of a console). Kept apart from the handler: registering the
        /// method again keeps it, and a description of a name that has no handler is kept but not listed. Main thread
        void DescribeMethod(StringView method, ControlMethodInfo info);

        /// Every registered method, sorted by name. The info pointers stay valid until the next DescribeMethod. Main thread
        [[nodiscard]] Vector<ControlMethodListing> GetMethods() const;

        /// GetMethods() for an answer written at once: the list is on the scratch arena (the caller's scope owns it) and the names are views of the table,
        /// so it allocates nothing on the heap. Main thread
        [[nodiscard]] ArenaVector<ControlMethodView> GetMethodViews() const;

        /// Grows when a method is registered under a new name or described, so a widget rebuilds its copy of GetMethods() only on a change
        [[nodiscard]] uint64_t GetMethodsVersion() const {
            return _methodsVersion;
        }

        /// Where Publish() sends its messages (the transport); none by default
        void SetEventSink(EventSink sink);

        /// Sends {"event":..,"data":..} to the event sink, if there is one. A sink may replace or clear itself; a Publish from inside the sink sends nothing
        void Publish(StringView event, const Json& data);

        /**
         * @brief Publish() with the data written by @p writeData: the same bytes as the `Json` form, no `Json` built.
         *
         * Neither a sink nor an observer: returns at once and does not call @p writeData. An observer gets a `Json` parsed back from the written data
         * (so the text is written to a local String first); with a sink alone the data is written in place into the publish buffer, and a second push
         * allocates nothing. @p writeData writes exactly one value; one that does not, or whose writer recorded a failure (JsonWriter::Fail), is not sent. It must not Publish itself (the nested push
         * reaches only the observers).
         */
        void Publish(StringView event, FunctionRef<void(JsonWriter&)> writeData);

        /**
         * @brief Calls @p observer with the name and the data of every Publish() from now on, whether or not an event sink is set.
         *
         * For a module that waits for an event of its own process (`run_until`). Main thread. An observer runs before the sink and may remove itself or add
         * others (an added one sees the next Publish); a Publish from inside an observer reaches the sink but no observer. Returns the id for RemovePublishObserver().
         */
        [[nodiscard]] PublishObserverId AddPublishObserver(PublishObserver observer);

        /// Stops calling the observer of @p id; an unknown or already removed id is ignored. Main thread. A running observer's capture lives until it returns
        void RemovePublishObserver(PublishObserverId id);

        /// How many observers are registered now (a test checks that a finished wait gave its own back)
        [[nodiscard]] size_t GetPublishObserverCount() const {
            return _publishObservers.size();
        }

        /// Runs @p hook at the end of every frame, before a deferred call is completed. A hook of the same @p name is replaced, so registering a module again adds nothing
        void SetFrameEndHook(StringView name, FrameEndHook hook);

        /// What RunNested() calls when a nested call that deferred has its answer, with the outer call (which may defer or answer it) and the answer
        using NestedContinuation = MoveOnlyFunction<void(ControlCall& outer, ControlAnswer answer)>;

        /**
         * @brief Runs @p method as part of @p outer, the call a handler is answering, and gives back its answer as a value.
         *
         * The nested call runs as a posted one would: the table lookup, the generation handling of the handler, an unknown method or a handler that
         * answers nothing is an error answer. It does not wait for the barrier: it is part of @p outer, which is the one that is pending. Main thread,
         * and @p outer must be unanswered and not deferred yet.
         *
         * Returns the answer when the nested call has given it. Returns nullopt when the nested call deferred (`step`, `screenshot`, ...): its frames
         * are now those of @p outer, so the handler of @p outer has to return without answering, and when the frames have passed @p onAnswer gets the
         * answer, with @p outer, which it may answer, defer again or use to run the next nested call. @p onAnswer is moved from only when this function
         * returns nullopt and is called once then, not at all when Shutdown() answers @p outer first; when an answer is returned the caller still has it. What @p onAnswer needs later it holds in its
         * own capture: the handler of @p outer has returned by then.
         */
        [[nodiscard]] Optional<ControlAnswer> RunNested(ControlCall& outer, StringView method, Json params, NestedContinuation&& onAnswer);

        /**
         * @brief Defers @p call by @p frames frames in which the game advances by @p seconds each, on whichever clock runs.
         *
         * `DeferFrames` alone waits for frames, which on a paused real clock are held frames that move nothing. This also asks the pause for the steps
         * there (the manual clock gets its frames when the call is settled), and counts the frame that is running when a handler (frame stage 1b)
         * calls it, which ends too; a continuation (@p done of an earlier deferral, run by EndFrame) counts none, its frame has ended. Main thread.
         */
        void DeferSteppedFrames(ControlCall& call, uint32_t frames, double seconds, MoveOnlyFunction<void(ControlCall&)> done);

        /// Runs the queued requests (frame stage 1b)
        void Drain();

        /// Ends a frame: runs the frame end hooks, then counts a deferred call down and completes it at zero
        void EndFrame();

        /// Turns the manual clock on or off; Application does it once, before the first frame
        void SetManualClock(bool enabled) {
            _manualClock = enabled;
        }

        [[nodiscard]] bool IsManualClock() const {
            return _manualClock;
        }

        /// With the manual clock: true while a deferred call waits for frames, so the next frame has to run. Always false on the real clock
        [[nodiscard]] bool IsStepDue() const {
            return _manualClock && _pending != nullptr;
        }

        /// The pause of the game that the deferred calls and the `pause` / `resume` / `step` methods drive; CoreManager attaches its own. None by default (a service on its own, as in a test)
        void SetPause(GamePause* pause) {
            _pause = pause;
        }

        [[nodiscard]] GamePause* GetPause() const {
            return _pause;
        }

        /// Seconds the frame that IsStepDue() asks for lasts (what the pending call gave to DeferFrames)
        [[nodiscard]] double GetStepSeconds() const {
            return _pendingSeconds;
        }

        /// The state of the transport as the application last published it; not listening until something publishes. Main thread only
        void SetServerStatus(const ControlServerStatus& status) {
            _serverStatus = status;
        }

        [[nodiscard]] const ControlServerStatus& GetServerStatus() const {
            return _serverStatus;
        }

        /// Blocks until a request has been posted or @p timeout has passed (the idle wait of the manual clock). Main thread
        void WaitForRequests(std::chrono::milliseconds timeout);

        /// Answers every queued and deferred request with shutting_down and stops accepting requests
        void Shutdown();

    private:
        friend class ControlCall;

        struct Request;
        struct HandlerEntry {
            Handler handler;
            bool bypassBarrier = false;
            uint64_t generation = 0;  // the registration that made this entry; a running handler goes back only if it is still the current one
        };
        struct ObserverEntry {
            PublishObserverId id = kNoPublishObserver;  // reset to none by a removal while Publish runs the observers
            PublishObserver observer;
        };
        struct HookEntry {
            String name;
            FrameEndHook hook;
            uint64_t generation = 0;
        };

        static void ParseRequest(StringView text, Request& request);

        /// Write a response into the reply buffer and give it to @p reply (main thread: ControlCall and the answers of Process and Shutdown). The buffer
        /// keeps its capacity between replies; a reply sent from inside a reply function gets a local String, never the busy buffer. SendOk sends the error
        /// response in place of the result when @p writeResult fails or does not write one value
        void SendOk(ControlReplyFn& reply, const Json& id, WriteResultFn writeResult);
        void SendError(ControlReplyFn& reply, const Json& id, const ControlError& error);

        [[nodiscard]] bool BypassesBarrier(const Request& request) const;
        void Process(Request& request);
        void InvokeHandler(HandlerEntry& entry, ControlCall& call);
        Optional<ControlAnswer> AwaitNested(ControlCall& outer, UniquePtr<ControlCall>& nested, NestedContinuation& onAnswer);
        void Settle(UniquePtr<ControlCall> call);
        void PrunePublishObservers();
        void NotifyPublishObservers(StringView event, const Json& data);

        Mutex _mutex{BE_LOCK_SITE("Control queue")};
        ConditionVariable _posted;        // notified by Post(); WaitForRequests() waits on it
        Fifo<UniquePtr<Request>> _queue;  // posted, not yet seen by the main thread; guarded by _mutex
        bool _stopped = false;            // guarded by _mutex

        // Main thread only
        Fifo<UniquePtr<Request>> _inbox;  // taken from _queue, in order, waiting for the barrier
        HashMap<String, HandlerEntry> _handlers;
        UniquePtr<ControlRoots> _roots;                    // behind a pointer so this header does not pull in the reflection registry
        UniquePtr<ControlLog> _log;                        // behind a pointer so this header does not pull in the logger
        HashMap<String, ControlMethodInfo> _descriptions;  // beside the handlers, so a handler registered again keeps its description
        uint64_t _methodsVersion = 0;
        // Before _pending: a deferred call that owns an observer (run_until) removes it when it dies with the service
        Vector<UniquePtr<ObserverEntry>> _publishObservers;  // entries do not move, so a running observer stays where it is when others are added
        PublishObserverId _lastPublishObserverId = kNoPublishObserver;
        bool _notifyingObservers = false;
        UniquePtr<ControlCall> _pending;
        uint32_t _pendingFramesLeft = 0;
        double _pendingSeconds = kDefaultStepSeconds;
        bool _manualClock = false;
        GamePause* _pause = nullptr;
        ControlServerStatus _serverStatus;
        EventSink _eventSink;               // empty while the sink runs (Publish moves it out)
        uint64_t _eventSinkGeneration = 0;  // the registration of the current sink; a running sink goes back only if no other was set
        String _replyScratch;               // the text of the response being sent on the main thread; a reply from the posting thread (Post) uses a local String
        bool _replyScratchBusy = false;
        String _publishScratch;  // the text of the event being published, apart from the replies: a sink may answer a request
        bool _publishScratchBusy = false;
        Vector<HookEntry> _frameEndHooks;
        bool _inFrameEnd = false;     // EndFrame is running the done function of a deferred call
        uint64_t _registrations = 0;  // counts RegisterHandler, SetFrameEndHook and SetEventSink calls; the source of the generations
    };

}  // namespace BECore
