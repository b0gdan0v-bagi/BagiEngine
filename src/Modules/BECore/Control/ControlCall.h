#pragma once

#include <BECore/Control/Json.h>

namespace BECore {

    class ControlService;

    /// The response text of a successful call: {"id":..,"ok":true,"result":..}
    [[nodiscard]] String MakeOkResponse(const Json& id, Json result);

    /// The response text of a failed call: {"id":..,"ok":false,"error":{"code":..,"message":..}}
    [[nodiscard]] String MakeErrorResponse(const Json& id, const ControlError& error);

    /**
     * @brief One request being handled: its parameters and the way to answer it.
     *
     * ControlService hands a call to the registered handler on the main thread. The handler has to end in exactly one
     * of Reply(), Fail() or DeferFrames(); a handler that does none of them is answered with an internal_error.
     * Params() is always a JSON object, but its members come from a client: read them with the helpers of Json.h.
     */
    class ControlCall {
    public:
        using DoneFn = std::function<void(ControlCall&)>;

        ControlCall(PassKey<ControlService>, String method, Json id, Json params, ControlReplyFn reply);

        ControlCall(const ControlCall&) = delete;
        ControlCall& operator=(const ControlCall&) = delete;

        /// The method name as the client sent it
        [[nodiscard]] const String& GetMethod() const {
            return _method;
        }

        /// The request parameters; an empty object when the request had none
        [[nodiscard]] const Json& Params() const {
            return _params;
        }

        /// Answers with success. @p result is the "result" member of the response
        void Reply(Json result);

        /// Answers with an error
        void Fail(StringView code, StringView message);
        void Fail(const ControlError& error);

        /**
         * @brief Answers later: @p done runs at the end of the frame in which the counter reaches zero.
         *
         * The counter starts at @p frames (at least 1) and drops by one at the end of every frame, so DeferFrames(1)
         * completes at the end of the frame that is running now (or of the next one when the call is handled between
         * frames). Until then ControlService holds back the requests behind this one. @p done has to answer the call
         * with Reply() or Fail(), or defer it again.
         */
        void DeferFrames(uint32_t frames, DoneFn done);

        /**
         * @brief DeferFrames() that also says how long each of the frames is, in seconds.
         *
         * Only a manual clock (ControlService::IsManualClock) uses @p secondsPerFrame: while this call is pending the
         * application runs one frame of that length per step. On the real clock the frames take the time they take.
         * A value that is not positive is a frame of zero seconds (a screenshot wants one: nothing moves).
         */
        void DeferFrames(uint32_t frames, double secondsPerFrame, DoneFn done);

        [[nodiscard]] bool IsAnswered() const {
            return _answered;
        }

    private:
        friend class ControlService;

        String _method;
        Json _id;
        Json _params;
        ControlReplyFn _reply;
        DoneFn _done;
        uint32_t _deferFrames = 0;
        double _deferSeconds = kDefaultStepSeconds;
        bool _answered = false;
    };

}  // namespace BECore
