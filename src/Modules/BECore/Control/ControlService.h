#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonFwd.h>
#include <EASTL/deque.h>

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
     * Everything except Post() belongs to the main thread.
     */
    class ControlService {
    public:
        /// Longest request text Post() accepts, in bytes; a longer one is answered with request_too_large
        static constexpr size_t kMaxRequestBytes = 1024 * 1024;

        /// Version of the request and response format, reported by the "ping" method
        static constexpr int kProtocolVersion = 1;

        using Handler = std::function<void(ControlCall&)>;
        using EventSink = std::function<void(const eastl::string&)>;
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
        void Post(eastl::string_view requestText, ControlReplyFn reply);

        /// Registers (or replaces) the handler of @p method. A method with @p bypassBarrier runs even while a deferred call is pending
        void RegisterHandler(eastl::string_view method, Handler handler, bool bypassBarrier = false);

        /// Where Publish() sends its messages (the transport); none by default
        void SetEventSink(EventSink sink);

        /// Sends {"event":..,"data":..} to the event sink, if there is one
        void Publish(eastl::string_view event, const Json& data);

        /// Runs @p hook at the end of every frame, before a deferred call is completed
        void AddFrameEndHook(FrameEndHook hook);

        /// Runs the queued requests (frame stage 1b)
        void Drain();

        /// Ends a frame: runs the frame end hooks, then counts a deferred call down and completes it at zero
        void EndFrame();

        /// Answers every queued and deferred request with shutting_down and stops accepting requests
        void Shutdown();

    private:
        struct Request;
        struct HandlerEntry {
            Handler handler;
            bool bypassBarrier = false;
        };

        static void ParseRequest(eastl::string_view text, Request& request);

        [[nodiscard]] bool BypassesBarrier(const Request& request) const;
        void Process(Request& request);
        void Settle(eastl::unique_ptr<ControlCall> call);

        std::mutex _mutex;
        eastl::deque<eastl::unique_ptr<Request>> _queue;  // posted, not yet seen by the main thread; guarded by _mutex
        bool _stopped = false;                            // guarded by _mutex

        // Main thread only
        eastl::deque<eastl::unique_ptr<Request>> _inbox;  // taken from _queue, in order, waiting for the barrier
        eastl::hash_map<eastl::string, HandlerEntry> _handlers;
        eastl::unique_ptr<ControlCall> _pending;
        uint32_t _pendingFramesLeft = 0;
        EventSink _eventSink;
        eastl::vector<FrameEndHook> _frameEndHooks;
    };

}  // namespace BECore
