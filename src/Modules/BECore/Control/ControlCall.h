#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Control/RequestBuffers.h>
#include <BECore/Memory/Pooled.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <BECore/Utils/PassKey.h>

namespace BECore {

    class ControlService;

    /// The id of a response to a request that has none (it could not be read, or the call is nested): the JSON `null`
    inline constexpr StringView kNullId = "null";

    /// The `params` of a call that has none: an empty object, one tape for all of them (a view that is valid for the life of the process)
    [[nodiscard]] JsonView EmptyParamsView();

    /// Appends the response of a successful call, {"id":..,"ok":true,"result":..}, to @p out (never cleared); @p id is the JSON text of the request's id, written as it
    /// is (`kNullId` when there is none); @p writeResult writes the one value of `result`. The keys are in alphabetical order, so the order a client sees does not
    /// depend on the way a response is written. Allocates only for the growth of @p out. False when @p writeResult did not write exactly one complete value: @p out is then back to its length before
    /// the call, with nothing appended
    bool AppendOkResponse(String& out, StringView id, FunctionRef<void(JsonWriter&)> writeResult);

    /// AppendOkResponse for a writer that may fail (a different name: two FunctionRef parameters would make a lambda ambiguous): its error, or `internal_error` when it wrote no complete value, leaves
    /// @p out as it was (nothing appended)
    [[nodiscard]] std::expected<void, ControlError> TryAppendOkResponse(String& out, StringView id, WriteResultFn writeResult);

    /// Appends the response of a failed call, {"error":{"code":..,"message":..},"id":..,"ok":false}, to @p out (never cleared)
    void AppendErrorResponse(String& out, StringView id, const ControlError& error);

    /// The response text of a failed call (AppendErrorResponse)
    [[nodiscard]] String MakeErrorResponse(StringView id, const ControlError& error);

    /**
     * @brief One request being handled: its parameters and the way to answer it.
     *
     * ControlService hands a call to the registered handler on the main thread. The handler has to end in exactly one
     * of Reply(), Fail() or DeferFrames(); a handler that does none of them is answered with an internal_error.
     * The parameters are always a JSON object, but its members come from a client: read them with the checked helpers (`ControlParams.h` over Params()).
     *
     * The handler and every `done` of DeferFrames() run inside a `ScratchScope` of the service, so an answer struct whose strings and lists are
     * `ArenaString` / `ArenaVector` is a local of the handler (or of `done`) and is given back right after the reply is written.
     */
    class ControlCall {
    public:
        BE_POOLED(ControlCall)  // one per handled request, a nested call too

        using DoneFn = MoveOnlyFunction<void(ControlCall&)>;

        /// @p id is the id's JSON text, a view into @p buffers, which the call owns together with the view @p params of their tape
        ControlCall(PassKey<ControlService>, ControlService& service, String method, RequestBuffersPtr buffers, StringView id, JsonView params, ControlReplyFn reply);

        /// A call that runs inside another one: its answer is kept as a value (ControlService::RunNested) instead of being sent. @p params is a view that
        /// the caller keeps alive while the call runs
        ControlCall(PassKey<ControlService>, String method, JsonView params);

        ControlCall(const ControlCall&) = delete;
        ControlCall& operator=(const ControlCall&) = delete;

        /// The method name as the client sent it
        [[nodiscard]] const String& GetMethod() const {
            return _method;
        }

        /// The request parameters; an empty object when the request had none. Valid while the call lives
        [[nodiscard]] JsonView Params() const {
            return _params;
        }

        /// Answers with an empty object, `{}`
        void ReplyEmpty();

        /**
         * @brief Answers with success, the `result` written by @p writeResult in place, with no allocation once the service's reply buffer has grown.
         *
         * @p writeResult runs before this returns and writes exactly one value (`writer` is the open writer of the response: `BeginObject()` ... `EndObject()`,
         * or a scalar). It must not answer or defer the call. The text the transport gets is valid for the reply function's call only (ControlReplyFn).
         * Inside a nested call (ControlService::RunNested) the written text is the answer, kept as it is.
         */
        void Reply(FunctionRef<void(JsonWriter&)> writeResult);

        /// Answers with a reflected value: its generated `WriteJson` writes the `result` (Reply(FunctionRef) with its rules; `Control/JsonWriteReflect.h` has the shapes).
        /// @p value may hold arena members: it is written before this returns and must be a local of the handler or of `done`, never kept or captured
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
         * with Reply() or Fail(), or defer it again. @p done runs after the handler's scratch scope has ended: it captures heap data only (an answer struct
         * with arena members made by the handler would dangle) and builds its answer inside itself.
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
        RequestBuffersPtr _buffers;          // the text that _id and _params point into
        String _method;
        StringView _id = kNullId;
        JsonView _params;
        ControlReplyFn _reply;
        DoneFn _done;
        uint32_t _deferFrames = 0;
        double _deferSeconds = kDefaultStepSeconds;
        bool _answered = false;
        bool _capturing = false;  // the answer goes to _captured, not to _reply
        Optional<ControlAnswer> _captured;
    };

}  // namespace BECore
