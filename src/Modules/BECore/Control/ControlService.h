#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonFwd.h>
#include <EASTL/deque.h>
#include <chrono>

namespace BECore {

    class ControlCall;

    /**
     * @brief Command queue of the external control API: requests come from any thread and run on the main thread.
     *
     * One instance lives in CoreManager (CoreManager::GetControlService()). It knows nothing about transports or games:
     * a transport (the WebSocket server) calls Post(), and modules register handlers by method name (the builtins of
     * ControlBuiltins.h, the game methods of GameControl, the screenshot of the SDL module).
     *
     * Requests are JSON objects {"id":1,"method":"name","params":{..}}; a response is {"id":1,"ok":true,"result":..} or
     * {"id":1,"ok":false,"error":{"code":..,"message":..}}; a push is {"event":"name","data":..}. See Control/CLAUDE.md.
     *
     * Order: requests run in the order they were posted (per posting thread). Drain() runs them at frame stage 1b,
     * between input and tasks. A handler that defers its answer (ControlCall::DeferFrames) is a barrier: the requests
     * behind it wait until it is answered, except those of methods registered with bypassBarrier.
     *
     * Manual clock: with SetManualClock(true) the application does not run frames by itself. It runs one only while
     * IsStepDue() says so, i.e. while a deferred call (a `step`, a `screenshot`) waits for frames, and moves the game
     * clock by GetStepSeconds() per frame. In between it only answers requests (WaitForRequests) and keeps the window
     * alive. Every state of this lives here, so that a test can play the application loop without the application.
     * The manual clock is a pause stepped by the API (GamePause, `Time/GamePause.h`): with a pause attached
     * (SetPause) a deferred call on the manual clock also requests its frames from it, and the pause is what decides
     * the kind and the seconds of every frame.
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

        using Handler = std::function<void(ControlCall&)>;
        using EventSink = std::function<void(const String&)>;
        using FrameEndHook = std::function<void()>;

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

        /// Where Publish() sends its messages (the transport); none by default
        void SetEventSink(EventSink sink);

        /// Sends {"event":..,"data":..} to the event sink, if there is one
        void Publish(StringView event, const Json& data);

        /// Runs @p hook at the end of every frame, before a deferred call is completed. A hook of the same @p name is replaced, so registering a module again adds nothing
        void SetFrameEndHook(StringView name, FrameEndHook hook);

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
        struct Request;
        struct HandlerEntry {
            Handler handler;
            bool bypassBarrier = false;
        };
        struct HookEntry {
            String name;
            FrameEndHook hook;
        };

        static void ParseRequest(StringView text, Request& request);

        [[nodiscard]] bool BypassesBarrier(const Request& request) const;
        void Process(Request& request);
        void Settle(eastl::unique_ptr<ControlCall> call);

        Mutex _mutex{BE_LOCK_SITE("Control queue")};
        ConditionVariable _posted;                        // notified by Post(); WaitForRequests() waits on it
        eastl::deque<eastl::unique_ptr<Request>> _queue;  // posted, not yet seen by the main thread; guarded by _mutex
        bool _stopped = false;                            // guarded by _mutex

        // Main thread only
        eastl::deque<eastl::unique_ptr<Request>> _inbox;  // taken from _queue, in order, waiting for the barrier
        eastl::hash_map<String, HandlerEntry> _handlers;
        eastl::unique_ptr<ControlCall> _pending;
        uint32_t _pendingFramesLeft = 0;
        double _pendingSeconds = kDefaultStepSeconds;
        bool _manualClock = false;
        GamePause* _pause = nullptr;
        ControlServerStatus _serverStatus;
        EventSink _eventSink;
        eastl::vector<HookEntry> _frameEndHooks;
    };

}  // namespace BECore
