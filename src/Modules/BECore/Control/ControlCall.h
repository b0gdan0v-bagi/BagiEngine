#pragma once

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Memory/Pooled.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <BECore/Utils/PassKey.h>

namespace BECore {

    class ControlService;

    /// Appends the response of a successful call, {"id":..,"ok":true,"result":..}, to @p out (never cleared); @p writeResult writes the one value
    /// of `result`. The keys are in alphabetical order, as a `Json` object keeps them, so the order a client sees does not depend on the way a response is written. Allocates only for the growth of @p
    /// out. False when @p writeResult did not write exactly one complete value: @p out is then back to its length before the call, with nothing appended
    bool AppendOkResponse(String& out, const Json& id, FunctionRef<void(JsonWriter&)> writeResult);

    /// AppendOkResponse for a writer that may fail (a different name: two FunctionRef parameters would make a lambda ambiguous): its error, or `internal_error` when it wrote no complete value, leaves
    /// @p out as it was (nothing appended)
    [[nodiscard]] std::expected<void, ControlError> TryAppendOkResponse(String& out, const Json& id, WriteResultFn writeResult);

    /// Appends the response of a failed call, {"error":{"code":..,"message":..},"id":..,"ok":false}, to @p out (never cleared)
    void AppendErrorResponse(String& out, const Json& id, const ControlError& error);

    /// The response text of a successful call (AppendOkResponse with a `Json` result)
    [[nodiscard]] String MakeOkResponse(const Json& id, Json result);

    /// The response text of a failed call (AppendErrorResponse)
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
        BE_POOLED(ControlCall)  // one per handled request, a nested call too

        using DoneFn = MoveOnlyFunction<void(ControlCall&)>;

        ControlCall(PassKey<ControlService>, ControlService& service, String method, Json id, Json params, ControlReplyFn reply);

        /// A call that runs inside another one: its answer is kept as a value (ControlService::RunNested) instead of being sent
        ControlCall(PassKey<ControlService>, String method, Json params);

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

        /// Answers with success; @p result is written as the `result` of the response
        void Reply(Json result);

        /**
         * @brief Answers with success, the `result` written by @p writeResult in place: no DOM, and no allocation once the service's reply buffer has grown.
         *
         * @p writeResult runs before this returns and writes exactly one value (`writer` is the open writer of the response: `BeginObject()` ... `EndObject()`,
         * or a scalar). It must not answer or defer the call. The text the transport gets is valid for the reply function's call only (ControlReplyFn).
         * Inside a nested call (ControlService::RunNested) the written value is read back into a `Json`.
         */
        void Reply(FunctionRef<void(JsonWriter&)> writeResult);

        /// Answers with a reflected value: its generated `WriteJson` writes the `result` (Reply(FunctionRef) with its rules; `Control/JsonWriteReflect.h` has the shapes)
        template <typename T>
        requires requires(const T& value, JsonWriter& writer) {
            value.WriteJson(writer);
        }
        void ReplyObject(const T& value) {
            Reply([&value](JsonWriter& writer) { value.WriteJson(writer); });
        }

        /**
         * @brief Reply() for a writer that can fail half way: when @p writeResult returns an error, what it wrote is discarded and the call is answered with that error.
         *
         * Nothing of the half-written text reaches the transport. The same holds for a failure the writer recorded (JsonWriter::Fail, which a streamed
         * `JsonPayload` of a reflected answer makes): the first one is answered, so `ReplyObject` answers it too. A callback that wrote no value, two, or left a container open is answered with
         * `internal_error` the same way (Reply(FunctionRef) too). Use Fail() when the error is known before the first byte.
         */
        void TryReply(WriteResultFn writeResult);

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

        ControlService* _service = nullptr;  // null for a nested call, which sends nothing
        String _method;
        Json _id;
        Json _params;
        ControlReplyFn _reply;
        DoneFn _done;
        uint32_t _deferFrames = 0;
        double _deferSeconds = kDefaultStepSeconds;
        bool _answered = false;
        bool _capturing = false;  // the answer goes to _captured, not to _reply
        Optional<ControlAnswer> _captured;
    };

}  // namespace BECore
